#include "sky_backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimeZone>

static const int kTickMs = 30000;
static const int kMaxSearchResults = 30;

SkyBackend::SkyBackend(const QString& settings_path, const QString& catalog_dir, QObject* parent)
    : QObject(parent),
      m_settings(settings_path, QSettings::IniFormat),
      m_observer{0.0, 0.0, 0.0},
      m_brightness(100)
{
    m_location_set = m_settings.contains(QStringLiteral("location/latitude"));
    m_observer.lat_deg = m_settings.value(QStringLiteral("location/latitude"), 0.0).toDouble();
    m_observer.lon_deg = m_settings.value(QStringLiteral("location/longitude"), 0.0).toDouble();
    m_observer.elev_m = m_settings.value(QStringLiteral("location/elevation"), 0.0).toDouble();
    m_utc_offset_minutes = m_settings.value(QStringLiteral("display/utcOffsetMinutes"), 0).toInt();
    m_night_mode = m_settings.value(QStringLiteral("display/nightMode"), false).toBool();

    find_backlight();
    if (!m_catalog.load(catalog_dir, astro::julian_now()))
        qWarning("Could not read the star catalogs from %s", qPrintable(catalog_dir));

    connect(&m_timer, &QTimer::timeout, this, &SkyBackend::ticked);
    m_timer.start(kTickMs);
}

void SkyBackend::find_backlight()
{
    QDir dir(QStringLiteral("/sys/class/backlight"));
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (!entries.isEmpty())
        m_backlight_dir = dir.filePath(entries.first());
}

void SkyBackend::setUtcOffsetMinutes(int minutes)
{
    m_utc_offset_minutes = minutes;
    m_settings.setValue(QStringLiteral("display/utcOffsetMinutes"), minutes);
    emit settingsChanged();
}

void SkyBackend::setNightMode(bool enabled)
{
    m_night_mode = enabled;
    m_settings.setValue(QStringLiteral("display/nightMode"), enabled);
    emit settingsChanged();
}

void SkyBackend::setBrightness(int percent)
{
    if (!hasBacklight())
        return;

    QFile maximum(m_backlight_dir + QStringLiteral("/max_brightness"));
    QFile value(m_backlight_dir + QStringLiteral("/brightness"));
    if (maximum.open(QIODevice::ReadOnly) && value.open(QIODevice::WriteOnly)) {
        int level = maximum.readAll().trimmed().toInt() * percent / 100;
        value.write(QByteArray::number(level));
    }
    m_brightness = percent;
    emit settingsChanged();
}

void SkyBackend::setLocation(double latitude, double longitude, double elevation)
{
    m_observer = {latitude, longitude, elevation};
    m_location_set = true;
    m_settings.setValue(QStringLiteral("location/latitude"), latitude);
    m_settings.setValue(QStringLiteral("location/longitude"), longitude);
    m_settings.setValue(QStringLiteral("location/elevation"), elevation);
    emit settingsChanged();
    emit ticked();
}

QString SkyBackend::local_time(double jd) const
{
    // Julian date 2440587.5 is the Unix epoch
    qint64 ms = qint64((jd - 2440587.5) * 86400000.0);
    QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(m_utc_offset_minutes * 60);
    return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(QStringLiteral("HH:mm"));
}

QString SkyBackend::clockText() const
{
    return local_time(astro::julian_now());
}

QVariantMap SkyBackend::body_map(astro::Body body, double jd) const
{
    astro::BodyReport r = astro::body_report(body, m_observer, jd);
    bool sets = r.horizon == astro::Horizon::RisesAndSets;
    QVariantMap map;

    map[QStringLiteral("name")] = QString::fromStdString(r.name);
    map[QStringLiteral("ra")] = r.ra_deg;
    map[QStringLiteral("dec")] = r.dec_deg;
    map[QStringLiteral("az")] = r.az_deg;
    map[QStringLiteral("alt")] = r.alt_deg;
    map[QStringLiteral("mag")] = r.magnitude;
    map[QStringLiteral("lit")] = r.illuminated;
    map[QStringLiteral("distance")] = r.distance_au;
    map[QStringLiteral("rise")] = sets ? local_time(r.rise_jd) : QStringLiteral("-");
    map[QStringLiteral("transit")] = local_time(r.transit_jd);
    map[QStringLiteral("set")] = sets ? local_time(r.set_jd) : QStringLiteral("-");
    map[QStringLiteral("isSun")] = body == astro::Body::Sun;
    return map;
}

QVariantList SkyBackend::bodies() const
{
    QVariantList list;
    double jd = astro::julian_now();

    for (astro::Body body : astro::all_bodies())
        list.append(body_map(body, jd));
    return list;
}

QVariantMap SkyBackend::nightInfo() const
{
    astro::Twilight t = astro::twilight(m_observer, astro::julian_now());
    bool normal = t.horizon == astro::Horizon::RisesAndSets;
    QVariantMap map;

    map[QStringLiteral("normal")] = normal;
    map[QStringLiteral("sunset")] = local_time(t.sunset_jd);
    map[QStringLiteral("civilDusk")] = local_time(t.civil_dusk_jd);
    map[QStringLiteral("nauticalDusk")] = local_time(t.nautical_dusk_jd);
    map[QStringLiteral("astronomicalDusk")] = local_time(t.astronomical_dusk_jd);
    map[QStringLiteral("astronomicalDawn")] = local_time(t.astronomical_dawn_jd);
    map[QStringLiteral("sunrise")] = local_time(t.sunrise_jd);

    double jd = astro::julian_now();
    astro::BodyReport moon = astro::body_report(astro::Body::Moon, m_observer, jd);
    astro::BodyReport later = astro::body_report(astro::Body::Moon, m_observer, jd + 0.5);
    map[QStringLiteral("moonLit")] = moon.illuminated;
    map[QStringLiteral("moonWaxing")] = later.illuminated > moon.illuminated;
    map[QStringLiteral("moonRise")] = moon.horizon == astro::Horizon::RisesAndSets ? local_time(moon.rise_jd) : QStringLiteral("-");
    map[QStringLiteral("moonSet")] = moon.horizon == astro::Horizon::RisesAndSets ? local_time(moon.set_jd) : QStringLiteral("-");
    return map;
}

QVariantList SkyBackend::bestTargets() const
{
    QVariantList list;
    double jd = astro::julian_now();

    for (const astro::NightTarget& target : astro::best_targets(m_observer, jd, 20.0)) {
        QVariantMap map = body_map(target.body, jd);
        map[QStringLiteral("maxAlt")] = target.max_alt_deg;
        map[QStringLiteral("bestTime")] = local_time(target.best_jd);
        list.append(map);
    }
    return list;
}

QVariantList SkyBackend::search(const QString& text) const
{
    QVariantList list;
    QString query = text.trimmed();
    double jd = astro::julian_now();
    double az;
    double alt;

    if (query.isEmpty())
        return list;

    for (astro::Body body : astro::all_bodies()) {
        if (QString::fromStdString(astro::body_name(body)).contains(query, Qt::CaseInsensitive))
            list.append(body_map(body, jd));
    }

    for (const CatalogObject& object : m_catalog.objects()) {
        if (list.size() >= kMaxSearchResults)
            return list;
        bool exact_name = object.name.compare(query, Qt::CaseInsensitive) == 0;
        bool partial = query.size() >= 2 && (object.name.contains(query, Qt::CaseInsensitive) ||
                                             object.common.contains(query, Qt::CaseInsensitive));
        if (!exact_name && !partial)
            continue;
        astro::equatorial_to_horizontal(object.ra_deg, object.dec_deg, m_observer, jd, &az, &alt);
        QVariantMap map;
        map[QStringLiteral("name")] = object.common.isEmpty() ? object.name : object.name + QStringLiteral(" ") + object.common;
        map[QStringLiteral("type")] = object.type;
        map[QStringLiteral("ra")] = object.ra_deg;
        map[QStringLiteral("dec")] = object.dec_deg;
        map[QStringLiteral("az")] = az;
        map[QStringLiteral("alt")] = alt;
        if (object.has_mag)
            map[QStringLiteral("mag")] = object.mag;
        list.append(map);
    }

    for (const CatalogStar& star : m_catalog.stars()) {
        if (list.size() >= kMaxSearchResults)
            return list;
        if (query.size() < 2 || !star.name.contains(query, Qt::CaseInsensitive))
            continue;
        astro::equatorial_to_horizontal(star.ra_deg, star.dec_deg, m_observer, jd, &az, &alt);
        QVariantMap map;
        map[QStringLiteral("name")] = star.name;
        map[QStringLiteral("type")] = QStringLiteral("Star");
        map[QStringLiteral("ra")] = star.ra_deg;
        map[QStringLiteral("dec")] = star.dec_deg;
        map[QStringLiteral("az")] = az;
        map[QStringLiteral("alt")] = alt;
        map[QStringLiteral("mag")] = star.mag;
        list.append(map);
    }
    return list;
}

double SkyBackend::polarisHourAngle() const
{
    return astro::polaris_hour_angle_deg(m_observer, astro::julian_now());
}
