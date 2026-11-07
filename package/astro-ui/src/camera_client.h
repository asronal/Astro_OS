#ifndef ASTRO_UI_CAMERA_CLIENT_H
#define ASTRO_UI_CAMERA_CLIENT_H

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <baseclient.h>
#include <basedevice.h>

#include "camera_image.h"

// Controls a camera through the local INDI server. Each picture arrives as data, which
// is saved and then prepared for showing. INDI calls back on its own thread, and that
// work happens on another, so results reach the UI thread as queued calls.
class CameraClient : public QObject, public INDI::BaseClient {
    Q_OBJECT
    Q_PROPERTY(bool serverUp READ serverUp NOTIFY stateChanged)
    Q_PROPERTY(bool cameraFound READ cameraFound NOTIFY stateChanged)
    Q_PROPERTY(bool cameraConnected READ cameraConnected NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY stateChanged)
    Q_PROPERTY(int frameNumber READ frameNumber NOTIFY stateChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY stateChanged)
    Q_PROPERTY(double secondsLeft READ secondsLeft NOTIFY stateChanged)
    Q_PROPERTY(bool hasGain READ hasGain NOTIFY stateChanged)
    Q_PROPERTY(double gain READ gain NOTIFY stateChanged)
    Q_PROPERTY(double gainMin READ gainMin NOTIFY stateChanged)
    Q_PROPERTY(double gainMax READ gainMax NOTIFY stateChanged)
    Q_PROPERTY(QStringList isoOptions READ isoOptions NOTIFY stateChanged)
    Q_PROPERTY(int isoIndex READ isoIndex NOTIFY stateChanged)
    Q_PROPERTY(bool hasTemperature READ hasTemperature NOTIFY stateChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY stateChanged)
    Q_PROPERTY(QString lastSaved READ lastSaved NOTIFY frameChanged)
    Q_PROPERTY(QImage previewImage READ previewImage NOTIFY previewChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY frameChanged)
    Q_PROPERTY(QString statsText READ statsText NOTIFY frameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
    Q_PROPERTY(bool stretch READ stretch WRITE setStretch NOTIFY previewChanged)
    Q_PROPERTY(bool showDetail READ showDetail WRITE setShowDetail NOTIFY previewChanged)
    Q_PROPERTY(bool nightMode READ nightMode WRITE setNightMode NOTIFY previewChanged)
    Q_PROPERTY(QString captureDirectory READ captureDirectory CONSTANT)
    Q_PROPERTY(QString freeSpaceText READ freeSpaceText NOTIFY frameChanged)

public:
    explicit CameraClient(const QString& capture_directory, QObject* parent = nullptr);
    ~CameraClient() override;

    bool serverUp() const { return m_server_up; }
    bool cameraFound() const { return !m_qt_device.isEmpty(); }
    bool cameraConnected() const { return m_connected; }
    QString deviceName() const { return m_qt_device; }
    QString message() const { return m_message; }
    bool capturing() const { return m_capturing; }
    int frameNumber() const { return m_frame_number; }
    int frameCount() const { return m_frame_count; }
    double secondsLeft() const { return m_seconds_left; }
    bool hasGain() const { return m_has_gain; }
    double gain() const { return m_gain; }
    double gainMin() const { return m_gain_min; }
    double gainMax() const { return m_gain_max; }
    QStringList isoOptions() const { return m_iso_options; }
    int isoIndex() const { return m_iso_index; }
    bool hasTemperature() const { return m_has_temperature; }
    double temperature() const { return m_temperature; }
    QString lastSaved() const { return m_last_saved; }
    QImage previewImage() const { return m_preview; }
    QVariantList histogram() const { return m_histogram; }
    QString statsText() const { return m_stats_text; }
    bool hasFrame() const { return !m_frame.fit.empty(); }
    bool stretch() const { return m_stretch; }
    void setStretch(bool enabled);
    bool showDetail() const { return m_show_detail; }
    void setShowDetail(bool enabled);
    bool nightMode() const { return m_night_mode; }
    void setNightMode(bool enabled);
    QString captureDirectory() const { return m_capture_directory; }
    QString freeSpaceText() const;

    // Sequences stop rather than fill the disk. Tests use this to pretend it's full.
    void setMinimumFreeBytes(qint64 bytes) { m_minimum_free_bytes = bytes; }

    // Where a frame goes, which is never a file that's already there
    static QString frame_path(const QString& directory, const QString& date, const QString& time, const QString& suffix);

    Q_INVOKABLE void connectCamera();
    Q_INVOKABLE void disconnectCamera();
    // Returns an empty string if it started, otherwise what went wrong
    Q_INVOKABLE QString capture(double seconds, int count);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString setGain(double gain);
    Q_INVOKABLE void setIso(int index);
    Q_INVOKABLE double nextExposure(double seconds, int direction) const;

signals:
    void stateChanged();
    void frameChanged();
    void previewChanged();

protected:
    void newProperty(INDI::Property property) override;
    void updateProperty(INDI::Property property) override;
    void removeDevice(INDI::BaseDevice device) override;
    void serverConnected() override;
    void serverDisconnected(int exit_code) override;

private:
    struct FrameResult {
        QString saved_path;
        QString error;
        FrameData frame;
        qint64 bytes = 0;
    };

    void try_connect_to_server();
    void read_property(INDI::Property property);
    void prepare_device();
    bool select_switch(const char* property_name, const char* element_name);
    std::string device_name() const;

    void start_exposure();
    void frame_arrived(const QByteArray& data, const QString& format);
    void frame_ready(const FrameResult& result);
    void finish_sequence(const QString& message);
    void refresh_preview();
    void join_worker();
    QString disk_problem() const;

    QString m_capture_directory;
    QTimer m_reconnect_timer;
    QTimer m_watchdog;
    std::atomic<bool> m_connect_running;
    std::thread m_worker;

    mutable std::mutex m_device_mutex;
    std::string m_device;

    QString m_qt_device;
    QString m_message;
    bool m_server_up;
    bool m_connected;
    bool m_capturing;
    bool m_waiting_for_frame;
    int m_frame_number;
    int m_frame_count;
    double m_exposure_seconds;
    double m_seconds_left;
    bool m_has_gain;
    double m_gain;
    double m_gain_min;
    double m_gain_max;
    QStringList m_iso_options;
    QStringList m_iso_names;
    int m_iso_index;
    bool m_has_temperature;
    double m_temperature;
    qint64 m_minimum_free_bytes;
    qint64 m_last_frame_bytes;

    QString m_last_saved;
    FrameData m_frame;
    QImage m_preview;
    QVariantList m_histogram;
    QString m_stats_text;
    bool m_stretch;
    bool m_show_detail;
    bool m_night_mode;
};

#endif
