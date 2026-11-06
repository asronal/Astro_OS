// Tests the limits that protect the telescope and the servos
#include <cstdio>

#include "../servo_math.h"

using namespace servo;

static int failures = 0;

static void check(const char* label, double actual, double expected)
{
    bool ok = std::fabs(actual - expected) < 0.001;
    if (!ok)
        failures++;
    std::printf("%-5s %-44s %10.3f expected %10.3f\n", ok ? "ok" : "FAIL", label, actual, expected);
}

int main()
{
    const AxisLimits az = {180.0, 90.0};
    const AxisLimits alt = {45.0, 45.0};

    check("Centre angle gives the centre pulse", angle_to_pulse_us(180.0, az), 1500.0);
    check("End of travel gives the maximum pulse", angle_to_pulse_us(270.0, az), 2000.0);
    check("Other end of travel gives the minimum pulse", angle_to_pulse_us(90.0, az), 1000.0);
    check("Beyond travel never exceeds the maximum", angle_to_pulse_us(300.0, az), 2000.0);
    check("Beyond travel never goes below the minimum", angle_to_pulse_us(10.0, az), 1000.0);
    check("Altitude below the horizon is clamped to 0", clamp_to_axis(-20.0, alt), 0.0);
    check("Altitude above the zenith is clamped to 90", clamp_to_axis(120.0, alt), 90.0);
    check("Azimuth in range is unchanged", clamp_to_axis(200.0, az), 200.0);
    check("Position outside the travel is detected", within_axis(10.0, az) ? 1.0 : 0.0, 0.0);
    check("Position inside the travel is accepted", within_axis(100.0, az) ? 1.0 : 0.0, 1.0);
    check("Moves are limited in speed going up", step_towards(10.0, 50.0, 2.0), 12.0);
    check("Moves are limited in speed going down", step_towards(10.0, -50.0, 2.0), 8.0);
    check("A small move reaches its target", step_towards(10.0, 10.5, 2.0), 10.5);
    check("1500 us is 307 counts at 50 Hz", pulse_to_pca_counts(1500.0), 307.0);
    check("PCA9685 prescale for 50 Hz is 121", pca_prescale(), 121.0);

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
