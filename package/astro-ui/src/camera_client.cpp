#include "camera_client.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>

#include <cmath>

#include "camera_math.h"

static const int kServerPort = 7624;
static const int kReconnectMs = 3000;
// Downloading a large frame over USB can take a while after the exposure has ended
static const int kDownloadGraceMs = 120000;
static const qint64 kDefaultMinimumFreeBytes = 200LL * 1024 * 1024;
static const double kMinExposure = 0.001;
static const double kMaxExposure = 3600.0;
static const int kMaxFrames = 999;
static const int kFitWidth = 1280;
static const int kDetailWidth = 800;
static const int kDetailHeight = 480;
static const int kHistogramBins = 128;

CameraClient::CameraClient(const QString& capture_directory, QObject* parent)
    : QObject(parent),
      m_capture_directory(capture_directory),
      m_connect_running(false),
      m_server_up(false),
      m_connected(false),
      m_capturing(false),
      m_waiting_for_frame(false),
      m_frame_number(0),
      m_frame_count(0),
      m_exposure_seconds(1.0),
      m_seconds_left(0.0),
      m_has_gain(false),
      m_gain(0.0),
      m_gain_min(0.0),
      m_gain_max(0.0),
      m_iso_index(-1),
      m_has_temperature(false),
      m_temperature(0.0),
      m_minimum_free_bytes(kDefaultMinimumFreeBytes),
      m_last_frame_bytes(0),
      m_stretch(true),
      m_show_detail(false),
      m_night_mode(false)
{
    setServer("localhost", kServerPort);
    setConnectionTimeout(1, 0);

    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        finish_sequence(tr("The camera didn't send the picture in time, so the sequence stopped."));
    });
    connect(&m_reconnect_timer, &QTimer::timeout, this, &CameraClient::try_connect_to_server);
    m_reconnect_timer.start(kReconnectMs);
    try_connect_to_server();
}

CameraClient::~CameraClient()
{
    m_reconnect_timer.stop();
    join_worker();
    // The thread trying to connect uses this object, so it has to finish first
    while (m_connect_running)
        std::this_thread::yield();
    if (isServerConnected())
        disconnectServer();
}

void CameraClient::try_connect_to_server()
{
    if (isServerConnected() || m_connect_running.exchange(true))
        return;

    // Connecting blocks until the timeout, so keep it off the UI thread
    std::thread([this]() {
        connectServer();
        m_connect_running = false;
    }).detach();
}

void CameraClient::serverConnected()
{
    QMetaObject::invokeMethod(this, [this]() {
        m_server_up = true;
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void CameraClient::serverDisconnected(int)
{
    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        m_device.clear();
    }
    QMetaObject::invokeMethod(this, [this]() {
        m_server_up = false;
        m_connected = false;
        m_qt_device.clear();
        if (m_capturing)
            finish_sequence(tr("Lost contact with the camera, so the sequence stopped."));
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void CameraClient::removeDevice(INDI::BaseDevice device)
{
    if (device_name() != device.getDeviceName())
        return;
    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        m_device.clear();
    }
    QMetaObject::invokeMethod(this, [this]() {
        m_connected = false;
        m_qt_device.clear();
        if (m_capturing)
            finish_sequence(tr("Lost contact with the camera, so the sequence stopped."));
        emit stateChanged();
    }, Qt::QueuedConnection);
}

std::string CameraClient::device_name() const
{
    std::lock_guard<std::mutex> lock(m_device_mutex);
    return m_device;
}

void CameraClient::newProperty(INDI::Property property)
{
    // A driver says that it's a camera through DRIVER_INFO. Its other properties can
    // arrive first, including CONNECTION, so go back over those once it's known.
    if (property.isNameMatch("DRIVER_INFO") && device_name().empty()) {
        INDI::BaseDevice device = property.getBaseDevice();
        if (device.getDriverInterface() & INDI::BaseDevice::CCD_INTERFACE) {
            QString name = QString::fromUtf8(device.getDeviceName());
            {
                std::lock_guard<std::mutex> lock(m_device_mutex);
                m_device = device.getDeviceName();
            }
            QMetaObject::invokeMethod(this, [this, name]() {
                m_qt_device = name;
                emit stateChanged();
            }, Qt::QueuedConnection);
            setBLOBMode(B_ALSO, device.getDeviceName(), "CCD1");
            for (INDI::Property known : device.getProperties())
                read_property(known);
            return;
        }
    }
    read_property(property);
}

void CameraClient::updateProperty(INDI::Property property)
{
    read_property(property);
}

void CameraClient::read_property(INDI::Property property)
{
    std::string name = device_name();
    if (name.empty() || !property.getBaseDevice().isDeviceNameMatch(name))
        return;

    if (property.isNameMatch("CONNECTION")) {
        INDI::PropertySwitch connection(property);
        auto connect_switch = connection.findWidgetByName("CONNECT");
        bool connected = connect_switch && connect_switch->getState() == ISS_ON;
        QMetaObject::invokeMethod(this, [this, connected]() {
            bool was_connected = m_connected;
            m_connected = connected;
            if (connected && !was_connected)
                prepare_device();
            if (!connected && m_capturing)
                finish_sequence(tr("The camera disconnected, so the sequence stopped."));
            emit stateChanged();
        }, Qt::QueuedConnection);
    } else if (property.isNameMatch("CCD_EXPOSURE")) {
        INDI::PropertyNumber exposure(property);
        auto value = exposure.findWidgetByName("CCD_EXPOSURE_VALUE");
        double left = value ? value->getValue() : 0.0;
        IPState state = exposure.getState();
        QMetaObject::invokeMethod(this, [this, left, state]() {
            m_seconds_left = left;
            if (state == IPS_ALERT && m_capturing)
                finish_sequence(tr("The camera reported a problem, so the sequence stopped."));
            emit stateChanged();
        }, Qt::QueuedConnection);
    } else if (property.isNameMatch("CCD_GAIN")) {
        INDI::PropertyNumber gain(property);
        auto value = gain.findWidgetByName("GAIN");
        if (value) {
            double now = value->getValue();
            double low = value->getMin();
            double high = value->getMax();
            QMetaObject::invokeMethod(this, [this, now, low, high]() {
                m_has_gain = true;
                m_gain = now;
                m_gain_min = low;
                m_gain_max = high;
                emit stateChanged();
            }, Qt::QueuedConnection);
        }
    } else if (property.isNameMatch("CCD_ISO")) {
        INDI::PropertySwitch iso(property);
        QStringList names;
        QStringList labels;
        int current = -1;
        for (std::size_t i = 0; i < iso.size(); i++) {
            names << QString::fromUtf8(iso[i].getName());
            labels << QString::fromUtf8(iso[i].getLabel());
            if (iso[i].getState() == ISS_ON)
                current = int(i);
        }
        QMetaObject::invokeMethod(this, [this, names, labels, current]() {
            m_iso_names = names;
            m_iso_options = labels;
            m_iso_index = current;
            emit stateChanged();
        }, Qt::QueuedConnection);
    } else if (property.isNameMatch("CCD_TEMPERATURE")) {
        INDI::PropertyNumber temperature(property);
        auto value = temperature.findWidgetByName("CCD_TEMPERATURE_VALUE");
        if (value) {
            double now = value->getValue();
            QMetaObject::invokeMethod(this, [this, now]() {
                m_has_temperature = true;
                m_temperature = now;
                emit stateChanged();
            }, Qt::QueuedConnection);
        }
    } else if (property.isNameMatch("CCD1") && property.getType() == INDI_BLOB && property.getState() == IPS_OK) {
        INDI::PropertyBlob blob(property);
        if (blob.size() > 0 && blob[0].getBlobLen() > 0 && blob[0].getBlob()) {
            QByteArray data(static_cast<const char*>(blob[0].getBlob()), blob[0].getBlobLen());
            QString format = QString::fromUtf8(blob[0].getFormat());
            QMetaObject::invokeMethod(this, [this, data, format]() { frame_arrived(data, format); }, Qt::QueuedConnection);
        }
    }
}

bool CameraClient::select_switch(const char* property_name, const char* element_name)
{
    std::string name = device_name();
    if (name.empty())
        return false;
    INDI::BaseDevice device = getDevice(name.c_str());
    if (!device.isValid())
        return false;
    INDI::PropertySwitch property = device.getProperty(property_name);
    if (!property.isValid())
        return false;
    auto element = property.findWidgetByName(element_name);
    if (!element)
        return false;

    // The helper that takes names would leave the previous choice on as well
    property.reset();
    element->setState(ISS_ON);
    sendNewSwitch(property);
    return true;
}

void CameraClient::prepare_device()
{
    // FITS holds the camera's real numbers, and it's the one format every driver can
    // send and this app can always show. Not all drivers have all of these.
    select_switch("CCD_TRANSFER_FORMAT", "FORMAT_FITS");
    select_switch("CCD_COMPRESSION", "INDI_DISABLED");
    select_switch("UPLOAD_MODE", "UPLOAD_CLIENT");
}

void CameraClient::connectCamera()
{
    std::string name = device_name();
    if (!name.empty())
        connectDevice(name.c_str());
    m_message.clear();
    emit stateChanged();
}

void CameraClient::disconnectCamera()
{
    if (m_capturing)
        cancel();
    std::string name = device_name();
    if (!name.empty())
        disconnectDevice(name.c_str());
}

double CameraClient::nextExposure(double seconds, int direction) const
{
    return camera::next_exposure(seconds, direction);
}

QString CameraClient::disk_problem() const
{
    if (!QDir().mkpath(m_capture_directory))
        return tr("Could not create the folder for pictures, %1.").arg(m_capture_directory);

    QStorageInfo storage(m_capture_directory);
    if (!storage.isValid() || storage.isReadOnly())
        return tr("The folder for pictures can't be written to.");
    // A frame must fit, and a full disk would stop settings being saved too
    if (storage.bytesAvailable() < m_minimum_free_bytes + m_last_frame_bytes * 2)
        return tr("The disk is nearly full, so no more pictures can be saved.");
    return QString();
}

QString CameraClient::freeSpaceText() const
{
    QStorageInfo storage(m_capture_directory);
    if (!storage.isValid())
        return QString();
    return tr("%1 GB free").arg(double(storage.bytesAvailable()) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QString CameraClient::capture(double seconds, int count)
{
    // Written this way round so that a value that isn't a number is refused too
    if (!(seconds >= kMinExposure && seconds <= kMaxExposure))
        return tr("Enter an exposure from %1 to %2 seconds.").arg(kMinExposure).arg(kMaxExposure);
    if (count < 1 || count > kMaxFrames)
        return tr("Enter from 1 to %1 pictures.").arg(kMaxFrames);
    if (!m_connected)
        return tr("The camera isn't connected.");
    if (m_capturing)
        return tr("The camera is already busy.");
    QString problem = disk_problem();
    if (!problem.isEmpty())
        return problem;

    m_capturing = true;
    m_exposure_seconds = seconds;
    m_frame_count = count;
    m_frame_number = 0;
    m_message.clear();
    start_exposure();
    return QString();
}

void CameraClient::start_exposure()
{
    m_frame_number++;
    m_seconds_left = m_exposure_seconds;
    m_waiting_for_frame = true;
    m_watchdog.start(int(m_exposure_seconds * 1000.0) + kDownloadGraceMs);
    sendNewNumber(device_name().c_str(), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", m_exposure_seconds);
    emit stateChanged();
}

void CameraClient::cancel()
{
    if (!m_capturing)
        return;
    select_switch("CCD_ABORT_EXPOSURE", "ABORT");
    finish_sequence(tr("Cancelled."));
}

void CameraClient::finish_sequence(const QString& message)
{
    m_watchdog.stop();
    m_capturing = false;
    m_waiting_for_frame = false;
    m_seconds_left = 0.0;
    m_message = message;
    emit stateChanged();
}

QString CameraClient::setGain(double gain)
{
    if (!m_has_gain)
        return tr("This camera has no gain setting.");
    if (!(gain >= m_gain_min && gain <= m_gain_max))
        return tr("Enter a gain from %1 to %2.").arg(m_gain_min).arg(m_gain_max);
    sendNewNumber(device_name().c_str(), "CCD_GAIN", "GAIN", gain);
    return QString();
}

void CameraClient::setIso(int index)
{
    if (index >= 0 && index < m_iso_names.size())
        select_switch("CCD_ISO", m_iso_names[index].toUtf8().constData());
}

QString CameraClient::frame_path(const QString& directory, const QString& date, const QString& time, const QString& suffix)
{
    QString base = directory + QLatin1Char('/') + date + QLatin1Char('/') + date + QLatin1Char('_') + time;
    QString path = base + suffix;
    // Pictures taken within the same second, or after the clock has been reset. A
    // night's work must never be overwritten by the next frame.
    for (int extra = 2; QFile::exists(path); extra++)
        path = base + QStringLiteral("_%1").arg(extra) + suffix;
    return path;
}

void CameraClient::frame_arrived(const QByteArray& data, const QString& format)
{
    // Pictures that this app didn't ask for, such as one taken by a laptop's app
    if (!m_waiting_for_frame)
        return;
    m_waiting_for_frame = false;
    m_watchdog.stop();
    join_worker();

    QString suffix = format.startsWith(QLatin1Char('.')) ? format : QStringLiteral(".fits");
    QDateTime now = QDateTime::currentDateTimeUtc();
    QString directory = m_capture_directory;
    QString date = now.toString(QStringLiteral("yyyy-MM-dd"));
    QString time = now.toString(QStringLiteral("HHmmss"));

    m_worker = std::thread([this, data, suffix, directory, date, time]() {
        FrameResult result;
        result.bytes = data.size();
        QDir().mkpath(directory + QLatin1Char('/') + date);
        QString path = frame_path(directory, date, time, suffix);

        // QSaveFile writes to a temporary name first, so that running out of space or
        // power can't leave half a picture looking like a whole one
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
            result.saved_path = path;
        else
            result.error = tr("Could not save the picture: %1").arg(file.errorString());

        bool is_fits = suffix.contains(QStringLiteral("fit"), Qt::CaseInsensitive) || suffix.contains(QStringLiteral(".fz"));
        result.frame = is_fits ? read_fits_frame(data, kFitWidth, kDetailWidth, kDetailHeight)
                               : read_picture_frame(data, kFitWidth, kDetailWidth, kDetailHeight);
        QMetaObject::invokeMethod(this, [this, result]() { frame_ready(result); }, Qt::QueuedConnection);
    });
}

void CameraClient::frame_ready(const FrameResult& result)
{
    if (!result.saved_path.isEmpty()) {
        m_last_saved = result.saved_path;
        m_last_frame_bytes = result.bytes;
    }
    if (result.frame.error.isEmpty()) {
        m_frame = result.frame;
        m_histogram = frame_histogram(m_frame.fit, m_frame.range, kHistogramBins);
        m_stats_text = tr("%1 × %2, background %3, peak %4, %5% at the limit")
            .arg(m_frame.full_width).arg(m_frame.full_height)
            .arg(m_frame.stats.median, 0, 'f', 0)
            .arg(m_frame.stats.peak, 0, 'f', 0)
            .arg(m_frame.stats.clipped * 100.0, 0, 'f', 1);
        refresh_preview();
    } else {
        m_message = result.frame.error;
    }
    emit frameChanged();

    // Nothing is lost by stopping, and carrying on would only lose more pictures
    if (!result.error.isEmpty()) {
        finish_sequence(result.error);
        return;
    }
    if (!m_capturing)
        return;
    if (m_frame_number >= m_frame_count) {
        finish_sequence(result.frame.error);
        return;
    }
    QString problem = disk_problem();
    if (!problem.isEmpty()) {
        finish_sequence(problem);
        return;
    }
    start_exposure();
}

void CameraClient::refresh_preview()
{
    const FramePlane& plane = m_show_detail ? m_frame.detail : m_frame.fit;
    // The statistics come from the whole frame, so the two views match in brightness
    m_preview = render_frame(plane, m_frame.stats, m_frame.range, m_stretch, m_night_mode);
    emit previewChanged();
}

void CameraClient::setStretch(bool enabled)
{
    m_stretch = enabled;
    refresh_preview();
}

void CameraClient::setShowDetail(bool enabled)
{
    m_show_detail = enabled;
    refresh_preview();
}

void CameraClient::setNightMode(bool enabled)
{
    m_night_mode = enabled;
    refresh_preview();
}

void CameraClient::join_worker()
{
    if (m_worker.joinable())
        m_worker.join();
}
