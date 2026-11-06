#include "sky_backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimeZone>

#include <unistd.h>

static const int kTickMs = 30000;
static const int kMaxSearchResults = 30;
static const int kDetectRetryMs = 60000;
static const int kDetectTimeoutMs = 15000;

// Sees this device's internet address. ASTRO_LOCATION_URL replaces it, for testing.
static const char kLocationUrl[] = "https://ipapi.co/json/";

static const char kSourceManual[] = "manual";
static const char kSourceNetwork[] = "network";

SkyBackend::SkyBackend(const QString& settings_path, const QString& catalog_dir, QObject* parent)
    : QObject(parent),
      m_settings(settings_path, QSettings::IniFormat),
      m_observer{0.0, 0.0, 0.0},
      m_brightness(100),
      m_catalog_dir(catalog_dir),
      m_clock_synced(false)
{
    m_location_set = m_settings.contains(QStringLiteral("location/latitude"));
    m_observer.lat_deg = m_settings.value(QStringLiteral("location/latitude"), 0.0).toDouble();
    m_observer.lon_deg = m_settings.value(QStringLiteral("location/longitude"), 0.0).toDouble();
    m_observer.elev_m = m_settings.value(QStringLiteral("location/elevation"), 0.0).toDouble();
    m_utc_offset_minutes = m_settings.value(QStringLiteral("display/utcOffsetMinutes"), 0).toInt();
    m_night_mode = m_settings.value(QStringLiteral("display/nightMode"), false).toBool();
    m_location_place = m_settings.value(QStringLiteral("location/place")).toString();
    // Locations saved before detection existed were all entered by hand
    m_location_source = m_settings.value(QStringLiteral("location/source"),
                                         m_location_set ? QLatin1String(kSourceManual) : QLatin1String("")).toString();
    m_clock_set = astro::clock_looks_set(astro::julian_now());

    find_backlight();
    if (!m_catalog.load(catalog_dir, astro::julian_now()))
        qWarning("Could not read the star catalogs from %s", qPrintable(catalog_dir));

    connect(&m_timer, &QTimer::timeout, this, [this]() {
        reload_settings();
        checkClock();
        emit ticked();
    });
    m_timer.start(kTickMs);

    connect(&m_detect_timer, &QTimer::timeout, this, [this]() { request_location(false); });
    checkClock();
}

// The astro_sky shell can change these while the touch app is running
void SkyBackend::reload_settings()
{
    m_settings.sync();
    bool location_set = m_settings.contains(QStringLiteral("location/latitude"));
    double latitude = m_settings.value(QStringLiteral("location/latitude"), 0.0).toDouble();
    double longitude = m_settings.value(QStringLiteral("location/longitude"), 0.0).toDouble();
    double elevation = m_settings.value(QStringLiteral("location/elevation"), 0.0).toDouble();
    int offset = m_settings.value(QStringLiteral("display/utcOffsetMinutes"), 0).toInt();

    if (location_set == m_location_set && latitude == m_observer.lat_deg && longitude == m_observer.lon_deg &&
        elevation == m_observer.elev_m && offset == m_utc_offset_minutes)
        return;

    m_location_set = location_set;
    m_observer = {latitude, longitude, elevation};
    m_utc_offset_minutes = offset;
    m_location_place = m_settings.value(QStringLiteral("location/place")).toString();
    m_location_source = m_settings.value(QStringLiteral("location/source"), QLatin1String(kSourceManual)).toString();
    emit settingsChanged();
    emit observingSiteChanged();
}

void SkyBackend::startAutoDetect()
{
    if (m_location_set && m_location_source != QLatin1String(kSourceNetwork))
        return;
    request_location(false);
    m_detect_timer.start(kDetectRetryMs);
}

void SkyBackend::detectLocation()
{
    request_location(true);
}

void SkyBackend::request_location(bool user_asked)
{
    if (m_detect_reply)
        return;

    QUrl url(qEnvironmentVariable("ASTRO_LOCATION_URL", QLatin1String(kLocationUrl)));
    QNetworkRequest request(url);
    request.setTransferTimeout(kDetectTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AstroOS"));

    m_detect_reply = m_network.get(request);
    emit settingsChanged();

    QNetworkReply* reply = m_detect_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, user_asked]() {
        reply->deleteLater();
        QByteArray body = reply->readAll();
        bool network_failed = reply->error() != QNetworkReply::NoError;
        bool refused_by_service = reply->error() == QNetworkReply::UnknownContentError ||
                                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400;
        m_detect_reply = nullptr;

        DetectedLocation location;
        QString problem;
        bool ok = false;
        if (network_failed && !refused_by_service)
            problem = QStringLiteral("no answer from the internet");
        else
            ok = parse_location_reply(body, &location, &problem);

        if (!ok) {
            m_detect_status = user_asked
                ? QStringLiteral("Couldn't find the location: %1. Enter it by hand instead.").arg(problem)
                : QStringLiteral("Waiting for an internet connection to find the location.");
        } else if (user_asked || m_location_source != QLatin1String(kSourceManual)) {
            m_detect_timer.stop();
            apply_detected(location);
        }
        emit settingsChanged();
        emit detectFinished(ok);
    });
}

void SkyBackend::apply_detected(const DetectedLocation& location)
{
    m_observer.lat_deg = location.latitude;
    m_observer.lon_deg = location.longitude;
    m_location_set = true;
    m_location_source = QLatin1String(kSourceNetwork);
    m_location_place = location.place;
    m_settings.setValue(QStringLiteral("location/latitude"), location.latitude);
    m_settings.setValue(QStringLiteral("location/longitude"), location.longitude);
    m_settings.setValue(QStringLiteral("location/elevation"), m_observer.elev_m);
    m_settings.setValue(QStringLiteral("location/source"), m_location_source);
    m_settings.setValue(QStringLiteral("location/place"), m_location_place);
    if (location.has_utc_offset) {
        m_utc_offset_minutes = location.utc_offset_minutes;
        m_settings.setValue(QStringLiteral("display/utcOffsetMinutes"), m_utc_offset_minutes);
    }
    m_settings.sync();

    m_detect_status = (location.place.isEmpty() ? QStringLiteral("Found the location")
                                                : QStringLiteral("Near %1").arg(location.place)) +
                      QStringLiteral(", from this device's internet address using ipapi.co. It's approximate, so correct it if needed.");
    emit settingsChanged();
    emit observingSiteChanged();
    emit ticked();
}

void SkyBackend::update_clock_set()
{
    bool looks_set = astro::clock_looks_set(astro::julian_now());
    if (looks_set == m_clock_set)
        return;
    m_clock_set = looks_set;
    if (looks_set) {
        // Star positions are worked out for the date when they're loaded
        m_catalog = Catalog();
        m_catalog.load(m_catalog_dir, astro::julian_now());
        emit observingSiteChanged();
    }
    emit settingsChanged();
    emit ticked();
}

void SkyBackend::checkClock()
{
    update_clock_set();
    if (m_clock_process)
        return;

    // Chrony sets the clock from the internet, and knows whether it has managed to
    auto* process = new QProcess(this);
    m_clock_process = process;
    auto finish = [this, process](bool synced) {
        process->deleteLater();
        m_clock_process = nullptr;
        if (synced != m_clock_synced) {
            m_clock_synced = synced;
            emit settingsChanged();
        }
        emit clockChecked();
    };
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    connect(process, &QProcess::finished, this, [finish, process]() {
        // Comma-separated, with "Normal" as the last field once it's synchronised
        QByteArray output = process->readAllStandardOutput().trimmed();
        finish(output.endsWith(",Normal"));
    });
    process->start(QStringLiteral("chronyc"), {QStringLiteral("-c"), QStringLiteral("tracking")});
}

QString SkyBackend::clockStatus() const
{
    if (m_clock_synced)
        return QStringLiteral("Set from the internet");
    if (m_clock_set)
        return QStringLiteral("Set, but not confirmed by the internet");
    return QStringLiteral("Not set. It sets itself when online, or set it by hand");
}

QString SkyBackend::setClockUtc(const QString& text)
{
    QString wanted = text.trimmed();
    QDateTime when = QDateTime::fromString(wanted, QStringLiteral("yyyy-MM-dd HH:mm"));
    if (!when.isValid())
        return QStringLiteral("Write the date and time as YYYY-MM-DD HH:MM, in UTC.");
    when.setTimeZone(QTimeZone::UTC);
    if (!astro::clock_looks_set(when.toMSecsSinceEpoch() / 86400000.0 + 2440587.5))
        return QStringLiteral("That date is too long ago to be right.");

    QStringList arguments = {QStringLiteral("date"), QStringLiteral("-u"), QStringLiteral("-s"), when.toString(QStringLiteral("yyyy-MM-dd HH:mm:00"))};
    QProcess process;
    if (geteuid() == 0) {
        QString program = arguments.takeFirst();
        process.start(program, arguments);
    } else {
        arguments.prepend(QStringLiteral("-n"));
        process.start(QStringLiteral("sudo"), arguments);
    }
    if (!process.waitForFinished(5000) || process.exitCode() != 0)
        return QStringLiteral("The clock couldn't be set.");

    checkClock();
    emit observingSiteChanged();
    return QString();
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
    m_settings.sync();
    emit settingsChanged();
    emit observingSiteChanged();
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
    m_location_source = QLatin1String(kSourceManual);
    m_location_place.clear();
    m_detect_status.clear();
    m_detect_timer.stop();
    m_settings.setValue(QStringLiteral("location/source"), m_location_source);
    m_settings.remove(QStringLiteral("location/place"));
    m_settings.sync();
    emit settingsChanged();
    emit observingSiteChanged();
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
    map[QStringLiteral("type")] = QStringLiteral("Solar system");
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
        list.append(object_map(object, jd));
    }

    for (const CatalogStar& star : m_catalog.stars()) {
        if (list.size() >= kMaxSearchResults)
            return list;
        if (query.size() < 2 || !star.name.contains(query, Qt::CaseInsensitive))
            continue;
        list.append(star_map(star, jd));
    }
    return list;
}

QVariantMap SkyBackend::object_map(const CatalogObject& object, double jd) const
{
    double az;
    double alt;
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
    return map;
}

QVariantMap SkyBackend::star_map(const CatalogStar& star, double jd) const
{
    double az;
    double alt;
    astro::equatorial_to_horizontal(star.ra_deg, star.dec_deg, m_observer, jd, &az, &alt);
    QVariantMap map;
    map[QStringLiteral("name")] = star.name;
    map[QStringLiteral("type")] = QStringLiteral("Star");
    map[QStringLiteral("ra")] = star.ra_deg;
    map[QStringLiteral("dec")] = star.dec_deg;
    map[QStringLiteral("az")] = az;
    map[QStringLiteral("alt")] = alt;
    map[QStringLiteral("mag")] = star.mag;
    return map;
}

QVariantMap SkyBackend::findExact(const QString& name) const
{
    QString wanted = name.trimmed();
    double jd = astro::julian_now();

    for (astro::Body body : astro::all_bodies()) {
        if (QString::fromStdString(astro::body_name(body)).compare(wanted, Qt::CaseInsensitive) == 0)
            return body_map(body, jd);
    }
    for (const CatalogObject& object : m_catalog.objects()) {
        if (object.name.compare(wanted, Qt::CaseInsensitive) == 0 || object.common.compare(wanted, Qt::CaseInsensitive) == 0)
            return object_map(object, jd);
    }
    for (const CatalogStar& star : m_catalog.stars()) {
        if (star.name.compare(wanted, Qt::CaseInsensitive) == 0)
            return star_map(star, jd);
    }
    return QVariantMap();
}

double SkyBackend::polarisHourAngle() const
{
    return astro::polaris_hour_angle_deg(m_observer, astro::julian_now());
}
