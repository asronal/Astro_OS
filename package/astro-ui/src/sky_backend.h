#ifndef ASTRO_UI_SKY_BACKEND_H
#define ASTRO_UI_SKY_BACKEND_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include "astro/sky.h"
#include "auto_detect.h"
#include "catalog.h"

class QNetworkReply;
class QProcess;

// Sky data and settings for the QML pages
class SkyBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool locationSet READ locationSet NOTIFY settingsChanged)
    Q_PROPERTY(double latitude READ latitude NOTIFY settingsChanged)
    Q_PROPERTY(double longitude READ longitude NOTIFY settingsChanged)
    Q_PROPERTY(double elevation READ elevation NOTIFY settingsChanged)
    Q_PROPERTY(int utcOffsetMinutes READ utcOffsetMinutes WRITE setUtcOffsetMinutes NOTIFY settingsChanged)
    Q_PROPERTY(bool nightMode READ nightMode WRITE setNightMode NOTIFY settingsChanged)
    Q_PROPERTY(bool hasBacklight READ hasBacklight CONSTANT)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY settingsChanged)
    Q_PROPERTY(QString clockText READ clockText NOTIFY ticked)
    Q_PROPERTY(QString locationSource READ locationSource NOTIFY settingsChanged)
    Q_PROPERTY(QString locationPlace READ locationPlace NOTIFY settingsChanged)
    Q_PROPERTY(QString detectStatus READ detectStatus NOTIFY settingsChanged)
    Q_PROPERTY(bool detecting READ detecting NOTIFY settingsChanged)
    Q_PROPERTY(bool clockSet READ clockSet NOTIFY settingsChanged)
    Q_PROPERTY(bool clockSynced READ clockSynced NOTIFY settingsChanged)
    Q_PROPERTY(QString clockStatus READ clockStatus NOTIFY settingsChanged)
    Q_PROPERTY(double telescopeFocalMm READ telescopeFocalMm NOTIFY equipmentChanged)
    Q_PROPERTY(QVariantList eyepieces READ eyepieces NOTIFY equipmentChanged)

public:
    SkyBackend(const QString& settings_path, const QString& catalog_dir, QObject* parent = nullptr);

    bool locationSet() const { return m_location_set; }
    double latitude() const { return m_observer.lat_deg; }
    double longitude() const { return m_observer.lon_deg; }
    double elevation() const { return m_observer.elev_m; }
    int utcOffsetMinutes() const { return m_utc_offset_minutes; }
    void setUtcOffsetMinutes(int minutes);
    bool nightMode() const { return m_night_mode; }
    void setNightMode(bool enabled);
    bool hasBacklight() const { return !m_backlight_dir.isEmpty(); }
    int brightness() const { return m_brightness; }
    void setBrightness(int percent);
    QString clockText() const;
    QString locationSource() const { return m_location_source; }
    QString locationPlace() const { return m_location_place; }
    QString detectStatus() const { return m_detect_status; }
    bool detecting() const { return !m_detect_reply.isNull(); }
    bool clockSet() const { return m_clock_set; }
    bool clockSynced() const { return m_clock_synced; }
    QString clockStatus() const;
    double telescopeFocalMm() const { return m_telescope_focal_mm; }
    QVariantList eyepieces() const;

    const astro::Observer& observer() const { return m_observer; }
    const Catalog& catalog() const { return m_catalog; }

    // A location saved by the user always wins over one that was detected
    Q_INVOKABLE void setLocation(double latitude, double longitude, double elevation);
    // Asks the location service, replacing any location, because the user asked for it
    Q_INVOKABLE void detectLocation();
    // Keeps trying in the background, unless the user has saved their own location
    void startAutoDetect();
    // Returns an empty string if the clock was set, otherwise what went wrong
    Q_INVOKABLE QString setClockUtc(const QString& text);
    Q_INVOKABLE void checkClock();
    void saveSettings() { m_settings.sync(); }
    Q_INVOKABLE QVariantList bodies() const;
    Q_INVOKABLE QVariantMap nightInfo() const;
    Q_INVOKABLE QVariantList bestTargets() const;
    Q_INVOKABLE QVariantList search(const QString& text) const;
    Q_INVOKABLE double polarisHourAngle() const;
    // An object whose full name is exactly this, or an empty map
    QVariantMap findExact(const QString& name) const;

    // These are always for the real time, so they are safe to send to a mount
    QVariantMap objectMap(const CatalogObject& object) const { return object_map(object, astro::julian_now()); }
    QVariantMap starMap(const CatalogStar& star) const { return star_map(star, astro::julian_now()); }
    QVariantMap bodyMap(astro::Body body) const { return body_map(body, astro::julian_now()); }

    // The same object as it really is right now. Planets move, so this must be used
    // for anything sent to a mount, whatever time the chart happens to be showing.
    Q_INVOKABLE QVariantMap livePosition(const QVariantMap& target) const;
    // One line about an object, as it is at this many hours from now. Quick.
    Q_INVOKABLE QString targetSummary(const QVariantMap& target, double offset_hours) const;
    // Everything known about an object, as "rows" of "label" and "value", ready to show.
    // Whether GoTo would allow it always uses the real time. Slower, so call on demand.
    Q_INVOKABLE QVariantMap targetDetails(const QVariantMap& target, double offset_hours, bool solar_filter_fitted) const;
    // The date and time at this many hours from now
    Q_INVOKABLE QString previewTimeText(double offset_hours) const;

    // Each returns an empty string if it worked, otherwise what went wrong
    Q_INVOKABLE QString setTelescopeFocal(double millimetres);
    Q_INVOKABLE QString addEyepiece(double focal_mm, double apparent_field_deg);
    Q_INVOKABLE void removeEyepiece(int index);
    Q_INVOKABLE void setEyepieceOn(int index, bool on);

signals:
    void settingsChanged();
    void ticked();
    void detectFinished(bool success);
    void clockChecked();
    // The location, time zone or clock changed, so a connected mount needs telling
    void observingSiteChanged();
    void equipmentChanged();

private:
    struct Eyepiece {
        double focal_mm;
        double apparent_field_deg;
        bool on;
    };

    void load_equipment();
    void save_equipment();
    void request_location(bool user_asked);
    void apply_detected(const DetectedLocation& location);
    void reload_settings();
    void update_clock_set();
    QVariantMap object_map(const CatalogObject& object, double jd) const;
    QVariantMap star_map(const CatalogStar& star, double jd) const;
    QString local_time(double jd) const;
    QString event_text(double event_jd, double from_jd, bool previewing) const;
    QVariantMap body_map(astro::Body body, double jd) const;
    void find_backlight();

    QSettings m_settings;
    Catalog m_catalog;
    QTimer m_timer;
    astro::Observer m_observer;
    bool m_location_set;
    int m_utc_offset_minutes;
    bool m_night_mode;
    QString m_backlight_dir;
    int m_brightness;
    QString m_catalog_dir;
    QString m_location_source;
    QString m_location_place;
    QString m_detect_status;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_detect_reply;
    QTimer m_detect_timer;
    QPointer<QProcess> m_clock_process;
    bool m_clock_set;
    bool m_clock_synced;
    double m_telescope_focal_mm;
    QVector<Eyepiece> m_eyepieces;
};

QString format_ra(double degrees);
QString format_dec(double degrees);

#endif
