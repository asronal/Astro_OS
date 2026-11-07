#ifndef ASTRO_UI_CAMERA_IMAGE_H
#define ASTRO_UI_CAMERA_IMAGE_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVariant>

#include <vector>

// Pixels ready for showing. A colour image has its three channels side by side.
struct FramePlane {
    int width = 0;
    int height = 0;
    int channels = 1;
    std::vector<float> pixels;

    bool empty() const { return pixels.empty(); }
};

struct FrameStats {
    double median = 0.0;
    double mad = 0.0;      // Median absolute deviation, a measure of noise that stars don't upset
    double peak = 0.0;
    double clipped = 0.0;  // Fraction of pixels at the top of the range, which have lost detail
};

// A smaller copy of a whole frame, and a piece from its middle at full detail, for
// checking focus. Frames can be far too large to keep in memory or show as they are.
struct FrameData {
    QString error;
    int full_width = 0;
    int full_height = 0;
    bool bayer = false;   // From a colour sensor, not yet turned into colour
    double range = 1.0;   // The largest value this format can hold
    FramePlane fit;
    FramePlane detail;
    FrameStats stats;
};

// Reads a FITS file held in memory. Damaged files give an error, and never a crash.
FrameData read_fits_frame(const QByteArray& data, int fit_width, int detail_width, int detail_height);

// Any other picture, such as a JPEG
FrameData read_picture_frame(const QByteArray& data, int fit_width, int detail_width, int detail_height);

FrameStats frame_stats(const FramePlane& plane, double range);

// Everything at night is shown in red only, to protect dark adaptation
QImage render_frame(const FramePlane& plane, const FrameStats& stats, double range, bool stretch, bool night);

// Heights from 0 to 1 on a log scale, since the sky's peak dwarfs everything else
QVariantList frame_histogram(const FramePlane& plane, double range, int bins);

#endif
