#ifndef ASTRO_SKY_H
#define ASTRO_SKY_H

#include <string>
#include <vector>

namespace astro {

// East longitude is positive, and elevation is in metres
struct Observer {
    double lat_deg;
    double lon_deg;
    double elev_m;
};

enum class Body { Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune };

enum class Horizon { RisesAndSets, AlwaysUp, NeverUp };

struct BodyReport {
    Body body;
    std::string name;
    double ra_deg;       // Apparent position as seen by the observer
    double dec_deg;
    double az_deg;       // 0 = north, 90 = east
    double alt_deg;      // Without atmospheric refraction
    double distance_au;
    double magnitude;
    double illuminated;  // Lit fraction of the disk, 0 to 1
    Horizon horizon;
    double rise_jd;      // Next events after the requested time. Valid
    double transit_jd;   // for rise and set only with RisesAndSets
    double set_jd;
};

// Each is the next time the Sun crosses that altitude after the requested time
struct Twilight {
    Horizon horizon;
    double sunset_jd;
    double civil_dusk_jd;
    double nautical_dusk_jd;
    double astronomical_dusk_jd;
    double astronomical_dawn_jd;
    double nautical_dawn_jd;
    double civil_dawn_jd;
    double sunrise_jd;
};

struct NightTarget {
    Body body;
    std::string name;
    double max_alt_deg;
    double best_jd;
};

const std::vector<Body>& all_bodies();
std::string body_name(Body body);

double julian_now();
double julian_from_utc(int year, int month, int day, int hour, int minute, double second);
std::string utc_string(double jd);

BodyReport body_report(Body body, const Observer& observer, double jd);
Twilight twilight(const Observer& observer, double jd);

// Bodies above min_alt_deg while the Sun is below -12 degrees during the next
// night, highest first. The Sun is never included.
std::vector<NightTarget> best_targets(const Observer& observer, double jd, double min_alt_deg);

void equatorial_to_horizontal(double ra_deg, double dec_deg, const Observer& observer, double jd,
                              double* az_deg, double* alt_deg);

// Convert a J2000 catalog position to the requested date
void precess_from_j2000(double ra_deg, double dec_deg, double jd, double* ra_out, double* dec_out);

// Local apparent sidereal time, in degrees
double local_sidereal_deg(const Observer& observer, double jd);

// Hour angle of Polaris at the requested time, in degrees, for polar alignment
double polaris_hour_angle_deg(const Observer& observer, double jd);

// Angle between a sky position and the Sun, used to keep a telescope away from it
double sun_separation_deg(double ra_deg, double dec_deg, double jd);

struct GotoLimits {
    double sun_min_separation_deg;
    double min_altitude_deg;
};

enum class GotoVerdict { Allowed, TooCloseToSun, BelowHorizon };

const GotoLimits kDefaultGotoLimits = {15.0, 0.0};

// Decide whether a telescope may slew to a position. Pointing near the Sun can
// blind an observer and destroy equipment, so it is refused unless the user has
// confirmed that a solar filter is fitted.
GotoVerdict check_goto(double ra_deg, double dec_deg, const Observer& observer, double jd,
                       const GotoLimits& limits, bool solar_filter_fitted);

// A Raspberry Pi has no battery-backed clock, so it can start up decades out until
// the network or the user sets the time. The Sun's position depends on the date,
// so the Sun check above means nothing until this returns true.
bool clock_looks_set(double jd);

} // namespace astro

#endif
