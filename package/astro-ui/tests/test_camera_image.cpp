// Checks reading, measuring and showing camera pictures, using FITS files made here so
// that the right answers are known, including damaged ones. No camera or display needed.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <fitsio.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

#include "camera_client.h"
#include "camera_image.h"
#include "camera_math.h"

static int failures = 0;
static int checks = 0;

static void check(bool ok, const QString& what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", qPrintable(what));
    }
}

// Makes a FITS file in memory. value(x, y, channel) decides each pixel.
static QByteArray make_fits(int bitpix, int width, int height, int channels, const std::function<double(int, int, int)>& value,
                            const char* bayer = nullptr)
{
    std::size_t size = 2880;
    void* memory = std::malloc(size);
    fitsfile* file = nullptr;
    int status = 0;
    fits_create_memfile(&file, &memory, &size, 2880, std::realloc, &status);
    long axes[3] = {width, height, channels};
    fits_create_img(file, bitpix, channels == 3 ? 3 : 2, axes, &status);
    if (bayer) {
        char pattern[16];
        std::snprintf(pattern, sizeof(pattern), "%s", bayer);
        fits_update_key(file, TSTRING, "BAYERPAT", pattern, nullptr, &status);
    }
    std::vector<double> pixels(std::size_t(width) * height * channels);
    std::size_t at = 0;
    for (int c = 0; c < channels; c++) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++)
                pixels[at++] = value(x, y, c);
        }
    }
    fits_write_img(file, TDOUBLE, 1, (long long)(pixels.size()), pixels.data(), &status);
    fits_close_file(file, &status);
    QByteArray result(static_cast<const char*>(memory), qsizetype(size));
    std::free(memory);
    if (status != 0)
        std::printf("FAIL could not make a test file, status %d\n", status);
    return result;
}

static void test_stretch_sums()
{
    using namespace camera;
    check(midtones(0.3, 0.0) == 0.0 && midtones(0.3, 1.0) == 1.0, QStringLiteral("the curve keeps black and white"));
    check(std::fabs(midtones(0.2, 0.2) - 0.5) < 1e-12, QStringLiteral("the curve sends the midtone to the middle"));
    check(std::fabs(midtones(0.5, 0.3) - 0.3) < 1e-12, QStringLiteral("a midtone of 0.5 changes nothing"));

    // The sky's background ends up where intended, for dim and bright skies alike
    for (double median : {50.0, 500.0, 5000.0, 30000.0}) {
        Stretch stretch = auto_stretch(median, median * 0.02, 65535.0);
        check(std::fabs(apply_stretch(stretch, median) - 0.25) < 0.02, QStringLiteral("background %1 lands on a quarter, got %2").arg(median).arg(apply_stretch(stretch, median)));

        // Brighter is never shown darker
        double last = -1.0;
        bool rising = true;
        for (double v = 0.0; v <= 65535.0; v += 97.0) {
            double shown = apply_stretch(stretch, v);
            rising = rising && shown >= last && shown >= 0.0 && shown <= 1.0;
            last = shown;
        }
        check(rising, QStringLiteral("the stretch only ever rises, and stays in range"));
    }

    // These have nothing to spread out, and must not produce nonsense
    for (double median : {0.0, 1000.0, 65535.0}) {
        Stretch flat = auto_stretch(median, 0.0, 65535.0);
        double shown = apply_stretch(flat, median);
        check(std::isfinite(shown) && shown >= 0.0 && shown <= 1.0, QStringLiteral("a flat image of %1 gives a sensible level").arg(median));
    }
    check(std::isfinite(apply_stretch(auto_stretch(0.0, 0.0, 0.0), 0.0)), QStringLiteral("an empty range is safe"));

    check(next_exposure(1.0, 1) == 2.0 && next_exposure(1.0, -1) == 0.5, QStringLiteral("exposure steps"));
    check(next_exposure(3600.0, 1) == 3600.0 && next_exposure(0.001, -1) == 0.001, QStringLiteral("exposure steps stop at the ends"));
    check(next_exposure(7.0, 1) == 10.0 && next_exposure(7.0, -1) == 5.0, QStringLiteral("a typed exposure steps to its neighbours"));
}

static void test_reading()
{
    // 16-bit values must arrive exactly, including above 32767, which FITS stores offset
    QByteArray unsigned_16 = make_fits(USHORT_IMG, 8, 4, 1, [](int x, int y, int) { return 1000.0 * (y * 8 + x); });
    FrameData frame = read_fits_frame(unsigned_16, 1280, 800, 480);
    check(frame.error.isEmpty(), QStringLiteral("a 16-bit file is read: %1").arg(frame.error));
    check(frame.full_width == 8 && frame.full_height == 4, QStringLiteral("its size is read"));
    check(frame.fit.width == 8 && frame.fit.height == 4, QStringLiteral("a small image isn't reduced"));
    bool exact = !frame.fit.pixels.empty();
    for (int i = 0; i < 32 && exact; i++)
        exact = std::fabs(frame.fit.pixels[i] - 1000.0f * i) < 0.5f;
    check(exact, QStringLiteral("each pixel has the right value, in the right place"));
    check(frame.range == 65535.0, QStringLiteral("the 16-bit range"));
    check(frame.stats.peak == 31000.0, QStringLiteral("the peak, got %1").arg(frame.stats.peak));

    FrameData bytes = read_fits_frame(make_fits(BYTE_IMG, 4, 4, 1, [](int, int, int) { return 200.0; }), 1280, 800, 480);
    check(bytes.error.isEmpty() && bytes.range == 255.0 && bytes.fit.pixels[0] == 200.0f, QStringLiteral("an 8-bit file"));

    FrameData floats = read_fits_frame(make_fits(FLOAT_IMG, 4, 4, 1, [](int x, int, int) { return 0.25 * x; }), 1280, 800, 480);
    check(floats.error.isEmpty() && floats.range == 0.75, QStringLiteral("floating point uses its peak as the range, got %1").arg(floats.range));

    // Values that aren't numbers can't be allowed to reach the sums
    FrameData nans = read_fits_frame(make_fits(FLOAT_IMG, 4, 4, 1, [](int x, int, int) { return x == 0 ? std::nan("") : 100.0; }), 1280, 800, 480);
    bool finite = nans.error.isEmpty();
    for (float v : nans.fit.pixels)
        finite = finite && std::isfinite(v);
    check(finite, QStringLiteral("not-a-number pixels are made safe"));
    check(!render_frame(nans.fit, nans.stats, nans.range, true, false).isNull(), QStringLiteral("and such an image can be shown"));

    // A large frame is averaged down, and the middle kept at full detail
    QByteArray large = make_fits(USHORT_IMG, 3000, 2000, 1, [](int x, int y, int) { return double((x / 10 + y / 10) % 2 ? 40000 : 1000); });
    FrameData big = read_fits_frame(large, 1280, 800, 480);
    check(big.error.isEmpty() && big.fit.width <= 1280 && big.fit.width >= 900, QStringLiteral("a large frame is reduced, to %1 wide").arg(big.fit.width));
    check(big.full_width == 3000, QStringLiteral("while still knowing its real size"));
    check(big.detail.width == 800 && big.detail.height == 480, QStringLiteral("the focus view has its full size"));
    bool crop_right = !big.detail.pixels.empty();
    int crop_x = (3000 - 800) / 2;
    int crop_y = (2000 - 480) / 2;
    for (int y = 0; y < 480 && crop_right; y += 37) {
        for (int x = 0; x < 800 && crop_right; x += 41) {
            float expected = ((crop_x + x) / 10 + (crop_y + y) / 10) % 2 ? 40000.0f : 1000.0f;
            crop_right = big.detail.pixels[std::size_t(y) * 800 + x] == expected;
        }
    }
    check(crop_right, QStringLiteral("the focus view holds the middle's real, unaveraged pixels"));

    // A smaller frame than the focus view just shows what there is
    FrameData tiny = read_fits_frame(make_fits(USHORT_IMG, 6, 5, 1, [](int, int, int) { return 7.0; }), 1280, 800, 480);
    check(tiny.detail.width == 6 && tiny.detail.height == 5, QStringLiteral("a tiny frame's focus view"));

    // A colour cube keeps its channels apart
    FrameData colour = read_fits_frame(make_fits(USHORT_IMG, 4, 2, 3, [](int, int, int c) { return 1000.0 * (c + 1); }), 1280, 800, 480);
    check(colour.error.isEmpty() && colour.fit.channels == 3, QStringLiteral("a colour file: %1").arg(colour.error));
    check(colour.fit.pixels.size() == 24 && colour.fit.pixels[0] == 1000.0f && colour.fit.pixels[1] == 2000.0f && colour.fit.pixels[2] == 3000.0f,
          QStringLiteral("its channels are in order"));

    // Raw colour sensor data: whole 2x2 groups must be averaged, or the pattern shows up
    QByteArray raw = make_fits(USHORT_IMG, 16, 16, 1, [](int x, int y, int) { return (x % 2 + y % 2) * 1000.0; }, "RGGB");
    FrameData bayer = read_fits_frame(raw, 1280, 800, 480);
    check(bayer.error.isEmpty() && bayer.bayer, QStringLiteral("a raw colour file is recognised"));
    bool smooth = !bayer.fit.pixels.empty();
    for (float v : bayer.fit.pixels)
        smooth = smooth && v == 1000.0f;
    check(smooth, QStringLiteral("and shows no filter pattern"));
    check(bayer.fit.width == 8, QStringLiteral("at half size, got %1").arg(bayer.fit.width));
    bool detail_smooth = !bayer.detail.pixels.empty();
    for (float v : bayer.detail.pixels)
        detail_smooth = detail_smooth && v == 1000.0f;
    check(detail_smooth, QStringLiteral("in its focus view too"));
}

static void test_damaged_files()
{
    QByteArray good = make_fits(USHORT_IMG, 64, 64, 1, [](int x, int, int) { return double(x); });
    auto refused = [](const QByteArray& data) { return !read_fits_frame(data, 1280, 800, 480).error.isEmpty(); };

    check(refused(QByteArray()), QStringLiteral("an empty file is refused"));
    check(refused(QByteArray("this is not a picture")), QStringLiteral("a text file is refused"));
    check(refused(good.left(2880)), QStringLiteral("a file with its pixels missing is refused"));
    check(refused(good.left(3000)), QStringLiteral("a file cut short is refused"));

    // A header claiming a vast image must be turned down, not attempted
    QByteArray vast = good;
    int naxis1 = vast.indexOf("NAXIS1  =");
    check(naxis1 >= 0, QStringLiteral("the test can find the header"));
    // A header card keeps its value in the columns up to 30, right-aligned
    auto with_width = [&](const QByteArray& value) {
        QByteArray changed = good;
        changed.replace(naxis1, 30, QByteArray("NAXIS1  = ") + value.rightJustified(20));
        return changed;
    };
    vast = with_width("2000000000");
    check(refused(vast), QStringLiteral("a header claiming a vast image is refused"));

    check(refused(with_width("0")), QStringLiteral("a header claiming no width is refused"));
    check(refused(with_width("-64")), QStringLiteral("a header claiming a negative width is refused"));
    check(!refused(with_width("64")), QStringLiteral("while the same edit with the true width still reads, so these tests are fair"));

    QByteArray one_axis = make_fits(USHORT_IMG, 8, 1, 1, [](int, int, int) { return 1.0; });
    check(read_fits_frame(one_axis, 1280, 800, 480).error.isEmpty(), QStringLiteral("a single row is fine"));

    // Random damage anywhere must never crash. Failing is fine, and so is a misread.
    std::mt19937 random(12345);
    for (int round = 0; round < 300; round++) {
        QByteArray damaged = good;
        int hits = 1 + int(random() % 40);
        for (int i = 0; i < hits; i++)
            damaged[int(random() % uint32_t(damaged.size()))] = char(random() % 256);
        if (round % 3 == 0)
            damaged.truncate(int(random() % uint32_t(damaged.size())));
        FrameData frame = read_fits_frame(damaged, 1280, 800, 480);
        render_frame(frame.fit, frame.stats, frame.range, true, true);
        frame_histogram(frame.fit, frame.range, 128);
    }
    check(true, QStringLiteral("300 randomly damaged files didn't crash"));

    FrameData empty_picture = read_picture_frame(QByteArray("not a picture"), 1280, 800, 480);
    check(!empty_picture.error.isEmpty(), QStringLiteral("a file that isn't a picture is reported"));
}

static void test_measuring_and_showing()
{
    // Known noise: a background of 1000 with a spread of 10
    std::mt19937 random(99);
    std::normal_distribution<double> noise(1000.0, 10.0);
    FrameData frame = read_fits_frame(make_fits(USHORT_IMG, 200, 200, 1, [&](int x, int y, int) {
        return x == 100 && y == 100 ? 65535.0 : noise(random);
    }), 1280, 800, 480);
    check(std::fabs(frame.stats.median - 1000.0) < 2.0, QStringLiteral("the background is measured, got %1").arg(frame.stats.median));
    check(std::fabs(frame.stats.mad * 1.4826 - 10.0) < 1.5, QStringLiteral("the noise is measured, got %1").arg(frame.stats.mad * 1.4826));
    check(frame.stats.peak == 65535.0, QStringLiteral("a single bright star doesn't upset either, and is the peak"));
    check(std::fabs(frame.stats.clipped - 1.0 / 40000.0) < 1e-5, QStringLiteral("the clipped fraction, got %1").arg(frame.stats.clipped));

    // Without a stretch this looks black, and with one the background is plainly visible
    QImage stretched = render_frame(frame.fit, frame.stats, frame.range, true, false);
    QImage plain = render_frame(frame.fit, frame.stats, frame.range, false, false);
    check(qGray(stretched.pixel(10, 10)) > 40, QStringLiteral("stretching shows the background, level %1").arg(qGray(stretched.pixel(10, 10))));
    check(qGray(plain.pixel(10, 10)) < 10, QStringLiteral("not stretching leaves it dark, level %1").arg(qGray(plain.pixel(10, 10))));
    check(qGray(stretched.pixel(100, 100)) == 255 && qGray(plain.pixel(100, 100)) == 255, QStringLiteral("the star is white either way"));

    // Night mode: not one pixel may hold any green or blue
    FrameData colour = read_fits_frame(make_fits(USHORT_IMG, 32, 32, 3, [&](int x, int y, int c) {
        return 500.0 + x * 100.0 + y * 50.0 + c * 3000.0;
    }), 1280, 800, 480);
    QImage night = render_frame(colour.fit, colour.stats, colour.range, true, true);
    QImage day = render_frame(colour.fit, colour.stats, colour.range, true, false);
    bool all_red = !night.isNull();
    bool any_lit = false;
    for (int y = 0; y < night.height(); y++) {
        for (int x = 0; x < night.width(); x++) {
            QRgb pixel = night.pixel(x, y);
            all_red = all_red && qGreen(pixel) == 0 && qBlue(pixel) == 0;
            any_lit = any_lit || qRed(pixel) > 0;
        }
    }
    check(all_red, QStringLiteral("a night picture holds only red"));
    check(any_lit, QStringLiteral("and isn't simply black"));
    check(qRed(day.pixel(5, 5)) != qBlue(day.pixel(5, 5)) || qGreen(day.pixel(5, 5)) != qBlue(day.pixel(5, 5)), QStringLiteral("a day picture keeps its colour"));

    QVariantList heights = frame_histogram(frame.fit, frame.range, 128);
    check(heights.size() == 128, QStringLiteral("the histogram has its bars"));
    double tallest = 0.0;
    int tallest_at = -1;
    bool in_range = true;
    for (int i = 0; i < heights.size(); i++) {
        double h = heights[i].toDouble();
        in_range = in_range && h >= 0.0 && h <= 1.0;
        if (h > tallest) {
            tallest = h;
            tallest_at = i;
        }
    }
    check(in_range && tallest == 1.0, QStringLiteral("its heights fit between 0 and 1"));
    check(tallest_at == int(1000.0 / 65535.0 * 127), QStringLiteral("its peak is at the background, bar %1").arg(tallest_at));
    check(heights[127].toDouble() > 0.0, QStringLiteral("and one star still shows at the top, on the log scale"));

    check(render_frame(FramePlane(), FrameStats(), 1.0, true, false).isNull(), QStringLiteral("no picture shows nothing"));
    check(frame_histogram(FramePlane(), 1.0, 128).isEmpty(), QStringLiteral("no picture has no histogram"));
}

static void test_never_overwriting()
{
    QTemporaryDir directory;
    const QString date = QStringLiteral("2026-11-07");
    const QString time = QStringLiteral("213000");
    QDir().mkpath(directory.path() + QLatin1Char('/') + date);

    QString first = CameraClient::frame_path(directory.path(), date, time, QStringLiteral(".fits"));
    check(first.endsWith(QStringLiteral("/2026-11-07/2026-11-07_213000.fits")), QStringLiteral("the first name, got %1").arg(first));

    QStringList taken;
    for (int i = 0; i < 5; i++) {
        QString path = CameraClient::frame_path(directory.path(), date, time, QStringLiteral(".fits"));
        check(!taken.contains(path) && !QFile::exists(path), QStringLiteral("frame %1 in the same second gets its own name, %2").arg(i).arg(path));
        taken << path;
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write("x");
    }
    check(taken.last().endsWith(QStringLiteral("_213000_5.fits")), QStringLiteral("names count up, got %1").arg(taken.last()));
    QString other = CameraClient::frame_path(directory.path(), date, time, QStringLiteral(".cr2"));
    check(other.endsWith(QStringLiteral("_213000.cr2")), QStringLiteral("another format is its own series"));
}

// How long a frame as large as a real camera's takes. Only for comparing builds, because
// this runs under emulation. Making the file is slow, so it's only done when asked for.
static void time_large_frame()
{
    QByteArray large = make_fits(USHORT_IMG, 6000, 4000, 1, [](int x, int y, int) { return double((x * 7 + y * 13) % 4000 + 500); }, "RGGB");
    QElapsedTimer timer;
    timer.start();
    FrameData frame = read_fits_frame(large, 1280, 800, 480);
    qint64 read_ms = timer.elapsed();
    timer.start();
    QImage image = render_frame(frame.fit, frame.stats, frame.range, true, false);
    std::printf("24 megapixels, %.0f MB: read and reduced in %lld ms, drawn in %lld ms, to %d x %d (%s)\n", large.size() / 1048576.0, read_ms,
                (long long)timer.elapsed(), image.width(), image.height(), frame.error.isEmpty() ? "ok" : qPrintable(frame.error));
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (qEnvironmentVariableIsSet("ASTRO_TEST_LARGE")) {
        time_large_frame();
        return 0;
    }

    test_stretch_sums();
    test_reading();
    test_damaged_files();
    test_measuring_and_showing();
    test_never_overwriting();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
