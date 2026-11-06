#include "sky_chart.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

static const double kMinFov = 5.0;
static const double kMaxFov = 180.0;
static const double kTapRadiusPx = 30.0;

namespace {

// Convert to horizontal coordinates without repeating the sidereal time
// calculation for each of the thousands of stars drawn
void fast_horizontal(double ra_deg, double dec_deg, double lst_rad, double sin_lat, double cos_lat,
                     double* az_deg, double* alt_deg)
{
    double hour_angle = lst_rad - qDegreesToRadians(ra_deg);
    double dec = qDegreesToRadians(dec_deg);
    double sin_dec = std::sin(dec);
    double cos_dec = std::cos(dec);
    double sin_alt = sin_lat * sin_dec + cos_lat * cos_dec * std::cos(hour_angle);
    double y = -cos_dec * std::sin(hour_angle);
    double x = sin_dec * cos_lat - cos_dec * std::cos(hour_angle) * sin_lat;

    *alt_deg = qRadiansToDegrees(std::asin(std::clamp(sin_alt, -1.0, 1.0)));
    *az_deg = qRadiansToDegrees(std::atan2(y, x));
}

double star_radius(double mag)
{
    return std::clamp(3.6 - mag * 0.5, 0.7, 5.0);
}

} // namespace

SkyChart::SkyChart(QQuickItem* parent)
    : QQuickPaintedItem(parent),
      m_sky(nullptr),
      m_center_az(180.0),
      m_center_alt(45.0),
      m_fov(90.0),
      m_night_mode(false),
      m_mount_visible(false),
      m_mount_ra(0.0),
      m_mount_dec(0.0)
{
    setOpaquePainting(true);
}

void SkyChart::setBackend(SkyBackend* backend)
{
    if (m_sky)
        disconnect(m_sky, nullptr, this, nullptr);
    m_sky = backend;
    if (m_sky) {
        connect(m_sky, &SkyBackend::ticked, this, [this]() { update(); });
        connect(m_sky, &SkyBackend::settingsChanged, this, [this]() { update(); });
    }
    emit viewChanged();
    update();
}

void SkyChart::setCenterAz(double az)
{
    m_center_az = std::fmod(std::fmod(az, 360.0) + 360.0, 360.0);
    emit viewChanged();
    update();
}

void SkyChart::setCenterAlt(double alt)
{
    m_center_alt = std::clamp(alt, -10.0, 90.0);
    emit viewChanged();
    update();
}

void SkyChart::setFieldOfView(double fov)
{
    m_fov = std::clamp(fov, kMinFov, kMaxFov);
    emit viewChanged();
    update();
}

void SkyChart::setNightMode(bool enabled)
{
    m_night_mode = enabled;
    emit viewChanged();
    update();
}

void SkyChart::setTarget(const QVariantMap& target)
{
    m_target = target;
    emit viewChanged();
    update();
}

void SkyChart::setMountVisible(bool visible)
{
    m_mount_visible = visible;
    emit viewChanged();
    update();
}

void SkyChart::setMountRa(double ra)
{
    m_mount_ra = ra;
    emit viewChanged();
    update();
}

void SkyChart::setMountDec(double dec)
{
    m_mount_dec = dec;
    emit viewChanged();
    update();
}

void SkyChart::panBy(double dx, double dy)
{
    double degrees_per_pixel = m_fov / std::max(width(), 1.0);
    double cos_alt = std::max(std::cos(qDegreesToRadians(m_center_alt)), 0.1);

    setCenterAz(m_center_az - dx * degrees_per_pixel / cos_alt);
    setCenterAlt(m_center_alt + dy * degrees_per_pixel);
}

void SkyChart::centerOn(double ra_deg, double dec_deg)
{
    double az;
    double alt;

    if (!m_sky)
        return;
    astro::equatorial_to_horizontal(ra_deg, dec_deg, m_sky->observer(), astro::julian_now(), &az, &alt);
    setCenterAz(az);
    setCenterAlt(alt);
}

bool SkyChart::project(double az, double alt, QPointF* point) const
{
    double d_az = qDegreesToRadians(az - m_center_az);
    double alt_rad = qDegreesToRadians(alt);
    double center_alt = qDegreesToRadians(m_center_alt);
    double sin_alt = std::sin(alt_rad);
    double cos_alt = std::cos(alt_rad);
    double sin_center = std::sin(center_alt);
    double cos_center = std::cos(center_alt);
    double cos_c = sin_center * sin_alt + cos_center * cos_alt * std::cos(d_az);

    // Points opposite the view centre have no useful position
    if (cos_c < -0.9)
        return false;

    double radius = (width() / 2.0) / (2.0 * std::tan(qDegreesToRadians(m_fov) / 4.0));
    double k = 2.0 * radius / (1.0 + cos_c);

    // Azimuth increases to the right when facing the horizon
    point->setX(width() / 2.0 + k * cos_alt * std::sin(d_az));
    point->setY(height() / 2.0 - k * (cos_center * sin_alt - sin_center * cos_alt * std::cos(d_az)));
    return true;
}

void SkyChart::equatorial_to_screen_inputs(double* lst_rad, double* sin_lat, double* cos_lat) const
{
    double latitude = qDegreesToRadians(m_sky->observer().lat_deg);

    *lst_rad = qDegreesToRadians(astro::local_sidereal_deg(m_sky->observer(), astro::julian_now()));
    *sin_lat = std::sin(latitude);
    *cos_lat = std::cos(latitude);
}

void SkyChart::paint(QPainter* painter)
{
    // Midnight and brass by day, and red only at night to protect dark adaptation
    QColor background = m_night_mode ? QColor(8, 0, 0) : QColor(5, 8, 18);
    QColor background_glow = m_night_mode ? QColor(20, 2, 2) : QColor(14, 22, 44);
    QColor star = m_night_mode ? QColor(214, 44, 32) : QColor(238, 241, 250);
    QColor label = m_night_mode ? QColor(168, 34, 26) : QColor(150, 162, 190);
    QColor deep_sky = m_night_mode ? QColor(150, 30, 22) : QColor(118, 196, 186);
    QColor body_color = m_night_mode ? QColor(255, 74, 54) : QColor(233, 184, 114);
    QColor ground = m_night_mode ? QColor(30, 3, 3) : QColor(11, 15, 24);
    QColor horizon_line = m_night_mode ? QColor(110, 20, 14) : QColor(120, 98, 66);
    QColor grid = m_night_mode ? QColor(60, 10, 8) : QColor(34, 44, 72);
    QPointF point;

    QRadialGradient glow(width() / 2.0, height() * 0.9, std::max(width(), height()));
    glow.setColorAt(0.0, background_glow);
    glow.setColorAt(1.0, background);
    painter->fillRect(boundingRect(), glow);
    if (!m_sky)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    double lst_rad;
    double sin_lat;
    double cos_lat;
    double az;
    double alt;
    equatorial_to_screen_inputs(&lst_rad, &sin_lat, &cos_lat);

    draw_grid(painter, grid);
    draw_ground(painter, ground, horizon_line);

    // Draw only the stars visible at this zoom level, brightest first
    double mag_limit = std::clamp(7.0 - m_fov / 45.0, 3.5, 6.5);
    painter->setPen(Qt::NoPen);
    painter->setBrush(star);
    for (const CatalogStar& s : m_sky->catalog().stars()) {
        if (s.mag > mag_limit)
            break;
        fast_horizontal(s.ra_deg, s.dec_deg, lst_rad, sin_lat, cos_lat, &az, &alt);
        if (alt < -1.0 || !project(az, alt, &point) || !boundingRect().contains(point))
            continue;
        double r = star_radius(s.mag);
        // A soft halo makes the bright stars stand out
        if (s.mag < 2.0) {
            QColor halo = star;
            halo.setAlpha(46);
            painter->setBrush(halo);
            painter->drawEllipse(point, r * 2.4, r * 2.4);
            painter->setBrush(star);
        }
        painter->drawEllipse(point, r, r);
    }

    // Name the brighter stars
    QFont font = painter->font();
    font.setPixelSize(13);
    painter->setFont(font);
    painter->setPen(label);
    painter->setBrush(Qt::NoBrush);
    double label_limit = m_fov > 60.0 ? 1.5 : (m_fov > 25.0 ? 3.0 : 5.0);
    for (const CatalogStar& s : m_sky->catalog().stars()) {
        if (s.mag > label_limit)
            break;
        if (s.name.size() < 4 || s.name.at(0).isLower())
            continue;
        fast_horizontal(s.ra_deg, s.dec_deg, lst_rad, sin_lat, cos_lat, &az, &alt);
        if (alt >= -1.0 && project(az, alt, &point) && boundingRect().contains(point))
            painter->drawText(point + QPointF(5, -4), s.name);
    }

    // Deep-sky objects, with Messier objects always shown first
    painter->setPen(QPen(deep_sky, 1.2));
    for (const CatalogObject& o : m_sky->catalog().objects()) {
        if (!o.is_messier && m_fov > 30.0)
            break;
        fast_horizontal(o.ra_deg, o.dec_deg, lst_rad, sin_lat, cos_lat, &az, &alt);
        if (alt < -1.0 || !project(az, alt, &point) || !boundingRect().contains(point))
            continue;
        painter->drawEllipse(point, 4.0, 4.0);
        if (m_fov <= 90.0)
            painter->drawText(point + QPointF(6, 4), o.name);
    }

    // The Sun, the Moon and the planets
    double jd = astro::julian_now();
    font.setBold(true);
    painter->setFont(font);
    for (astro::Body b : astro::all_bodies()) {
        astro::BodyReport r = astro::body_report(b, m_sky->observer(), jd);
        if (r.alt_deg < -1.0 || !project(r.az_deg, r.alt_deg, &point) || !boundingRect().contains(point))
            continue;
        double size = (b == astro::Body::Sun || b == astro::Body::Moon) ? 8.0 : 5.0;
        QColor body_halo = body_color;
        body_halo.setAlpha(60);
        painter->setPen(Qt::NoPen);
        painter->setBrush(body_halo);
        painter->drawEllipse(point, size * 1.9, size * 1.9);
        painter->setBrush(body_color);
        painter->drawEllipse(point, size, size);
        painter->setPen(body_color);
        painter->setBrush(Qt::NoBrush);
        painter->drawText(point + QPointF(size + 3, 5), QString::fromStdString(r.name));
    }
    font.setBold(false);
    painter->setFont(font);

    // Compass points along the horizon
    painter->setPen(label);
    static const struct { double az; const char* name; } kCompass[] = {
        {0, "N"}, {45, "NE"}, {90, "E"}, {135, "SE"}, {180, "S"}, {225, "SW"}, {270, "W"}, {315, "NW"},
    };
    for (const auto& c : kCompass) {
        if (project(c.az, 0.0, &point) && boundingRect().contains(point))
            painter->drawText(point + QPointF(-8, 16), QString::fromLatin1(c.name));
    }

    // The selected target
    if (m_target.contains(QStringLiteral("ra"))) {
        fast_horizontal(m_target.value(QStringLiteral("ra")).toDouble(), m_target.value(QStringLiteral("dec")).toDouble(),
                        lst_rad, sin_lat, cos_lat, &az, &alt);
        if (project(az, alt, &point) && boundingRect().contains(point)) {
            // Corner brackets, like a viewfinder
            painter->setPen(QPen(body_color, 1.6));
            painter->setBrush(Qt::NoBrush);
            const double o = 15.0;
            const double l = 6.0;
            for (double sx : {-1.0, 1.0}) {
                for (double sy : {-1.0, 1.0}) {
                    QPointF corner = point + QPointF(sx * o, sy * o);
                    painter->drawLine(corner, corner - QPointF(sx * l, 0));
                    painter->drawLine(corner, corner - QPointF(0, sy * l));
                }
            }
        }
    }

    // Where the telescope is pointing
    if (m_mount_visible) {
        fast_horizontal(m_mount_ra, m_mount_dec, lst_rad, sin_lat, cos_lat, &az, &alt);
        if (project(az, alt, &point) && boundingRect().contains(point)) {
            painter->setPen(QPen(deep_sky, 2.0));
            painter->drawLine(point + QPointF(-16, 0), point + QPointF(-5, 0));
            painter->drawLine(point + QPointF(5, 0), point + QPointF(16, 0));
            painter->drawLine(point + QPointF(0, -16), point + QPointF(0, -5));
            painter->drawLine(point + QPointF(0, 5), point + QPointF(0, 16));
        }
    }
}

void SkyChart::draw_ground(QPainter* painter, const QColor& color, const QColor& line) const
{
    // In this projection the horizon is a circle. It encloses the sky when the
    // view is centred above the horizon, and the ground when centred below it.
    QPolygonF horizon;
    QPointF point;
    for (double a = m_center_az - 180.0; a < m_center_az + 180.0; a += 1.0) {
        if (project(a, 0.0, &point))
            horizon.append(point);
    }
    if (horizon.size() < 3)
        return;

    QPainterPath ground;
    ground.addPolygon(horizon);
    ground.closeSubpath();
    if (m_center_alt >= 0.0) {
        QPainterPath everything;
        everything.addRect(boundingRect());
        ground = everything.subtracted(ground);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->setClipRect(boundingRect());
    painter->drawPath(ground);
    painter->setPen(QPen(line, 1.2));
    painter->setBrush(Qt::NoBrush);
    painter->drawPolygon(horizon);
    painter->setClipping(false);
}

void SkyChart::draw_grid(QPainter* painter, const QColor& color) const
{
    QPointF point;
    QPointF previous;
    bool have_previous;

    painter->setPen(QPen(color, 1.0));
    painter->setBrush(Qt::NoBrush);

    // Altitude circles
    for (double alt = 30.0; alt < 90.0; alt += 30.0) {
        have_previous = false;
        for (double az = 0.0; az <= 360.0; az += 3.0) {
            bool visible = project(az, alt, &point);
            if (visible && have_previous)
                painter->drawLine(previous, point);
            previous = point;
            have_previous = visible;
        }
    }

    // Azimuth lines
    for (double az = 0.0; az < 360.0; az += 45.0) {
        have_previous = false;
        for (double alt = 0.0; alt <= 90.0; alt += 3.0) {
            bool visible = project(az, alt, &point);
            if (visible && have_previous)
                painter->drawLine(previous, point);
            previous = point;
            have_previous = visible;
        }
    }
}

QVariantMap SkyChart::objectAt(double x, double y) const
{
    QVariantMap best;
    if (!m_sky)
        return best;

    double lst_rad;
    double sin_lat;
    double cos_lat;
    double az;
    double alt;
    double best_distance = std::numeric_limits<double>::max();
    QPointF tap(x, y);
    QPointF point;
    equatorial_to_screen_inputs(&lst_rad, &sin_lat, &cos_lat);

    auto consider = [&](double ra, double dec, double priority_px, const QVariantMap& details) {
        fast_horizontal(ra, dec, lst_rad, sin_lat, cos_lat, &az, &alt);
        if (!project(az, alt, &point))
            return;
        double distance = std::hypot(point.x() - tap.x(), point.y() - tap.y()) - priority_px;
        if (distance < best_distance && distance < kTapRadiusPx) {
            best_distance = distance;
            best = details;
            best[QStringLiteral("ra")] = ra;
            best[QStringLiteral("dec")] = dec;
            best[QStringLiteral("az")] = std::fmod(az + 360.0, 360.0);
            best[QStringLiteral("alt")] = alt;
        }
    };

    // Named objects win over a nearby faint star
    double mag_limit = std::clamp(7.0 - m_fov / 45.0, 3.5, 6.5);
    for (const CatalogStar& s : m_sky->catalog().stars()) {
        if (s.mag > mag_limit)
            break;
        QVariantMap details;
        details[QStringLiteral("name")] = s.name.isEmpty() ? QStringLiteral("Star") : s.name;
        details[QStringLiteral("type")] = QStringLiteral("Star");
        details[QStringLiteral("mag")] = s.mag;
        consider(s.ra_deg, s.dec_deg, 0.0, details);
    }
    for (const CatalogObject& o : m_sky->catalog().objects()) {
        if (!o.is_messier && m_fov > 30.0)
            break;
        QVariantMap details;
        details[QStringLiteral("name")] = o.common.isEmpty() ? o.name : o.name + QStringLiteral(" ") + o.common;
        details[QStringLiteral("type")] = o.type;
        if (o.has_mag)
            details[QStringLiteral("mag")] = o.mag;
        consider(o.ra_deg, o.dec_deg, 8.0, details);
    }
    double jd = astro::julian_now();
    for (astro::Body b : astro::all_bodies()) {
        astro::BodyReport r = astro::body_report(b, m_sky->observer(), jd);
        QVariantMap details;
        details[QStringLiteral("name")] = QString::fromStdString(r.name);
        details[QStringLiteral("type")] = QStringLiteral("Solar system");
        details[QStringLiteral("mag")] = r.magnitude;
        consider(r.ra_deg, r.dec_deg, 16.0, details);
    }
    return best;
}
