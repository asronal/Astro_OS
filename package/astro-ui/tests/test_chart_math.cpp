// Checks that the chart's forward and reverse geometry agree with each other.
#include <cstdio>

#include "chart_math.h"

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what, double a, double b)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s: %.6f %.6f\n", what, a, b);
    }
}

static double angle_difference(double a, double b)
{
    return std::fabs(std::fmod(a - b + 540.0, 360.0) - 180.0);
}

int main()
{
    double x_out, y_out;
    const double kAlts[] = {-8.0, 0.0, 2.0, 45.0, 89.0, 90.0};
    const double kFovs[] = {5.0, 25.0, 90.0, 180.0};

    // Every pixel on screen must map to a direction that projects back to it
    for (double center_alt : kAlts) {
        for (double fov : kFovs) {
            chart::View view = chart::make_view(123.0, center_alt, fov, 800.0, 480.0);
            for (double x = 0.0; x <= 800.0; x += 100.0) {
                for (double y = 0.0; y <= 480.0; y += 80.0) {
                    double az, alt, px = 0.0, py = 0.0;
                    chart::unproject(view, x, y, &az, &alt);
                    bool visible = chart::project(view, az, alt, &px, &py);
                    check(visible && std::hypot(px - x, py - y) < 1e-6, "pixel round trip", px - x, py - y);
                }
            }
        }
    }

    // The width of the screen must span the field of view when level
    chart::View level = chart::make_view(180.0, 0.0, 60.0, 800.0, 480.0);
    double az, alt;
    chart::unproject(level, 800.0, 240.0, &az, &alt);
    check(std::fabs(az - 210.0) < 1e-6 && std::fabs(alt) < 1e-6, "field of view", az, alt);

    // East is to the left when facing south, as in the real sky
    double x, y;
    chart::project(level, 170.0, 0.0, &x, &y);
    check(x < 400.0, "east is left when facing south", x, 400.0);
    chart::project(level, 180.0, 10.0, &x, &y);
    check(y < 240.0, "up is up", y, 240.0);

    // Equatorial and horizontal conversions must be inverses at any latitude
    const double kLats[] = {-65.0, -20.0, 0.0, 19.0, 51.5, 80.0};
    for (double lat : kLats) {
        chart::Site site = {1.234, std::sin(lat * chart::kDegToRad), std::cos(lat * chart::kDegToRad)};
        for (double ra = 0.0; ra < 360.0; ra += 30.0) {
            for (double dec = -80.0; dec <= 80.0; dec += 40.0) {
                double ra2, dec2;
                chart::equatorial_to_horizontal(site, ra, dec, &az, &alt);
                chart::horizontal_to_equatorial(site, az, alt, &ra2, &dec2);
                check(angle_difference(ra, ra2) < 1e-6, "right ascension", ra, ra2);
                check(std::fabs(dec - dec2) < 1e-6, "declination", dec, dec2);
            }
        }
    }

    // A star on the meridian south of the zenith is due south at the expected height
    chart::Site greenwich = {0.0, std::sin(51.5 * chart::kDegToRad), std::cos(51.5 * chart::kDegToRad)};
    chart::equatorial_to_horizontal(greenwich, 0.0, 0.0, &az, &alt);
    check(angle_difference(az, 180.0) < 1e-6, "due south", az, 180.0);
    check(std::fabs(alt - 38.5) < 1e-6, "height on the meridian", alt, 38.5);

    check(std::fabs(chart::separation(10.0, 0.0, 40.0, 0.0) - 30.0) < 1e-6, "separation", 0.0, 0.0);

    // A ring on the sky stays the right size wherever it is, including near the zenith
    for (double center_alt : {0.0, 20.0, 45.0, 80.0, 89.5}) {
        for (double center_az : {0.0, 90.0, 275.0}) {
            for (double radius : {0.05, 0.5, 2.0, 30.0}) {
                for (double bearing = 0.0; bearing < 360.0; bearing += 15.0) {
                    double ring_az, ring_alt;
                    chart::point_at_bearing(center_az, center_alt, radius, bearing, &ring_az, &ring_alt);
                    double apart = chart::separation(center_az, center_alt, ring_az, ring_alt);
                    check(std::fabs(apart - radius) < 1e-6, "ring radius", apart, radius);
                }
            }
        }
    }

    // A bearing of 0 is straight up, and 90 is towards higher azimuth
    double up_az, up_alt;
    chart::point_at_bearing(100.0, 30.0, 5.0, 0.0, &up_az, &up_alt);
    check(std::fabs(up_alt - 35.0) < 1e-6 && std::fabs(up_az - 100.0) < 1e-6, "bearing 0 is up", up_alt, up_az);
    chart::point_at_bearing(100.0, 0.0, 5.0, 90.0, &up_az, &up_alt);
    check(std::fabs(up_az - 105.0) < 1e-6, "bearing 90 is towards higher azimuth", up_az, 105.0);

    // A ring in the middle of the view has the radius the projection promises
    chart::View ring_view = chart::make_view(180.0, 45.0, 10.0, 800.0, 480.0);
    chart::point_at_bearing(180.0, 45.0, 1.0, 90.0, &up_az, &up_alt);
    chart::project(ring_view, up_az, up_alt, &x_out, &y_out);
    double expected_px = 2.0 * ring_view.radius * std::tan(0.5 * chart::kDegToRad);
    check(std::fabs(std::hypot(x_out - 400.0, y_out - 240.0) - expected_px) < 1e-6, "ring size on screen", x_out, expected_px);

    // Worked by hand: 1000 mm with a 25 mm eyepiece is 40x, and 52 / 40 is 1.3 degrees
    check(std::fabs(chart::magnification(1000.0, 25.0) - 40.0) < 1e-9, "magnification", 0.0, 0.0);
    check(std::fabs(chart::true_field(1000.0, 25.0, 52.0) - 1.3) < 1e-9, "true field", 0.0, 0.0);
    check(std::fabs(chart::true_field(650.0, 10.0, 82.0) - 82.0 / 65.0) < 1e-9, "true field, wide eyepiece", 0.0, 0.0);

    check(chart::next_in_cycle(-1, 3) == 0, "first tap takes the nearest", 0.0, 0.0);
    check(chart::next_in_cycle(0, 3) == 1, "second tap takes the next", 0.0, 0.0);
    check(chart::next_in_cycle(2, 3) == 0, "the cycle wraps", 0.0, 0.0);
    check(chart::next_in_cycle(0, 1) == 0, "one object stays chosen", 0.0, 0.0);
    check(chart::next_in_cycle(-1, 0) == -1, "nothing to choose", 0.0, 0.0);

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
