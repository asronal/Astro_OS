// Saves a screenshot of every page of the real interface, without a display.
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml>

#include <cstdio>

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

    SkyBackend sky(config.path() + QStringLiteral("/astro-ui.ini"), QStringLiteral("/usr/share/astro"));
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
        for (const auto &view : views) {
            chart->setCenterAz(view.az);
            chart->setCenterAlt(view.alt);
            chart->setFieldOfView(view.fov);
            wait_ms(1500);
            QString path = out + QLatin1Char('/') + (night ? QStringLiteral("night-") : QStringLiteral("day-")) +
                           QString::fromLatin1(view.name) + QStringLiteral(".png");
            std::printf("%s %s\n", window->grabWindow().save(path) ? "ok" : "FAIL", qPrintable(path));
            std::fflush(stdout);
        }
    }
    return 0;
}
