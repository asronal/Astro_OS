// Checks choosing objects on the chart, the details shown for them, and the saved
// equipment, using the real catalog and with no display.
//
// The most important check here is that a time preview can't reach the mount: a
// planet tapped at a future time must still report where it really is now.
#include <QGuiApplication>
#include <QSet>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

#include "astro/sky.h"
#include "chart_math.h"
#include "sky_backend.h"
#include "sky_chart.h"

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

static const double kScreenX = 400.0;
static const double kScreenY = 240.0;

// Finds the offset, in hours from now, when this position is highest
static double when_high(const SkyBackend& sky, double ra, double dec)
{
    double best = 0.0;
    double best_alt = -90.0;
    for (double hours = -12.0; hours < 12.0; hours += 0.25) {
        double az, alt;
        astro::equatorial_to_horizontal(ra, dec, sky.observer(), astro::julian_now() + hours / 24.0, &az, &alt);
        if (alt > best_alt) {
            best_alt = alt;
            best = hours;
        }
    }
    return best;
}

static QString row_value(const QVariantMap& details, const QString& label)
{
    const QVariantList rows = details.value(QStringLiteral("rows")).toList();
    for (const QVariant& row : rows) {
        if (row.toMap().value(QStringLiteral("label")).toString() == label)
            return row.toMap().value(QStringLiteral("value")).toString();
    }
    return QString();
}

static void test_cycling(SkyBackend& sky, SkyChart& chart)
{
    // The Pleiades, where several named stars sit close together
    const double ra = 56.87;
    const double dec = 24.11;
    chart.setTimeOffsetHours(when_high(sky, ra, dec));
    chart.centerOn(ra, dec);
    // Wide enough that a finger's reach covers several of them
    chart.setFieldOfView(20.0);
    chart.setTarget(QVariantMap());

    QVariantMap first = chart.nextObjectAt(kScreenX, kScreenY);
    int count = first.value(QStringLiteral("cycleCount")).toInt();
    check(first.contains(QStringLiteral("name")), QStringLiteral("a tap on the Pleiades finds something"));
    check(count >= 3, QStringLiteral("several objects are found there, got %1").arg(count));
    check(first.value(QStringLiteral("cycleIndex")).toInt() == 1, QStringLiteral("the first tap is first in the cycle"));

    // Every tap moves on, each object is visited once, and then it comes round again.
    // Names can repeat, so position tells them apart.
    QSet<QString> seen;
    QVariantMap now = first;
    for (int i = 0; i < count; i++) {
        check(now.value(QStringLiteral("cycleIndex")).toInt() == i + 1, QStringLiteral("tap %1 is in sequence").arg(i + 1));
        seen.insert(now.value(QStringLiteral("name")).toString() + QStringLiteral("@") +
                    QString::number(now.value(QStringLiteral("ra")).toDouble(), 'f', 3));
        chart.setTarget(now);
        now = chart.nextObjectAt(kScreenX, kScreenY);
    }
    check(seen.size() == count, QStringLiteral("each of %1 objects is visited, saw %2").arg(count).arg(seen.size()));
    check(now.value(QStringLiteral("cycleIndex")).toInt() == 1, QStringLiteral("the cycle comes back round to the start"));

    // Tapping somewhere new starts again, rather than continuing the cycle
    chart.setTarget(first);
    chart.nextObjectAt(kScreenX, kScreenY);
    QVariantMap elsewhere = chart.nextObjectAt(kScreenX + 300.0, kScreenY);
    check(elsewhere.isEmpty() || elsewhere.value(QStringLiteral("cycleIndex")).toInt() == 1, QStringLiteral("a tap elsewhere starts a new cycle"));
}

// What's under the pointer stays there while zooming, anywhere in the view
static void test_zoom_at_pointer(SkyChart& chart)
{
    chart.setTimeOffsetHours(0.0);
    const struct { double az; double alt; double fov; double x; double y; double factor; } cases[] = {
        {180.0, 45.0, 90.0, 650.0, 120.0, 0.8},
        {180.0, 45.0, 90.0, 120.0, 400.0, 0.5},
        {60.0, 20.0, 30.0, 700.0, 300.0, 1.25},
        {300.0, 70.0, 10.0, 200.0, 100.0, 0.8},
    };
    for (const auto& c : cases) {
        chart.setCenterAz(c.az);
        chart.setCenterAlt(c.alt);
        chart.setFieldOfView(c.fov);
        chart::View before = chart::make_view(chart.centerAz(), chart.centerAlt(), chart.fieldOfView(), chart.width(), chart.height());
        double az, alt, x, y;
        chart::unproject(before, c.x, c.y, &az, &alt);

        chart.zoomAt(c.x, c.y, c.factor);
        check(std::fabs(chart.fieldOfView() - c.fov * c.factor) < 1e-9, QStringLiteral("zooming changes the field of view"));
        chart::View after = chart::make_view(chart.centerAz(), chart.centerAlt(), chart.fieldOfView(), chart.width(), chart.height());
        chart::project(after, az, alt, &x, &y);
        double moved = std::hypot(x - c.x, y - c.y);
        check(moved < 2.0, QStringLiteral("the sky under the pointer moved %1 pixels").arg(moved));
    }
}

static void test_hidden_objects(SkyBackend& sky, SkyChart& chart)
{
    Q_UNUSED(sky);
    chart.setTimeOffsetHours(0.0);
    chart.setTarget(QVariantMap());
    chart.setCenterAz(180.0);
    chart.setCenterAlt(-25.0);
    chart.setFieldOfView(25.0);
    QVariantMap under_ground = chart.nextObjectAt(kScreenX, kScreenY);
    check(under_ground.isEmpty(), QStringLiteral("nothing below the ground can be tapped, got %1").arg(under_ground.value(QStringLiteral("name")).toString()));
}

// A planet tapped at a previewed time must report its real position for now
static void test_preview_never_reaches_the_mount(SkyBackend& sky, SkyChart& chart)
{
    const double now = astro::julian_now();
    const QString moon = QStringLiteral("Moon");

    // When the Moon is highest in the next two days, at least 6 hours away, so it has
    // moved by at least 3 degrees. It can stay low for days at this latitude, but never
    // below about 10 degrees at its highest, which clears the chart's treeline.
    double offset = 0.0;
    double highest = -90.0;
    for (double hours = 6.0; hours < 48.0; hours += 0.5) {
        double alt = astro::body_position(astro::Body::Moon, sky.observer(), now + hours / 24.0).alt_deg;
        if (alt > highest) {
            highest = alt;
            offset = hours;
        }
    }
    check(highest > 8.0, QStringLiteral("the Moon clears the horizon in the next two days, reaching %1").arg(highest));

    astro::BodyReport shown = astro::body_position(astro::Body::Moon, sky.observer(), now + offset / 24.0);
    chart.setTarget(QVariantMap());
    chart.setTimeOffsetHours(offset);
    chart.setCenterAz(shown.az_deg);
    chart.setCenterAlt(shown.alt_deg);
    chart.setFieldOfView(8.0);

    // Far enough from the last tap to start a new cycle
    chart.nextObjectAt(5.0, 5.0);
    QVariantMap tapped = chart.nextObjectAt(kScreenX, kScreenY);
    check(tapped.value(QStringLiteral("name")).toString() == moon, QStringLiteral("the Moon is found where it's shown, got %1").arg(tapped.value(QStringLiteral("name")).toString()));

    astro::BodyReport real = astro::body_position(astro::Body::Moon, sky.observer(), astro::julian_now());
    double tapped_ra = tapped.value(QStringLiteral("ra")).toDouble();
    double tapped_dec = tapped.value(QStringLiteral("dec")).toDouble();
    double from_real = chart::separation(tapped_ra, tapped_dec, real.ra_deg, real.dec_deg);
    double from_shown = chart::separation(tapped_ra, tapped_dec, shown.ra_deg, shown.dec_deg);
    check(from_real < 0.05, QStringLiteral("a tapped Moon has its real position, %1 degrees off").arg(from_real));
    check(from_shown > 1.0, QStringLiteral("and not the previewed one, %1 degrees from it").arg(from_shown));

    // Whatever a target holds, the position for the mount comes out real
    QVariantMap stale = tapped;
    stale[QStringLiteral("ra")] = shown.ra_deg;
    stale[QStringLiteral("dec")] = shown.dec_deg;
    QVariantMap live = sky.livePosition(stale);
    check(chart::separation(live.value(QStringLiteral("ra")).toDouble(), live.value(QStringLiteral("dec")).toDouble(), real.ra_deg, real.dec_deg) < 0.05,
          QStringLiteral("livePosition replaces a planet's stale position"));

    // Turning to the target goes to where it is at the time shown
    chart.setTarget(tapped);
    chart.setCenterAz(0.0);
    chart.setCenterAlt(10.0);
    chart.centerOnTarget();
    check(chart::separation(chart.centerAz(), chart.centerAlt(), shown.az_deg, shown.alt_deg) < 0.2,
          QStringLiteral("centerOnTarget uses the time shown"));

    chart.setTimeOffsetHours(0.0);

    // A star doesn't move, and keeps its catalog position
    QVariantMap vega = sky.findExact(QStringLiteral("Vega"));
    QVariantMap vega_live = sky.livePosition(vega);
    check(vega_live.value(QStringLiteral("ra")) == vega.value(QStringLiteral("ra")) &&
          vega_live.value(QStringLiteral("dec")) == vega.value(QStringLiteral("dec")),
          QStringLiteral("livePosition leaves a star where it is"));
}

static void test_details(SkyBackend& sky)
{
    QVariantMap polaris = sky.targetDetails(sky.findExact(QStringLiteral("Polaris")), 0.0, false);
    check(row_value(polaris, QStringLiteral("Rises and sets")) == QStringLiteral("Always up from here"), QStringLiteral("Polaris never sets at Greenwich"));
    check(row_value(polaris, QStringLiteral("Rises")).isEmpty(), QStringLiteral("Polaris has no rise time at Greenwich"));
    check(row_value(polaris, QStringLiteral("GoTo now")).startsWith(QStringLiteral("Allowed")) ||
          row_value(polaris, QStringLiteral("GoTo now")).contains(QStringLiteral("Sun")), QStringLiteral("Polaris is above the horizon for GoTo"));

    QVariantMap canopus = sky.targetDetails(sky.findExact(QStringLiteral("Canopus")), 0.0, false);
    check(row_value(canopus, QStringLiteral("Rises and sets")) == QStringLiteral("Never rises from here"), QStringLiteral("Canopus never rises at Greenwich"));
    check(row_value(canopus, QStringLiteral("GoTo now")) == QStringLiteral("Refused, below the horizon"), QStringLiteral("GoTo to Canopus is refused"));

    QVariantMap m31 = sky.targetDetails(sky.findExact(QStringLiteral("M31")), 0.0, false);
    check(row_value(m31, QStringLiteral("Also known as")) == QStringLiteral("NGC 224"), QStringLiteral("M31 is also NGC 224"));
    check(row_value(m31, QStringLiteral("Size")) == QStringLiteral("178′ × 70′"), QStringLiteral("M31's real size, got %1").arg(row_value(m31, QStringLiteral("Size"))));
    check(!row_value(m31, QStringLiteral("Best when dark")).isEmpty(), QStringLiteral("M31 has a best time"));
    check(!row_value(m31, QStringLiteral("From the Moon")).isEmpty(), QStringLiteral("M31 has a distance from the Moon"));
    check(row_value(m31, QStringLiteral("Lit")).isEmpty(), QStringLiteral("a galaxy has no phase"));

    QVariantMap moon = sky.targetDetails(sky.findExact(QStringLiteral("Moon")), 0.0, false);
    check(row_value(moon, QStringLiteral("Lit")).endsWith(QLatin1Char('%')), QStringLiteral("the Moon has a phase"));
    check(row_value(moon, QStringLiteral("Distance")).endsWith(QStringLiteral(" km")), QStringLiteral("the Moon's distance is in km"));
    check(row_value(moon, QStringLiteral("From the Moon")).isEmpty(), QStringLiteral("the Moon isn't measured from itself"));

    QVariantMap sun = sky.findExact(QStringLiteral("Sun"));
    QVariantMap sun_details = sky.targetDetails(sun, 0.0, false);
    check(row_value(sun_details, QStringLiteral("From the Sun")).isEmpty(), QStringLiteral("the Sun isn't measured from itself"));
    check(row_value(sun_details, QStringLiteral("Best when dark")).isEmpty(), QStringLiteral("the Sun has no best time in the dark"));

    // The Sun check doesn't depend on the time being previewed, so the verdict matches
    // the real safety check, even for a time when the Sun is somewhere else
    astro::GotoVerdict verdict = astro::check_goto(sun.value(QStringLiteral("ra")).toDouble(), sun.value(QStringLiteral("dec")).toDouble(),
                                                   sky.observer(), astro::julian_now(), astro::kDefaultGotoLimits, false);
    QString expected = verdict == astro::GotoVerdict::TooCloseToSun ? QStringLiteral("Refused, too close to the Sun")
                     : verdict == astro::GotoVerdict::BelowHorizon ? QStringLiteral("Refused, below the horizon") : QStringLiteral("Allowed");
    check(row_value(sun_details, QStringLiteral("GoTo now")) == expected, QStringLiteral("the GoTo row matches the safety check for the Sun"));
    check(row_value(sky.targetDetails(sun, 7.0, false), QStringLiteral("GoTo now")) == expected, QStringLiteral("and doesn't change with the time shown"));

    // An event on a later day says so, and one later today doesn't
    QVariantMap betelgeuse = sky.findExact(QStringLiteral("Betelgeuse"));
    for (double hours = 0.0; hours < 24.0; hours += 3.0) {
        double jd = astro::julian_now() + hours / 24.0;
        astro::FixedRiseSet times = astro::fixed_rise_set(betelgeuse.value(QStringLiteral("ra")).toDouble(), betelgeuse.value(QStringLiteral("dec")).toDouble(), sky.observer(), jd);
        // Greenwich here, with the time zone left at UTC, and days start at midnight
        bool same_day = std::floor(times.rise_jd - 0.5) == std::floor(jd - 0.5);
        QString rises = row_value(sky.targetDetails(betelgeuse, hours, false), QStringLiteral("Rises"));
        const QString later = hours == 0.0 ? QStringLiteral(" tomorrow") : QStringLiteral(" next day");
        check(same_day ? rises.size() == 5 : rises.endsWith(later), QStringLiteral("rise at +%1 h says which day, got %2").arg(hours).arg(rises));
    }

    // A summary follows the time shown
    QVariantMap vega = sky.findExact(QStringLiteral("Vega"));
    check(sky.targetSummary(vega, 0.0) != sky.targetSummary(vega, 6.0), QStringLiteral("the summary follows the time shown"));
    check(sky.targetSummary(QVariantMap(), 0.0).isEmpty(), QStringLiteral("no target has no summary"));
    check(sky.targetDetails(QVariantMap(), 0.0, false).isEmpty(), QStringLiteral("no target has no details"));

    check(sky.previewTimeText(0.0).startsWith(QStringLiteral("Now, ")), QStringLiteral("the present is called now"));
    check(!sky.previewTimeText(3.0).startsWith(QStringLiteral("Now")), QStringLiteral("a preview isn't called now"));
}

static void test_equipment(const QString& settings_path, const QString& catalog_dir)
{
    const double not_a_number = std::nan("");
    {
        SkyBackend sky(settings_path, catalog_dir);
        check(sky.telescopeFocalMm() == 0.0, QStringLiteral("no telescope is assumed"));
        check(sky.eyepieces().isEmpty(), QStringLiteral("no eyepieces are assumed"));

        for (double bad : {not_a_number, 0.0, -100.0, 49.0, 20001.0})
            check(!sky.setTelescopeFocal(bad).isEmpty(), QStringLiteral("focal length %1 is refused").arg(bad));
        check(sky.telescopeFocalMm() == 0.0, QStringLiteral("a refused value isn't kept"));

        // An eyepiece can be entered first, but says nothing about the sky yet
        check(sky.addEyepiece(25.0, 52.0).isEmpty(), QStringLiteral("a normal eyepiece is accepted"));
        check(!sky.eyepieces().first().toMap().contains(QStringLiteral("fieldDeg")), QStringLiteral("no field without a telescope"));

        for (double bad : {not_a_number, 0.0, -5.0, 1.0, 101.0})
            check(!sky.addEyepiece(bad, 52.0).isEmpty(), QStringLiteral("eyepiece focal length %1 is refused").arg(bad));
        for (double bad : {not_a_number, 0.0, 19.0, 121.0})
            check(!sky.addEyepiece(25.0, bad).isEmpty(), QStringLiteral("apparent field %1 is refused").arg(bad));
        check(sky.eyepieces().size() == 1, QStringLiteral("refused eyepieces aren't kept"));

        check(sky.setTelescopeFocal(1000.0).isEmpty(), QStringLiteral("a normal focal length is accepted"));
        QVariantMap first = sky.eyepieces().first().toMap();
        check(std::fabs(first.value(QStringLiteral("fieldDeg")).toDouble() - 1.3) < 1e-9, QStringLiteral("25 mm in 1000 mm at 52 degrees shows 1.3 degrees"));
        check(first.value(QStringLiteral("label")).toString() == QStringLiteral("25 mm, 40×, 1.30°"), QStringLiteral("the ring's label, got %1").arg(first.value(QStringLiteral("label")).toString()));

        sky.addEyepiece(10.0, 82.0);
        sky.addEyepiece(6.5, 60.0);
        sky.setEyepieceOn(1, false);
        sky.removeEyepiece(2);
        sky.removeEyepiece(9);
        sky.setEyepieceOn(-1, true);
        check(sky.eyepieces().size() == 2, QStringLiteral("removing works, and bad indexes are ignored"));

        for (int i = 2; i < 12; i++)
            check(sky.addEyepiece(20.0, 50.0).isEmpty(), QStringLiteral("eyepiece %1 fits").arg(i));
        check(!sky.addEyepiece(20.0, 50.0).isEmpty(), QStringLiteral("the list has a limit"));
        for (int i = 11; i >= 2; i--)
            sky.removeEyepiece(i);
    }

    // Everything is still there after a restart
    SkyBackend reloaded(settings_path, catalog_dir);
    check(reloaded.telescopeFocalMm() == 1000.0, QStringLiteral("the telescope is remembered"));
    check(reloaded.eyepieces().size() == 2, QStringLiteral("the eyepieces are remembered"));
    QVariantMap second = reloaded.eyepieces().at(1).toMap();
    check(second.value(QStringLiteral("focalMm")).toDouble() == 10.0 && second.value(QStringLiteral("apparentField")).toDouble() == 82.0,
          QStringLiteral("each eyepiece's sizes are remembered"));
    check(!second.value(QStringLiteral("on")).toBool(), QStringLiteral("an eyepiece switched off stays off"));
    check(reloaded.eyepieces().at(0).toMap().value(QStringLiteral("on")).toBool(), QStringLiteral("an eyepiece switched on stays on"));
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QTemporaryDir config;
    QString catalog_dir = qEnvironmentVariable("ASTRO_CATALOG_DIR", QStringLiteral("/usr/share/astro"));

    SkyBackend sky(config.path() + QStringLiteral("/astro-ui.ini"), catalog_dir);
    sky.setLocation(51.4769, -0.0005, 46.0);
    check(!sky.catalog().stars().isEmpty(), QStringLiteral("the catalog loaded, so set ASTRO_CATALOG_DIR if this fails"));

    SkyChart chart;
    chart.setSize(QSizeF(kScreenX * 2.0, kScreenY * 2.0));
    chart.setBackend(&sky);
    // Dark, so that nothing is hidden by the time of day when these run
    chart.setShowAtmosphere(false);

    test_cycling(sky, chart);
    test_zoom_at_pointer(chart);
    test_hidden_objects(sky, chart);
    test_preview_never_reaches_the_mount(sky, chart);
    test_details(sky);
    test_equipment(config.path() + QStringLiteral("/equipment.ini"), catalog_dir);

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
