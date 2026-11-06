#include "astro/sky.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <libnova/libnova.h>

namespace astro {

namespace {

const double kKmPerAu = 149597870.7;
const double kSunMagnitudeAt1Au = -26.74;
const double kNightSunAltitude = -12.0;
const double kEarthRadiusKm = 6378.14;

ln_lnlat_posn to_lnlat(const Observer& observer)
{
    ln_lnlat_posn position;
    position.lng = observer.lon_deg;
    position.lat = observer.lat_deg;
    return position;
}

double wrap_360(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

typedef void (*EquFunction)(double, ln_equ_posn*);

EquFunction equ_function(Body body)
{
    switch (body) {
    case Body::Sun: return ln_get_solar_equ_coords;
    case Body::Moon: return ln_get_lunar_equ_coords;
    case Body::Mercury: return ln_get_mercury_equ_coords;
    case Body::Venus: return ln_get_venus_equ_coords;
    case Body::Mars: return ln_get_mars_equ_coords;
    case Body::Jupiter: return ln_get_jupiter_equ_coords;
    case Body::Saturn: return ln_get_saturn_equ_coords;
    case Body::Uranus: return ln_get_uranus_equ_coords;
    case Body::Neptune: return ln_get_neptune_equ_coords;
    }
    return ln_get_solar_equ_coords;
}

// libnova returns positions in the J2000 frame, despite its documentation
// calling them apparent, so precess them to the requested date. Without this
// they are wrong by about 0.36 degrees in 2026, more than the Moon's diameter.
void equ_of_date(Body body, double jd, ln_equ_posn* position)
{
    ln_equ_posn j2000;

    equ_function(body)(jd, &j2000);
    ln_get_equ_prec2(&j2000, JD2000, jd, position);
}

double distance_au(Body body, double jd)
{
    switch (body) {
    case Body::Sun: return ln_get_earth_solar_dist(jd);
    case Body::Moon: return ln_get_lunar_earth_dist(jd) / kKmPerAu;
    case Body::Mercury: return ln_get_mercury_earth_dist(jd);
    case Body::Venus: return ln_get_venus_earth_dist(jd);
    case Body::Mars: return ln_get_mars_earth_dist(jd);
    case Body::Jupiter: return ln_get_jupiter_earth_dist(jd);
    case Body::Saturn: return ln_get_saturn_earth_dist(jd);
    case Body::Uranus: return ln_get_uranus_earth_dist(jd);
    case Body::Neptune: return ln_get_neptune_earth_dist(jd);
    }
    return 0.0;
}

double lunar_magnitude(double jd)
{
    double phase = ln_get_lunar_phase(jd);
    double sun = ln_get_earth_solar_dist(jd);
    double moon = ln_get_lunar_earth_dist(jd) / kKmPerAu;
    return 0.21 + 5.0 * std::log10(sun * moon) + 0.026 * phase + 4.0e-9 * std::pow(phase, 4.0);
}

double magnitude(Body body, double jd)
{
    switch (body) {
    case Body::Sun: return kSunMagnitudeAt1Au + 5.0 * std::log10(ln_get_earth_solar_dist(jd));
    case Body::Moon: return lunar_magnitude(jd);
    case Body::Mercury: return ln_get_mercury_magnitude(jd);
    case Body::Venus: return ln_get_venus_magnitude(jd);
    case Body::Mars: return ln_get_mars_magnitude(jd);
    case Body::Jupiter: return ln_get_jupiter_magnitude(jd);
    case Body::Saturn: return ln_get_saturn_magnitude(jd);
    case Body::Uranus: return ln_get_uranus_magnitude(jd);
    case Body::Neptune: return ln_get_neptune_magnitude(jd);
    }
    return 0.0;
}

double illuminated(Body body, double jd)
{
    switch (body) {
    case Body::Sun: return 1.0;
    case Body::Moon: return ln_get_lunar_disk(jd);
    case Body::Mercury: return ln_get_mercury_disk(jd);
    case Body::Venus: return ln_get_venus_disk(jd);
    case Body::Mars: return ln_get_mars_disk(jd);
    case Body::Jupiter: return ln_get_jupiter_disk(jd);
    case Body::Saturn: return ln_get_saturn_disk(jd);
    case Body::Uranus: return ln_get_uranus_disk(jd);
    case Body::Neptune: return ln_get_neptune_disk(jd);
    }
    return 1.0;
}

// libnova's rise and set functions use its J2000 positions, so give it corrected ones
template <Body B>
void equ_of_date_callback(double jd, ln_equ_posn* position)
{
    equ_of_date(B, jd, position);
}

EquFunction equ_of_date_function(Body body)
{
    switch (body) {
    case Body::Sun: return equ_of_date_callback<Body::Sun>;
    case Body::Moon: return equ_of_date_callback<Body::Moon>;
    case Body::Mercury: return equ_of_date_callback<Body::Mercury>;
    case Body::Venus: return equ_of_date_callback<Body::Venus>;
    case Body::Mars: return equ_of_date_callback<Body::Mars>;
    case Body::Jupiter: return equ_of_date_callback<Body::Jupiter>;
    case Body::Saturn: return equ_of_date_callback<Body::Saturn>;
    case Body::Uranus: return equ_of_date_callback<Body::Uranus>;
    case Body::Neptune: return equ_of_date_callback<Body::Neptune>;
    }
    return equ_of_date_callback<Body::Sun>;
}

double standard_horizon(Body body)
{
    switch (body) {
    case Body::Sun: return LN_SOLAR_STANDART_HORIZON;
    case Body::Moon: return LN_LUNAR_STANDART_HORIZON;
    default: return LN_STAR_STANDART_HORIZON;
    }
}

int rise_set(Body body, double jd, ln_lnlat_posn* observer, double horizon, ln_rst_time* rst)
{
    return ln_get_body_rst_horizon(jd, observer, equ_of_date_function(body), horizon, rst);
}

Horizon to_horizon(int result)
{
    if (result > 0)
        return Horizon::AlwaysUp;
    if (result < 0)
        return Horizon::NeverUp;
    return Horizon::RisesAndSets;
}

double sun_altitude(const Observer& observer, double jd)
{
    ln_equ_posn sun;
    double az;
    double alt;

    equ_of_date(Body::Sun, jd, &sun);
    equatorial_to_horizontal(sun.ra, sun.dec, observer, jd, &az, &alt);
    return alt;
}

} // namespace

const std::vector<Body>& all_bodies()
{
    static const std::vector<Body> bodies = {
        Body::Sun, Body::Moon, Body::Mercury, Body::Venus, Body::Mars,
        Body::Jupiter, Body::Saturn, Body::Uranus, Body::Neptune,
    };
    return bodies;
}

std::string body_name(Body body)
{
    switch (body) {
    case Body::Sun: return "Sun";
    case Body::Moon: return "Moon";
    case Body::Mercury: return "Mercury";
    case Body::Venus: return "Venus";
    case Body::Mars: return "Mars";
    case Body::Jupiter: return "Jupiter";
    case Body::Saturn: return "Saturn";
    case Body::Uranus: return "Uranus";
    case Body::Neptune: return "Neptune";
    }
    return "";
}

double julian_now()
{
    return ln_get_julian_from_sys();
}

double julian_from_utc(int year, int month, int day, int hour, int minute, double second)
{
    ln_date date;
    date.years = year;
    date.months = month;
    date.days = day;
    date.hours = hour;
    date.minutes = minute;
    date.seconds = second;
    return ln_get_julian_day(&date);
}

std::string utc_string(double jd)
{
    ln_date date;
    char text[32];

    // Round to the nearest minute
    ln_get_date(jd + 0.5 / 1440.0, &date);
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", date.years, date.months,
                  date.days, date.hours, date.minutes);
    return text;
}

void equatorial_to_horizontal(double ra_deg, double dec_deg, const Observer& observer, double jd,
                              double* az_deg, double* alt_deg)
{
    ln_equ_posn object;
    ln_hrz_posn horizontal;
    ln_lnlat_posn position = to_lnlat(observer);

    object.ra = ra_deg;
    object.dec = dec_deg;
    ln_get_hrz_from_equ(&object, &position, jd, &horizontal);

    // libnova measures azimuth from the south
    *az_deg = wrap_360(horizontal.az + 180.0);
    *alt_deg = horizontal.alt;
}

BodyReport body_report(Body body, const Observer& observer, double jd)
{
    BodyReport report;
    ln_equ_posn geocentric;
    ln_equ_posn parallax;
    ln_rst_time rst;
    ln_lnlat_posn position = to_lnlat(observer);

    report.body = body;
    report.name = body_name(body);
    report.magnitude = magnitude(body, jd);
    report.illuminated = illuminated(body, jd);

    double geocentric_au = distance_au(body, jd);
    equ_of_date(body, jd, &geocentric);
    ln_get_parallax(&geocentric, geocentric_au, &position, observer.elev_m, jd, &parallax);
    report.ra_deg = wrap_360(geocentric.ra + parallax.ra);
    report.dec_deg = geocentric.dec + parallax.dec;
    equatorial_to_horizontal(report.ra_deg, report.dec_deg, observer, jd, &report.az_deg,
                             &report.alt_deg);

    // Measure from the observer rather than Earth's centre, which matters for the Moon
    double geocentric_az;
    double geocentric_alt;
    equatorial_to_horizontal(geocentric.ra, geocentric.dec, observer, jd, &geocentric_az, &geocentric_alt);
    double radius_au = kEarthRadiusKm / kKmPerAu;
    report.distance_au = std::sqrt(geocentric_au * geocentric_au + radius_au * radius_au -
                                   2.0 * geocentric_au * radius_au *
                                   std::sin(geocentric_alt * M_PI / 180.0));

    report.horizon = to_horizon(rise_set(body, jd, &position, standard_horizon(body), &rst));
    report.rise_jd = rst.rise;
    report.transit_jd = rst.transit;
    report.set_jd = rst.set;
    return report;
}

Twilight twilight(const Observer& observer, double jd)
{
    Twilight result;
    ln_rst_time rst;
    ln_lnlat_posn position = to_lnlat(observer);

    result.horizon = to_horizon(rise_set(Body::Sun, jd, &position, LN_SOLAR_STANDART_HORIZON, &rst));
    result.sunset_jd = rst.set;
    result.sunrise_jd = rst.rise;

    rise_set(Body::Sun, jd, &position, LN_SOLAR_CIVIL_HORIZON, &rst);
    result.civil_dusk_jd = rst.set;
    result.civil_dawn_jd = rst.rise;

    rise_set(Body::Sun, jd, &position, LN_SOLAR_NAUTIC_HORIZON, &rst);
    result.nautical_dusk_jd = rst.set;
    result.nautical_dawn_jd = rst.rise;

    rise_set(Body::Sun, jd, &position, LN_SOLAR_ASTRONOMICAL_HORIZON, &rst);
    result.astronomical_dusk_jd = rst.set;
    result.astronomical_dawn_jd = rst.rise;
    return result;
}

std::vector<NightTarget> best_targets(const Observer& observer, double jd, double min_alt_deg)
{
    const double step_days = 15.0 / 1440.0;
    std::vector<NightTarget> targets;

    for (Body body : all_bodies()) {
        if (body == Body::Sun)
            continue;

        NightTarget target;
        target.body = body;
        target.name = body_name(body);
        target.max_alt_deg = -90.0;
        target.best_jd = jd;

        // Search the next 24 hours, counting only times when the sky is dark
        for (double t = jd; t < jd + 1.0; t += step_days) {
            if (sun_altitude(observer, t) > kNightSunAltitude)
                continue;
            BodyReport report = body_report(body, observer, t);
            if (report.alt_deg > target.max_alt_deg) {
                target.max_alt_deg = report.alt_deg;
                target.best_jd = t;
            }
        }
        if (target.max_alt_deg >= min_alt_deg)
            targets.push_back(target);
    }

    std::sort(targets.begin(), targets.end(), [](const NightTarget& a, const NightTarget& b) {
        return a.max_alt_deg > b.max_alt_deg;
    });
    return targets;
}

double sun_separation_deg(double ra_deg, double dec_deg, double jd)
{
    ln_equ_posn object;
    ln_equ_posn sun;

    object.ra = ra_deg;
    object.dec = dec_deg;
    equ_of_date(Body::Sun, jd, &sun);
    return ln_get_angular_separation(&object, &sun);
}

void precess_from_j2000(double ra_deg, double dec_deg, double jd, double* ra_out, double* dec_out)
{
    ln_equ_posn j2000;
    ln_equ_posn position;

    j2000.ra = ra_deg;
    j2000.dec = dec_deg;
    ln_get_equ_prec2(&j2000, JD2000, jd, &position);
    *ra_out = wrap_360(position.ra);
    *dec_out = position.dec;
}

double local_sidereal_deg(const Observer& observer, double jd)
{
    return wrap_360(ln_get_apparent_sidereal_time(jd) * 15.0 + observer.lon_deg);
}

double polaris_hour_angle_deg(const Observer& observer, double jd)
{
    const double kPolarisRaJ2000 = 37.9546;
    const double kPolarisDecJ2000 = 89.2641;
    double ra;
    double dec;

    precess_from_j2000(kPolarisRaJ2000, kPolarisDecJ2000, jd, &ra, &dec);
    return wrap_360(local_sidereal_deg(observer, jd) - ra);
}

GotoVerdict check_goto(double ra_deg, double dec_deg, const Observer& observer, double jd,
                       const GotoLimits& limits, bool solar_filter_fitted)
{
    double az;
    double alt;

    if (!solar_filter_fitted && sun_separation_deg(ra_deg, dec_deg, jd) < limits.sun_min_separation_deg)
        return GotoVerdict::TooCloseToSun;

    equatorial_to_horizontal(ra_deg, dec_deg, observer, jd, &az, &alt);
    if (alt < limits.min_altitude_deg)
        return GotoVerdict::BelowHorizon;
    return GotoVerdict::Allowed;
}

} // namespace astro
