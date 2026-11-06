// astro_sky: an interactive shell for Astro OS, in the style of tools such as
// dc_shell. It runs Tcl, so scripts can use variables, loops and procedures, and
// adds commands for the sky and the telescope. It shares its settings, its sky
// calculations and its GoTo safety checks with the touch app.
#include <cstdio>  // readline's headers need this first

#include <readline/history.h>
#include <readline/readline.h>
#include <tcl.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSocketNotifier>
#include <QStringList>
#include <QThread>
#include <QTimeZone>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "astro/sky.h"
#include "mount_client.h"
#include "sky_backend.h"

namespace {

const int kConnectTimeoutMs = 90000;
const int kFirstDetectWaitMs = 6000;
const int kDetectWaitMs = 20000;
const int kClockCheckWaitMs = 2000;
const int kDefaultSlewWaitSeconds = 300;

volatile sig_atomic_t g_interrupted = 0;
int g_interrupt_pipe[2] = {-1, -1};

void on_interrupt(int)
{
    g_interrupted = 1;
    if (write(g_interrupt_pipe[1], "x", 1) < 0) {
        // Nothing can be done about this in a signal handler
    }
}

bool g_colour = false;

const char* colour(const char* code)
{
    return g_colour ? code : "";
}

const char* const kBrass = "\033[1;38;5;223m";
const char* const kDim = "\033[38;5;246m";
const char* const kRed = "\033[38;5;203m";
const char* const kPlain = "\033[0m";

void heading(const QString& text)
{
    std::printf("\n%s%s%s\n", colour(kBrass), qPrintable(text), colour(kPlain));
}

// Right ascension as hours, minutes and seconds
QString format_ra(double degrees)
{
    double hours = std::fmod(degrees + 360.0, 360.0) / 15.0;
    int h = int(hours);
    int m = int((hours - h) * 60.0);
    int s = int(((hours - h) * 60.0 - m) * 60.0);
    return QStringLiteral("%1h %2m %3s").arg(h, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QString format_dec(double degrees)
{
    double size = std::fabs(degrees);
    int d = int(size);
    int m = int((size - d) * 60.0);
    return QStringLiteral("%1%2° %3'").arg(degrees < 0.0 ? QLatin1Char('-') : QLatin1Char('+')).arg(d, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0'));
}

QString format_offset(int minutes)
{
    int size = std::abs(minutes);
    return size % 60 == 0 ? QStringLiteral("UTC%1%2").arg(minutes < 0 ? '-' : '+').arg(size / 60)
                          : QStringLiteral("UTC%1%2:%3").arg(minutes < 0 ? '-' : '+').arg(size / 60).arg(size % 60, 2, 10, QLatin1Char('0'));
}

QString moon_phase_name(double lit, bool waxing)
{
    if (lit < 0.02)
        return QStringLiteral("New Moon");
    if (lit > 0.98)
        return QStringLiteral("Full Moon");
    QString direction = waxing ? QStringLiteral("Waxing") : QStringLiteral("Waning");
    if (lit < 0.45)
        return direction + QStringLiteral(" crescent");
    if (lit > 0.55)
        return direction + QStringLiteral(" gibbous");
    return waxing ? QStringLiteral("First quarter") : QStringLiteral("Last quarter");
}

class Shell {
public:
    Shell(SkyBackend* sky, const QString& config_dir, bool verbose);

    Tcl_Interp* interp() const { return m_interp; }
    SkyBackend* sky() const { return m_sky; }
    MountClient* mount();
    bool mount_started() const { return m_mount != nullptr; }

    bool wait_until(const std::function<bool()>& condition, int timeout_ms);
    void print_error(const QString& text) const;
    void fix_file_owners() const;
    int error(const QString& text) const;
    bool require_location() const;
    int find_object(const QString& query, QVariantMap* found) const;
    int clock_error_if_unset() const;
    void wait_for_clock_check();

private:
    SkyBackend* m_sky;
    QString m_config_dir;
    bool m_verbose;
    int m_error_fd;
    Tcl_Interp* m_interp;
    std::unique_ptr<MountClient> m_mount;
};

Shell::Shell(SkyBackend* sky, const QString& config_dir, bool verbose)
    : m_sky(sky),
      m_config_dir(config_dir),
      m_verbose(verbose),
      m_error_fd(dup(STDERR_FILENO)),
      m_interp(Tcl_CreateInterp())
{
}

// Created only when needed, as it keeps trying to reach the device service
MountClient* Shell::mount()
{
    if (!m_mount) {
        // INDI's library writes connection attempts to stderr, over the prompt
        if (!m_verbose) {
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }
        m_mount = std::make_unique<MountClient>(m_sky, m_config_dir + QStringLiteral("/astro-hub.conf"));
    }
    return m_mount.get();
}

// Keeps Qt and INDI running while a command waits. Ctrl+C ends the wait.
bool Shell::wait_until(const std::function<bool()>& condition, int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (condition())
            return true;
        if (g_interrupted)
            return false;
        QThread::msleep(20);
    }
    return condition();
}

void Shell::print_error(const QString& text) const
{
    QByteArray message = QByteArray(colour(kRed)) + "Error: " + text.toUtf8() + colour(kPlain) + "\n";
    if (write(m_error_fd, message.constData(), size_t(message.size())) < 0)
        std::fputs(message.constData(), stdout);
}

// A root shell must not leave settings that the touch app, running as a normal
// user, can no longer change
void Shell::fix_file_owners() const
{
    struct stat directory;
    if (geteuid() != 0 || stat(QFile::encodeName(m_config_dir).constData(), &directory) != 0)
        return;
    for (const char* name : {"astro-ui.ini", "astro-hub.conf"}) {
        QByteArray path = QFile::encodeName(m_config_dir + QLatin1Char('/') + QLatin1String(name));
        if (access(path.constData(), F_OK) == 0 && chown(path.constData(), directory.st_uid, directory.st_gid) != 0)
            print_error(QStringLiteral("Could not set the owner of %1").arg(QString::fromLocal8Bit(path)));
    }
}

int Shell::error(const QString& text) const
{
    Tcl_SetObjResult(m_interp, Tcl_NewStringObj(text.toUtf8().constData(), -1));
    return TCL_ERROR;
}

bool Shell::require_location() const
{
    if (m_sky->locationSet())
        return true;
    error(QStringLiteral("The location isn't set. Use detect_location, or set_location LAT LON."));
    return false;
}

int Shell::clock_error_if_unset() const
{
    if (m_sky->clockSet())
        return TCL_OK;
    return error(QStringLiteral("The clock isn't set, so the sky can't be worked out. Connect to the internet, or use set_time."));
}

void Shell::wait_for_clock_check()
{
    bool done = false;
    QMetaObject::Connection connection = QObject::connect(m_sky, &SkyBackend::clockChecked, m_sky, [&done]() { done = true; });
    m_sky->checkClock();
    wait_until([&done]() { return done; }, kClockCheckWaitMs);
    QObject::disconnect(connection);
}

// Finds one object by name. A telescope may be sent to the result, so an unclear
// name is an error rather than a guess.
int Shell::find_object(const QString& query, QVariantMap* found) const
{
    QString wanted = query.trimmed();

    // "M3" must find M3 and nothing else, however many names contain those letters
    *found = m_sky->findExact(wanted);
    if (!found->isEmpty())
        return TCL_OK;

    const QVariantList results = m_sky->search(wanted);
    if (results.size() == 1) {
        *found = results.first().toMap();
        return TCL_OK;
    }
    if (results.isEmpty())
        return error(QStringLiteral("Nothing called \"%1\" is in the catalog. Try find_objects.").arg(wanted));

    QStringList names;
    for (int i = 0; i < results.size() && i < 6; i++)
        names << results.at(i).toMap().value(QStringLiteral("name")).toString();
    return error(QStringLiteral("\"%1\" could mean several objects: %2. Use a fuller name.").arg(wanted, names.join(QStringLiteral(", "))));
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

using CommandFunction = int (*)(Shell&, int, Tcl_Obj* const*);

struct Command {
    const char* name;
    const char* group;
    const char* usage;
    const char* summary;
    const char* details;
    CommandFunction run;
};

QString arg_text(Tcl_Obj* object)
{
    return QString::fromUtf8(Tcl_GetString(object));
}

bool arg_double(Shell& shell, Tcl_Obj* object, double* value)
{
    return Tcl_GetDoubleFromObj(shell.interp(), object, value) == TCL_OK;
}

int result_text(Shell& shell, const QString& text)
{
    Tcl_SetObjResult(shell.interp(), Tcl_NewStringObj(text.toUtf8().constData(), -1));
    return TCL_OK;
}

void dict_put(Tcl_Interp* interp, Tcl_Obj* dict, const char* key, Tcl_Obj* value)
{
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj(key, -1), value);
}

int usage_error(Shell& shell, const char* usage)
{
    return shell.error(QStringLiteral("Usage: %1").arg(QString::fromUtf8(usage)));
}

int cmd_report_tonight(Shell& shell, int, Tcl_Obj* const*)
{
    if (!shell.require_location() || shell.clock_error_if_unset() != TCL_OK)
        return TCL_ERROR;

    QVariantMap night = shell.sky()->nightInfo();
    double lit = night.value(QStringLiteral("moonLit")).toDouble();
    bool waxing = night.value(QStringLiteral("moonWaxing")).toBool();

    heading(QStringLiteral("Tonight"));
    if (night.value(QStringLiteral("normal")).toBool()) {
        std::printf("  Sunset      %s\n", qPrintable(night.value(QStringLiteral("sunset")).toString()));
        std::printf("  Dark sky    %s to %s\n", qPrintable(night.value(QStringLiteral("astronomicalDusk")).toString()),
                    qPrintable(night.value(QStringLiteral("astronomicalDawn")).toString()));
        std::printf("  Sunrise     %s\n", qPrintable(night.value(QStringLiteral("sunrise")).toString()));
    } else {
        std::puts("  The Sun doesn't rise and set normally here at this time of year.");
    }
    std::printf("  Moon        %s, %.0f%% lit, rises %s, sets %s\n", qPrintable(moon_phase_name(lit, waxing)), lit * 100.0,
                qPrintable(night.value(QStringLiteral("moonRise")).toString()),
                qPrintable(night.value(QStringLiteral("moonSet")).toString()));

    heading(QStringLiteral("Best targets"));
    const QVariantList targets = shell.sky()->bestTargets();
    if (targets.isEmpty())
        std::puts("  Nothing is above 20° while the sky is dark.");
    for (const QVariant& target : targets) {
        QVariantMap map = target.toMap();
        std::printf("  %-9s up to %2.0f° around %s, magnitude %.1f\n", qPrintable(map.value(QStringLiteral("name")).toString()),
                    map.value(QStringLiteral("maxAlt")).toDouble(), qPrintable(map.value(QStringLiteral("bestTime")).toString()),
                    map.value(QStringLiteral("mag")).toDouble());
    }
    std::puts("");
    return TCL_OK;
}

int cmd_report_planets(Shell& shell, int, Tcl_Obj* const*)
{
    if (!shell.require_location() || shell.clock_error_if_unset() != TCL_OK)
        return TCL_ERROR;

    heading(QStringLiteral("Sun, Moon and planets at %1 local time").arg(shell.sky()->clockText()));
    std::printf("%s  %-9s %7s %7s %7s %5s  %-5s  %-5s  %-5s%s\n", colour(kDim), "", "Up", "Bearing", "Mag", "Lit", "Rise", "High", "Set", colour(kPlain));
    const QVariantList bodies = shell.sky()->bodies();
    for (const QVariant& body : bodies) {
        QVariantMap map = body.toMap();
        std::printf("  %-9s %6.1f° %6.1f° %7.1f %4.0f%%  %-5s  %-5s  %-5s\n", qPrintable(map.value(QStringLiteral("name")).toString()),
                    map.value(QStringLiteral("alt")).toDouble(), map.value(QStringLiteral("az")).toDouble(),
                    map.value(QStringLiteral("mag")).toDouble(), map.value(QStringLiteral("lit")).toDouble() * 100.0,
                    qPrintable(map.value(QStringLiteral("rise")).toString()), qPrintable(map.value(QStringLiteral("transit")).toString()),
                    qPrintable(map.value(QStringLiteral("set")).toString()));
    }
    std::puts("");
    return TCL_OK;
}

int cmd_report_object(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2)
        return usage_error(shell, "report_object NAME");
    if (!shell.require_location() || shell.clock_error_if_unset() != TCL_OK)
        return TCL_ERROR;

    QVariantMap object;
    if (shell.find_object(arg_text(objv[1]), &object) != TCL_OK)
        return TCL_ERROR;

    double ra = object.value(QStringLiteral("ra")).toDouble();
    double dec = object.value(QStringLiteral("dec")).toDouble();
    double jd = astro::julian_now();

    heading(object.value(QStringLiteral("name")).toString());
    if (object.contains(QStringLiteral("type")))
        std::printf("  Type             %s\n", qPrintable(object.value(QStringLiteral("type")).toString()));
    if (object.contains(QStringLiteral("mag")))
        std::printf("  Magnitude        %.1f\n", object.value(QStringLiteral("mag")).toDouble());
    std::printf("  Right ascension  %s\n", qPrintable(format_ra(ra)));
    std::printf("  Declination      %s\n", qPrintable(format_dec(dec)));
    std::printf("  Height           %.1f°\n", object.value(QStringLiteral("alt")).toDouble());
    std::printf("  Bearing          %.1f°\n", object.value(QStringLiteral("az")).toDouble());
    if (object.contains(QStringLiteral("rise"))) {
        std::printf("  Rises, sets      %s, %s\n", qPrintable(object.value(QStringLiteral("rise")).toString()),
                    qPrintable(object.value(QStringLiteral("set")).toString()));
    }
    std::printf("  From the Sun     %.1f°\n", astro::sun_separation_deg(ra, dec, jd));

    const char* verdict = "";
    switch (astro::check_goto(ra, dec, shell.sky()->observer(), jd, astro::kDefaultGotoLimits, false)) {
    case astro::GotoVerdict::Allowed: verdict = "allowed"; break;
    case astro::GotoVerdict::TooCloseToSun: verdict = "refused, too close to the Sun"; break;
    case astro::GotoVerdict::BelowHorizon: verdict = "refused, below the horizon"; break;
    }
    std::printf("  GoTo             %s\n\n", verdict);
    return TCL_OK;
}

int cmd_get_object(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2)
        return usage_error(shell, "get_object NAME");
    if (!shell.require_location() || shell.clock_error_if_unset() != TCL_OK)
        return TCL_ERROR;

    QVariantMap object;
    if (shell.find_object(arg_text(objv[1]), &object) != TCL_OK)
        return TCL_ERROR;

    Tcl_Interp* interp = shell.interp();
    Tcl_Obj* dict = Tcl_NewDictObj();
    dict_put(interp, dict, "name", Tcl_NewStringObj(object.value(QStringLiteral("name")).toString().toUtf8().constData(), -1));
    dict_put(interp, dict, "type", Tcl_NewStringObj(object.value(QStringLiteral("type"), QStringLiteral("Solar system")).toString().toUtf8().constData(), -1));
    dict_put(interp, dict, "ra", Tcl_NewDoubleObj(object.value(QStringLiteral("ra")).toDouble()));
    dict_put(interp, dict, "dec", Tcl_NewDoubleObj(object.value(QStringLiteral("dec")).toDouble()));
    dict_put(interp, dict, "alt", Tcl_NewDoubleObj(object.value(QStringLiteral("alt")).toDouble()));
    dict_put(interp, dict, "az", Tcl_NewDoubleObj(object.value(QStringLiteral("az")).toDouble()));
    if (object.contains(QStringLiteral("mag")))
        dict_put(interp, dict, "mag", Tcl_NewDoubleObj(object.value(QStringLiteral("mag")).toDouble()));
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
}

int cmd_get_planets(Shell& shell, int, Tcl_Obj* const*)
{
    Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
    for (astro::Body body : astro::all_bodies()) {
        std::string name = astro::body_name(body);
        Tcl_ListObjAppendElement(shell.interp(), list, Tcl_NewStringObj(name.c_str(), -1));
    }
    Tcl_SetObjResult(shell.interp(), list);
    return TCL_OK;
}

int cmd_find_objects(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2)
        return usage_error(shell, "find_objects TEXT");
    Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
    const QVariantList results = shell.sky()->search(arg_text(objv[1]));
    for (const QVariant& result : results) {
        QByteArray name = result.toMap().value(QStringLiteral("name")).toString().toUtf8();
        Tcl_ListObjAppendElement(shell.interp(), list, Tcl_NewStringObj(name.constData(), -1));
    }
    Tcl_SetObjResult(shell.interp(), list);
    return TCL_OK;
}

int cmd_report_location(Shell& shell, int, Tcl_Obj* const*)
{
    SkyBackend* sky = shell.sky();
    heading(QStringLiteral("Location"));
    if (!sky->locationSet()) {
        std::puts("  Not set. Use detect_location, or set_location LAT LON.\n");
        return TCL_OK;
    }
    std::printf("  Latitude   %.4f°\n", sky->latitude());
    std::printf("  Longitude  %.4f° (east is positive)\n", sky->longitude());
    std::printf("  Elevation  %.0f m\n", sky->elevation());
    std::printf("  Time zone  %s\n", qPrintable(format_offset(sky->utcOffsetMinutes())));
    if (sky->locationSource() == QLatin1String("network"))
        std::printf("  Source     approximate, from this device's internet address (%s)\n\n", qPrintable(sky->locationPlace()));
    else
        std::puts("  Source     entered by hand\n");
    return TCL_OK;
}

int cmd_get_location(Shell& shell, int, Tcl_Obj* const*)
{
    if (!shell.require_location())
        return TCL_ERROR;
    SkyBackend* sky = shell.sky();
    Tcl_Interp* interp = shell.interp();
    Tcl_Obj* dict = Tcl_NewDictObj();
    dict_put(interp, dict, "lat", Tcl_NewDoubleObj(sky->latitude()));
    dict_put(interp, dict, "lon", Tcl_NewDoubleObj(sky->longitude()));
    dict_put(interp, dict, "elev", Tcl_NewDoubleObj(sky->elevation()));
    dict_put(interp, dict, "utc_offset", Tcl_NewDoubleObj(sky->utcOffsetMinutes() / 60.0));
    dict_put(interp, dict, "source", Tcl_NewStringObj(sky->locationSource().toUtf8().constData(), -1));
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
}

int cmd_set_location(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    const char* usage = "set_location LAT LON ?ELEVATION_M? ?-utc_offset HOURS?";
    std::vector<Tcl_Obj*> positional;
    bool has_offset = false;
    double offset = 0.0;

    for (int i = 1; i < objc; i++) {
        if (arg_text(objv[i]) == QLatin1String("-utc_offset")) {
            if (++i >= objc || !arg_double(shell, objv[i], &offset))
                return usage_error(shell, usage);
            has_offset = true;
        } else {
            positional.push_back(objv[i]);
        }
    }
    if (positional.size() < 2 || positional.size() > 3)
        return usage_error(shell, usage);

    double lat;
    double lon;
    double elev = shell.sky()->elevation();
    if (!arg_double(shell, positional[0], &lat) || !arg_double(shell, positional[1], &lon))
        return TCL_ERROR;
    if (positional.size() == 3 && !arg_double(shell, positional[2], &elev))
        return TCL_ERROR;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return shell.error(QStringLiteral("Latitude must be -90 to 90, and longitude -180 to 180."));
    if (has_offset && (offset < -14.0 || offset > 14.0))
        return shell.error(QStringLiteral("The time zone must be -14 to 14 hours from UTC."));

    shell.sky()->setLocation(lat, lon, elev);
    if (has_offset)
        shell.sky()->setUtcOffsetMinutes(int(std::lround(offset * 60.0)));
    return cmd_report_location(shell, 1, objv);
}

int cmd_detect_location(Shell& shell, int, Tcl_Obj* const*)
{
    SkyBackend* sky = shell.sky();
    bool done = false;
    bool success = false;
    QMetaObject::Connection connection = QObject::connect(sky, &SkyBackend::detectFinished, sky, [&](bool ok) {
        done = true;
        success = ok;
    });
    std::puts("Asking ipapi.co where this device's internet address is...");
    std::fflush(stdout);
    sky->detectLocation();
    shell.wait_until([&done]() { return done; }, kDetectWaitMs);
    QObject::disconnect(connection);

    if (!done)
        return shell.error(QStringLiteral("No reply yet. Check the internet connection, or use set_location."));
    if (!success)
        return shell.error(sky->detectStatus());
    return cmd_report_location(shell, 1, nullptr);
}

int cmd_report_time(Shell& shell, int, Tcl_Obj* const*)
{
    SkyBackend* sky = shell.sky();
    shell.wait_for_clock_check();

    QDateTime utc = QDateTime::currentDateTimeUtc();
    QDateTime local = utc.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(sky->utcOffsetMinutes() * 60));

    heading(QStringLiteral("Time"));
    std::printf("  Local      %s (%s)\n", qPrintable(local.toString(QStringLiteral("ddd d MMM yyyy, HH:mm:ss"))), qPrintable(format_offset(sky->utcOffsetMinutes())));
    std::printf("  UTC        %s\n", qPrintable(utc.toString(QStringLiteral("ddd d MMM yyyy, HH:mm:ss"))));
    if (sky->locationSet() && sky->clockSet()) {
        double lst = astro::local_sidereal_deg(sky->observer(), astro::julian_now());
        std::printf("  Star time  %s\n", qPrintable(format_ra(lst)));
    }
    std::printf("  Clock      %s\n\n", qPrintable(sky->clockStatus()));
    return TCL_OK;
}

int cmd_set_time(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2)
        return usage_error(shell, "set_time \"YYYY-MM-DD HH:MM\" (in UTC)");
    QString problem = shell.sky()->setClockUtc(arg_text(objv[1]));
    if (!problem.isEmpty())
        return shell.error(problem);
    return cmd_report_time(shell, 1, objv);
}

int cmd_connect_mount(Shell& shell, int, Tcl_Obj* const*)
{
    MountClient* mount = shell.mount();
    std::puts("Connecting to the mount...");
    std::fflush(stdout);
    if (!shell.wait_until([mount]() { return mount->serverUp(); }, kConnectTimeoutMs / 3))
        return shell.error(QStringLiteral("The device service isn't running. Choose a mount with set_mount, which starts it."));
    if (!shell.wait_until([mount]() { return mount->mountFound(); }, kConnectTimeoutMs / 3))
        return shell.error(QStringLiteral("The device service found no mount. Check the cable and set_mount."));
    mount->connectMount();
    if (!shell.wait_until([mount]() { return mount->mountConnected(); }, kConnectTimeoutMs)) {
        QString detail = mount->message();
        return shell.error(detail.isEmpty() ? QStringLiteral("The mount didn't connect.") : detail);
    }
    std::printf("Connected to %s\n", qPrintable(mount->deviceName()));
    return TCL_OK;
}

int cmd_disconnect_mount(Shell& shell, int, Tcl_Obj* const*)
{
    shell.mount()->disconnectMount();
    return TCL_OK;
}

int cmd_report_mount(Shell& shell, int, Tcl_Obj* const*)
{
    MountClient* mount = shell.mount();
    // Give a newly started client a moment to reach the service, and the mount a
    // moment to report on the commands just sent to it
    shell.wait_until([mount]() { return mount->mountFound(); }, 1500);
    shell.wait_until([]() { return false; }, 400);

    heading(QStringLiteral("Mount"));
    std::printf("  Driver    %s\n", qPrintable(mount->driver()));
    std::printf("  Port      %s\n", qPrintable(mount->port()));
    std::printf("  Service   %s\n", mount->serverUp() ? "running" : "not reachable");
    if (mount->mountConnected()) {
        const char* state = mount->slewing() ? "slewing" : (mount->parked() ? "parked" : (mount->tracking() ? "tracking" : "idle"));
        std::printf("  State     %s\n", state);
        std::printf("  Pointing  %s, %s\n", qPrintable(format_ra(mount->ra())), qPrintable(format_dec(mount->dec())));
    } else {
        std::puts("  State     not connected");
    }
    if (!mount->message().isEmpty())
        std::printf("  Message   %s\n", qPrintable(mount->message()));
    std::puts("");
    return TCL_OK;
}

int cmd_list_mount_drivers(Shell& shell, int, Tcl_Obj* const*)
{
    heading(QStringLiteral("Mount drivers"));
    const QVariantList drivers = shell.mount()->drivers();
    for (const QVariant& driver : drivers) {
        QVariantMap map = driver.toMap();
        std::printf("  %-28s %s\n", qPrintable(map.value(QStringLiteral("binary")).toString()), qPrintable(map.value(QStringLiteral("label")).toString()));
    }
    std::puts("");
    return TCL_OK;
}

int cmd_set_mount(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc < 2 || objc > 3)
        return usage_error(shell, "set_mount DRIVER ?PORT?");
    MountClient* mount = shell.mount();
    QString driver = arg_text(objv[1]);
    bool known = false;
    const QVariantList drivers = mount->drivers();
    for (const QVariant& entry : drivers)
        known = known || entry.toMap().value(QStringLiteral("binary")).toString() == driver;
    if (!known)
        return shell.error(QStringLiteral("\"%1\" isn't a known driver. See list_mount_drivers.").arg(driver));

    mount->applyProfile(driver, objc == 3 ? arg_text(objv[2]) : mount->port(), mount->remoteAccess());
    std::printf("Restarting the device service with %s\n", qPrintable(driver));
    return TCL_OK;
}

int cmd_goto_object(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    const char* usage = "goto_object NAME ?-solar_filter_fitted?";
    bool filter = false;
    if (objc == 3 && arg_text(objv[2]) == QLatin1String("-solar_filter_fitted"))
        filter = true;
    else if (objc != 2)
        return usage_error(shell, usage);
    if (!shell.require_location())
        return TCL_ERROR;

    QVariantMap object;
    if (shell.find_object(arg_text(objv[1]), &object) != TCL_OK)
        return TCL_ERROR;

    // The same checks as the touch app's GoTo button, which live in MountClient
    QString problem = shell.mount()->gotoTarget(object.value(QStringLiteral("ra")).toDouble(),
                                                object.value(QStringLiteral("dec")).toDouble(), filter);
    if (!problem.isEmpty()) {
        // The touch app's wording points to its Settings page
        problem.replace(QStringLiteral("confirm it in Settings first"), QStringLiteral("add -solar_filter_fitted"));
        problem.replace(QStringLiteral("set the clock in Settings"), QStringLiteral("use set_time"));
        return shell.error(problem);
    }
    std::printf("Slewing to %s. Use stop_mount to stop it.\n", qPrintable(object.value(QStringLiteral("name")).toString()));
    return TCL_OK;
}

int cmd_sync_object(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2)
        return usage_error(shell, "sync_object NAME");
    if (!shell.require_location())
        return TCL_ERROR;
    QVariantMap object;
    if (shell.find_object(arg_text(objv[1]), &object) != TCL_OK)
        return TCL_ERROR;
    QString problem = shell.mount()->syncTarget(object.value(QStringLiteral("ra")).toDouble(), object.value(QStringLiteral("dec")).toDouble());
    if (!problem.isEmpty())
        return shell.error(problem);
    std::printf("The mount now knows it's pointing at %s.\n", qPrintable(object.value(QStringLiteral("name")).toString()));
    return TCL_OK;
}

int cmd_wait_mount(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    double seconds = kDefaultSlewWaitSeconds;
    if (objc == 2 && !arg_double(shell, objv[1], &seconds))
        return TCL_ERROR;
    if (objc > 2)
        return usage_error(shell, "wait_mount ?TIMEOUT_SECONDS?");

    MountClient* mount = shell.mount();
    // A slew takes a moment to be reported after it's requested
    shell.wait_until([mount]() { return mount->slewing(); }, 3000);
    if (!shell.wait_until([mount]() { return !mount->slewing(); }, int(seconds * 1000.0))) {
        return shell.error(g_interrupted ? QStringLiteral("Stopped waiting, but the mount is still moving. Use stop_mount to stop it.")
                                         : QStringLiteral("The mount is still moving. Use stop_mount to stop it."));
    }
    return TCL_OK;
}

int cmd_stop_mount(Shell& shell, int, Tcl_Obj* const*)
{
    shell.mount()->abort();
    std::puts("Stop sent to the mount.");
    return TCL_OK;
}

int cmd_park_mount(Shell& shell, int, Tcl_Obj* const*)
{
    shell.mount()->park();
    return TCL_OK;
}

int cmd_unpark_mount(Shell& shell, int, Tcl_Obj* const*)
{
    shell.mount()->unpark();
    return TCL_OK;
}

int cmd_set_tracking(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2 || (arg_text(objv[1]) != QLatin1String("on") && arg_text(objv[1]) != QLatin1String("off")))
        return usage_error(shell, "set_tracking on|off");
    shell.mount()->setTracking(arg_text(objv[1]) == QLatin1String("on"));
    return TCL_OK;
}

int cmd_calc_fov(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc < 4 || objc > 5)
        return usage_error(shell, "calc_fov FOCAL_MM SENSOR_WIDTH_MM SENSOR_HEIGHT_MM ?PIXEL_UM?");
    double focal, width, height, pixel = 0.0;
    if (!arg_double(shell, objv[1], &focal) || !arg_double(shell, objv[2], &width) || !arg_double(shell, objv[3], &height))
        return TCL_ERROR;
    if (objc == 5 && !arg_double(shell, objv[4], &pixel))
        return TCL_ERROR;
    if (focal <= 0.0 || width <= 0.0 || height <= 0.0)
        return shell.error(QStringLiteral("Sizes must be greater than zero."));

    std::printf("Field of view  %.2f° x %.2f°\n", 57.2958 * width / focal, 57.2958 * height / focal);
    if (pixel > 0.0)
        std::printf("Image scale    %.2f arcseconds per pixel\n", 206.265 * pixel / focal);
    return TCL_OK;
}

int cmd_calc_exposure(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc != 2 && objc != 4)
        return usage_error(shell, "calc_exposure FOCAL_MM ?APERTURE_MM PIXEL_UM?");
    double focal, aperture, pixel;
    if (!arg_double(shell, objv[1], &focal))
        return TCL_ERROR;
    if (focal <= 0.0)
        return shell.error(QStringLiteral("The focal length must be greater than zero."));

    std::puts("Longest exposure before stars trail, without tracking:");
    std::printf("  500 rule  %.1f s\n", 500.0 / focal);
    if (objc == 4) {
        if (!arg_double(shell, objv[2], &aperture) || !arg_double(shell, objv[3], &pixel))
            return TCL_ERROR;
        if (aperture <= 0.0 || pixel <= 0.0)
            return shell.error(QStringLiteral("Sizes must be greater than zero."));
        std::printf("  NPF rule  %.1f s\n", (35.0 * focal / aperture + 30.0 * pixel) / focal);
    }
    return TCL_OK;
}

int cmd_report_polar(Shell& shell, int, Tcl_Obj* const*)
{
    if (!shell.require_location() || shell.clock_error_if_unset() != TCL_OK)
        return TCL_ERROR;
    std::printf("Polaris's hour angle is %.2f h, shown as a clock with 0 h at the top.\n"
                "Check how your own polar scope is marked.\n", shell.sky()->polarisHourAngle() / 15.0);
    return TCL_OK;
}

int cmd_help(Shell& shell, int objc, Tcl_Obj* const* objv);
int cmd_quit(Shell&, int, Tcl_Obj* const*)
{
    Tcl_Exit(0);
    return TCL_OK;
}

const Command kCommands[] = {
    {"help", "Shell", "help ?COMMAND?", "List the commands, or explain one", "", cmd_help},
    {"quit", "Shell", "quit", "Leave the shell, the same as exit", "", cmd_quit},

    {"report_tonight", "Sky", "report_tonight", "Sunset, dark sky, the Moon and the best targets", "", cmd_report_tonight},
    {"report_planets", "Sky", "report_planets", "Where the Sun, Moon and planets are now", "", cmd_report_planets},
    {"report_object", "Sky", "report_object NAME", "Details of one object, and whether GoTo would allow it",
     "NAME can be a planet, star or deep-sky object. Quote names with spaces:\n  report_object Vega\n  report_object {Pleiades}", cmd_report_object},
    {"get_object", "Sky", "get_object NAME", "An object's details as a Tcl dictionary, for scripts",
     "Keys: name, type, ra, dec, alt, az and, if known, mag.\n  puts [dict get [get_object Vega] alt]", cmd_get_object},
    {"get_planets", "Sky", "get_planets", "The names of the Sun, Moon and planets, as a Tcl list", "", cmd_get_planets},
    {"find_objects", "Sky", "find_objects TEXT", "Names containing TEXT, as a Tcl list", "", cmd_find_objects},
    {"report_polar", "Sky", "report_polar", "Where Polaris should sit in a polar scope", "", cmd_report_polar},

    {"report_location", "Location and time", "report_location", "The location in use, and where it came from", "", cmd_report_location},
    {"get_location", "Location and time", "get_location", "The location as a Tcl dictionary, for scripts", "Keys: lat, lon, elev, utc_offset and source.", cmd_get_location},
    {"set_location", "Location and time", "set_location LAT LON ?ELEVATION_M? ?-utc_offset HOURS?", "Enter the location by hand",
     "East longitude is positive. This stops automatic detection, as your own location\nalways wins. detect_location turns it back on.", cmd_set_location},
    {"detect_location", "Location and time", "detect_location", "Find the location and time zone from the internet address",
     "Asks ipapi.co, which sees this device's internet address. The result is only\napproximate, often to the nearest city, and is wrong behind a VPN. Good enough\nto see what's up, but use set_location for precise polar alignment.", cmd_detect_location},
    {"report_time", "Location and time", "report_time", "Local time, UTC, star time, and whether the clock can be trusted",
     "The clock sets itself from the internet. A Raspberry Pi has no battery to keep\ntime while it's off, so without internet use set_time after each start.", cmd_report_time},
    {"set_time", "Location and time", "set_time \"YYYY-MM-DD HH:MM\"", "Set the clock by hand, in UTC", "", cmd_set_time},

    {"list_mount_drivers", "Telescope", "list_mount_drivers", "The mounts Astro OS can drive", "", cmd_list_mount_drivers},
    {"set_mount", "Telescope", "set_mount DRIVER ?PORT?", "Choose the mount and restart the device service",
     "  set_mount indi_simulator_telescope\n  set_mount indi_eqmod_telescope /dev/ttyUSB0", cmd_set_mount},
    {"connect_mount", "Telescope", "connect_mount", "Connect to the chosen mount", "", cmd_connect_mount},
    {"disconnect_mount", "Telescope", "disconnect_mount", "Disconnect from the mount", "", cmd_disconnect_mount},
    {"report_mount", "Telescope", "report_mount", "The mount's state and where it's pointing", "", cmd_report_mount},
    {"goto_object", "Telescope", "goto_object NAME ?-solar_filter_fitted?", "Slew to an object, with the same safety checks as the touch app",
     "GoTo is refused within 15° of the Sun, below the horizon, or if the clock isn't\nset. Pointing near the Sun without a proper filter can cause permanent blindness\nand destroy equipment. Only use -solar_filter_fitted if a filter is on the telescope.", cmd_goto_object},
    {"wait_mount", "Telescope", "wait_mount ?TIMEOUT_SECONDS?", "Wait for a slew to finish, for scripts",
     "Ctrl+C stops waiting but doesn't stop the mount. Use stop_mount for that.", cmd_wait_mount},
    {"stop_mount", "Telescope", "stop_mount", "Stop the mount immediately", "", cmd_stop_mount},
    {"sync_object", "Telescope", "sync_object NAME", "Tell the mount it's pointing at an object", "", cmd_sync_object},
    {"set_tracking", "Telescope", "set_tracking on|off", "Follow the sky, or stop following it", "", cmd_set_tracking},
    {"park_mount", "Telescope", "park_mount", "Move the mount to its parked position", "", cmd_park_mount},
    {"unpark_mount", "Telescope", "unpark_mount", "Release the mount from its parked position", "", cmd_unpark_mount},

    {"calc_fov", "Tools", "calc_fov FOCAL_MM SENSOR_WIDTH_MM SENSOR_HEIGHT_MM ?PIXEL_UM?", "Field of view and image scale", "", cmd_calc_fov},
    {"calc_exposure", "Tools", "calc_exposure FOCAL_MM ?APERTURE_MM PIXEL_UM?", "Longest exposure without tracking", "", cmd_calc_exposure},
};

int cmd_help(Shell& shell, int objc, Tcl_Obj* const* objv)
{
    if (objc > 2)
        return usage_error(shell, "help ?COMMAND?");

    if (objc == 2) {
        QString wanted = arg_text(objv[1]);
        for (const Command& command : kCommands) {
            if (wanted == QLatin1String(command.name)) {
                std::printf("\n%s%s%s\n%s\n", colour(kBrass), command.usage, colour(kPlain), command.summary);
                if (command.details[0] != '\0')
                    std::printf("\n%s\n", command.details);
                std::puts("");
                return TCL_OK;
            }
        }
        return shell.error(QStringLiteral("No help for \"%1\". If it's a standard Tcl command, see the Tcl documentation.").arg(wanted));
    }

    const char* group = "";
    for (const Command& command : kCommands) {
        if (std::strcmp(group, command.group) != 0) {
            group = command.group;
            heading(QString::fromUtf8(group));
        }
        std::printf("  %-20s %s\n", command.name, command.summary);
    }
    std::puts("\nThis is a full Tcl shell, so scripts can use variables, loops and procedures:\n"
              "  foreach body [get_planets] { puts \"$body [dict get [get_object $body] alt]\" }\n"
              "  if {[dict get [get_object M31] alt] > 30} { goto_object M31 }\n\n"
              "Run a script with: source my_script.tcl\n"
              "Tab completes commands and object names. Up and down recall earlier commands.\n"
              "Other programs, such as ls, also work here.\n");
    return TCL_OK;
}

int dispatch(ClientData data, Tcl_Interp*, int objc, Tcl_Obj* const objv[])
{
    auto* shell = static_cast<Shell*>(data);
    const char* name = Tcl_GetString(objv[0]);
    for (const Command& command : kCommands) {
        if (std::strcmp(name, command.name) == 0) {
            g_interrupted = 0;
            int code = command.run(*shell, objc, objv);
            std::fflush(stdout);
            shell->fix_file_owners();
            return code;
        }
    }
    return TCL_ERROR;
}

// ---------------------------------------------------------------------------
// Interactive use
// ---------------------------------------------------------------------------

Shell* g_shell = nullptr;
QString g_history_path;
std::string g_pending;
QSocketNotifier* g_input_notifier = nullptr;
std::vector<std::string> g_object_names;

std::string prompt_text()
{
    if (!g_pending.empty())
        return g_colour ? "\001\033[38;5;246m\002          ...\001\033[0m\002 " : "          ... ";
    return g_colour ? "\001\033[1;38;5;179m\002astro_sky>\001\033[0m\002 " : "astro_sky> ";
}

void on_line(char* line);

void show_prompt()
{
    rl_callback_handler_install(prompt_text().c_str(), on_line);
}

void print_outcome(int code)
{
    const char* result = Tcl_GetStringResult(g_shell->interp());
    if (code == TCL_OK || code == TCL_RETURN) {
        if (result[0] != '\0')
            std::printf("%s\n", result);
    } else if (code == TCL_ERROR) {
        g_shell->print_error(QString::fromUtf8(result));
    }
    std::fflush(stdout);
}

void on_line(char* line)
{
    rl_callback_handler_remove();

    if (!line) {
        std::puts("");
        Tcl_Exit(0);
    }

    if (line[0] != '\0') {
        g_pending += line;
        g_pending += '\n';
    }
    free(line);

    if (!g_pending.empty() && Tcl_CommandComplete(g_pending.c_str())) {
        std::string command = g_pending;
        g_pending.clear();
        command.pop_back();
        add_history(command.c_str());

        // Keep what is typed during a long command for the next prompt
        g_input_notifier->setEnabled(false);
        g_interrupted = 0;
        print_outcome(Tcl_RecordAndEval(g_shell->interp(), command.c_str(), 0));
        g_interrupted = 0;
        g_input_notifier->setEnabled(true);
    }
    show_prompt();
}

char* complete_word(const char* text, int state)
{
    static std::vector<std::string> matches;
    static std::size_t next;

    if (state == 0) {
        matches.clear();
        next = 0;
        std::size_t length = std::strlen(text);
        bool first_word = rl_point <= 0 || rl_line_buffer[0] == '\0' || !std::strchr(rl_line_buffer, ' ');
        if (first_word) {
            for (const Command& command : kCommands) {
                if (std::strncmp(command.name, text, length) == 0)
                    matches.push_back(command.name);
            }
            for (const char* extra : {"exit", "puts", "set", "foreach", "source", "proc", "if", "dict"}) {
                if (std::strncmp(extra, text, length) == 0)
                    matches.push_back(extra);
            }
        } else {
            for (const std::string& name : g_object_names) {
                if (strncasecmp(name.c_str(), text, length) == 0)
                    matches.push_back(name);
            }
        }
    }
    if (next >= matches.size())
        return nullptr;
    return strdup(matches[next++].c_str());
}

char** complete_line(const char* text, int, int)
{
    // A script's name is a file, so leave that to the normal completion
    if (std::strncmp(rl_line_buffer, "source ", 7) == 0)
        return nullptr;
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, complete_word);
}

void build_object_names(const SkyBackend& sky)
{
    for (astro::Body body : astro::all_bodies())
        g_object_names.push_back(astro::body_name(body));
    for (const CatalogObject& object : sky.catalog().objects()) {
        if (object.is_messier)
            g_object_names.push_back(object.name.toStdString());
    }
    for (const CatalogStar& star : sky.catalog().stars()) {
        if (star.proper_name && !star.name.contains(QLatin1Char(' ')))
            g_object_names.push_back(star.name.toStdString());
    }
}

void leave_shell(ClientData data)
{
    rl_callback_handler_remove();
    if (!g_history_path.isEmpty() && isatty(STDIN_FILENO)) {
        write_history(g_history_path.toLocal8Bit().constData());
        history_truncate_file(g_history_path.toLocal8Bit().constData(), 1000);
    }
    Tcl_Channel channel = Tcl_GetStdChannel(TCL_STDOUT);
    if (channel)
        Tcl_Flush(channel);
    std::fflush(stdout);
    if (g_shell) {
        g_shell->sky()->saveSettings();
        g_shell->fix_file_owners();
    }
    // Skip the destructors: INDI's threads may still be running
    std::_Exit(int(reinterpret_cast<intptr_t>(data)));
}

void print_banner(const Shell& shell)
{
    SkyBackend* sky = shell.sky();
    QString version = QStringLiteral("development");
    QFile file(QStringLiteral("/usr/lib/astro/version"));
    if (file.open(QIODevice::ReadOnly))
        version = QString::fromUtf8(file.readAll()).trimmed();

    QString where = QStringLiteral("not set, so use detect_location or set_location");
    if (sky->locationSet()) {
        where = QStringLiteral("%1, %2").arg(sky->latitude(), 0, 'f', 2).arg(sky->longitude(), 0, 'f', 2);
        if (sky->locationSource() == QLatin1String("network") && !sky->locationPlace().isEmpty())
            where = QStringLiteral("near %1 (%2, from the internet)").arg(sky->locationPlace(), where);
    }

    std::printf("\n%sAstro OS %s, astro_sky shell%s\n", colour(kBrass), qPrintable(version), colour(kPlain));
    std::printf("%sLocation%s  %s\n", colour(kDim), colour(kPlain), qPrintable(where));
    QString clock = sky->clockStatus();
    clock[0] = clock.at(0).toLower();
    std::printf("%sClock%s     %s, %s\n", colour(kDim), colour(kPlain), qPrintable(sky->clockText()), qPrintable(clock));
    std::printf("%sType help for the commands, and exit to leave.%s\n\n", colour(kDim), colour(kPlain));
}

void print_usage()
{
    std::puts("Usage: astro_sky [-f SCRIPT] [-x COMMANDS] [-no_init] [-verbose]\n"
              "\n"
              "With no options, starts the interactive shell. Commands in ~/.astro_sky_setup\n"
              "run first, unless -no_init is given.\n"
              "\n"
              "  -f SCRIPT    Run a Tcl script, then carry on at the prompt if there is a terminal\n"
              "  -x COMMANDS  Run Tcl commands, in the same way\n"
              "  -no_init     Skip ~/.astro_sky_setup\n"
              "  -verbose     Show the device library's messages\n"
              "\n"
              "End a script with exit to leave when it finishes. Example:\n"
              "  astro_sky -x \"report_tonight; exit\"");
}

} // namespace

int main(int argc, char* argv[])
{
    QString script;
    QString commands;
    bool use_setup_file = true;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-f") && i + 1 < argc) {
            script = QString::fromLocal8Bit(argv[++i]);
        } else if (!std::strcmp(argv[i], "-x") && i + 1 < argc) {
            commands = QString::fromLocal8Bit(argv[++i]);
        } else if (!std::strcmp(argv[i], "-no_init")) {
            use_setup_file = false;
        } else if (!std::strcmp(argv[i], "-verbose")) {
            verbose = true;
        } else {
            print_usage();
            return std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }

    // Buildroot's image has no UTF-8 locale, and Qt warns about that on every start,
    // even though its text output is UTF-8 regardless. Keep that off the shell's screen.
    int saved_stderr = dup(STDERR_FILENO);
    int null_stderr = open("/dev/null", O_WRONLY);
    if (!verbose && null_stderr >= 0)
        dup2(null_stderr, STDERR_FILENO);
    QCoreApplication app(argc, argv);
    if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }
    if (null_stderr >= 0)
        close(null_stderr);
    app.setApplicationName(QStringLiteral("astro_sky"));

    // Keep this program's output and Tcl's in order when both go to a file
    setvbuf(stdout, nullptr, _IOLBF, 0);

    bool interactive_input = isatty(STDIN_FILENO);
    g_colour = isatty(STDOUT_FILENO) && qEnvironmentVariableIsEmpty("NO_COLOR");

    QString config_dir = qEnvironmentVariable("ASTRO_CONFIG_DIR", QStringLiteral("/data/config"));
    QString catalog_dir = qEnvironmentVariable("ASTRO_CATALOG_DIR", QStringLiteral("/usr/share/astro"));
    QDir().mkpath(config_dir);

    SkyBackend sky(config_dir + QStringLiteral("/astro-ui.ini"), catalog_dir);
    Shell shell(&sky, config_dir, verbose);
    g_shell = &shell;
    build_object_names(sky);

    Tcl_FindExecutable(argv[0]);
    Tcl_Interp* interp = shell.interp();
    if (Tcl_Init(interp) != TCL_OK)
        shell.print_error(QStringLiteral("Tcl's library is incomplete: %1").arg(QString::fromUtf8(Tcl_GetStringResult(interp))));
    Tcl_SetExitProc(leave_shell);
    Tcl_Eval(interp, "fconfigure stdout -buffering line");
    for (const Command& command : kCommands)
        Tcl_CreateObjCommand(interp, command.name, dispatch, &shell, nullptr);

    // Ctrl+C clears the line or ends a wait, rather than leaving the shell
    if (pipe(g_interrupt_pipe) != 0)
        return 1;
    fcntl(g_interrupt_pipe[1], F_SETFL, O_NONBLOCK);
    struct sigaction action = {};
    action.sa_handler = on_interrupt;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, nullptr);

    // Find the location if it's missing, but don't hold things up for long
    sky.startAutoDetect();
    if (!sky.locationSet()) {
        bool done = false;
        QObject::connect(&sky, &SkyBackend::detectFinished, &app, [&done](bool) { done = true; });
        shell.wait_until([&done]() { return done; }, kFirstDetectWaitMs);
    }
    shell.wait_for_clock_check();

    bool stay_at_prompt = interactive_input;
    if (interactive_input) {
        Tcl_SetVar(interp, "tcl_interactive", "1", TCL_GLOBAL_ONLY);
        if (isatty(STDOUT_FILENO))
            print_banner(shell);
    }

    auto run_and_report = [&](int code) {
        if (code == TCL_ERROR) {
            shell.print_error(QString::fromUtf8(Tcl_GetStringResult(interp)));
            // A failed script must not be followed by whatever comes next
            if (!stay_at_prompt)
                Tcl_Exit(1);
        }
    };

    QString setup = QDir::homePath() + QStringLiteral("/.astro_sky_setup");
    if (use_setup_file && QFile::exists(setup))
        run_and_report(Tcl_EvalFile(interp, setup.toLocal8Bit().constData()));
    if (!script.isEmpty())
        run_and_report(Tcl_EvalFile(interp, script.toLocal8Bit().constData()));
    if (!commands.isEmpty())
        run_and_report(Tcl_Eval(interp, commands.toUtf8().constData()));

    if (!interactive_input) {
        // Commands from a pipe or a file, and only if none were given as options
        if (script.isEmpty() && commands.isEmpty()) {
            std::string line;
            std::string pending;
            while (std::getline(std::cin, line)) {
                pending += line;
                pending += '\n';
                if (!Tcl_CommandComplete(pending.c_str()))
                    continue;
                int code = Tcl_Eval(interp, pending.c_str());
                pending.clear();
                if (code == TCL_ERROR)
                    run_and_report(code);
                else if (Tcl_GetStringResult(interp)[0] != '\0')
                    std::printf("%s\n", Tcl_GetStringResult(interp));
            }
        }
        Tcl_Exit(0);
    }

    g_history_path = QDir::homePath() + QStringLiteral("/.astro_sky_history");
    read_history(g_history_path.toLocal8Bit().constData());
    rl_catch_signals = 0;
    rl_readline_name = "astro_sky";
    rl_attempted_completion_function = complete_line;

    QSocketNotifier interrupts(g_interrupt_pipe[0], QSocketNotifier::Read);
    QObject::connect(&interrupts, &QSocketNotifier::activated, &app, []() {
        char drained[16];
        if (read(g_interrupt_pipe[0], drained, sizeof drained) < 0)
            return;
        // While a command runs, its wait notices the interrupt by itself
        if (!g_input_notifier->isEnabled())
            return;
        g_interrupted = 0;
        g_pending.clear();
        rl_free_line_state();
        rl_callback_sigcleanup();
        rl_replace_line("", 0);
        std::puts("");
        rl_callback_handler_remove();
        show_prompt();
    });

    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    g_input_notifier = &input;
    QObject::connect(&input, &QSocketNotifier::activated, &app, []() { rl_callback_read_char(); });

    show_prompt();
    return app.exec();
}
