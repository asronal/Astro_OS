// Compares astro-core with reference values from NASA JPL Horizons.
// Limits were set before any results were seen, so don't widen them to pass.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "astro/sky.h"

using namespace astro;

static const double kPositionLimitDeg = 1.0 / 60.0;
static const double kMoonPositionLimitDeg = 3.0 / 60.0;
static const double kDistanceLimit = 0.005;
static const double kMagnitudeLimit = 0.6;
static const double kRiseSetLimitMin = 3.0;
static const double kMoonRiseSetLimitMin = 5.0;

static const Observer kGreenwich = {51.4769, -0.0005, 46.0};

static std::map<std::string, Body> body_names()
{
    std::map<std::string, Body> names;
    for (Body body : all_bodies())
        names[body_name(body)] = body;
    return names;
}

static double angle_error(double a, double b)
{
    double difference = std::fabs(std::fmod(a - b, 360.0));
    return difference > 180.0 ? 360.0 - difference : difference;
}

static int month_number(const std::string& name)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) {
        if (name == months[i])
            return i + 1;
    }
    return 0;
}

static int failures = 0;

static void check(const std::string& label, double error, double limit)
{
    bool ok = error <= limit;
    if (!ok)
        failures++;
    std::printf("%-5s %-34s error %9.4f  limit %8.4f\n", ok ? "ok" : "FAIL", label.c_str(), error, limit);
}

static void test_positions(const std::string& path)
{
    std::ifstream file(path);
    std::string line;
    auto names = body_names();

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        std::stringstream stream(line);
        std::vector<std::string> f;
        std::string item;
        while (std::getline(stream, item, ','))
            f.push_back(item);

        int year, month, day, hour, minute;
        std::sscanf(f[1].c_str(), "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
        double jd = julian_from_utc(year, month, day, hour, minute, 0.0);
        Body body = names[f[0]];
        BodyReport r = body_report(body, kGreenwich, jd);
        std::string tag = f[0] + " " + f[1];
        double limit = body == Body::Moon ? kMoonPositionLimitDeg : kPositionLimitDeg;
        double cos_dec = std::cos(std::stod(f[3]) * M_PI / 180.0);
        double cos_alt = std::cos(std::stod(f[5]) * M_PI / 180.0);

        check(tag + " RA", angle_error(r.ra_deg, std::stod(f[2])) * cos_dec, limit);
        check(tag + " Dec", std::fabs(r.dec_deg - std::stod(f[3])), limit);
        check(tag + " Az", angle_error(r.az_deg, std::stod(f[4])) * cos_alt, limit);
        check(tag + " Alt", std::fabs(r.alt_deg - std::stod(f[5])), limit);
        check(tag + " Distance", std::fabs(r.distance_au / std::stod(f[7]) - 1.0), kDistanceLimit);
        check(tag + " Magnitude", std::fabs(r.magnitude - std::stod(f[6])), kMagnitudeLimit);
    }
}

static void test_rise_set(const std::string& path)
{
    std::ifstream file(path);
    std::string line;
    auto names = body_names();
    double start = julian_from_utc(2026, 6, 21, 0, 0, 0.0);

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        char body_text[16];
        char type;
        char month_text[8];
        int year, day, hour, minute;
        if (std::sscanf(line.c_str(), "%15[^,],%c, %d-%3s-%d %d:%d", body_text, &type, &year,
                        month_text, &day, &hour, &minute) != 7) {
            std::printf("FAIL  could not read: %s\n", line.c_str());
            failures++;
            continue;
        }

        Body body = names[body_text];
        BodyReport r = body_report(body, kGreenwich, start);
        double expected = julian_from_utc(year, month_number(month_text), day, hour, minute, 0.0);
        double actual = type == 'r' ? r.rise_jd : (type == 't' ? r.transit_jd : r.set_jd);
        double limit = body == Body::Moon ? kMoonRiseSetLimitMin : kRiseSetLimitMin;

        check(std::string(body_text) + " " + type + " minutes", std::fabs(actual - expected) * 1440.0, limit);
    }
}

static void test_sun_separation()
{
    double jd = julian_from_utc(2026, 6, 21, 12, 0, 0.0);
    BodyReport sun = body_report(Body::Sun, kGreenwich, jd);

    check("Sun separation from itself", sun_separation_deg(sun.ra_deg, sun.dec_deg, jd), 0.01);
    check("Sun separation from opposite",
          std::fabs(sun_separation_deg(std::fmod(sun.ra_deg + 180.0, 360.0), -sun.dec_deg, jd) - 180.0), 0.01);
}

static void check_verdict(const std::string& label, GotoVerdict actual, GotoVerdict expected)
{
    check(label, actual == expected ? 0.0 : 1.0, 0.0);
}

static void test_goto_safety()
{
    // Midday, with the Sun high over Greenwich
    double jd = julian_from_utc(2026, 6, 21, 12, 0, 0.0);
    BodyReport sun = body_report(Body::Sun, kGreenwich, jd);
    GotoLimits limits = kDefaultGotoLimits;

    check_verdict("GoTo the Sun is refused",
                  check_goto(sun.ra_deg, sun.dec_deg, kGreenwich, jd, limits, false),
                  GotoVerdict::TooCloseToSun);
    check_verdict("GoTo 14 degrees from the Sun is refused",
                  check_goto(sun.ra_deg, sun.dec_deg - 14.0, kGreenwich, jd, limits, false),
                  GotoVerdict::TooCloseToSun);
    check_verdict("GoTo 16 degrees from the Sun is allowed",
                  check_goto(sun.ra_deg, sun.dec_deg - 16.0, kGreenwich, jd, limits, false),
                  GotoVerdict::Allowed);
    check_verdict("GoTo the Sun with a filter is allowed",
                  check_goto(sun.ra_deg, sun.dec_deg, kGreenwich, jd, limits, true),
                  GotoVerdict::Allowed);

    // The point opposite the Sun is below the horizon at midday
    check_verdict("GoTo below the horizon is refused",
                  check_goto(std::fmod(sun.ra_deg + 180.0, 360.0), -sun.dec_deg, kGreenwich, jd, limits, false),
                  GotoVerdict::BelowHorizon);
    check_verdict("A filter does not allow GoTo below the horizon",
                  check_goto(std::fmod(sun.ra_deg + 180.0, 360.0), -sun.dec_deg, kGreenwich, jd, limits, true),
                  GotoVerdict::BelowHorizon);

    // A Pi that hasn't set its clock yet starts at about 1970
    check("A clock in 1970 doesn't look set", clock_looks_set(julian_from_utc(1970, 1, 1, 0, 0, 0.0)) ? 1.0 : 0.0, 0.5);
    check("A clock in late 2025 doesn't look set", clock_looks_set(julian_from_utc(2025, 12, 31, 23, 0, 0.0)) ? 1.0 : 0.0, 0.5);
    check("A clock in 2026 looks set", clock_looks_set(julian_from_utc(2026, 1, 1, 1, 0, 0.0)) ? 0.0 : 1.0, 0.5);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::puts("Usage: test_horizons horizons.csv horizons-rst.csv");
        return 2;
    }

    test_positions(argv[1]);
    test_rise_set(argv[2]);
    test_sun_separation();
    test_goto_safety();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
