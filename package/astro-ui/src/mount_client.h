#ifndef ASTRO_UI_MOUNT_CLIENT_H
#define ASTRO_UI_MOUNT_CLIENT_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <atomic>
#include <mutex>
#include <string>

#include <baseclient.h>
#include <basedevice.h>

class SkyBackend;

// Controls the telescope mount through the local INDI server. INDI calls back
// on its own thread, so results are passed to the UI thread as queued calls.
class MountClient : public QObject, public INDI::BaseClient {
    Q_OBJECT
    Q_PROPERTY(bool serverUp READ serverUp NOTIFY stateChanged)
    Q_PROPERTY(bool mountFound READ mountFound NOTIFY stateChanged)
    Q_PROPERTY(bool mountConnected READ mountConnected NOTIFY stateChanged)
    Q_PROPERTY(bool tracking READ tracking NOTIFY stateChanged)
    Q_PROPERTY(bool slewing READ slewing NOTIFY stateChanged)
    Q_PROPERTY(bool parked READ parked NOTIFY stateChanged)
    Q_PROPERTY(double ra READ ra NOTIFY stateChanged)
    Q_PROPERTY(double dec READ dec NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString driver READ driver NOTIFY profileChanged)
    Q_PROPERTY(QString port READ port NOTIFY profileChanged)
    Q_PROPERTY(bool remoteAccess READ remoteAccess NOTIFY profileChanged)
    Q_PROPERTY(QVariantList drivers READ drivers CONSTANT)

public:
    MountClient(SkyBackend* sky, const QString& hub_config_path, QObject* parent = nullptr);
    ~MountClient() override;

    bool serverUp() const { return m_server_up; }
    bool mountFound() const { return !m_qt_device.isEmpty(); }
    bool mountConnected() const { return m_connected; }
    bool tracking() const { return m_tracking; }
    bool slewing() const { return m_slewing; }
    bool parked() const { return m_parked; }
    double ra() const { return m_ra; }
    double dec() const { return m_dec; }
    QString deviceName() const { return m_qt_device; }
    QString message() const { return m_message; }
    QString driver() const { return m_driver; }
    QString port() const { return m_port; }
    bool remoteAccess() const { return m_remote_access; }
    QVariantList drivers() const;

    Q_INVOKABLE void applyProfile(const QString& driver, const QString& port, bool remote_access);
    Q_INVOKABLE void connectMount();
    Q_INVOKABLE void disconnectMount();
    // Returns an empty string if accepted, otherwise why the slew was refused
    Q_INVOKABLE QString gotoTarget(double ra_deg, double dec_deg, bool solar_filter_fitted);
    Q_INVOKABLE QString syncTarget(double ra_deg, double dec_deg);
    Q_INVOKABLE void abort();
    Q_INVOKABLE void setTracking(bool enabled);
    Q_INVOKABLE void moveStart(const QString& direction);
    Q_INVOKABLE void moveStop();
    Q_INVOKABLE void park();
    Q_INVOKABLE void unpark();

signals:
    void stateChanged();
    void profileChanged();

protected:
    void newProperty(INDI::Property property) override;
    void updateProperty(INDI::Property property) override;
    void removeDevice(INDI::BaseDevice device) override;
    void serverConnected() override;
    void serverDisconnected(int exit_code) override;

private:
    void handle_property(INDI::Property property);
    void read_property(INDI::Property property);
    void send_location_and_time();
    void try_connect_to_server();
    void load_profile();
    void select_switch(const char* property_name, const char* element_name);
    bool send_position(const char* mode, double ra_deg, double dec_deg);
    std::string device_name() const;

    SkyBackend* m_sky;
    QString m_hub_config_path;
    QTimer m_reconnect_timer;
    std::atomic<bool> m_connect_running;
    mutable std::mutex m_device_mutex;
    std::string m_device;

    QString m_qt_device;
    QString m_message;
    QString m_driver;
    QString m_port;
    bool m_remote_access;
    bool m_server_up;
    bool m_connected;
    bool m_tracking;
    bool m_slewing;
    bool m_parked;
    double m_ra;
    double m_dec;
};

#endif
