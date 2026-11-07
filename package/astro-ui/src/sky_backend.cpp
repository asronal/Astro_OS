#include "sky_backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimeZone>

#include <unistd.h>

#include <cmath>
#include <limits>

#include "chart_math.h"

static const int kTickMs = 30000;
static const int kMaxSearchResults = 30;
static const int kDetectRetryMs = 60000;
static const int kDetectTimeoutMs = 15000;

// Sees this device's internet address. ASTRO_LOCATION_URL replaces it, for testing.
static const char kLocationUrl[] = "https://ipapi.co/json/";

// Limits on what can be entered as equipment. Wide enough for a camera lens on one
// side and a long Schmidt-Cassegrain on the other.
static const double kMinTelescopeMm = 50.0;
static const double kMaxTelescopeMm = 20000.0;
static const double kMinEyepieceMm = 2.0;
static const double kMaxEyepieceMm = 100.0;
static const double kMinApparentField = 20.0;
static const double kMaxApparentField = 120.0;
static const int kMaxEyepieces = 12;

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
    load_equipment();
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
    if (!object.other_name.isEmpty())
        map[QStringLiteral("otherName")] = object.other_name;
    if (object.major_arcmin > 0.0f) {
        map[QStringLiteral("majorArcmin")] = object.major_arcmin;
        map[QStringLiteral("minorArcmin")] = object.minor_arcmin;
    }
    return map;
}

QVariantMap SkyBackend::star_map(const CatalogStar& star, double jd) const
{
    double az;
    double alt;
    astro::equatorial_to_horizontal(star.ra_deg, star.dec_deg, m_observer, jd, &az, &alt);
    QVariantMap map;
    map[QStringLiteral("name")] = star.name.isEmpty() ? QStringLiteral("Star") : star.name;
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

namespace {

bool find_body(const QVariantMap& target, astro::Body* found)
{
    if (target.value(QStringLiteral("type")).toString() != QLatin1String("Solar system"))
        return false;
    QString name = target.value(QStringLiteral("name")).toString();
    for (astro::Body body : astro::all_bodies()) {
        if (QString::fromStdString(astro::body_name(body)) == name) {
            *found = body;
            return true;
        }
    }
    return false;
}

QString compass_point(double az)
{
    static const char* const kNames[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return QLatin1String(kNames[int(std::lround(std::fmod(az + 360.0, 360.0) / 45.0)) % 8]);
}

QString height_text(double alt)
{
    return alt >= 0.0 ? QStringLiteral("%1° up").arg(alt, 0, 'f', 0)
                      : QStringLiteral("%1° below the horizon").arg(-alt, 0, 'f', 0);
}

QString size_text(double major, double minor)
{
    auto one = [](double arcmin) {
        return arcmin >= 10.0 ? QString::number(arcmin, 'f', 0) : QString::number(arcmin, 'f', 1);
    };
    if (minor <= 0.0 || std::fabs(major - minor) < 0.05)
        return one(major) + QStringLiteral("′");
    return one(major) + QStringLiteral("′ × ") + one(minor) + QStringLiteral("′");
}

} // namespace

QString format_ra(double degrees)
{
    double hours = std::fmod(degrees + 360.0, 360.0) / 15.0;
    int h = int(hours);
    int m = int((hours - h) * 60.0);
    int s = int(((hours - h) * 60.0 - m) * 60.0);
    return QStringLiteral("%1h %2m %3s").arg(h, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QString format_dec(double degrees)
{
    double size = std::fabs(degrees);
    int d = int(size);
    int m = int((size - d) * 60.0);
    return QStringLiteral("%1%2° %3'").arg(degrees < 0.0 ? QLatin1Char('-') : QLatin1Char('+')).arg(d, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0'));
}

// The time of an event, and which day if it isn't the same one. Something already up
// next rises tomorrow, and "rises 06:22" alone would look like a mistake.
QString SkyBackend::event_text(double event_jd, double from_jd, bool previewing) const
{
    QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(m_utc_offset_minutes * 60);
    auto to_local = [&zone](double jd) {
        return QDateTime::fromMSecsSinceEpoch(qint64((jd - 2440587.5) * 86400000.0), zone);
    };
    QDateTime when = to_local(event_jd);
    qint64 days = to_local(from_jd).date().daysTo(when.date());

    if (days == 0)
        return when.toString(QStringLiteral("HH:mm"));
    if (days == 1)
        return when.toString(QStringLiteral("HH:mm ")) + (previewing ? QStringLiteral("next day") : QStringLiteral("tomorrow"));
    return when.toString(QStringLiteral("HH:mm, ddd d MMM"));
}

QVariantMap SkyBackend::livePosition(const QVariantMap& target) const
{
    QVariantMap live = target;
    double jd = astro::julian_now();
    astro::Body body;

    if (find_body(target, &body)) {
        astro::BodyReport r = astro::body_position(body, m_observer, jd);
        live[QStringLiteral("ra")] = r.ra_deg;
        live[QStringLiteral("dec")] = r.dec_deg;
        live[QStringLiteral("az")] = r.az_deg;
        live[QStringLiteral("alt")] = r.alt_deg;
    } else if (target.contains(QStringLiteral("ra"))) {
        double az;
        double alt;
        astro::equatorial_to_horizontal(target.value(QStringLiteral("ra")).toDouble(),
                                        target.value(QStringLiteral("dec")).toDouble(), m_observer, jd, &az, &alt);
        live[QStringLiteral("az")] = az;
        live[QStringLiteral("alt")] = alt;
    }
    return live;
}

QString SkyBackend::targetSummary(const QVariantMap& target, double offset_hours) const
{
    if (!target.contains(QStringLiteral("name")))
        return QString();

    double jd = astro::julian_now() + offset_hours / 24.0;
    double magnitude = target.value(QStringLiteral("mag"), std::numeric_limits<double>::quiet_NaN()).toDouble();
    double az;
    double alt;
    astro::Body body;

    if (find_body(target, &body)) {
        astro::BodyReport r = astro::body_position(body, m_observer, jd);
        magnitude = r.magnitude;
        alt = r.alt_deg;
    } else {
        astro::equatorial_to_horizontal(target.value(QStringLiteral("ra")).toDouble(),
                                        target.value(QStringLiteral("dec")).toDouble(), m_observer, jd, &az, &alt);
    }

    QStringList parts;
    QString type = target.value(QStringLiteral("type")).toString();
    if (!type.isEmpty())
        parts << type;
    if (!std::isnan(magnitude))
        parts << QStringLiteral("magnitude %1").arg(magnitude, 0, 'f', 1);
    parts << height_text(alt);
    return parts.join(QStringLiteral(", "));
}

QVariantMap SkyBackend::targetDetails(const QVariantMap& target, double offset_hours, bool solar_filter_fitted) const
{
    QVariantMap details;
    if (!target.contains(QStringLiteral("name")))
        return details;

    const double now = astro::julian_now();
    const double jd = now + offset_hours / 24.0;
    // Nothing that depends on the date or the location can be trusted without them
    const bool sky_known = m_location_set && astro::clock_looks_set(now);

    QVariantList rows;
    auto add = [&rows](const QString& label, const QString& value, bool warning = false) {
        QVariantMap row;
        row[QStringLiteral("label")] = label;
        row[QStringLiteral("value")] = value;
        row[QStringLiteral("warning")] = warning;
        rows.append(row);
    };

    astro::Body body = astro::Body::Sun;
    const bool is_body = find_body(target, &body);
    const bool is_sun = is_body && body == astro::Body::Sun;
    const bool is_moon = is_body && body == astro::Body::Moon;

    if (target.contains(QStringLiteral("otherName")))
        add(QStringLiteral("Also known as"), target.value(QStringLiteral("otherName")).toString());

    // Where it is at the time being shown, and when it's up
    double ra = target.value(QStringLiteral("ra")).toDouble();
    double dec = target.value(QStringLiteral("dec")).toDouble();
    double az = 0.0;
    double alt = 0.0;
    double magnitude = target.value(QStringLiteral("mag"), std::numeric_limits<double>::quiet_NaN()).toDouble();
    double lit = 0.0;
    double distance_au = 0.0;
    astro::Horizon horizon;
    double rise_jd, transit_jd, set_jd, highest;
    astro::DarkBest best;

    if (is_body) {
        astro::BodyReport r = astro::body_report(body, m_observer, jd);
        ra = r.ra_deg;
        dec = r.dec_deg;
        az = r.az_deg;
        alt = r.alt_deg;
        magnitude = r.magnitude;
        lit = r.illuminated;
        distance_au = r.distance_au;
        horizon = r.horizon;
        rise_jd = r.rise_jd;
        transit_jd = r.transit_jd;
        set_jd = r.set_jd;
        highest = astro::body_position(body, m_observer, transit_jd).alt_deg;
        best = astro::best_dark_time(body, m_observer, jd);
    } else {
        astro::equatorial_to_horizontal(ra, dec, m_observer, jd, &az, &alt);
        astro::FixedRiseSet r = astro::fixed_rise_set(ra, dec, m_observer, jd);
        horizon = r.horizon;
        rise_jd = r.rise_jd;
        transit_jd = r.transit_jd;
        set_jd = r.set_jd;
        highest = r.max_alt_deg;
        best = astro::best_dark_time(ra, dec, m_observer, jd);
    }

    if (sky_known) {
        add(offset_hours == 0.0 ? QStringLiteral("Now") : QStringLiteral("At that time"),
            alt >= 0.0 ? height_text(alt) + QStringLiteral(", facing ") + compass_point(az) : height_text(alt));
    }

    // This matches MountClient::gotoTarget, using the real time and real position
    if (!astro::clock_looks_set(now)) {
        add(QStringLiteral("GoTo now"), QStringLiteral("Refused, the clock isn't set"), true);
    } else if (!m_location_set) {
        add(QStringLiteral("GoTo now"), QStringLiteral("Refused, the location isn't set"), true);
    } else {
        QVariantMap live = livePosition(target);
        switch (astro::check_goto(live.value(QStringLiteral("ra")).toDouble(), live.value(QStringLiteral("dec")).toDouble(),
                                  m_observer, now, astro::kDefaultGotoLimits, solar_filter_fitted)) {
        case astro::GotoVerdict::Allowed:
            add(QStringLiteral("GoTo now"), QStringLiteral("Allowed"));
            break;
        case astro::GotoVerdict::TooCloseToSun:
            add(QStringLiteral("GoTo now"), QStringLiteral("Refused, too close to the Sun"), true);
            break;
        case astro::GotoVerdict::BelowHorizon:
            add(QStringLiteral("GoTo now"), QStringLiteral("Refused, below the horizon"), true);
            break;
        }
    }

    if (sky_known) {
        const bool previewing = offset_hours != 0.0;
        if (horizon == astro::Horizon::RisesAndSets) {
            add(QStringLiteral("Rises"), event_text(rise_jd, jd, previewing));
            add(QStringLiteral("Highest"), event_text(transit_jd, jd, previewing) + QStringLiteral(", ") + height_text(highest));
            add(QStringLiteral("Sets"), event_text(set_jd, jd, previewing));
        } else if (horizon == astro::Horizon::AlwaysUp) {
            add(QStringLiteral("Rises and sets"), QStringLiteral("Always up from here"));
            add(QStringLiteral("Highest"), event_text(transit_jd, jd, previewing) + QStringLiteral(", ") + height_text(highest));
        } else {
            add(QStringLiteral("Rises and sets"), QStringLiteral("Never rises from here"));
        }

        if (!is_sun && horizon != astro::Horizon::NeverUp) {
            if (!best.has_dark_time)
                add(QStringLiteral("Best when dark"), QStringLiteral("No dark sky in the next day"));
            else if (best.max_alt_deg < 0.0)
                add(QStringLiteral("Best when dark"), QStringLiteral("Below the horizon while dark"));
            else
                add(QStringLiteral("Best when dark"), event_text(best.best_jd, jd, previewing) + QStringLiteral(", ") + height_text(best.max_alt_deg));
        }
    }

    if (!std::isnan(magnitude))
        add(QStringLiteral("Magnitude"), QString::number(magnitude, 'f', 1));
    if (target.contains(QStringLiteral("majorArcmin"))) {
        add(QStringLiteral("Size"), size_text(target.value(QStringLiteral("majorArcmin")).toDouble(),
                                              target.value(QStringLiteral("minorArcmin")).toDouble()));
    }
    if (is_body && !is_sun)
        add(QStringLiteral("Lit"), QStringLiteral("%1%").arg(lit * 100.0, 0, 'f', 0));
    if (is_body) {
        add(QStringLiteral("Distance"), is_moon ? QStringLiteral("%1 km").arg(distance_au * 149597870.7, 0, 'f', 0)
                                                : QStringLiteral("%1 AU").arg(distance_au, 0, 'f', 2));
    }
    if (sky_known) {
        if (!is_sun)
            add(QStringLiteral("From the Sun"), QStringLiteral("%1°").arg(astro::sun_separation_deg(ra, dec, jd), 0, 'f', 0));
        if (!is_moon) {
            astro::BodyReport moon = astro::body_position(astro::Body::Moon, m_observer, jd);
            add(QStringLiteral("From the Moon"), QStringLiteral("%1°").arg(chart::separation(ra, dec, moon.ra_deg, moon.dec_deg), 0, 'f', 0));
        }
    }
    add(QStringLiteral("Right ascension"), format_ra(ra));
    add(QStringLiteral("Declination"), format_dec(dec));

    details[QStringLiteral("rows")] = rows;
    return details;
}

QString SkyBackend::previewTimeText(double offset_hours) const
{
    if (!astro::clock_looks_set(astro::julian_now()))
        return QStringLiteral("Clock not set");

    qint64 ms = qint64((astro::julian_now() + offset_hours / 24.0 - 2440587.5) * 86400000.0);
    QDateTime when = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::fromSecondsAheadOfUtc(m_utc_offset_minutes * 60));
    return offset_hours == 0.0 ? when.toString(QStringLiteral("'Now,' HH:mm")) : when.toString(QStringLiteral("ddd d MMM, HH:mm"));
}

void SkyBackend::load_equipment()
{
    m_telescope_focal_mm = m_settings.value(QStringLiteral("equipment/telescopeFocalMm"), 0.0).toDouble();
    m_eyepieces.clear();
    int count = m_settings.beginReadArray(QStringLiteral("eyepieces"));
    for (int i = 0; i < count; i++) {
        m_settings.setArrayIndex(i);
        m_eyepieces.append({m_settings.value(QStringLiteral("focalMm")).toDouble(),
                            m_settings.value(QStringLiteral("apparentField")).toDouble(),
                            m_settings.value(QStringLiteral("on")).toBool()});
    }
    m_settings.endArray();
}

void SkyBackend::save_equipment()
{
    m_settings.setValue(QStringLiteral("equipment/telescopeFocalMm"), m_telescope_focal_mm);
    m_settings.remove(QStringLiteral("eyepieces"));
    m_settings.beginWriteArray(QStringLiteral("eyepieces"), int(m_eyepieces.size()));
    for (int i = 0; i < m_eyepieces.size(); i++) {
        m_settings.setArrayIndex(i);
        m_settings.setValue(QStringLiteral("focalMm"), m_eyepieces[i].focal_mm);
        m_settings.setValue(QStringLiteral("apparentField"), m_eyepieces[i].apparent_field_deg);
        m_settings.setValue(QStringLiteral("on"), m_eyepieces[i].on);
    }
    m_settings.endArray();
    m_settings.sync();
    emit equipmentChanged();
}

QVariantList SkyBackend::eyepieces() const
{
    QVariantList list;
    for (const Eyepiece& eyepiece : m_eyepieces) {
        QVariantMap map;
        map[QStringLiteral("focalMm")] = eyepiece.focal_mm;
        map[QStringLiteral("apparentField")] = eyepiece.apparent_field_deg;
        map[QStringLiteral("on")] = eyepiece.on;
        QString name = QStringLiteral("%1 mm").arg(eyepiece.focal_mm, 0, 'g', 4);
        // Without a telescope, there is no way to know how much sky it shows
        if (m_telescope_focal_mm > 0.0) {
            double field = chart::true_field(m_telescope_focal_mm, eyepiece.focal_mm, eyepiece.apparent_field_deg);
            map[QStringLiteral("fieldDeg")] = field;
            map[QStringLiteral("label")] = name + QStringLiteral(", %1×, %2°")
                .arg(chart::magnification(m_telescope_focal_mm, eyepiece.focal_mm), 0, 'f', 0)
                .arg(field, 0, 'f', 2);
        } else {
            map[QStringLiteral("label")] = name;
        }
        list.append(map);
    }
    return list;
}

QString SkyBackend::setTelescopeFocal(double millimetres)
{
    // Written this way round so that a value that isn't a number is refused too
    if (!(millimetres >= kMinTelescopeMm && millimetres <= kMaxTelescopeMm))
        return QStringLiteral("Enter a focal length from %1 to %2 mm.").arg(kMinTelescopeMm).arg(kMaxTelescopeMm);
    m_telescope_focal_mm = millimetres;
    save_equipment();
    return QString();
}

QString SkyBackend::addEyepiece(double focal_mm, double apparent_field_deg)
{
    if (!(focal_mm >= kMinEyepieceMm && focal_mm <= kMaxEyepieceMm))
        return QStringLiteral("Enter an eyepiece focal length from %1 to %2 mm.").arg(kMinEyepieceMm).arg(kMaxEyepieceMm);
    if (!(apparent_field_deg >= kMinApparentField && apparent_field_deg <= kMaxApparentField))
        return QStringLiteral("Enter an apparent field from %1 to %2 degrees.").arg(kMinApparentField).arg(kMaxApparentField);
    if (m_eyepieces.size() >= kMaxEyepieces)
        return QStringLiteral("Remove an eyepiece first. The limit is %1.").arg(kMaxEyepieces);
    m_eyepieces.append({focal_mm, apparent_field_deg, true});
    save_equipment();
    return QString();
}

void SkyBackend::removeEyepiece(int index)
{
    if (index < 0 || index >= m_eyepieces.size())
        return;
    m_eyepieces.removeAt(index);
    save_equipment();
}

void SkyBackend::setEyepieceOn(int index, bool on)
{
    if (index < 0 || index >= m_eyepieces.size())
        return;
    m_eyepieces[index].on = on;
    save_equipment();
}
