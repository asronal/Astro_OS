// Checks rise, transit and set times.
//
// Stars are compared with PyEphem (star-rst.csv, generated separately). Limits were
// set before any results were seen, so don't widen them to pass.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "astro/sky.h"

using namespace astro;

static const double kStarLimitMin = 3.0;
// A star that only just clears the horizon climbs too slowly for time to be a fair
// measure: from Quito, Polaris climbs 8 arcseconds a minute, so the aberration and
// nutation that PyEphem includes and our fixed positions don't move its rise by minutes.
// Below this climbing rate, the error is limited as height instead.
static const double kSlowClimbArcsecPerMin = 30.0;
static const double kSlowHeightLimitArcsec = 60.0;
// The height of a body when it rises, which its size and nearness shift from -0.57
static const double kHeightAtRiseLimitDeg = 1.5;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const std::string& what, double a = 0.0, double b = 0.0)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s: %.5f %.5f\n", what.c_str(), a, b);
    }
}

static std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    std::stringstream stream(line);
    while (std::getline(stream, field, ','))
        fields.push_back(field);
    if (!line.empty() && line.back() == ',')
        fields.push_back("");
    return fields;
}

static void check_star_row(const std::vector<std::string>& f)
{
    std::string label = f[0] + " at " + f[3];
    Observer site = {std::stod(f[4]), std::stod(f[5]), std::stod(f[6])};
    double jd = std::stod(f[7]);
    double ra;
    double dec;
    precess_from_j2000(std::stod(f[1]) * 15.0, std::stod(f[2]), jd, &ra, &dec);
    FixedRiseSet r = fixed_rise_set(ra, dec, site, jd);

    // How far it climbs in the minute after rising, in arcseconds
    double climb = 0.0;
    if (r.horizon == Horizon::RisesAndSets) {
        double az;
        double before;
        double after;
        equatorial_to_horizontal(ra, dec, site, r.rise_jd, &az, &before);
        equatorial_to_horizontal(ra, dec, site, r.rise_jd + 1.0 / 1440.0, &az, &after);
        climb = std::fabs(after - before) * 3600.0;
    }
    bool slow = climb > 0.0 && climb < kSlowClimbArcsecPerMin;
    auto close_enough = [&](double days_apart) {
        double minutes = std::fabs(days_apart) * 1440.0;
        return slow ? minutes * climb < kSlowHeightLimitArcsec : minutes < kStarLimitMin;
    };

    double transit = std::stod(f[10]);
    check(std::fabs(r.transit_jd - jd - transit) * 1440.0 < kStarLimitMin, label + " transit", r.transit_jd - jd, transit);

    if (f[8] == "up") {
        check(r.horizon == Horizon::AlwaysUp, label + " should stay up");
    } else if (f[8] == "never") {
        check(r.horizon == Horizon::NeverUp, label + " should never rise");
    } else {
        check(r.horizon == Horizon::RisesAndSets, label + " should rise and set");
        check(close_enough(r.rise_jd - jd - std::stod(f[9])), label + " rise", r.rise_jd - jd, std::stod(f[9]));
        check(close_enough(r.set_jd - jd - std::stod(f[11])), label + " set", r.set_jd - jd, std::stod(f[11]));
    }
}

// Whatever the time asked about, every event must be the next one
static void check_body_events_are_next(const Observer& site, const char* site_name)
{
    double base = julian_from_utc(2026, 11, 7, 0, 0, 0);
    for (Body body : all_bodies()) {
        for (double hours = 0.0; hours < 72.0; hours += 3.0) {
            double jd = base + hours / 24.0;
            BodyReport r = body_report(body, site, jd);
            std::string label = body_name(body) + " at " + site_name;
            check(r.transit_jd >= jd && r.transit_jd < jd + 1.2, label + " next transit", r.transit_jd - jd);
            if (r.horizon != Horizon::RisesAndSets)
                continue;
            check(r.rise_jd >= jd && r.rise_jd < jd + 1.2, label + " next rise", r.rise_jd - jd);
            check(r.set_jd >= jd && r.set_jd < jd + 1.2, label + " next set", r.set_jd - jd);

            BodyReport at_rise = body_position(body, site, r.rise_jd);
            check(std::fabs(at_rise.alt_deg + 0.57) < kHeightAtRiseLimitDeg, label + " height at rise", at_rise.alt_deg);
        }
    }
}

static void check_position_matches_report()
{
    Observer site = {51.4769, -0.0005, 46.0};
    double jd = julian_from_utc(2026, 11, 7, 21, 30, 0);
    for (Body body : all_bodies()) {
        BodyReport full = body_report(body, site, jd);
        BodyReport quick = body_position(body, site, jd);
        // Not exactly equal, as libnova reuses its last nutation result for nearby times
        check(std::fabs(full.ra_deg - quick.ra_deg) < 1e-6 && std::fabs(full.dec_deg - quick.dec_deg) < 1e-6, body_name(body) + " position");
        check(std::fabs(full.alt_deg - quick.alt_deg) < 1e-6 && full.magnitude == quick.magnitude, body_name(body) + " height and brightness");
        check(std::isnan(quick.rise_jd), body_name(body) + " has no rise time");
    }
}

static void check_dark_time()
{
    Observer greenwich = {51.4769, -0.0005, 46.0};
    double jd = julian_from_utc(2026, 11, 7, 12, 0, 0);

    // A star over the pole is about the same height all night
    DarkBest polaris = best_dark_time(37.95, 89.26, greenwich, jd);
    check(polaris.has_dark_time, "Greenwich has a dark November night");
    check(std::fabs(polaris.max_alt_deg - 51.48) < 1.0, "Polaris height", polaris.max_alt_deg);

    // The best time must be when the Sun really is down
    DarkBest vega = best_dark_time(279.2, 38.8, greenwich, jd);
    check(body_position(Body::Sun, greenwich, vega.best_jd).alt_deg < -11.5, "best time is dark", vega.best_jd);
    double az;
    double alt;
    equatorial_to_horizontal(279.2, 38.8, greenwich, vega.best_jd, &az, &alt);
    check(std::fabs(alt - vega.max_alt_deg) < 1e-6, "best height matches the time", alt, vega.max_alt_deg);

    // Midsummer at Tromso, under the midnight Sun
    Observer tromso = {69.65, 18.96, 10.0};
    DarkBest summer = best_dark_time(279.2, 38.8, tromso, julian_from_utc(2027, 6, 21, 12, 0, 0));
    check(!summer.has_dark_time, "no dark time under the midnight Sun");

    // A moving body uses its own position, not one fixed at the start
    DarkBest moon = best_dark_time(Body::Moon, greenwich, jd);
    check(std::fabs(body_position(Body::Moon, greenwich, moon.best_jd).alt_deg - moon.max_alt_deg) < 1e-6, "Moon's best height");
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::puts("usage: test_rise_set star-rst.csv");
        return 2;
    }
    std::ifstream stars(argv[1]);
    std::string line;
    int rows = 0;
    while (std::getline(stars, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields = split(line);
        if (fields.size() < 12 || fields[8] == "skip")
            continue;
        check_star_row(fields);
        rows++;
    }
    check(rows > 100, "the reference table was read", rows);

    check_body_events_are_next({51.4769, -0.0005, 46.0}, "Greenwich");
    check_body_events_are_next({-33.8688, 151.2093, 58.0}, "Sydney");
    check_position_matches_report();
    check_dark_time();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
