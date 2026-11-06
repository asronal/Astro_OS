#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "astro/sky.h"

using namespace astro;

static void usage()
{
    std::puts("Usage: astro-sky --lat DEG --lon DEG [--elev M] [--when \"YYYY-MM-DD HH:MM\"] [--json]\n"
              "East longitude is positive. Times are UTC.");
}

static const char *horizon_name(Horizon horizon)
{
    switch (horizon) {
    case Horizon::AlwaysUp: return "always-up";
    case Horizon::NeverUp: return "never-up";
    case Horizon::RisesAndSets: break;
    }
    return "rises-and-sets";
}

static void print_json(const Observer& observer, double jd)
{
    bool first = true;

    std::printf("{\"utc\":\"%s\",\"bodies\":[", utc_string(jd).c_str());
    for (Body body : all_bodies()) {
        BodyReport r = body_report(body, observer, jd);
        std::printf("%s{\"name\":\"%s\",\"ra\":%.4f,\"dec\":%.4f,\"az\":%.3f,\"alt\":%.3f,"
                    "\"distance_au\":%.6f,\"magnitude\":%.2f,\"illuminated\":%.3f,"
                    "\"horizon\":\"%s\",\"rise\":\"%s\",\"transit\":\"%s\",\"set\":\"%s\"}",
                    first ? "" : ",", r.name.c_str(), r.ra_deg, r.dec_deg, r.az_deg, r.alt_deg,
                    r.distance_au, r.magnitude, r.illuminated, horizon_name(r.horizon),
                    utc_string(r.rise_jd).c_str(), utc_string(r.transit_jd).c_str(),
                    utc_string(r.set_jd).c_str());
        first = false;
    }
    std::puts("]}");
}

static void print_text(const Observer& observer, double jd)
{
    std::printf("Sky at %s UTC, latitude %.4f, longitude %.4f\n\n", utc_string(jd).c_str(),
                observer.lat_deg, observer.lon_deg);
    std::printf("%-8s %7s %7s %6s %5s  %-5s %-5s %-5s\n", "Body", "Alt", "Az", "Mag", "Lit", "Rise",
                "Trans", "Set");
    for (Body body : all_bodies()) {
        BodyReport r = body_report(body, observer, jd);
        std::string rise = r.horizon == Horizon::RisesAndSets ? utc_string(r.rise_jd).substr(11) : "-";
        std::string set = r.horizon == Horizon::RisesAndSets ? utc_string(r.set_jd).substr(11) : "-";
        std::printf("%-8s %7.2f %7.2f %6.2f %4.0f%%  %-5s %-5s %-5s\n", r.name.c_str(), r.alt_deg,
                    r.az_deg, r.magnitude, r.illuminated * 100.0, rise.c_str(),
                    utc_string(r.transit_jd).substr(11).c_str(), set.c_str());
    }

    Twilight t = twilight(observer, jd);
    std::puts("");
    if (t.horizon == Horizon::RisesAndSets) {
        std::printf("Sunset %s, astronomical dusk %s, astronomical dawn %s, sunrise %s\n",
                    utc_string(t.sunset_jd).substr(11).c_str(),
                    utc_string(t.astronomical_dusk_jd).substr(11).c_str(),
                    utc_string(t.astronomical_dawn_jd).substr(11).c_str(),
                    utc_string(t.sunrise_jd).substr(11).c_str());
    }

    std::puts("\nBest targets tonight:");
    std::vector<NightTarget> targets = best_targets(observer, jd, 20.0);
    if (targets.empty())
        std::puts("  None above 20 degrees while the sky is dark");
    for (const NightTarget& target : targets) {
        std::printf("  %-8s up to %5.1f degrees, around %s\n", target.name.c_str(),
                    target.max_alt_deg, utc_string(target.best_jd).substr(11).c_str());
    }
}

int main(int argc, char *argv[])
{
    Observer observer = {0.0, 0.0, 0.0};
    bool have_lat = false;
    bool have_lon = false;
    bool json = false;
    double jd = julian_now();

    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--json")) {
            json = true;
        } else if (i + 1 >= argc) {
            usage();
            return 1;
        } else if (!std::strcmp(argv[i], "--lat")) {
            observer.lat_deg = std::atof(argv[++i]);
            have_lat = true;
        } else if (!std::strcmp(argv[i], "--lon")) {
            observer.lon_deg = std::atof(argv[++i]);
            have_lon = true;
        } else if (!std::strcmp(argv[i], "--elev")) {
            observer.elev_m = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--when")) {
            int year, month, day, hour, minute;
            if (std::sscanf(argv[++i], "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) != 5) {
                usage();
                return 1;
            }
            jd = julian_from_utc(year, month, day, hour, minute, 0.0);
        } else {
            usage();
            return 1;
        }
    }

    if (!have_lat || !have_lon) {
        usage();
        return 1;
    }

    if (json)
        print_json(observer, jd);
    else
        print_text(observer, jd);
    return 0;
}
