#include "sky_chart.h"

#include <QDateTime>
#include <QBrush>
#include <QFontMetricsF>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <limits>

using chart::kDegToRad;
using chart::kRadToDeg;

static const double kMinFov = 1.0;
static const double kMaxFov = 180.0;
static const double kTapRadiusPx = 30.0;
static const int kSkyGridWidth = 120;
static const int kBodyUpdateMs = 4000;
static const int kRidgeSamples = 1440;  // One every quarter of a degree of azimuth

// Star images, by colour and by how bright the star looks at this zoom
static const int kStarColours = 6;
static const int kStarSizes = 16;

namespace {

double smoothstep(double low, double high, double value)
{
    double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

int star_colour_bucket(float colour_index)
{
    if (colour_index < 0.0f)
        return 0;
    if (colour_index < 0.3f)
        return 1;
    if (colour_index < 0.6f)
        return 2;
    if (colour_index < 1.0f)
        return 3;
    return colour_index < 1.4f ? 4 : 5;
}

QImage make_star_sprite(QColor colour, int size)
{
    double radius = 0.5 + 0.24 * size;
    double glow = size >= 6 ? radius * 2.8 : radius + 0.9;
    int half = int(std::ceil(glow)) + 1;
    QImage image(2 * half + 1, 2 * half + 1, QImage::Format_ARGB32_Premultiplied);
    double peak = std::min(1.0, 0.38 + 0.09 * size);
    double halo = size >= 6 ? 0.26 : 0.0;

    for (int y = 0; y < image.height(); y++) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); x++) {
            double d = std::hypot(double(x - half), double(y - half));
            double core = 1.0 - smoothstep(radius - 0.7, radius + 0.7, d);
            double outer = halo * std::pow(std::max(0.0, 1.0 - d / glow), 2.0);
            double alpha = std::clamp(core * peak + outer * (1.0 - core), 0.0, 1.0);
            // A hot centre makes a bright star look like a light, not a coloured dot
            double white = core * smoothstep(4.0, 10.0, size) * 0.8;
            int r = int(alpha * (colour.red() + (255 - colour.red()) * white));
            int g = int(alpha * (colour.green() + (255 - colour.green()) * white));
            int b = int(alpha * (colour.blue() + (255 - colour.blue()) * white));
            line[x] = qRgba(r, g, b, int(alpha * 255.0));
        }
    }
    return image;
}

// A rolling landscape, the same every time and independent of the real location
std::vector<double> make_ridge(double base, double hills, double detail, double phase)
{
    std::vector<double> ridge(kRidgeSamples);
    for (int i = 0; i < kRidgeSamples; i++) {
        double a = i * 2.0 * M_PI / kRidgeSamples;
        double height = base;
        height += hills * (0.55 * std::sin(2.0 * a + phase) + 0.3 * std::sin(5.0 * a + phase * 2.1) +
                           0.25 * std::sin(11.0 * a + phase * 0.7));
        height += detail * (std::sin(37.0 * a + phase) * 0.5 + std::sin(83.0 * a + phase * 1.7) * 0.3);
        ridge[i] = std::max(0.15, height);
    }
    return ridge;
}

// Stands of conifers along a ridge. Always the same, from a fixed sequence.
void add_trees(std::vector<double>* ridge)
{
    unsigned int random = 20261106u;
    auto next = [&random]() {
        random = random * 1664525u + 1013904223u;
        return ((random >> 16) & 0x7fff) / 32767.0;
    };

    std::vector<double> land = *ridge;
    double az = 0.0;
    while (az < 360.0) {
        // Woods come and go, leaving open ground between them
        double woods = std::sin(az * 0.052 + 1.0) + 0.6 * std::sin(az * 0.137);
        if (woods < -0.15) {
            az += 2.0;
            continue;
        }
        double height = 0.7 + 1.5 * next();
        double half_width = 0.28 + 0.3 * next();
        int center = int(az * kRidgeSamples / 360.0);
        int reach = int(half_width * kRidgeSamples / 360.0) + 1;
        for (int i = -reach; i <= reach; i++) {
            std::size_t index = std::size_t((center + i + kRidgeSamples) % kRidgeSamples);
            double along = 1.0 - std::fabs(double(i)) / reach;
            // Tiers of branches, narrowing towards the tip
            double tiered = along + 0.12 * std::sin(along * 16.0);
            (*ridge)[index] = std::max((*ridge)[index], land[index] + height * std::clamp(tiered, 0.0, 1.0));
        }
        az += 0.35 + 1.1 * next();
    }
}

// The Moon's near side as seen from Earth, from a NASA map of its whole surface.
// Its slight wobble (libration) is ignored, so this is only approximately the view
// on any given night. North is at the top, and the map has longitude 0 at its centre.
QImage make_moon_disc(const QImage& map)
{
    const int size = 512;
    const double half = size / 2.0;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    for (int y = 0; y < size; y++) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size; x++) {
            double nx = (x + 0.5 - half) / half;
            double ny = (half - y - 0.5) / half;
            double r2 = nx * nx + ny * ny;
            if (r2 > 1.0)
                continue;
            double nz = std::sqrt(1.0 - r2);
            double u = (std::atan2(nx, nz) / (2.0 * M_PI) + 0.5) * map.width();
            double v = (0.5 - std::asin(ny) / M_PI) * map.height();
            QRgb pixel = map.pixel(std::clamp(int(u), 0, map.width() - 1), std::clamp(int(v), 0, map.height() - 1));
            // The surface is really quite dark, so brighten it to how it looks in
            // the night sky, and darken it slightly towards the edge
            double gain = 1.35 * (0.82 + 0.18 * nz);
            line[x] = qRgba(std::min(int(qRed(pixel) * gain), 255), std::min(int(qGreen(pixel) * gain), 255),
                            std::min(int(qBlue(pixel) * gain), 255), 255);
        }
    }
    return image;
}

QColor planet_colour(astro::Body body)
{
    switch (body) {
    case astro::Body::Sun: return QColor(255, 244, 214);
    case astro::Body::Moon: return QColor(232, 228, 216);
    case astro::Body::Mercury: return QColor(196, 186, 174);
    case astro::Body::Venus: return QColor(255, 244, 222);
    case astro::Body::Mars: return QColor(236, 132, 92);
    case astro::Body::Jupiter: return QColor(236, 214, 178);
    case astro::Body::Saturn: return QColor(232, 206, 148);
    case astro::Body::Uranus: return QColor(170, 226, 226);
    case astro::Body::Neptune: return QColor(134, 164, 242);
    }
    return Qt::white;
}

} // namespace

class SkyChart::LabelPlacer {
public:
    explicit LabelPlacer(QPainter* painter, const QRectF& bounds) : m_painter(painter), m_bounds(bounds) { }

    // Reserve space that labels must avoid
    void block(const QRectF& rect) { m_used.append(rect); }

    // Draws the text unless it would collide with something more important
    bool place(const QPointF& anchor, const QString& text, const QFontMetricsF& metrics, bool centered = false)
    {
        double width = metrics.horizontalAdvance(text);
        QPointF start = centered ? anchor - QPointF(width / 2.0, 0.0) : anchor;
        QRectF rect(start.x() - 2.0, start.y() - metrics.ascent() - 1.0, width + 4.0, metrics.height() + 2.0);

        if (!m_bounds.contains(rect))
            return false;
        for (const QRectF& used : m_used) {
            if (used.intersects(rect))
                return false;
        }
        m_used.append(rect);
        m_painter->drawText(start, text);
        return true;
    }

private:
    QPainter* m_painter;
    QRectF m_bounds;
    QVector<QRectF> m_used;
};

SkyChart::SkyChart(QQuickItem* parent)
    : QQuickPaintedItem(parent),
      m_sky(nullptr),
      m_center_az(180.0),
      m_center_alt(45.0),
      m_fov(90.0),
      m_night_mode(false),
      m_mount_visible(false),
      m_mount_ra(0.0),
      m_mount_dec(0.0),
      m_show_constellations(true),
      m_show_milky_way(true),
      m_show_landscape(true),
      m_show_atmosphere(true),
      m_safe_top(0.0),
      m_safe_bottom(0.0),
      m_time_offset_hours(0.0),
      m_frame_star_limit(6.5),
      m_frame_daylight(0.0),
      m_frame_moon_light(0.0),
      m_next_body(0),
      m_last_body_update_ms(0),
      m_sprites_for_night(false),
      m_ridge_far(make_ridge(1.9, 1.0, 0.05, 0.6)),
      m_ridge_near(make_ridge(0.7, 0.45, 0.06, 2.4))
{
    setOpaquePainting(true);
    add_trees(&m_ridge_near);
    std::fill(std::begin(m_sky_key), std::end(m_sky_key), std::numeric_limits<double>::quiet_NaN());
}

void SkyChart::setBackend(SkyBackend* backend)
{
    if (m_sky)
        disconnect(m_sky, nullptr, this, nullptr);
    m_sky = backend;
    m_bodies.clear();
    if (m_sky) {
        connect(m_sky, &SkyBackend::ticked, this, [this]() { update(); });
        connect(m_sky, &SkyBackend::settingsChanged, this, [this]() {
            // The observer may have moved, and everything depends on that
            m_bodies.clear();
            update();
        });
        m_milky_way = QImage(m_sky->catalog().directory() + QStringLiteral("/milkyway.png")).convertToFormat(QImage::Format_Grayscale8);
        if (m_milky_way.isNull())
            qWarning("Could not read milkyway.png, so the chart will not show the Milky Way");
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

void SkyChart::setShowConstellations(bool show)
{
    m_show_constellations = show;
    emit viewChanged();
    update();
}

void SkyChart::setShowMilkyWay(bool show)
{
    m_show_milky_way = show;
    emit viewChanged();
    update();
}

void SkyChart::setShowLandscape(bool show)
{
    m_show_landscape = show;
    emit viewChanged();
    update();
}

void SkyChart::setShowAtmosphere(bool show)
{
    m_show_atmosphere = show;
    emit viewChanged();
    update();
}

void SkyChart::setSafeTop(double pixels)
{
    m_safe_top = pixels;
    emit viewChanged();
    update();
}

void SkyChart::setSafeBottom(double pixels)
{
    m_safe_bottom = pixels;
    emit viewChanged();
    update();
}

void SkyChart::setTimeOffsetHours(double hours)
{
    m_time_offset_hours = hours;
    m_bodies.clear();
    emit viewChanged();
    update();
}

void SkyChart::panBy(double dx, double dy)
{
    double degrees_per_pixel = m_fov / std::max(width(), 1.0);
    double cos_alt = std::max(std::cos(m_center_alt * kDegToRad), 0.1);

    setCenterAz(m_center_az - dx * degrees_per_pixel / cos_alt);
    setCenterAlt(m_center_alt + dy * degrees_per_pixel);
}

void SkyChart::centerOn(double ra_deg, double dec_deg)
{
    double az;
    double alt;

    if (!m_sky)
        return;
    astro::equatorial_to_horizontal(ra_deg, dec_deg, m_sky->observer(), julian_date(), &az, &alt);
    setCenterAz(az);
    setCenterAlt(alt);
}

chart::View SkyChart::view() const
{
    return chart::make_view(m_center_az, m_center_alt, m_fov, width(), height());
}

chart::Site SkyChart::site() const
{
    double latitude = m_sky->observer().lat_deg * kDegToRad;
    return {astro::local_sidereal_deg(m_sky->observer(), julian_date()) * kDegToRad,
            std::sin(latitude), std::cos(latitude)};
}

double SkyChart::julian_date() const
{
    return astro::julian_now() + m_time_offset_hours / 24.0;
}

void SkyChart::prepare_frame(const chart::Site& site)
{
    double az;
    double sun_alt;
    chart::equatorial_to_horizontal(site, m_bodies.front().ra_deg, m_bodies.front().dec_deg, &az, &sun_alt);
    bool real_sky = !m_night_mode && m_show_atmosphere;

    // 0 in full darkness, and 1 in full daylight. Always dark in night mode, and
    // with the atmosphere switched off, so stars can be planned for during the day.
    m_frame_daylight = real_sky ? smoothstep(-14.0, 4.0, sun_alt) : 0.0;

    // 0 with the Moon down or new, and 1 with a full Moon high up
    m_frame_moon_light = 0.0;
    if (real_sky) {
        for (const astro::BodyReport& r : m_bodies) {
            if (r.body == astro::Body::Moon) {
                double moon_alt;
                chart::equatorial_to_horizontal(site, r.ra_deg, r.dec_deg, &az, &moon_alt);
                m_frame_moon_light = r.illuminated * smoothstep(-2.0, 25.0, moon_alt);
            }
        }
    }

    // The faintest star shown. Zooming in reveals more, as a telescope would, and
    // a bright sky hides them, as the real one does.
    double limit = std::clamp(9.0 - 1.1 * std::log2(m_fov / 25.0), 6.0, 9.0);
    if (real_sky) {
        if (sun_alt > -18.0) {
            // About magnitude 6 at the end of twilight, 3 at nautical, 0 around sunset
            double sky_limit = sun_alt < -6.0 ? 3.0 + (-6.0 - sun_alt) * 0.25 : std::max(-2.0, 3.0 + (-6.0 - sun_alt) * 0.5);
            limit = std::min(limit, sky_limit);
        }
        limit -= 1.2 * m_frame_moon_light;
    }
    m_frame_star_limit = limit;
}

double SkyChart::ridge_height(const std::vector<double>& ridge, double az) const
{
    double wrapped = std::fmod(std::fmod(az, 360.0) + 360.0, 360.0);
    return ridge[std::size_t(wrapped * ridge.size() / 360.0) % ridge.size()];
}

void SkyChart::refresh_bodies()
{
    const auto& bodies = astro::all_bodies();
    double jd = julian_date();

    if (m_bodies.size() != bodies.size()) {
        m_bodies.clear();
        for (astro::Body body : bodies)
            m_bodies.push_back(astro::body_report(body, m_sky->observer(), jd));
        m_next_body = 0;
        m_last_body_update_ms = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_last_body_update_ms < kBodyUpdateMs)
        return;
    m_bodies[m_next_body] = astro::body_report(bodies[m_next_body], m_sky->observer(), jd);
    m_next_body = (m_next_body + 1) % bodies.size();
    m_last_body_update_ms = now;
}

void SkyChart::build_star_sprites()
{
    if (!m_star_sprites.isEmpty() && m_sprites_for_night == m_night_mode)
        return;

    static const QColor kDayColours[kStarColours] = {
        QColor(166, 194, 255), QColor(206, 222, 255), QColor(252, 248, 238),
        QColor(255, 228, 176), QColor(255, 198, 134), QColor(255, 166, 112),
    };
    m_star_sprites.clear();
    for (int colour = 0; colour < kStarColours; colour++) {
        // Star colours would spoil dark adaptation, so they are all red at night
        QColor tint = m_night_mode ? QColor(226, 40, 28) : kDayColours[colour];
        for (int size = 0; size < kStarSizes; size++)
            m_star_sprites.append(make_star_sprite(tint, size));
    }
    m_sprites_for_night = m_night_mode;
}

double SkyChart::milky_way_at(double ra, double dec) const
{
    if (m_milky_way.isNull())
        return 0.0;

    // Blend the four nearest map pixels. x is RA and y is Dec, starting at -90.
    int w = m_milky_way.width();
    int h = m_milky_way.height();
    double fx = ra / 360.0 * w - 0.5;
    double fy = std::clamp((dec + 90.0) / 180.0 * h - 0.5, 0.0, h - 1.001);
    int x0 = int(std::floor(fx));
    int y0 = int(fy);
    double tx = fx - x0;
    double ty = fy - y0;
    const uchar* row0 = m_milky_way.constScanLine(y0);
    const uchar* row1 = m_milky_way.constScanLine(y0 + 1);
    int xa = (x0 + w) % w;
    int xb = (x0 + 1 + w) % w;
    double top = row0[xa] + (row0[xb] - row0[xa]) * tx;
    double bottom = row1[xa] + (row1[xb] - row1[xa]) * tx;
    return (top + (bottom - top) * ty) / 255.0;
}

QRgb SkyChart::sky_colour(double az, double alt, double ra, double dec, double sun_az, double sun_alt) const
{
    double glow = m_show_milky_way ? milky_way_at(ra, dec) : 0.0;
    // Light passes through more air near the horizon, which dims the Milky Way
    double clarity = smoothstep(-2.0, 22.0, alt);

    if (m_night_mode) {
        // Dim and red, whatever the time of day
        return qRgb(int(std::min(7.0 + 46.0 * glow * clarity, 70.0)), 0, 0);
    }

    double day = m_show_atmosphere ? smoothstep(-14.0, 4.0, sun_alt) : 0.0;
    double moon = moon_light();
    glow *= clarity * (1.0 - day) * (1.0 - day) * (1.0 - 0.75 * moon);
    // Zoomed in, starlight spreads out rather than merging into a glow
    glow *= 0.35 + 0.65 * smoothstep(12.0, 70.0, m_fov);

    double low = std::exp(-std::max(alt, 0.0) / 16.0);

    // Night and day skies, each paler towards the horizon
    double r = 4.0 + 10.0 * low + moon * (10.0 + 12.0 * low);
    double g = 6.0 + 13.0 * low + moon * (18.0 + 18.0 * low);
    double b = 14.0 + 22.0 * low + moon * (34.0 + 24.0 * low);
    r += (52.0 + 120.0 * low - r) * day;
    g += (112.0 + 88.0 * low - g) * day;
    b += (196.0 + 44.0 * low - b) * day;

    if (m_show_atmosphere) {
        double sep = chart::separation(az, alt, sun_az, sun_alt);

        // Dusk and dawn colours, strongest near the Sun's position on the horizon
        double twilight = smoothstep(-18.0, -6.0, sun_alt) * (1.0 - smoothstep(0.0, 9.0, sun_alt));
        double towards_sun = 0.22 + 0.78 * std::exp(-(sep * sep) / 3200.0);
        double band = std::exp(-std::max(alt, 0.0) / 9.0);
        r += 236.0 * twilight * band * towards_sun;
        g += 104.0 * twilight * band * towards_sun;
        b += 26.0 * twilight * band * towards_sun;
        // The deep blue above the sunset glow
        double above = twilight * std::exp(-std::fabs(alt - 22.0) / 18.0);
        r += 14.0 * above;
        g += 34.0 * above;
        b += 72.0 * above;

        // The glare around the Sun
        if (sun_alt > -3.0) {
            double up = smoothstep(-3.0, 2.0, sun_alt);
            double glare = (std::exp(-(sep * sep) / 24.0) + 0.3 * std::exp(-sep / 16.0)) * up;
            r += 180.0 * glare;
            g += 150.0 * glare;
            b += 100.0 * glare;
        }
    }

    // Starlight is slightly warm where it is densest, towards the galaxy's centre
    r += 78.0 * glow + 26.0 * glow * glow;
    g += 82.0 * glow + 14.0 * glow * glow;
    b += 98.0 * glow;

    return qRgb(int(std::min(r, 255.0)), int(std::min(g, 255.0)), int(std::min(b, 255.0)));
}

void SkyChart::draw_sky(QPainter* painter, const chart::View& view, const chart::Site& site)
{
    int grid_w = kSkyGridWidth;
    int grid_h = std::max(1, int(std::round(kSkyGridWidth * height() / std::max(width(), 1.0))));
    const astro::BodyReport& sun = m_bodies.front();
    double sun_az;
    double sun_alt;
    chart::equatorial_to_horizontal(site, sun.ra_deg, sun.dec_deg, &sun_az, &sun_alt);

    // Redraw only after the view moves, or after the sky turns by about a minute's worth
    double key[6] = {m_center_az, m_center_alt, m_fov,
                     width() + (m_night_mode ? 10000.0 : 0.0) + (m_show_milky_way ? 20000.0 : 0.0) + (m_show_atmosphere ? 40000.0 : 0.0),
                     std::floor(site.lst_rad * kRadToDeg * 4.0), height()};
    if (m_sky_image.size() != QSize(grid_w, grid_h) || !std::equal(std::begin(key), std::end(key), std::begin(m_sky_key))) {
        if (m_sky_image.size() != QSize(grid_w, grid_h))
            m_sky_image = QImage(grid_w, grid_h, QImage::Format_RGB32);
        std::copy(std::begin(key), std::end(key), std::begin(m_sky_key));

        double step_x = width() / grid_w;
        double step_y = height() / grid_h;
        for (int y = 0; y < grid_h; y++) {
            QRgb* line = reinterpret_cast<QRgb*>(m_sky_image.scanLine(y));
            for (int x = 0; x < grid_w; x++) {
                double az;
                double alt;
                double ra;
                double dec;
                chart::unproject(view, (x + 0.5) * step_x, (y + 0.5) * step_y, &az, &alt);
                chart::horizontal_to_equatorial(site, az, alt, &ra, &dec);
                line[x] = sky_colour(az, alt, ra, dec, sun_az, sun_alt);
            }
        }
    }

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(boundingRect(), m_sky_image);
    painter->restore();
}

void SkyChart::draw_star(QPainter* painter, const chart::View& view, const chart::Site& site, double ra, double dec,
                         double mag, float colour_index, double limit)
{
    double az;
    double alt;
    double x;
    double y;

    chart::equatorial_to_horizontal(site, ra, dec, &az, &alt);
    if (alt < -1.0 || !chart::project(view, az, alt, &x, &y))
        return;
    if (x < -8.0 || y < -8.0 || x > width() + 8.0 || y > height() + 8.0)
        return;

    // Starlight dims and reddens on its long path through the air near the horizon
    double brightness = limit - mag;
    int colour = star_colour_bucket(colour_index);
    if (alt < 14.0 && m_show_atmosphere) {
        brightness -= (14.0 - alt) * 0.12;
        if (brightness < 0.0)
            return;
        colour = std::min(kStarColours - 1, colour + (alt < 3.0 ? 2 : (alt < 8.0 ? 1 : 0)));
    }

    int size = std::clamp(int(brightness * 2.0), 0, kStarSizes - 1);
    const QImage& sprite = m_star_sprites.at(colour * kStarSizes + size);
    painter->drawImage(QPoint(int(std::lround(x)) - sprite.width() / 2, int(std::lround(y)) - sprite.height() / 2), sprite);
}

void SkyChart::draw_stars(QPainter* painter, const chart::View& view, const chart::Site& site)
{
    build_star_sprites();

    double limit = star_limit();
    for (const CatalogStar& s : m_sky->catalog().stars()) {
        if (s.mag > limit)
            break;
        draw_star(painter, view, site, s.ra_deg, s.dec_deg, s.mag, s.colour_index, limit);
    }
    if (limit <= 6.5)
        return;

    // Look only in the blocks of sky near the view. A corner of the screen is
    // further away than half the width, and a block reaches about 7 degrees.
    double center_ra;
    double center_dec;
    double center_alt = std::atan2(view.sin_center, view.cos_center) * kRadToDeg;
    chart::horizontal_to_equatorial(site, view.center_az, center_alt, &center_ra, &center_dec);
    double diagonal = std::hypot(width(), height()) / std::max(width(), 1.0);
    double reach = m_fov * diagonal / 2.0 + 8.0;

    for (const FaintStarCell& cell : m_sky->catalog().faintCells()) {
        if (chart::separation(cell.center_ra_deg, cell.center_dec_deg, center_ra, center_dec) > reach)
            continue;
        for (const FaintStar& s : cell.stars) {
            if (s.mag > limit)
                break;
            draw_star(painter, view, site, s.ra_deg, s.dec_deg, s.mag, s.colour_index, limit);
        }
    }
}

void SkyChart::draw_constellation_lines(QPainter* painter, const chart::View& view, const chart::Site& site) const
{
    if (!m_show_constellations)
        return;

    QVector<QLineF> lines;
    QRectF bounds = boundingRect();

    for (const ConstellationSegment& s : m_sky->catalog().constellationLines()) {
        double az1, alt1, az2, alt2, x1, y1, x2, y2;
        chart::equatorial_to_horizontal(site, s.ra1_deg, s.dec1_deg, &az1, &alt1);
        chart::equatorial_to_horizontal(site, s.ra2_deg, s.dec2_deg, &az2, &alt2);
        if ((alt1 < -3.0 && alt2 < -3.0) || !chart::project(view, az1, alt1, &x1, &y1) || !chart::project(view, az2, alt2, &x2, &y2))
            continue;
        if (QRectF(QPointF(std::min(x1, x2), std::min(y1, y2)), QPointF(std::max(x1, x2), std::max(y1, y2))).intersects(bounds))
            lines.append(QLineF(x1, y1, x2, y2));
    }

    // Faint, so that the stars stay the main thing to look at
    QColor colour = m_night_mode ? QColor(150, 26, 18, 78) : QColor(150, 174, 226, 56);
    colour.setAlphaF(colour.alphaF() * (1.0 + 0.5 * daylight()));
    painter->setPen(QPen(colour, 1.0));
    painter->drawLines(lines);
}

void SkyChart::draw_object(QPainter* painter, const chart::View& view, const chart::Site& site, const CatalogObject& o,
                           QPointF p) const
{
    double pixels_per_degree = width() / m_fov;
    double major = o.major_arcmin / 120.0 * pixels_per_degree;
    double minor = std::max(o.minor_arcmin, 0.1f) / 120.0 * pixels_per_degree;
    const QString& type = o.type;
    bool galaxy = type == QLatin1String("Galaxy");
    bool open_cluster = type == QLatin1String("Open cluster") || type == QLatin1String("Star association");
    bool globular = type == QLatin1String("Globular cluster");
    bool planetary = type == QLatin1String("Planetary nebula");
    bool remnant = type == QLatin1String("Supernova remnant");
    bool reflection = type == QLatin1String("Reflection nebula");

    // Each kind has its real colour from long-exposure photographs
    QColor colour;
    if (m_night_mode)
        colour = QColor(200, 36, 26);
    else if (galaxy || globular)
        colour = QColor(255, 236, 206);
    else if (open_cluster)
        colour = QColor(190, 212, 255);
    else if (planetary)
        colour = QColor(118, 224, 204);
    else if (reflection)
        colour = QColor(130, 160, 255);
    else if (remnant)
        colour = QColor(246, 150, 110);
    else
        colour = QColor(250, 104, 126);

    // Real shape and size, where that is large enough to see. The stars within
    // clusters are real ones, drawn by the star layers, so none are made up here.
    if (major >= 2.5) {
        double angle = 0.0;
        double az, alt, x, y;
        chart::equatorial_to_horizontal(site, o.ra_deg, std::min(double(o.dec_deg) + 0.3, 89.9), &az, &alt);
        if (chart::project(view, az, alt, &x, &y)) {
            // East is anticlockwise from north on the sky
            angle = std::atan2(y - p.y(), x - p.x()) * kRadToDeg - o.angle_deg;
        }

        double strength = std::clamp(1.15 - (o.has_mag ? o.mag : 9.0) / 16.0, 0.4, 1.0);
        QRadialGradient light(QPointF(0, 0), 1.0);
        QColor c = colour;
        auto stop = [&](double at, double alpha) {
            c.setAlphaF(std::clamp(alpha * strength, 0.0, 1.0));
            light.setColorAt(at, c);
        };
        if (galaxy) {
            stop(0.0, 0.85); stop(0.08, 0.5); stop(0.3, 0.2); stop(0.65, 0.07); stop(1.0, 0.0);
        } else if (globular) {
            stop(0.0, 0.8); stop(0.35, 0.42); stop(0.7, 0.1); stop(1.0, 0.0);
        } else if (open_cluster) {
            stop(0.0, 0.16); stop(0.7, 0.08); stop(1.0, 0.0);
        } else if (planetary || remnant) {
            stop(0.0, 0.12); stop(0.55, 0.28); stop(0.78, 0.62); stop(1.0, 0.0);
        } else {
            stop(0.0, 0.75); stop(0.35, 0.42); stop(0.7, 0.14); stop(1.0, 0.0);
        }

        painter->save();
        painter->translate(p);
        painter->rotate(angle);
        painter->scale(major, std::max(minor, 1.5));
        painter->setPen(Qt::NoPen);
        painter->setBrush(light);
        painter->drawEllipse(QPointF(0, 0), 1.0, 1.0);
        painter->restore();
    }

    // Too small to show as it is, so mark it faintly to show that something is there
    if (major < 9.0 && m_fov <= 120.0) {
        QColor marker = colour;
        marker.setAlpha(m_night_mode ? 130 : 110);
        QPen pen(marker, 1.0);
        painter->setBrush(Qt::NoBrush);
        if (open_cluster) {
            pen.setDashPattern({1.5, 2.2});
            painter->setPen(pen);
            painter->drawEllipse(p, 4.0, 4.0);
        } else if (globular) {
            painter->setPen(pen);
            painter->drawEllipse(p, 3.5, 3.5);
            painter->drawLine(p + QPointF(-3.5, 0), p + QPointF(3.5, 0));
            painter->drawLine(p + QPointF(0, -3.5), p + QPointF(0, 3.5));
        } else if (galaxy) {
            painter->setPen(pen);
            painter->save();
            painter->translate(p);
            painter->rotate(-28.0);
            painter->drawEllipse(QPointF(0, 0), 5.0, 2.2);
            painter->restore();
        } else if (planetary) {
            painter->setPen(pen);
            painter->drawEllipse(p, 2.5, 2.5);
            painter->drawLine(p + QPointF(-5, 0), p + QPointF(-2.5, 0));
            painter->drawLine(p + QPointF(2.5, 0), p + QPointF(5, 0));
            painter->drawLine(p + QPointF(0, -5), p + QPointF(0, -2.5));
            painter->drawLine(p + QPointF(0, 2.5), p + QPointF(0, 5));
        } else {
            painter->setPen(pen);
            painter->drawRoundedRect(QRectF(p.x() - 3.5, p.y() - 3.5, 7.0, 7.0), 2.0, 2.0);
        }
    }
}

void SkyChart::draw_deep_sky(QPainter* painter, const chart::View& view, const chart::Site& site) const
{
    // A bright sky hides them all
    double day = daylight();
    if (day > 0.6)
        return;

    painter->save();
    painter->setOpacity((1.0 - day / 0.6) * (1.0 - 0.5 * moon_light()));

    double margin = 200.0 * 90.0 / m_fov;
    QRectF near_screen = boundingRect().adjusted(-margin, -margin, margin, margin);
    for (const CatalogObject& o : m_sky->catalog().objects()) {
        if (!o.is_messier && m_fov > 30.0)
            break;
        double az, alt, x, y;
        chart::equatorial_to_horizontal(site, o.ra_deg, o.dec_deg, &az, &alt);
        if (alt < -1.0 || !chart::project(view, az, alt, &x, &y) || !near_screen.contains(QPointF(x, y)))
            continue;
        draw_object(painter, view, site, o, QPointF(x, y));
    }
    painter->restore();
}

void SkyChart::draw_moon(QPainter* painter, const chart::View& view, const chart::Site& site, QPointF center, double radius)
{
    const astro::BodyReport& sun = m_bodies[0];
    const astro::BodyReport* moon = nullptr;
    for (const astro::BodyReport& r : m_bodies) {
        if (r.body == astro::Body::Moon)
            moon = &r;
    }
    if (!moon)
        return;
    if (m_moon_map.isNull()) {
        QImage map(m_sky->catalog().directory() + QStringLiteral("/moon-map.jpg"));
        if (!map.isNull()) {
            m_moon_map = make_moon_disc(map.convertToFormat(QImage::Format_RGB32));
        } else {
            qWarning("Could not read moon-map.jpg, so the Moon will be plain");
            m_moon_map = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
            m_moon_map.fill(QColor(226, 222, 212));
        }
    }

    // Find which way the Sun lies on screen by stepping slightly towards it
    double sun_angle = 0.0;
    double moon_az, moon_alt, sun_az, sun_alt, x, y;
    chart::equatorial_to_horizontal(site, moon->ra_deg, moon->dec_deg, &moon_az, &moon_alt);
    chart::equatorial_to_horizontal(site, sun.ra_deg, sun.dec_deg, &sun_az, &sun_alt);
    auto unit = [](double az, double alt, double v[3]) {
        v[0] = std::cos(alt * kDegToRad) * std::cos(az * kDegToRad);
        v[1] = std::cos(alt * kDegToRad) * std::sin(az * kDegToRad);
        v[2] = std::sin(alt * kDegToRad);
    };
    double m[3], s[3];
    unit(moon_az, moon_alt, m);
    unit(sun_az, sun_alt, s);
    double p[3] = {m[0] + 0.03 * (s[0] - m[0]), m[1] + 0.03 * (s[1] - m[1]), m[2] + 0.03 * (s[2] - m[2])};
    double step_az = std::atan2(p[1], p[0]) * kRadToDeg;
    double step_alt = std::atan2(p[2], std::hypot(p[0], p[1])) * kRadToDeg;
    if (chart::project(view, step_az, step_alt, &x, &y))
        sun_angle = std::atan2(y - center.y(), x - center.x()) * kRadToDeg;

    // The map has north at the top, so turn it to match celestial north here
    double north_angle = -90.0;
    double north_az, north_alt;
    chart::equatorial_to_horizontal(site, moon->ra_deg, std::min(moon->dec_deg + 0.3, 89.9), &north_az, &north_alt);
    if (chart::project(view, north_az, north_alt, &x, &y))
        north_angle = std::atan2(y - center.y(), x - center.x()) * kRadToDeg;

    // Half a circle on the sunward side, closed by the curved edge of shadow
    double curve = radius * (1.0 - 2.0 * std::clamp(moon->illuminated, 0.0, 1.0));
    QPainterPath shape;
    shape.moveTo(0.0, -radius);
    shape.arcTo(QRectF(-radius, -radius, 2.0 * radius, 2.0 * radius), 90.0, -180.0);
    shape.cubicTo(curve * 1.33, radius, curve * 1.33, -radius, 0.0, -radius);

    // The dark part is faintly visible by light reflected from Earth
    int side = int(std::ceil(radius)) * 2 + 4;
    double middle = side / 2.0;
    QImage disc(side, side, QImage::Format_ARGB32_Premultiplied);
    disc.fill(Qt::transparent);
    QPainter drawing(&disc);
    drawing.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    drawing.translate(middle, middle);
    drawing.rotate(north_angle + 90.0);
    drawing.drawImage(QRectF(-radius, -radius, 2.0 * radius, 2.0 * radius), m_moon_map);
    drawing.end();

    // Draw the lit shape, then keep the Moon's surface only where that shape is
    QImage lit(side, side, QImage::Format_ARGB32_Premultiplied);
    lit.fill(Qt::transparent);
    QPainter cutting(&lit);
    cutting.setRenderHint(QPainter::Antialiasing);
    cutting.save();
    cutting.translate(middle, middle);
    cutting.rotate(sun_angle);
    cutting.setPen(Qt::NoPen);
    cutting.setBrush(Qt::black);
    cutting.drawPath(shape);
    cutting.restore();
    cutting.setCompositionMode(QPainter::CompositionMode_SourceIn);
    cutting.drawImage(0, 0, disc);
    cutting.end();

    QPointF corner = center - QPointF(middle, middle);

    // The night side, in the same place but very dim. Keep the sky from showing through.
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_night_mode ? QColor(16, 2, 1) : QColor(14, 17, 26));
    painter->drawEllipse(center, radius, radius);
    if (!m_night_mode) {
        painter->save();
        painter->setOpacity(painter->opacity() * 0.09);
        painter->drawImage(corner, disc);
        painter->restore();
    }

    if (m_night_mode) {
        // Only the red channel, to keep the map's shading without its white
        QImage red = lit.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int row = 0; row < red.height(); row++) {
            QRgb* line = reinterpret_cast<QRgb*>(red.scanLine(row));
            for (int col = 0; col < red.width(); col++)
                line[col] = qRgba(qRed(line[col]) * 225 / 255, 0, 0, qAlpha(line[col]));
        }
        painter->drawImage(corner, red);
    } else {
        painter->drawImage(corner, lit);
    }
}

void SkyChart::draw_bodies(QPainter* painter, const chart::View& view, const chart::Site& site)
{
    double pixels_per_degree = width() / m_fov;
    QColor night_colour(232, 56, 40);

    // Draw from the farthest in, so that the Moon passes in front of the planets
    for (auto it = m_bodies.rbegin(); it != m_bodies.rend(); ++it) {
        const astro::BodyReport& r = *it;
        double az, alt, x, y;
        chart::equatorial_to_horizontal(site, r.ra_deg, r.dec_deg, &az, &alt);
        if (alt < -2.0 || !chart::project(view, az, alt, &x, &y))
            continue;
        QPointF p(x, y);
        if (!boundingRect().adjusted(-40, -40, 40, 40).contains(p))
            continue;

        bool big = r.body == astro::Body::Sun || r.body == astro::Body::Moon;
        // A bright sky hides the planets, as it does the stars
        if (!big && r.magnitude > star_limit() + 2.0)
            continue;

        QColor colour = m_night_mode ? night_colour : planet_colour(r.body);
        // The planets look like bright stars, with size following their brightness
        double radius = big ? std::max(8.0, 0.26 * pixels_per_degree)
                            : std::clamp(3.0 - 0.42 * r.magnitude, 1.6, 5.5);
        double glow = radius * (r.body == astro::Body::Sun ? 4.5 : (big ? 2.2 : 3.0));

        QRadialGradient halo(p, glow);
        QColor inner = colour;
        inner.setAlpha(r.body == astro::Body::Moon ? 60 : (r.body == astro::Body::Sun ? 220 : 90));
        QColor outer = colour;
        outer.setAlpha(0);
        halo.setColorAt(0.0, inner);
        halo.setColorAt(1.0, outer);
        painter->setPen(Qt::NoPen);
        painter->setBrush(halo);
        painter->drawEllipse(p, glow, glow);

        if (r.body == astro::Body::Moon) {
            draw_moon(painter, view, site, p, radius);
            continue;
        }
        // Saturn's rings need magnification to be seen
        if (r.body == astro::Body::Saturn && m_fov < 12.0) {
            painter->save();
            painter->translate(p);
            painter->rotate(-18.0);
            painter->setPen(QPen(colour, 1.2));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QPointF(0, 0), radius * 2.0, radius * 0.7);
            painter->restore();
            painter->setPen(Qt::NoPen);
        }
        painter->setBrush(colour);
        painter->drawEllipse(p, radius, radius);
    }
}

void SkyChart::draw_ridge(QPainter* painter, const chart::View& view, const std::vector<double>& ridge,
                          const QBrush& fill, const QColor& rim) const
{
    QPolygonF outline;
    outline.reserve(int(ridge.size()));
    double x;
    double y;

    // Start opposite the view, so that the gap left by points behind it stays off screen
    double step = 360.0 / ridge.size();
    std::size_t start = std::size_t(std::fmod(m_center_az + 180.0, 360.0) / step) % ridge.size();
    // Fine detail can't be seen when zoomed out, so skip some of the points
    std::size_t stride = std::size_t(std::clamp(m_fov / 45.0, 1.0, 4.0));
    for (std::size_t i = 0; i < ridge.size(); i += stride) {
        std::size_t index = (start + i) % ridge.size();
        if (chart::project(view, index * step, ridge[index], &x, &y))
            outline.append(QPointF(x, y));
    }
    if (outline.size() < 3)
        return;

    // The ridge is a closed loop around the zenith. Inside it is sky when the view
    // is centred above the ridge, so paint outside it. Below the ridge, paint inside.
    QPainterPath ground;
    ground.setFillRule(Qt::OddEvenFill);
    ground.addPolygon(outline);
    ground.closeSubpath();
    if (m_center_alt >= ridge_height(ridge, m_center_az))
        ground.addRect(boundingRect().adjusted(-4, -4, 4, 4));

    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawPath(ground);
    if (rim.alpha() > 0) {
        painter->setPen(QPen(rim, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(outline);
    }
}

void SkyChart::draw_landscape(QPainter* painter, const chart::View& view) const
{
    painter->save();
    painter->setClipRect(boundingRect());

    if (!m_show_landscape) {
        // Still mark the true horizon, as the ground hides what is below it
        static const std::vector<double> flat(kRidgeSamples, 0.0);
        QColor ground = m_night_mode ? QColor(20, 2, 2) : QColor(9, 12, 20);
        QColor rim = m_night_mode ? QColor(120, 22, 14) : QColor(150, 122, 80);
        draw_ridge(painter, view, flat, ground, rim);
        painter->restore();
        return;
    }

    double day = daylight();
    QColor far_land;
    QColor near_top;
    QColor near_bottom;
    if (m_night_mode) {
        far_land = QColor(14, 1, 1);
        near_top = QColor(8, 0, 0);
        near_bottom = QColor(4, 0, 0);
    } else {
        // Hazy blue in the distance, and greener and darker close by
        far_land = QColor(int(13 + 96 * day), int(17 + 118 * day), int(27 + 134 * day));
        near_top = QColor(int(5 + 40 * day), int(7 + 66 * day), int(11 + 40 * day));
        near_bottom = QColor(int(3 + 22 * day), int(4 + 34 * day), int(7 + 18 * day));
    }

    // Darken the ground towards the viewer's feet
    QBrush near_brush(near_top);
    double x0, y0, x1, y1;
    if (m_center_alt < 50.0 && chart::project(view, m_center_az, 0.0, &x0, &y0) && chart::project(view, m_center_az, -25.0, &x1, &y1)) {
        QLinearGradient shade(0.0, y0, 0.0, y1);
        shade.setColorAt(0.0, near_top);
        shade.setColorAt(1.0, near_bottom);
        near_brush = QBrush(shade);
    }

    draw_ridge(painter, view, m_ridge_far, far_land, QColor(0, 0, 0, 0));
    draw_ridge(painter, view, m_ridge_near, near_brush, QColor(0, 0, 0, 0));
    painter->restore();
}

void SkyChart::draw_labels(QPainter* painter, const chart::View& view, const chart::Site& site) const
{
    QRectF bounds(0.0, m_safe_top, width(), std::max(height() - m_safe_top - m_safe_bottom, 1.0));
    LabelPlacer labels(painter, bounds);
    double az, alt, x, y;

    // The buttons down each side of the Sky page
    labels.block(QRectF(0.0, m_safe_top, 70.0, 120.0));
    labels.block(QRectF(width() - 70.0, m_safe_top, 70.0, 120.0));

    QFont font(QStringLiteral("IBM Plex Sans"));
    font.setPixelSize(13);

    // Most important first, as a later label gives way to an earlier one
    font.setBold(true);
    painter->setFont(font);
    QFontMetricsF body_metrics(font);
    double day = daylight();
    QColor body_label = m_night_mode ? QColor(236, 60, 44) : (day > 0.5 ? QColor(255, 244, 216) : QColor(243, 203, 144));
    painter->setPen(body_label);
    for (const astro::BodyReport& r : m_bodies) {
        chart::equatorial_to_horizontal(site, r.ra_deg, r.dec_deg, &az, &alt);
        // Nothing behind the hills should be labelled as if it could be seen
        double land = m_show_landscape ? std::max(ridge_height(m_ridge_far, az), ridge_height(m_ridge_near, az)) : 0.0;
        if (alt < land || !chart::project(view, az, alt, &x, &y) || !bounds.contains(QPointF(x, y)))
            continue;
        painter->setPen(r.body == astro::Body::Sun && !m_night_mode ? QColor(24, 34, 58) : body_label);
        bool big = r.body == astro::Body::Sun || r.body == astro::Body::Moon;
        double radius = big ? std::max(8.0, 0.26 * width() / m_fov) : 5.5;
        if (!big && r.magnitude > star_limit() + 2.0)
            continue;
        labels.block(QRectF(x - radius, y - radius, 2.0 * radius, 2.0 * radius));
        labels.place(QPointF(x + radius + 5.0, y + 4.5), QString::fromStdString(r.name), body_metrics);
    }

    // Compass points on the ground
    font.setPixelSize(13);
    painter->setFont(font);
    QFontMetricsF compass_metrics(font);
    static const struct { double az; const char* name; bool major; } kCompass[] = {
        {0, "N", true}, {45, "NE", false}, {90, "E", true}, {135, "SE", false},
        {180, "S", true}, {225, "SW", false}, {270, "W", true}, {315, "NW", false},
    };
    for (const auto& c : kCompass) {
        if (!chart::project(view, c.az, -1.2, &x, &y) || !bounds.contains(QPointF(x, y)))
            continue;
        painter->setPen(c.major ? (m_night_mode ? QColor(214, 48, 34) : QColor(233, 184, 114))
                                : (m_night_mode ? QColor(140, 26, 18) : QColor(150, 162, 190)));
        labels.place(QPointF(x, y + 9.0), QString::fromLatin1(c.name), compass_metrics, true);
    }

    // Bright stars
    font.setBold(false);
    font.setPixelSize(12);
    painter->setFont(font);
    QFontMetricsF star_metrics(font);
    painter->setPen(m_night_mode ? QColor(176, 34, 24) : (day > 0.5 ? QColor(238, 244, 255) : QColor(190, 200, 222)));
    double name_limit = std::min(m_fov > 90.0 ? 1.6 : (m_fov > 45.0 ? 2.4 : (m_fov > 20.0 ? 3.6 : 5.0)), star_limit());
    for (const CatalogStar& s : m_sky->catalog().stars()) {
        if (s.mag > name_limit)
            break;
        if (!s.proper_name && !(m_fov < 20.0 && s.name.size() >= 4))
            continue;
        chart::equatorial_to_horizontal(site, s.ra_deg, s.dec_deg, &az, &alt);
        if (alt >= 0.0 && chart::project(view, az, alt, &x, &y) && bounds.contains(QPointF(x, y)))
            labels.place(QPointF(x + 7.0, y - 3.0), s.name, star_metrics);
    }

    // Messier numbers, and other objects when zoomed in
    if (m_fov <= 70.0 && day < 0.6) {
        font.setPixelSize(11);
        painter->setFont(font);
        QFontMetricsF object_metrics(font);
        painter->setPen(m_night_mode ? QColor(150, 28, 20) : QColor(166, 182, 200));
        for (const CatalogObject& o : m_sky->catalog().objects()) {
            if (!o.is_messier && m_fov > 30.0)
                break;
            chart::equatorial_to_horizontal(site, o.ra_deg, o.dec_deg, &az, &alt);
            if (alt >= 0.0 && chart::project(view, az, alt, &x, &y) && bounds.contains(QPointF(x, y)))
                labels.place(QPointF(x + 8.0, y + 4.0), o.name, object_metrics);
        }
    }

    // Constellation names, spaced out so that they read as areas of sky
    if (m_show_constellations && m_fov >= 15.0) {
        font.setPixelSize(10);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        painter->setFont(font);
        QFontMetricsF con_metrics(font);
        QColor name_colour = m_night_mode ? QColor(116, 22, 14) : (day > 0.5 ? QColor(230, 240, 255, 170) : QColor(130, 148, 196, 200));
        painter->setPen(name_colour);
        for (const ConstellationName& n : m_sky->catalog().constellationNames()) {
            chart::equatorial_to_horizontal(site, n.ra_deg, n.dec_deg, &az, &alt);
            if (alt >= 3.0 && chart::project(view, az, alt, &x, &y) && bounds.contains(QPointF(x, y)))
                labels.place(QPointF(x, y), n.name, con_metrics, true);
        }
    }
}

void SkyChart::draw_markers(QPainter* painter, const chart::View& view, const chart::Site& site) const
{
    double az, alt, x, y;
    QColor brass = m_night_mode ? QColor(255, 74, 54) : QColor(243, 203, 144);
    QColor teal = m_night_mode ? QColor(214, 48, 34) : QColor(118, 196, 186);

    painter->setBrush(Qt::NoBrush);

    // The selected target, in corner brackets like a viewfinder
    if (m_target.contains(QStringLiteral("ra"))) {
        chart::equatorial_to_horizontal(site, m_target.value(QStringLiteral("ra")).toDouble(),
                                        m_target.value(QStringLiteral("dec")).toDouble(), &az, &alt);
        if (chart::project(view, az, alt, &x, &y) && boundingRect().contains(QPointF(x, y))) {
            QPointF point(x, y);
            painter->setPen(QPen(brass, 1.6));
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
        chart::equatorial_to_horizontal(site, m_mount_ra, m_mount_dec, &az, &alt);
        if (chart::project(view, az, alt, &x, &y) && boundingRect().contains(QPointF(x, y))) {
            QPointF point(x, y);
            painter->setPen(QPen(teal, 2.0));
            painter->drawEllipse(point, 11.0, 11.0);
            painter->drawLine(point + QPointF(-18, 0), point + QPointF(-7, 0));
            painter->drawLine(point + QPointF(7, 0), point + QPointF(18, 0));
            painter->drawLine(point + QPointF(0, -18), point + QPointF(0, -7));
            painter->drawLine(point + QPointF(0, 7), point + QPointF(0, 18));
        }
    }
}

void SkyChart::paint(QPainter* painter)
{
    if (!m_sky) {
        painter->fillRect(boundingRect(), m_night_mode ? QColor(8, 0, 0) : QColor(5, 8, 18));
        return;
    }

    refresh_bodies();
    chart::View current_view = view();
    chart::Site current_site = site();
    prepare_frame(current_site);

    draw_sky(painter, current_view, current_site);
    painter->setRenderHint(QPainter::Antialiasing, false);
    draw_stars(painter, current_view, current_site);

    painter->setRenderHint(QPainter::Antialiasing, true);
    draw_constellation_lines(painter, current_view, current_site);
    draw_deep_sky(painter, current_view, current_site);
    draw_bodies(painter, current_view, current_site);
    draw_landscape(painter, current_view);
    draw_labels(painter, current_view, current_site);
    draw_markers(painter, current_view, current_site);
}

QVariantMap SkyChart::objectAt(double x, double y) const
{
    QVariantMap best;
    if (!m_sky)
        return best;

    chart::View current_view = view();
    chart::Site current_site = site();
    double best_distance = std::numeric_limits<double>::max();

    auto consider = [&](double ra, double dec, double priority_px, const QVariantMap& details) {
        double az, alt, px, py;
        chart::equatorial_to_horizontal(current_site, ra, dec, &az, &alt);
        if (!chart::project(current_view, az, alt, &px, &py))
            return;
        double distance = std::hypot(px - x, py - y) - priority_px;
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
    // Faint stars are for looks, so a tap always finds something named
    double limit = std::min(star_limit(), 6.5);
    for (const CatalogStar& s : m_sky->catalog().stars()) {
        if (s.mag > limit)
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
    for (const astro::BodyReport& r : m_bodies) {
        QVariantMap details;
        details[QStringLiteral("name")] = QString::fromStdString(r.name);
        details[QStringLiteral("type")] = QStringLiteral("Solar system");
        details[QStringLiteral("mag")] = r.magnitude;
        consider(r.ra_deg, r.dec_deg, 16.0, details);
    }
    return best;
}
