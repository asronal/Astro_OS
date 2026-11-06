// Runs the real MountClient against INDI's telescope simulator: connecting,
// the GoTo safety checks, slewing, STOP, sync and manual moves.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>

#include "astro/sky.h"
#include "mount_client.h"
#include "sky_backend.h"

static const int kConnectTimeoutMs = 90000;
static const int kSlewTimeoutMs = 180000;

static int failures = 0;

static void report(const char* label, bool ok, const QString& detail = QString())
{
    if (!ok)
        failures++;
    std::printf("%-5s %-52s %s\n", ok ? "ok" : "FAIL", label, qPrintable(detail));
    std::fflush(stdout);
}

static bool wait_for(const std::function<bool()>& condition, int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        if (condition())
            return true;
        QThread::msleep(50);
    }
    return false;
}

static void settle(int ms)
{
    wait_for([]() { return false; }, ms);
}

static double angle_between(double a, double b)
{
    double d = std::fabs(std::fmod(a - b, 360.0));
    return d > 180.0 ? 360.0 - d : d;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir config;

    SkyBackend sky(config.path() + QStringLiteral("/astro-ui.ini"), QStringLiteral("/usr/share/astro"));
    sky.setLocation(51.4769, -0.0005, 46.0);
    MountClient mount(&sky, config.path() + QStringLiteral("/astro-hub.conf"));

    report("Connects to the INDI server", wait_for([&]() { return mount.serverUp(); }, kConnectTimeoutMs));
    report("Finds the telescope driver", wait_for([&]() { return mount.mountFound(); }, kConnectTimeoutMs),
           mount.deviceName());

    mount.connectMount();
    report("Connects to the mount", wait_for([&]() { return mount.mountConnected(); }, kConnectTimeoutMs));
    settle(3000);

    double jd = astro::julian_now();
    const astro::Observer& observer = sky.observer();
    astro::BodyReport sun = astro::body_report(astro::Body::Sun, observer, jd);

    // Safety checks must refuse before anything is sent to the mount
    double before_ra = mount.ra();
    double before_dec = mount.dec();
    QString refused = mount.gotoTarget(sun.ra_deg, sun.dec_deg, false);
    report("GoTo the Sun is refused", refused.contains(QStringLiteral("Sun")), refused);

    // The point below the observer's feet
    double lst = astro::local_sidereal_deg(observer, jd);
    double below_ra = std::fmod(lst + 180.0, 360.0);
    double below_dec = -observer.lat_deg;
    refused = mount.gotoTarget(below_ra, below_dec, sun.alt_deg > 0.0 ? false : true);
    report("GoTo below the horizon is refused", refused.contains(QStringLiteral("horizon")), refused);
    settle(2000);
    report("The mount did not move after being refused",
           angle_between(mount.ra(), before_ra) < 1.0 && std::fabs(mount.dec() - before_dec) < 1.0);

    // Choose a target high in the sky, well away from both the Sun and the start
    double target_ra = -1.0;
    double target_dec = 0.0;
    for (double hours = 0.0; hours < 6.0 && target_ra < 0.0; hours += 0.5) {
        for (double dec : {60.0, 40.0, 75.0}) {
            double ra = std::fmod(lst - hours * 15.0 + 360.0, 360.0);
            if (astro::check_goto(ra, dec, observer, jd, astro::kDefaultGotoLimits, false) == astro::GotoVerdict::Allowed &&
                angle_between(ra, before_ra) > 20.0) {
                target_ra = ra;
                target_dec = dec;
                break;
            }
        }
    }
    report("Found a safe target", target_ra >= 0.0, QStringLiteral("RA %1 Dec %2").arg(target_ra).arg(target_dec));

    QString answer = mount.gotoTarget(target_ra, target_dec, false);
    report("GoTo a safe target is accepted", answer.isEmpty(), answer);
    report("The mount starts slewing", wait_for([&]() { return mount.slewing(); }, kConnectTimeoutMs));

    // STOP while it is moving
    settle(1000);
    mount.abort();
    bool stopped = wait_for([&]() { return !mount.slewing(); }, kConnectTimeoutMs);
    report("STOP ends the slew", stopped);
    settle(2000);
    double stopped_ra = mount.ra();
    double stopped_dec = mount.dec();
    bool short_of_target = angle_between(stopped_ra, target_ra) > 0.5 || std::fabs(stopped_dec - target_dec) > 0.5;
    report("STOP left it short of the target", short_of_target,
           QStringLiteral("RA %1 Dec %2").arg(stopped_ra).arg(stopped_dec));
    settle(3000);
    report("It stays stopped", std::fabs(mount.dec() - stopped_dec) < 0.2,
           QStringLiteral("Dec moved %1").arg(std::fabs(mount.dec() - stopped_dec)));

    // A full slew
    answer = mount.gotoTarget(target_ra, target_dec, false);
    report("A second GoTo is accepted", answer.isEmpty(), answer);
    wait_for([&]() { return mount.slewing(); }, kConnectTimeoutMs);
    bool arrived = wait_for([&]() {
        return !mount.slewing() && angle_between(mount.ra(), target_ra) < 0.2 && std::fabs(mount.dec() - target_dec) < 0.2;
    }, kSlewTimeoutMs);
    report("It arrives at the target", arrived,
           QStringLiteral("RA %1 Dec %2").arg(mount.ra()).arg(mount.dec()));

    settle(2000);
    report("It is tracking after arrival", mount.tracking());

    // Tracking holds the sky position steady
    double tracking_ra = mount.ra();
    settle(5000);
    report("Tracking holds the position", angle_between(mount.ra(), tracking_ra) < 0.05,
           QStringLiteral("RA moved %1").arg(angle_between(mount.ra(), tracking_ra)));

    mount.setTracking(false);
    report("Tracking can be turned off", wait_for([&]() { return !mount.tracking(); }, kConnectTimeoutMs));
    mount.setTracking(true);
    report("Tracking can be turned on", wait_for([&]() { return mount.tracking(); }, kConnectTimeoutMs));

    // Hold north, then release
    double north_start = mount.dec();
    mount.moveStart(QStringLiteral("N"));
    settle(3000);
    mount.moveStop();
    settle(1500);
    double north_end = mount.dec();
    report("Holding North moves the mount", north_end - north_start > 0.01,
           QStringLiteral("Dec moved %1").arg(north_end - north_start));
    settle(2000);
    report("Releasing North stops it", std::fabs(mount.dec() - north_end) < 0.05,
           QStringLiteral("Dec moved %1").arg(std::fabs(mount.dec() - north_end)));

    // South must work after North, which fails if North is left switched on
    double south_start = mount.dec();
    mount.moveStart(QStringLiteral("S"));
    settle(3000);
    mount.moveStop();
    settle(1500);
    report("Holding South moves the other way", south_start - mount.dec() > 0.01,
           QStringLiteral("Dec moved %1").arg(mount.dec() - south_start));

    // Sync must only change where the mount thinks it is, and must never slew
    wait_for([&]() { return !mount.slewing(); }, kSlewTimeoutMs);
    settle(1000);
    double sync_ra = std::fmod(mount.ra() + 30.0, 360.0);
    double sync_dec = 30.0;
    answer = mount.syncTarget(sync_ra, sync_dec);
    report("Sync is accepted", answer.isEmpty(), answer);
    settle(500);
    report("Sync does not slew", !mount.slewing());
    wait_for([&]() { return std::fabs(mount.dec() - sync_dec) < 0.1; }, 15000);
    report("Sync sets the position straight away", std::fabs(mount.dec() - sync_dec) < 0.1,
           QStringLiteral("Dec %1").arg(mount.dec()));

    // A GoTo must still work after a Sync, which fails if SYNC is left switched on
    answer = mount.gotoTarget(target_ra, target_dec, false);
    report("GoTo after Sync is accepted", answer.isEmpty(), answer);
    report("GoTo after Sync really slews", wait_for([&]() { return mount.slewing(); }, 15000));
    mount.abort();
    wait_for([&]() { return !mount.slewing(); }, kConnectTimeoutMs);

    mount.park();
    report("Parks", wait_for([&]() { return mount.parked(); }, kSlewTimeoutMs));
    mount.unpark();
    report("Unparks", wait_for([&]() { return !mount.parked(); }, kSlewTimeoutMs));

    // A program started while the mount is connected must notice: the touch app
    // after a restart, or the astro_sky shell running beside it
    {
        MountClient second(&sky, config.path() + QStringLiteral("/astro-hub-second.conf"));
        report("A second program sees the connected mount", wait_for([&]() { return second.mountConnected(); }, kConnectTimeoutMs));
    }

        std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
