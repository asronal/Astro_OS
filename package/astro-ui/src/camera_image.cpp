#include "camera_image.h"

#include <fitsio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "camera_math.h"

// Larger than any sensor in use, and stops a damaged header asking for absurd memory
static const qint64 kMaxPixels = 1000000000;
// Sampling this many pixels gives a good enough median, and much sooner
static const std::size_t kStatsSamples = 500000;

namespace {

class FitsFile {
public:
    FitsFile(const QByteArray& data) : m_file(nullptr), m_status(0), m_memory(const_cast<char*>(data.constData())), m_size(std::size_t(data.size()))
    {
        // READONLY means that it never writes to or reallocates this memory. cfitsio
        // keeps the addresses of m_memory and m_size until closed, so they must last.
        fits_open_memfile(&m_file, "frame.fits", READONLY, &m_memory, &m_size, 0, nullptr, &m_status);
    }
    ~FitsFile()
    {
        int status = 0;
        if (m_file)
            fits_close_file(m_file, &status);
    }

    fitsfile* file() const { return m_file; }
    int* status() { return &m_status; }
    bool ok() const { return m_status == 0 && m_file; }

private:
    fitsfile* m_file;
    int m_status;
    void* m_memory;
    std::size_t m_size;
};

// Bins pixels into the smaller copy as rows arrive, and copies the part in the middle
class FrameBuilder {
public:
    FrameBuilder(FrameData* frame, int width, int height, int channels, int fit_width, int detail_width, int detail_height)
        : m_frame(frame), m_width(width), m_height(height), m_channels(channels)
    {
        m_bin = std::max(1, int(std::ceil(double(width) / std::max(fit_width, 1))));
        // Sensors with a colour filter need whole 2x2 groups, or the pattern shows
        m_detail_bin = 1;
        if (frame->bayer) {
            m_bin = std::max(2, m_bin + m_bin % 2);
            m_detail_bin = 2;
        }

        frame->fit.width = width / m_bin;
        frame->fit.height = height / m_bin;
        frame->fit.channels = channels;
        frame->fit.pixels.assign(std::size_t(frame->fit.width) * frame->fit.height * channels, 0.0f);
        m_counts.assign(frame->fit.pixels.size(), 0);

        frame->detail.width = std::min(detail_width, width / m_detail_bin);
        frame->detail.height = std::min(detail_height, height / m_detail_bin);
        frame->detail.channels = channels;
        frame->detail.pixels.assign(std::size_t(frame->detail.width) * frame->detail.height * channels, 0.0f);
        m_detail_counts.assign(frame->detail.pixels.size(), 0);
        m_detail_x = ((width - frame->detail.width * m_detail_bin) / 2) & ~1;
        m_detail_y = ((height - frame->detail.height * m_detail_bin) / 2) & ~1;
    }

    void add_row(int channel, int y, const float* row)
    {
        int fit_y = y / m_bin;
        bool in_fit = fit_y < m_frame->fit.height;
        int detail_y = (y - m_detail_y) / m_detail_bin;
        bool in_detail = y >= m_detail_y && detail_y < m_frame->detail.height;

        for (int x = 0; x < m_width; x++) {
            float value = row[x];
            // A camera's floating point data can hold values that aren't numbers
            if (!std::isfinite(value))
                value = 0.0f;
            if (in_fit) {
                int fit_x = x / m_bin;
                if (fit_x < m_frame->fit.width) {
                    std::size_t at = (std::size_t(fit_y) * m_frame->fit.width + fit_x) * m_channels + channel;
                    m_frame->fit.pixels[at] += value;
                    m_counts[at]++;
                }
            }
            if (in_detail && x >= m_detail_x) {
                int detail_x = (x - m_detail_x) / m_detail_bin;
                if (detail_x < m_frame->detail.width) {
                    std::size_t at = (std::size_t(detail_y) * m_frame->detail.width + detail_x) * m_channels + channel;
                    m_frame->detail.pixels[at] += value;
                    m_detail_counts[at]++;
                }
            }
        }
    }

    void finish()
    {
        for (std::size_t i = 0; i < m_counts.size(); i++) {
            if (m_counts[i] > 0)
                m_frame->fit.pixels[i] /= m_counts[i];
        }
        for (std::size_t i = 0; i < m_detail_counts.size(); i++) {
            if (m_detail_counts[i] > 0)
                m_frame->detail.pixels[i] /= m_detail_counts[i];
        }
    }

private:
    FrameData* m_frame;
    int m_width;
    int m_height;
    int m_channels;
    int m_bin;
    int m_detail_bin;
    int m_detail_x;
    int m_detail_y;
    std::vector<int> m_counts;
    std::vector<int> m_detail_counts;
};

QString fits_error(int status)
{
    char text[FLEN_STATUS] = {0};
    fits_get_errstatus(status, text);
    // cfitsio keeps its messages in a global stack, which would otherwise keep growing
    fits_clear_errmsg();
    return QStringLiteral("Could not read the image: %1").arg(QString::fromLatin1(text));
}

} // namespace

FrameData read_fits_frame(const QByteArray& data, int fit_width, int detail_width, int detail_height)
{
    FrameData frame;
    FitsFile fits(data);
    if (!fits.ok()) {
        frame.error = fits_error(*fits.status());
        return frame;
    }

    // The image is usually first. A compressed file keeps it in a later section.
    int naxis = 0;
    int bitpix = 0;
    long axes[3] = {1, 1, 1};
    int hdu_type = 0;
    for (;;) {
        int status = 0;
        fits_get_hdu_type(fits.file(), &hdu_type, &status);
        fits_get_img_param(fits.file(), 3, &bitpix, &naxis, axes, &status);
        if (status == 0 && hdu_type == IMAGE_HDU && naxis > 0)
            break;
        status = 0;
        if (fits_movrel_hdu(fits.file(), 1, nullptr, &status) != 0) {
            fits_clear_errmsg();
            frame.error = QStringLiteral("There is no image in this file.");
            return frame;
        }
    }

    if (naxis < 2 || naxis > 3 || axes[0] <= 0 || axes[1] <= 0) {
        frame.error = QStringLiteral("This image has a shape that can't be shown.");
        return frame;
    }
    int channels = naxis == 3 ? int(axes[2]) : 1;
    if (channels != 1 && channels != 3) {
        frame.error = QStringLiteral("This image has a shape that can't be shown.");
        return frame;
    }
    if (axes[0] > kMaxPixels / axes[1] || axes[0] * axes[1] > kMaxPixels) {
        frame.error = QStringLiteral("This image is too large to show.");
        return frame;
    }

    frame.full_width = int(axes[0]);
    frame.full_height = int(axes[1]);
    switch (bitpix) {
    case BYTE_IMG: frame.range = 255.0; break;
    case SHORT_IMG: frame.range = 65535.0; break;
    case LONG_IMG: frame.range = 4294967295.0; break;
    default: frame.range = 0.0; break;  // Floating point says nothing, so use its peak
    }

    char bayer[FLEN_VALUE] = {0};
    int key_status = 0;
    fits_read_key(fits.file(), TSTRING, "BAYERPAT", bayer, nullptr, &key_status);
    frame.bayer = key_status == 0 && channels == 1;
    fits_clear_errmsg();

    FrameBuilder builder(&frame, frame.full_width, frame.full_height, channels, fit_width, detail_width, detail_height);
    std::vector<float> row(static_cast<std::size_t>(frame.full_width));
    double peak = 0.0;
    float nothing = 0.0f;
    for (int channel = 0; channel < channels; channel++) {
        for (int y = 0; y < frame.full_height; y++) {
            long first[3] = {1, y + 1L, channel + 1L};
            int any_null = 0;
            int status = 0;
            if (fits_read_pix(fits.file(), TFLOAT, first, frame.full_width, &nothing, row.data(), &any_null, &status) != 0) {
                frame.error = fits_error(status);
                frame.fit = FramePlane();
                frame.detail = FramePlane();
                return frame;
            }
            for (float value : row) {
                if (std::isfinite(value) && value > peak)
                    peak = value;
            }
            builder.add_row(channel, y, row.data());
        }
    }
    builder.finish();

    if (frame.range <= 0.0)
        frame.range = peak > 0.0 ? peak : 1.0;
    frame.stats = frame_stats(frame.fit, frame.range);
    frame.stats.peak = peak;
    return frame;
}

FrameData read_picture_frame(const QByteArray& data, int fit_width, int detail_width, int detail_height)
{
    FrameData frame;
    QImage picture = QImage::fromData(data).convertToFormat(QImage::Format_RGB888);
    if (picture.isNull()) {
        frame.error = QStringLiteral("Saved, but this kind of file can't be previewed.");
        return frame;
    }

    frame.full_width = picture.width();
    frame.full_height = picture.height();
    frame.range = 255.0;
    FrameBuilder builder(&frame, picture.width(), picture.height(), 3, fit_width, detail_width, detail_height);
    std::vector<float> row(static_cast<std::size_t>(picture.width()));
    for (int channel = 0; channel < 3; channel++) {
        for (int y = 0; y < picture.height(); y++) {
            const uchar* line = picture.constScanLine(y);
            for (int x = 0; x < picture.width(); x++)
                row[x] = line[x * 3 + channel];
            builder.add_row(channel, y, row.data());
        }
    }
    builder.finish();
    frame.stats = frame_stats(frame.fit, frame.range);
    return frame;
}

FrameStats frame_stats(const FramePlane& plane, double range)
{
    FrameStats stats;
    if (plane.empty())
        return stats;

    std::size_t step = std::max<std::size_t>(1, plane.pixels.size() / kStatsSamples);
    std::vector<float> sample;
    sample.reserve(plane.pixels.size() / step + 1);
    std::size_t clipped = 0;
    std::size_t total = 0;
    float peak = 0.0f;
    const float full = float(range * 0.999);
    for (std::size_t i = 0; i < plane.pixels.size(); i += step) {
        float value = plane.pixels[i];
        sample.push_back(value);
        peak = std::max(peak, value);
        total++;
        if (value >= full)
            clipped++;
    }

    auto middle = sample.begin() + std::ptrdiff_t(sample.size() / 2);
    std::nth_element(sample.begin(), middle, sample.end());
    stats.median = *middle;
    for (float& value : sample)
        value = std::fabs(value - float(stats.median));
    std::nth_element(sample.begin(), middle, sample.end());
    stats.mad = *middle;
    stats.peak = peak;
    stats.clipped = double(clipped) / double(total);
    return stats;
}

QImage render_frame(const FramePlane& plane, const FrameStats& stats, double range, bool stretch, bool night)
{
    if (plane.empty())
        return QImage();

    // Shown stretched, or plainly from black up to the brightest thing in the frame
    camera::Stretch curve = camera::auto_stretch(stats.median, stats.mad, range);
    const double plain_top = stats.peak > 0.0 ? stats.peak : 1.0;
    auto level = [&](float value) {
        double shown = stretch ? camera::apply_stretch(curve, value) : std::clamp(value / plain_top, 0.0, 1.0);
        return int(std::lround(shown * 255.0));
    };

    QImage image(plane.width, plane.height, QImage::Format_RGB32);
    for (int y = 0; y < plane.height; y++) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        const float* in = plane.pixels.data() + std::size_t(y) * plane.width * plane.channels;
        for (int x = 0; x < plane.width; x++) {
            int r = level(in[0]);
            int g = plane.channels == 3 ? level(in[1]) : r;
            int b = plane.channels == 3 ? level(in[2]) : r;
            line[x] = night ? qRgb((r * 3 + g * 6 + b) / 10, 0, 0) : qRgb(r, g, b);
            in += plane.channels;
        }
    }
    return image;
}

QVariantList frame_histogram(const FramePlane& plane, double range, int bins)
{
    QVariantList heights;
    if (plane.empty() || bins <= 0 || range <= 0.0)
        return heights;

    std::vector<std::size_t> counts(static_cast<std::size_t>(bins), 0);
    for (float value : plane.pixels) {
        int bin = int(std::clamp(value / range, 0.0, 1.0) * (bins - 1));
        counts[static_cast<std::size_t>(bin)]++;
    }
    std::size_t top = *std::max_element(counts.begin(), counts.end());
    double log_top = std::log1p(double(top));
    for (std::size_t count : counts)
        heights.append(log_top > 0.0 ? std::log1p(double(count)) / log_top : 0.0);
    return heights;
}
