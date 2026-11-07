#ifndef ASTRO_UI_CAMERA_MATH_H
#define ASTRO_UI_CAMERA_MATH_H

#include <algorithm>
#include <cmath>

// The sums for showing a camera's image, kept free of Qt so they can be tested alone.
//
// An astronomical image is nearly black, with almost all its pixels crowded into a
// small range. Stretching spreads that range out so that faint detail can be seen. It
// only changes what is shown. Saved files always hold the camera's own numbers.
namespace camera {

// The midtones transfer function: a curve through 0 and 1 that sends m to 0.5
inline double midtones(double m, double x)
{
    if (x <= 0.0)
        return 0.0;
    if (x >= 1.0)
        return 1.0;
    if (x == m)
        return 0.5;
    return (m - 1.0) * x / ((2.0 * m - 1.0) * x - m);
}

struct Stretch {
    double black;
    double middle;
    double range;  // What counts as 1.0 in the data
};

// median and mad are in the data's own units. The usual choice for astronomy: black a
// little below the sky background, and the background ending up a quarter of the way up.
inline Stretch auto_stretch(double median, double mad, double range)
{
    const double kBackground = 0.25;
    const double kShadowClip = 2.8;
    const double kMadToSigma = 1.4826;

    Stretch stretch;
    stretch.range = range > 0.0 ? range : 1.0;
    double m = median / stretch.range;
    double sigma = kMadToSigma * mad / stretch.range;
    stretch.black = std::clamp(m - kShadowClip * sigma, 0.0, 1.0);
    // A completely flat image has nothing to spread out, so show it as it is
    double above = m - stretch.black;
    stretch.middle = above > 0.0 ? midtones(kBackground, above / (1.0 - stretch.black + 1e-12)) : 0.5;
    return stretch;
}

inline double apply_stretch(const Stretch& stretch, double value)
{
    double x = (value / stretch.range - stretch.black) / (1.0 - stretch.black + 1e-12);
    return midtones(stretch.middle, std::clamp(x, 0.0, 1.0));
}

// Exposure lengths offered by the plus and minus buttons
inline double next_exposure(double seconds, int direction)
{
    static const double kSteps[] = {0.001, 0.01, 0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 180, 300, 600, 900, 1800, 3600};
    const int count = int(sizeof(kSteps) / sizeof(kSteps[0]));
    if (direction > 0) {
        for (int i = 0; i < count; i++) {
            if (kSteps[i] > seconds)
                return kSteps[i];
        }
        return kSteps[count - 1];
    }
    for (int i = count - 1; i >= 0; i--) {
        if (kSteps[i] < seconds)
            return kSteps[i];
    }
    return kSteps[0];
}

} // namespace camera

#endif
