#ifndef ASTRO_UI_CHART_MATH_H
#define ASTRO_UI_CHART_MATH_H

#include <algorithm>
#include <cmath>

// The sky chart's geometry, kept free of Qt so that it can be tested alone.
// Azimuth is measured from north through east, and all angles are in degrees
// unless the name says otherwise.
namespace chart {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

// A stereographic view, which keeps shapes recognisable towards the edge
struct View {
    double center_az;
    double sin_center;
    double cos_center;
    double radius;  // Pixels. Twice this times tan(c / 2) is the distance from the centre
    double cx;
    double cy;
};

inline View make_view(double center_az, double center_alt, double fov, double width, double height)
{
    View view;
    view.center_az = center_az;
    view.sin_center = std::sin(center_alt * kDegToRad);
    view.cos_center = std::cos(center_alt * kDegToRad);
    view.radius = (width / 2.0) / (2.0 * std::tan(fov * kDegToRad / 4.0));
    view.cx = width / 2.0;
    view.cy = height / 2.0;
    return view;
}

// Returns false for points so far behind the view that they have no useful position
inline bool project(const View& view, double az, double alt, double* x, double* y)
{
    double d_az = (az - view.center_az) * kDegToRad;
    double alt_rad = alt * kDegToRad;
    double sin_alt = std::sin(alt_rad);
    double cos_alt = std::cos(alt_rad);
    double cos_d_az = std::cos(d_az);
    double cos_c = view.sin_center * sin_alt + view.cos_center * cos_alt * cos_d_az;

    if (cos_c < -0.9)
        return false;

    double k = 2.0 * view.radius / (1.0 + cos_c);
    // Azimuth increases to the right when facing the horizon
    *x = view.cx + k * cos_alt * std::sin(d_az);
    *y = view.cy - k * (view.cos_center * sin_alt - view.sin_center * cos_alt * cos_d_az);
    return true;
}

// The direction seen at a pixel
inline void unproject(const View& view, double x, double y, double* az, double* alt)
{
    double dx = x - view.cx;
    double dy = view.cy - y;
    double rho = std::hypot(dx, dy);

    if (rho < 1e-9) {
        *az = view.center_az;
        *alt = std::atan2(view.sin_center, view.cos_center) * kRadToDeg;
        return;
    }

    double c = 2.0 * std::atan2(rho, 2.0 * view.radius);
    double sin_c = std::sin(c);
    double cos_c = std::cos(c);
    double sin_alt = cos_c * view.sin_center + dy * sin_c * view.cos_center / rho;

    *alt = std::asin(std::clamp(sin_alt, -1.0, 1.0)) * kRadToDeg;
    *az = view.center_az + std::atan2(dx * sin_c, rho * view.cos_center * cos_c - dy * view.sin_center * sin_c) * kRadToDeg;
}

// The parts of a horizontal conversion that are the same for every star in a frame
struct Site {
    double lst_rad;
    double sin_lat;
    double cos_lat;
};

inline void equatorial_to_horizontal(const Site& site, double ra, double dec, double* az, double* alt)
{
    double hour_angle = site.lst_rad - ra * kDegToRad;
    double dec_rad = dec * kDegToRad;
    double sin_dec = std::sin(dec_rad);
    double cos_dec = std::cos(dec_rad);
    double cos_ha = std::cos(hour_angle);
    double sin_alt = site.sin_lat * sin_dec + site.cos_lat * cos_dec * cos_ha;

    *alt = std::asin(std::clamp(sin_alt, -1.0, 1.0)) * kRadToDeg;
    *az = std::atan2(-cos_dec * std::sin(hour_angle), sin_dec * site.cos_lat - cos_dec * cos_ha * site.sin_lat) * kRadToDeg;
}

inline void horizontal_to_equatorial(const Site& site, double az, double alt, double* ra, double* dec)
{
    double az_rad = az * kDegToRad;
    double alt_rad = alt * kDegToRad;
    double sin_alt = std::sin(alt_rad);
    double cos_alt = std::cos(alt_rad);
    double cos_az = std::cos(az_rad);
    double sin_dec = sin_alt * site.sin_lat + cos_alt * site.cos_lat * cos_az;
    double hour_angle = std::atan2(-std::sin(az_rad) * cos_alt, sin_alt * site.cos_lat - cos_alt * cos_az * site.sin_lat);

    *dec = std::asin(std::clamp(sin_dec, -1.0, 1.0)) * kRadToDeg;
    *ra = std::fmod((site.lst_rad - hour_angle) * kRadToDeg + 720.0, 360.0);
}

// Angle between two directions on the sky
inline double separation(double az1, double alt1, double az2, double alt2)
{
    double a1 = alt1 * kDegToRad;
    double a2 = alt2 * kDegToRad;
    double cos_sep = std::sin(a1) * std::sin(a2) + std::cos(a1) * std::cos(a2) * std::cos((az1 - az2) * kDegToRad);
    return std::acos(std::clamp(cos_sep, -1.0, 1.0)) * kRadToDeg;
}

} // namespace chart

#endif
