// Saves a screenshot of every page of the real interface, without a display.
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml>

#include <cmath>
#include <cstdio>

#include "astro/sky.h"
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

    qmlRegisterType<SkyChart>("Astro", 1, 0, "SkyChart");
    qmlRegisterUncreatableType<SkyBackend>("Astro", 1, 0, "SkyBackend", QStringLiteral("Use the sky object"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("sky"), &sky);
    engine.rootContext()->setContextProperty(QStringLiteral("mount"), &mount);
    engine.rootContext()->setContextProperty(QStringLiteral("runWindowed"), true);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        std::puts("FAIL: the interface did not load");
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->setVisibility(QWindow::Windowed);
    window->resize(800, 480);
    wait_ms(4000);

    const char *names[] = {"tonight", "sky", "mount", "tools", "settings"};
    int count = argc < 4 ? 5 : 1;
    for (int page = 0; page < count; page++) {
        int index = argc < 4 ? page : 4;
        window->setProperty("page", index);
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
        window->setProperty("page", 1);

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
    }
    return 0;
}
