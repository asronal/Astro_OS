#ifndef ASTRO_UI_SKY_BACKEND_H
#define ASTRO_UI_SKY_BACKEND_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QVariant>

#include "astro/sky.h"
#include "catalog.h"

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

    const astro::Observer& observer() const { return m_observer; }
    const Catalog& catalog() const { return m_catalog; }

    Q_INVOKABLE void setLocation(double latitude, double longitude, double elevation);
    Q_INVOKABLE QVariantList bodies() const;
    Q_INVOKABLE QVariantMap nightInfo() const;
    Q_INVOKABLE QVariantList bestTargets() const;
    Q_INVOKABLE QVariantList search(const QString& text) const;
    Q_INVOKABLE double polarisHourAngle() const;

signals:
    void settingsChanged();
    void ticked();

private:
    QString local_time(double jd) const;
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
};

#endif
