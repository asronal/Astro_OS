#ifndef ASTRO_SERVO_MATH_H
#define ASTRO_SERVO_MATH_H

#include <algorithm>
#include <cmath>

// Calculations for the RC servo driver, kept free of hardware access so that
// the limits can be tested on any computer.
namespace servo {

// Typical RC servo: 1000 to 2000 microseconds across its full travel
const double kMinPulseUs = 1000.0;
const double kMaxPulseUs = 2000.0;
const double kPwmHz = 50.0;
const double kPcaOscillatorHz = 25000000.0;
const int kPcaCounts = 4096;

// Travel of each axis, as angles from the servo's centre position
struct AxisLimits {
    double center_deg;
    double half_travel_deg;
};

inline double wrap_180(double degrees)
{
    degrees = std::fmod(degrees + 180.0, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;
    return degrees - 180.0;
}

// Limit a requested angle to the travel of the axis, taking the short way
// around the circle
inline double clamp_to_axis(double angle_deg, const AxisLimits& limits)
{
    double offset = wrap_180(angle_deg - limits.center_deg);
    return limits.center_deg + std::clamp(offset, -limits.half_travel_deg, limits.half_travel_deg);
}

inline bool within_axis(double angle_deg, const AxisLimits& limits)
{
    return std::fabs(wrap_180(angle_deg - limits.center_deg)) <= limits.half_travel_deg;
}

// Move no further than max_step_deg towards the target, so the servo cannot
// swing the telescope at full speed
inline double step_towards(double current_deg, double target_deg, double max_step_deg)
{
    double difference = target_deg - current_deg;
    return current_deg + std::clamp(difference, -max_step_deg, max_step_deg);
}

// Pulse width for an angle. Angles outside the travel are clamped, so a bad
// position can never command the servo beyond its end stops.
inline double angle_to_pulse_us(double angle_deg, const AxisLimits& limits)
{
    double offset = wrap_180(angle_deg - limits.center_deg);
    double fraction = std::clamp(offset / limits.half_travel_deg, -1.0, 1.0);
    return (kMinPulseUs + kMaxPulseUs) / 2.0 + fraction * (kMaxPulseUs - kMinPulseUs) / 2.0;
}

inline int pulse_to_pca_counts(double pulse_us)
{
    double counts = pulse_us * kPwmHz * kPcaCounts / 1000000.0;
    return std::clamp(int(std::lround(counts)), 0, kPcaCounts - 1);
}

// PCA9685 prescale value for the servo frame rate
inline int pca_prescale()
{
    return int(std::lround(kPcaOscillatorHz / (kPcaCounts * kPwmHz))) - 1;
}

} // namespace servo

#endif
