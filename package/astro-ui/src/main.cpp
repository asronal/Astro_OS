#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QtQml>

#include "input_mode.h"
#include "mount_client.h"
#include "sky_backend.h"
#include "sky_chart.h"

int main(int argc, char *argv[])
{
    qputenv("QT_IM_MODULE", QByteArrayLiteral("qtvirtualkeyboard"));

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("astro-ui"));

    QString config_dir = qEnvironmentVariable("ASTRO_CONFIG_DIR", QStringLiteral("/data/config"));
    QString catalog_dir = qEnvironmentVariable("ASTRO_CATALOG_DIR", QStringLiteral("/usr/share/astro"));

    SkyBackend sky(config_dir + QStringLiteral("/astro-ui.ini"), catalog_dir);
    MountClient mount(&sky, config_dir + QStringLiteral("/astro-hub.conf"));
    sky.startAutoDetect();
    InputMode input_mode;

    qmlRegisterType<SkyChart>("Astro", 1, 0, "SkyChart");
    qmlRegisterUncreatableType<SkyBackend>("Astro", 1, 0, "SkyBackend", QStringLiteral("Use the sky object"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("sky"), &sky);
    engine.rootContext()->setContextProperty(QStringLiteral("mount"), &mount);
    engine.rootContext()->setContextProperty(QStringLiteral("inputMode"), &input_mode);
    // For trying the app in a window on a PC
    engine.rootContext()->setContextProperty(QStringLiteral("runWindowed"), qEnvironmentVariableIsSet("ASTRO_WINDOWED"));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
