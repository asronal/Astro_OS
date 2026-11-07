// Saves a screenshot of every page of the real interface, without a display.
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml>

#include <cmath>
#include <cstdio>

#include "astro/sky.h"
#include "camera_client.h"
#include "camera_view.h"
#include "input_mode.h"
#include "mount_client.h"
#include "sky_backend.h"
#include "sky_chart.h"

static void wait_ms(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
        return 2;

    qputenv("QT_IM_MODULE", QByteArrayLiteral("qtvirtualkeyboard"));
    QGuiApplication app(argc, argv);
    QTemporaryDir config;
    QString out = QString::fromLocal8Bit(argv[1]);
    bool night = QString::fromLocal8Bit(argv[2]) == QLatin1String("night");
    QDir().mkpath(out);

    SkyBackend sky(config.path() + QStringLiteral("/astro-ui.ini"),
                   qEnvironmentVariable("ASTRO_CATALOG_DIR", QStringLiteral("/usr/share/astro")));
    if (argc < 4)
        sky.setLocation(51.4769, -0.0005, 46.0);
    sky.setNightMode(night);
    MountClient mount(&sky, config.path() + QStringLiteral("/astro-hub.conf"));
    InputMode input_mode;
    QTemporaryDir captures;
    CameraClient camera(captures.path());

    qmlRegisterType<SkyChart>("Astro", 1, 0, "SkyChart");
    qmlRegisterType<CameraView>("Astro", 1, 0, "CameraView");
    qmlRegisterUncreatableType<SkyBackend>("Astro", 1, 0, "SkyBackend", QStringLiteral("Use the sky object"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("sky"), &sky);
    engine.rootContext()->setContextProperty(QStringLiteral("mount"), &mount);
    engine.rootContext()->setContextProperty(QStringLiteral("inputMode"), &input_mode);
    engine.rootContext()->setContextProperty(QStringLiteral("camera"), &camera);
    engine.rootContext()->setContextProperty(QStringLiteral("runWindowed"), true);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        std::puts("FAIL: the interface did not load");
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    // As a tap on a tab does. page starts as a binding, which setProperty would leave in
    // place, so that any later settings update would send the app back to the first page.
    auto go_to_page = [&](int number) { QQmlProperty::write(window, QStringLiteral("page"), number); };
    window->setVisibility(QWindow::Windowed);
    // ASTRO_TEST_SIZE=1920x1080 tries the layout at the size of a monitor
    const QStringList size = qEnvironmentVariable("ASTRO_TEST_SIZE", QStringLiteral("800x480")).split(QLatin1Char('x'));
    window->resize(size.at(0).toInt(), size.at(1).toInt());
    wait_ms(4000);

    const char *names[] = {"tonight", "sky", "mount", "camera", "tools", "settings"};
    int count = argc < 4 ? 6 : 1;
    for (int page = 0; page < count; page++) {
        int index = argc < 4 ? page : 5;
        go_to_page(index);
        wait_ms(3000);
        QImage image = window->grabWindow();
        QString path = out + QLatin1Char('/') + (night ? QStringLiteral("night-") : QStringLiteral("day-")) +
                       QString::fromLatin1(argc < 4 ? names[page] : "firstrun") + QStringLiteral(".png");
        bool saved = !image.isNull() && image.save(path);
        std::printf("%s %s (%dx%d)\n", saved ? "ok" : "FAIL", qPrintable(path), image.width(), image.height());
        std::fflush(stdout);
    }
    // The chart from several directions, to catch drawing mistakes
    if (argc < 4) {
        auto *chart = window->findChild<SkyChart *>();
        const struct { const char *name; double az; double alt; double fov; } views[] = {
            {"sky-overhead", 180.0, 90.0, 180.0},
            {"sky-horizon", 180.0, 2.0, 90.0},
            {"sky-below", 180.0, -8.0, 60.0},
            {"sky-zoom", 120.0, 40.0, 25.0},
        };
        go_to_page(1);

        // The same sky at chosen hours, by moving to where the Sun is at those times
        const struct { const char *name; double sun_alt; double fov; double look_up; } times[] = {
            {"sky-sunset", -3.0, 90.0, 12.0},
            {"sky-daytime", 14.0, 120.0, 4.0},
        };
        for (const auto &when : times) {
            double offset = 0.0;
            for (double hours = -24.0; hours < 24.0; hours += 0.1) {
                astro::BodyReport sun = astro::body_report(astro::Body::Sun, sky.observer(), astro::julian_now() + hours / 24.0);
                if (std::fabs(sun.alt_deg - when.sun_alt) < 1.0) {
                    offset = hours;
                    break;
                }
            }
            chart->setProperty("timeOffsetHours", offset);
            astro::BodyReport sun = astro::body_report(astro::Body::Sun, sky.observer(), astro::julian_now() + offset / 24.0);
            chart->setCenterAz(sun.az_deg);
            chart->setCenterAlt(std::max(sun.alt_deg + when.look_up, 8.0));
            chart->setFieldOfView(when.fov);
            wait_ms(1500);
            QString path = out + QLatin1Char('/') + (night ? QStringLiteral("night-") : QStringLiteral("day-")) +
                           QString::fromLatin1(when.name) + QStringLiteral(".png");
            std::printf("%s %s\n", window->grabWindow().save(path) ? "ok" : "FAIL", qPrintable(path));
        }
        chart->setProperty("timeOffsetHours", 0.0);

        // Real shapes and sizes, close up, at dark sky
        chart->setProperty("showAtmosphere", false);
        const struct { const char *name; double ra; double dec; double fov; } close_ups[] = {
            {"sky-m31", 10.68, 41.27, 8.0},
            {"sky-orion", 83.82, -5.39, 20.0},
            {"sky-pleiades", 56.87, 24.11, 6.0},
        };
        // Move the clock to when each target is high in the sky
        auto when_high = [&sky](double ra, double dec) {
            double best = 0.0;
            double best_alt = -90.0;
            for (double hours = -12.0; hours < 12.0; hours += 0.25) {
                double az;
                double alt;
                astro::equatorial_to_horizontal(ra, dec, sky.observer(), astro::julian_now() + hours / 24.0, &az, &alt);
                if (alt > best_alt) {
                    best_alt = alt;
                    best = hours;
                }
            }
            return best;
        };
        auto save_view = [&](const QString &name) {
            wait_ms(1500);
            QString path = out + QLatin1Char('/') + (night ? QStringLiteral("night-") : QStringLiteral("day-")) + name + QStringLiteral(".png");
            std::printf("%s %s\n", window->grabWindow().save(path) ? "ok" : "FAIL", qPrintable(path));
        };
        for (const auto &shot : close_ups) {
            chart->setProperty("timeOffsetHours", when_high(shot.ra, shot.dec));
            chart->centerOn(shot.ra, shot.dec);
            chart->setFieldOfView(shot.fov);
            save_view(QString::fromLatin1(shot.name));
        }
        // The Moon moves against the stars, so look it up again for the chosen hour
        astro::BodyReport moon = astro::body_report(astro::Body::Moon, sky.observer(), astro::julian_now());
        double moon_hours = when_high(moon.ra_deg, moon.dec_deg);
        moon = astro::body_report(astro::Body::Moon, sky.observer(), astro::julian_now() + moon_hours / 24.0);
        chart->setProperty("timeOffsetHours", moon_hours);
        chart->centerOn(moon.ra_deg, moon.dec_deg);
        chart->setFieldOfView(1.6);
        save_view(QStringLiteral("sky-moon"));
        std::printf("Moon is %.0f%% lit\n", moon.illuminated * 100.0);

        // A nearly full Moon, to check the map's position and the lit shape
        for (double hours = 0.0; hours < 24.0 * 30.0; hours += 6.0) {
            astro::BodyReport m = astro::body_report(astro::Body::Moon, sky.observer(), astro::julian_now() + hours / 24.0);
            if (m.illuminated > 0.97) {
                // Pick the hour that day with the Moon highest
                double best = hours;
                double best_alt = -90.0;
                for (double h = hours; h < hours + 24.0; h += 1.0) {
                    astro::BodyReport r = astro::body_report(astro::Body::Moon, sky.observer(), astro::julian_now() + h / 24.0);
                    if (r.alt_deg > best_alt) {
                        best_alt = r.alt_deg;
                        best = h;
                        m = r;
                    }
                }
                chart->setProperty("timeOffsetHours", best);
                chart->centerOn(m.ra_deg, m.dec_deg);
                save_view(QStringLiteral("sky-moon-full"));
                break;
            }
        }
        chart->setProperty("timeOffsetHours", 0.0);
        chart->setProperty("showAtmosphere", true);

        for (const auto &view : views) {
            chart->setCenterAz(view.az);
            chart->setCenterAlt(view.alt);
            chart->setFieldOfView(view.fov);
            wait_ms(1500);
            QString path = out + QLatin1Char('/') + (night ? QStringLiteral("night-") : QStringLiteral("day-")) +
                           QString::fromLatin1(view.name) + QStringLiteral(".png");
            std::printf("%s %s\n", window->grabWindow().save(path) ? "ok" : "FAIL", qPrintable(path));

            // How long one frame takes to draw. Only useful for comparing builds,
            // because this runs under emulation rather than on a Pi.
            QImage frame(int(chart->width()), int(chart->height()), QImage::Format_ARGB32_Premultiplied);
            QElapsedTimer timer;
            qint64 total = 0;
            for (int i = 0; i < 10; i++) {
                chart->setCenterAz(view.az + i * 0.1);
                QPainter painter(&frame);
                timer.start();
                chart->paint(&painter);
                total += timer.nsecsElapsed();
            }
            std::printf("frame %s %.1f ms\n", view.name, total / 10.0 / 1e6);
            std::fflush(stdout);
        }

        // The cost of a frame while the time slider is being dragged
        {
            QImage frame(int(chart->width()), int(chart->height()), QImage::Format_ARGB32_Premultiplied);
            QElapsedTimer timer;
            qint64 total = 0;
            for (int i = 1; i <= 10; i++) {
                chart->setProperty("timeOffsetHours", i * 0.25);
                QPainter painter(&frame);
                timer.start();
                chart->paint(&painter);
                total += timer.nsecsElapsed();
            }
            chart->setProperty("timeOffsetHours", 0.0);
            std::printf("frame time-drag %.1f ms\n", total / 10.0 / 1e6);
            std::fflush(stdout);
        }

        // The panels on the Sky page
        auto *page = window->findChild<QObject *>(QStringLiteral("skyPage"));
        auto select = [&](const QString &name) {
            QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(sky.findExact(name))));
        };
        auto look_at = [&](const QString &name, double fov, double hours) {
            select(name);
            QMetaObject::invokeMethod(page, "setPreview", Q_ARG(QVariant, QVariant(int(std::round(hours / 24.0)))),
                                      Q_ARG(QVariant, QVariant(hours - std::round(hours / 24.0) * 24.0)));
            chart->centerOnTarget();
            chart->setFieldOfView(fov);
        };

        // A sensible preview of M31, with the details open
        double m31_hours = when_high(10.68, 41.27);
        look_at(QStringLiteral("M31"), 30.0, m31_hours);
        page->setProperty("detailsOpen", true);
        save_view(QStringLiteral("sky-details-m31"));
        look_at(QStringLiteral("Moon"), 30.0, 0.0);
        save_view(QStringLiteral("sky-details-moon"));
        page->setProperty("timeOpen", true);
        look_at(QStringLiteral("Jupiter"), 40.0, 3.5);
        save_view(QStringLiteral("sky-details-and-time"));
        page->setProperty("detailsOpen", false);
        look_at(QStringLiteral("M31"), 30.0, m31_hours);
        save_view(QStringLiteral("sky-time"));
        page->setProperty("timeOpen", false);
        save_view(QStringLiteral("sky-preview-chip"));

        // Rings, using made-up example equipment
        page->setProperty("ringsOpen", true);
        save_view(QStringLiteral("sky-rings-empty"));
        sky.setTelescopeFocal(1000.0);
        sky.addEyepiece(32.0, 52.0);
        sky.addEyepiece(25.0, 52.0);
        sky.addEyepiece(10.0, 82.0);
        wait_ms(300);
        look_at(QStringLiteral("M31"), 5.0, m31_hours);
        save_view(QStringLiteral("sky-rings"));
        page->setProperty("ringsOpen", false);
        save_view(QStringLiteral("sky-rings-closed"));

        // A tap on a crowded place, as the app would handle it
        QVariantMap pleiades = sky.findExact(QStringLiteral("M45"));
        look_at(QStringLiteral("M45"), 20.0, when_high(pleiades.value(QStringLiteral("ra")).toDouble(), pleiades.value(QStringLiteral("dec")).toDouble()));
        sky.setEyepieceOn(0, false);
        sky.setEyepieceOn(1, false);
        sky.setEyepieceOn(2, false);
        QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(QVariantMap())));
        for (int tap = 0; tap < 2; tap++) {
            QVariantMap tapped = chart->nextObjectAt(chart->width() / 2.0, chart->height() / 2.0);
            QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(tapped)));
            std::printf("tap %d: %s (%d of %d)\n", tap + 1, qPrintable(tapped.value(QStringLiteral("name")).toString()),
                        tapped.value(QStringLiteral("cycleIndex")).toInt(), tapped.value(QStringLiteral("cycleCount")).toInt());
        }
        save_view(QStringLiteral("sky-cycle"));

        // The Camera page with a picture on it, if a simulator is running
        go_to_page(3);
        wait_ms(1500);
        for (int i = 0; i < 100 && !camera.cameraFound(); i++)
            wait_ms(100);
        if (camera.cameraFound()) {
            camera.connectCamera();
            for (int i = 0; i < 200 && !camera.cameraConnected(); i++)
                wait_ms(100);
            wait_ms(2000);
            camera.capture(1.0, 1);
            for (int i = 0; i < 600 && camera.capturing(); i++)
                wait_ms(100);
            save_view(QStringLiteral("camera-picture"));
            camera.setShowDetail(true);
            save_view(QStringLiteral("camera-focus"));
            camera.setShowDetail(false);
            camera.setStretch(false);
            save_view(QStringLiteral("camera-plain"));
            camera.setStretch(true);
            camera.capture(600.0, 3);
            wait_ms(1500);
            save_view(QStringLiteral("camera-busy"));
            camera.cancel();
            std::printf("camera: %s\n", qPrintable(camera.statsText()));
        }
        go_to_page(1);
        wait_ms(300);

        // The list of keys
        window->setProperty("helpOpen", true);
        save_view(QStringLiteral("keys"));
        window->setProperty("helpOpen", false);

        // Equipment in Settings
        go_to_page(5);
        wait_ms(500);
        if (auto *scroll = window->findChild<QObject *>(QStringLiteral("settingsScroll")))
            scroll->setProperty("contentY", scroll->property("contentHeight").toDouble() - scroll->property("height").toDouble());
        save_view(QStringLiteral("settings-equipment"));
    }
    return 0;
}
