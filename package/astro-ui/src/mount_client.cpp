#include "mount_client.h"

#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QTextStream>

#include <cmath>
#include <thread>

#include "astro/sky.h"
#include "sky_backend.h"

static const int kServerPort = 7624;
static const int kReconnectMs = 3000;
static const char kDefaultDriver[] = "indi_simulator_telescope";
static const char kDefaultPort[] = "/dev/ttyUSB0";
static const char kHubScript[] = "/etc/init.d/S85astro-hub";

MountClient::MountClient(SkyBackend* sky, const QString& hub_config_path, QObject* parent)
    : QObject(parent),
      m_sky(sky),
      m_hub_config_path(hub_config_path),
      m_connect_running(false),
      m_driver(QString::fromLatin1(kDefaultDriver)),
      m_port(QString::fromLatin1(kDefaultPort)),
      m_remote_access(false),
      m_server_up(false),
      m_connected(false),
      m_tracking(false),
      m_slewing(false),
      m_parked(false),
      m_ra(0.0),
      m_dec(0.0)
{
    load_profile();
    setServer("localhost", kServerPort);
    setConnectionTimeout(1, 0);

    // A mount needs the right time and place to know where it's really pointing
    connect(m_sky, &SkyBackend::observingSiteChanged, this, &MountClient::send_location_and_time);

    connect(&m_reconnect_timer, &QTimer::timeout, this, &MountClient::try_connect_to_server);
    m_reconnect_timer.start(kReconnectMs);
    try_connect_to_server();
}

MountClient::~MountClient()
{
    m_reconnect_timer.stop();
    if (isServerConnected())
        disconnectServer();
}

QVariantList MountClient::drivers() const
{
    static const struct { const char* label; const char* binary; bool serial; } kDrivers[] = {
        {"Simulator (no hardware)", "indi_simulator_telescope", false},
        {"DIY steppers (OnStep)", "indi_lx200_OnStep", true},
        {"Meade LX200 Autostar", "indi_lx200autostar", true},
        {"Meade LX200 GPS", "indi_lx200gps", true},
        {"LX200 Basic", "indi_lx200basic", true},
        {"Celestron", "indi_celestron_gps", true},
        {"Sky-Watcher SynScan", "indi_synscan_telescope", true},
        {"Sky-Watcher direct (EQMod)", "indi_eqmod_telescope", true},
        {"iOptron", "indi_ioptronv3_telescope", true},
        {"RC servos (pointing only)", "indi_astro_servo", false},
    };
    QVariantList list;

    for (const auto& d : kDrivers) {
        QVariantMap map;
        map[QStringLiteral("label")] = QString::fromLatin1(d.label);
        map[QStringLiteral("binary")] = QString::fromLatin1(d.binary);
        map[QStringLiteral("serial")] = d.serial;
        list.append(map);
    }
    return list;
}

void MountClient::load_profile()
{
    QFile file(m_hub_config_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        QString key = line.left(equals);
        QString value = line.mid(equals + 1).remove(QLatin1Char('"'));
        if (key == QLatin1String("DRIVERS") && !value.isEmpty())
            m_driver = value.section(QLatin1Char(' '), 0, 0);
        else if (key == QLatin1String("PORT") && !value.isEmpty())
            m_port = value;
        else if (key == QLatin1String("REMOTE_ACCESS"))
            m_remote_access = value == QLatin1String("yes");
    }
}

void MountClient::applyProfile(const QString& driver, const QString& port, bool remote_access)
{
    m_driver = driver;
    m_port = port;
    m_remote_access = remote_access;

    QFile file(m_hub_config_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream stream(&file);
        stream << "DRIVERS=\"" << driver << "\"\n";
        stream << "PORT=\"" << port << "\"\n";
        stream << "REMOTE_ACCESS=\"" << (remote_access ? "yes" : "no") << "\"\n";
    } else {
        m_message = tr("Could not save the mount settings.");
    }
    emit profileChanged();
    emit stateChanged();

    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        m_device.clear();
    }
    m_qt_device.clear();
    if (isServerConnected())
        disconnectServer();
    QProcess::startDetached(QStringLiteral("sudo"), {QStringLiteral("-n"), QString::fromLatin1(kHubScript), QStringLiteral("restart")});
}

void MountClient::try_connect_to_server()
{
    if (isServerConnected() || m_connect_running.exchange(true))
        return;

    // Connecting blocks until the timeout, so keep it off the UI thread
    std::thread([this]() {
        connectServer();
        m_connect_running = false;
    }).detach();
}

void MountClient::serverConnected()
{
    QMetaObject::invokeMethod(this, [this]() {
        m_server_up = true;
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void MountClient::serverDisconnected(int)
{
    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        m_device.clear();
    }
    QMetaObject::invokeMethod(this, [this]() {
        m_server_up = false;
        m_connected = false;
        m_slewing = false;
        m_qt_device.clear();
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void MountClient::removeDevice(INDI::BaseDevice device)
{
    std::string name = device.getDeviceName();
    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        if (name != m_device)
            return;
        m_device.clear();
    }
    QMetaObject::invokeMethod(this, [this]() {
        m_qt_device.clear();
        m_connected = false;
        emit stateChanged();
    }, Qt::QueuedConnection);
}

std::string MountClient::device_name() const
{
    std::lock_guard<std::mutex> lock(m_device_mutex);
    return m_device;
}

void MountClient::newProperty(INDI::Property property)
{
    // A driver identifies itself as a telescope through DRIVER_INFO
    if (property.isNameMatch("DRIVER_INFO")) {
        INDI::BaseDevice device = property.getBaseDevice();
        if (device.getDriverInterface() & INDI::BaseDevice::TELESCOPE_INTERFACE) {
            QString name = QString::fromUtf8(device.getDeviceName());
            {
                std::lock_guard<std::mutex> lock(m_device_mutex);
                m_device = device.getDeviceName();
            }
            QMetaObject::invokeMethod(this, [this, name]() {
                m_qt_device = name;
                emit stateChanged();
            }, Qt::QueuedConnection);

            // Some properties arrive before this one, and were ignored because the
            // telescope wasn't known yet. That includes CONNECTION, so without this
            // a program starting while the mount is connected would never notice.
            for (INDI::Property known : device.getProperties())
                handle_property(known);
            return;
        }
    }
    handle_property(property);
}

void MountClient::handle_property(INDI::Property property)
{
    std::string name = device_name();
    bool ours = !name.empty() && property.getBaseDevice().isDeviceNameMatch(name);

    // The port can't change on a mount that's already connected
    if (ours && property.isNameMatch("DEVICE_PORT") && !property.getBaseDevice().isConnected()) {
        QByteArray port = m_port.toUtf8();
        sendNewText(name.c_str(), "DEVICE_PORT", "PORT", port.constData());
    }
    // Mounts need the location and time to convert sky coordinates
    if (ours && (property.isNameMatch("GEOGRAPHIC_COORD") || property.isNameMatch("TIME_UTC")))
        QMetaObject::invokeMethod(this, [this]() { send_location_and_time(); }, Qt::QueuedConnection);

    read_property(property);
}

void MountClient::send_location_and_time()
{
    std::string name = device_name();
    if (name.empty() || !m_sky->locationSet())
        return;

    INDI::BaseDevice device = getDevice(name.c_str());
    if (!device.isValid())
        return;

    const astro::Observer& observer = m_sky->observer();
    INDI::PropertyNumber geographic = device.getProperty("GEOGRAPHIC_COORD");
    if (geographic.isValid()) {
        auto latitude = geographic.findWidgetByName("LAT");
        auto longitude = geographic.findWidgetByName("LONG");
        auto elevation = geographic.findWidgetByName("ELEV");
        if (latitude && longitude && elevation) {
            latitude->setValue(observer.lat_deg);
            // INDI expects east longitude from 0 to 360
            longitude->setValue(observer.lon_deg < 0.0 ? observer.lon_deg + 360.0 : observer.lon_deg);
            elevation->setValue(observer.elev_m);
            sendNewNumber(geographic);
        }
    }

    // Better to leave the mount's own clock alone than to tell it that it's 1970
    if (!astro::clock_looks_set(astro::julian_now()))
        return;

    INDI::PropertyText time = device.getProperty("TIME_UTC");
    if (time.isValid()) {
        auto utc = time.findWidgetByName("UTC");
        auto offset = time.findWidgetByName("OFFSET");
        if (utc && offset) {
            QByteArray now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8();
            now.chop(now.endsWith('Z') ? 1 : 0);
            QByteArray hours = QByteArray::number(m_sky->utcOffsetMinutes() / 60.0);
            utc->setText(now.constData());
            offset->setText(hours.constData());
            sendNewText(time);
        }
    }
}

void MountClient::updateProperty(INDI::Property property)
{
    read_property(property);
}

void MountClient::read_property(INDI::Property property)
{
    std::string name = device_name();
    if (name.empty() || !property.getBaseDevice().isDeviceNameMatch(name))
        return;

    if (property.isNameMatch("EQUATORIAL_EOD_COORD")) {
        INDI::PropertyNumber coords(property);
        auto ra = coords.findWidgetByName("RA");
        auto dec = coords.findWidgetByName("DEC");
        if (!ra || !dec)
            return;
        double ra_deg = ra->getValue() * 15.0;
        double dec_deg = dec->getValue();
        bool slewing = coords.getState() == IPS_BUSY;
        QMetaObject::invokeMethod(this, [this, ra_deg, dec_deg, slewing]() {
            m_ra = ra_deg;
            m_dec = dec_deg;
            m_slewing = slewing;
            emit stateChanged();
        }, Qt::QueuedConnection);
    } else if (property.isNameMatch("CONNECTION")) {
        INDI::PropertySwitch connection(property);
        auto on = connection.findWidgetByName("CONNECT");
        bool connected = on && on->getState() == ISS_ON && connection.getState() == IPS_OK;
        bool failed = connection.getState() == IPS_ALERT;
        QMetaObject::invokeMethod(this, [this, connected, failed]() {
            m_connected = connected;
            if (failed)
                m_message = tr("The mount did not respond. Check the cable and port.");
            else if (connected)
                m_message.clear();
            emit stateChanged();
        }, Qt::QueuedConnection);
    } else if (property.isNameMatch("TELESCOPE_TRACK_STATE")) {
        INDI::PropertySwitch track(property);
        auto on = track.findWidgetByName("TRACK_ON");
        bool tracking = on && on->getState() == ISS_ON;
        QMetaObject::invokeMethod(this, [this, tracking]() {
            m_tracking = tracking;
            emit stateChanged();
        }, Qt::QueuedConnection);
    } else if (property.isNameMatch("TELESCOPE_PARK")) {
        INDI::PropertySwitch parking(property);
        auto on = parking.findWidgetByName("PARK");
        bool parked = on && on->getState() == ISS_ON;
        QMetaObject::invokeMethod(this, [this, parked]() {
            m_parked = parked;
            emit stateChanged();
        }, Qt::QueuedConnection);
    }
}

void MountClient::connectMount()
{
    std::string name = device_name();
    if (!name.empty())
        connectDevice(name.c_str());
}

void MountClient::disconnectMount()
{
    std::string name = device_name();
    if (!name.empty())
        disconnectDevice(name.c_str());
}

// Turn on one switch and all others off. INDI's own helper only turns one on and
// leaves the previous choice on too, so the driver would act on the old one: a
// Sync could become a GoTo, and tracking could not be turned off.
void MountClient::select_switch(const char* property_name, const char* element_name)
{
    std::string name = device_name();
    if (name.empty())
        return;

    INDI::BaseDevice device = getDevice(name.c_str());
    if (!device.isValid())
        return;

    INDI::PropertySwitch property = device.getProperty(property_name);
    if (!property.isValid())
        return;

    auto element = property.findWidgetByName(element_name);
    if (!element)
        return;

    property.reset();
    element->setState(ISS_ON);
    sendNewSwitch(property);
}

bool MountClient::send_position(const char* mode, double ra_deg, double dec_deg)
{
    std::string name = device_name();
    if (name.empty())
        return false;

    INDI::BaseDevice device = getDevice(name.c_str());
    if (!device.isValid())
        return false;

    INDI::PropertyNumber coords = device.getProperty("EQUATORIAL_EOD_COORD");
    if (!coords.isValid())
        return false;

    auto ra = coords.findWidgetByName("RA");
    auto dec = coords.findWidgetByName("DEC");
    if (!ra || !dec)
        return false;

    select_switch("ON_COORD_SET", mode);
    ra->setValue(ra_deg / 15.0);
    dec->setValue(dec_deg);
    sendNewNumber(coords);
    return true;
}

QString MountClient::gotoTarget(double ra_deg, double dec_deg, bool solar_filter_fitted)
{
    if (!astro::clock_looks_set(astro::julian_now()))
        return tr("Refused: the clock isn't set, so the Sun's position is unknown. Connect to the internet, or set the clock in Settings.");

    switch (astro::check_goto(ra_deg, dec_deg, m_sky->observer(), astro::julian_now(),
                              astro::kDefaultGotoLimits, solar_filter_fitted)) {
    case astro::GotoVerdict::TooCloseToSun:
        return tr("Refused: too close to the Sun. Fit a solar filter and confirm it in Settings first.");
    case astro::GotoVerdict::BelowHorizon:
        return tr("Refused: that position is below the horizon.");
    case astro::GotoVerdict::Allowed:
        break;
    }
    if (!m_connected || !send_position("TRACK", ra_deg, dec_deg))
        return tr("The mount is not connected.");
    return QString();
}

QString MountClient::syncTarget(double ra_deg, double dec_deg)
{
    if (!m_connected || !send_position("SYNC", ra_deg, dec_deg))
        return tr("The mount is not connected.");
    return QString();
}

void MountClient::abort()
{
    std::string name = device_name();
    if (!name.empty())
        select_switch("TELESCOPE_ABORT_MOTION", "ABORT");
    m_message = tr("Stopped.");
    emit stateChanged();
}

void MountClient::setTracking(bool enabled)
{
    std::string name = device_name();
    if (!name.empty())
        select_switch("TELESCOPE_TRACK_STATE", enabled ? "TRACK_ON" : "TRACK_OFF");
}

void MountClient::moveStart(const QString& direction)
{
    std::string name = device_name();
    if (name.empty())
        return;

    if (direction == QLatin1String("N"))
        select_switch("TELESCOPE_MOTION_NS", "MOTION_NORTH");
    else if (direction == QLatin1String("S"))
        select_switch("TELESCOPE_MOTION_NS", "MOTION_SOUTH");
    else if (direction == QLatin1String("E"))
        select_switch("TELESCOPE_MOTION_WE", "MOTION_EAST");
    else if (direction == QLatin1String("W"))
        select_switch("TELESCOPE_MOTION_WE", "MOTION_WEST");
}

void MountClient::moveStop()
{
    std::string name = device_name();
    if (name.empty())
        return;

    INDI::BaseDevice device = getDevice(name.c_str());
    for (const char* axis : {"TELESCOPE_MOTION_NS", "TELESCOPE_MOTION_WE"}) {
        INDI::PropertySwitch motion = device.getProperty(axis);
        if (motion.isValid()) {
            motion.reset();
            sendNewSwitch(motion);
        }
    }
}

void MountClient::park()
{
    std::string name = device_name();
    if (!name.empty())
        select_switch("TELESCOPE_PARK", "PARK");
}

void MountClient::unpark()
{
    std::string name = device_name();
    if (!name.empty())
        select_switch("TELESCOPE_PARK", "UNPARK");
}
