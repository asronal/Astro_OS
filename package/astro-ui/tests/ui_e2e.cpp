// Drives the real interface with real touch events, against INDI's telescope simulator.
//
// The point is the whole path from finger to mount: a planet chosen while the chart
// shows another date must slew to where the planet really is now.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml>
#include <qpa/qwindowsysteminterface.h>

#include <cmath>
#include <cstdio>
#include <functional>

#include "astro/sky.h"
#include "chart_math.h"
#include "input_mode.h"
#include "mount_client.h"
#include "sky_backend.h"
#include "sky_chart.h"

static const int kConnectTimeoutMs = 90000;
static const int kSlewTimeoutMs = 180000;

static int failures = 0;

static void report(const char* label, bool ok, const QString& detail = QString())
{
    if (!ok)
        failures++;
    std::printf("%-5s %-58s %s\n", ok ? "ok" : "FAIL", label, qPrintable(detail));
    std::fflush(stdout);
}

static bool wait_for(const std::function<bool()>& condition, int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (condition())
            return true;
    }
    return false;
}

static void settle(int ms)
{
    wait_for([]() { return false; }, ms);
}

class Finger {
public:
    explicit Finger(QQuickWindow* window) : m_window(window) { }

    void press(QPointF at) { send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton); }
    void move(QPointF at) { send(QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton); }
    void release(QPointF at) { send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton); }

    void tap(QPointF at)
    {
        press(at);
        settle(30);
        release(at);
        settle(200);
    }

    void tap(const char* name) { tap(center_of(name)); }

    // Items made by a Repeater can't be found with findChild, so walk what's on screen
    static QQuickItem* find_item(QQuickItem* parent, const QString& name)
    {
        const QList<QQuickItem*> children = parent->childItems();
        for (QQuickItem* child : children) {
            if (child->objectName() == name)
                return child;
            if (QQuickItem* found = find_item(child, name))
                return found;
        }
        return nullptr;
    }

    QPointF center_of(const char* name) const
    {
        auto* item = find_item(m_window->contentItem(), QString::fromUtf8(name));
        if (!item)
            std::printf("FAIL there is no %s to tap\n", name);
        return item ? item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)) : QPointF(-1.0, -1.0);
    }

private:
    void send(QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, at, at, m_window->mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(m_window, &event);
    }

    QQuickWindow* m_window;
};

int main(int argc, char* argv[])
{
    qputenv("QT_IM_MODULE", QByteArrayLiteral("qtvirtualkeyboard"));
    QGuiApplication app(argc, argv);
    QTemporaryDir config;

    SkyBackend sky(config.path() + QStringLiteral("/astro-ui.ini"),
                   qEnvironmentVariable("ASTRO_CATALOG_DIR", QStringLiteral("/usr/share/astro")));
    sky.setLocation(51.4769, -0.0005, 46.0);
    MountClient mount(&sky, config.path() + QStringLiteral("/astro-hub.conf"));
    InputMode input_mode;

    qmlRegisterType<SkyChart>("Astro", 1, 0, "SkyChart");
    qmlRegisterUncreatableType<SkyBackend>("Astro", 1, 0, "SkyBackend", QStringLiteral("Use the sky object"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("sky"), &sky);
    engine.rootContext()->setContextProperty(QStringLiteral("mount"), &mount);
    engine.rootContext()->setContextProperty(QStringLiteral("inputMode"), &input_mode);
    engine.rootContext()->setContextProperty(QStringLiteral("runWindowed"), true);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        std::puts("FAIL: the interface did not load");
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    // As a tap on a tab does. page starts as a binding, which setProperty would leave in
    // place, so that any later settings update would send the app back to the first page.
    auto go_to_page = [&](int number) { QQmlProperty::write(window, QStringLiteral("page"), number); };
    window->setVisibility(QWindow::Windowed);
    window->resize(800, 480);
    settle(2500);
    go_to_page(1);
    settle(500);

    auto* chart = window->findChild<SkyChart*>();
    auto* page = window->findChild<QObject*>(QStringLiteral("skyPage"));
    Finger finger(window);
    chart->setProperty("showAtmosphere", false);

    auto offset = [&]() { return chart->property("timeOffsetHours").toDouble(); };
    auto target = [&]() { return window->property("target").toMap(); };
    auto set_preview = [&](double hours) {
        double days = std::round(hours / 24.0);
        QMetaObject::invokeMethod(page, "setPreview", Q_ARG(QVariant, QVariant(int(days))), Q_ARG(QVariant, QVariant(hours - days * 24.0)));
    };
    auto chart_center = [&]() { return chart->mapToScene(QPointF(chart->width() / 2.0, chart->height() / 2.0)); };
    // Far from anything else tapped, so that the next tap starts afresh
    auto forget_last_tap = [&]() { chart->nextObjectAt(-1000.0, -1000.0); };

    report("Connects to the INDI server", wait_for([&]() { return mount.serverUp(); }, kConnectTimeoutMs));
    report("Finds the telescope driver", wait_for([&]() { return mount.mountFound(); }, kConnectTimeoutMs), mount.deviceName());
    mount.connectMount();
    report("Connects to the mount", wait_for([&]() { return mount.mountConnected(); }, kConnectTimeoutMs));
    settle(3000);

    // A planet GoTo allows now, which is well up at some hour about 20 days ahead, when
    // it will have moved enough to tell the two positions apart
    const double now = astro::julian_now();
    astro::Body chosen = astro::Body::Sun;
    double preview_hours = 0.0;
    for (astro::Body body : astro::all_bodies()) {
        if (chosen != astro::Body::Sun || body == astro::Body::Sun)
            continue;
        astro::BodyReport real = astro::body_position(body, sky.observer(), now);
        if (astro::check_goto(real.ra_deg, real.dec_deg, sky.observer(), now, astro::kDefaultGotoLimits, false) != astro::GotoVerdict::Allowed || real.alt_deg < 10.0)
            continue;
        for (double hours = 20.0 * 24.0; hours < 21.0 * 24.0; hours += 0.5) {
            astro::BodyReport later = astro::body_position(body, sky.observer(), now + hours / 24.0);
            if (later.alt_deg > 15.0 && chart::separation(real.ra_deg, real.dec_deg, later.ra_deg, later.dec_deg) > 1.0) {
                chosen = body;
                preview_hours = hours;
                break;
            }
        }
    }
    if (chosen == astro::Body::Sun) {
        // This depends on where the planets are. Try again later rather than guessing.
        std::puts("SKIP: no planet is usable at this moment, so nothing was tested. Run it again in a few hours.");
        return 3;
    }
    QString body_name = QString::fromStdString(astro::body_name(chosen));
    std::printf("Using %s, previewed %.1f days ahead\n", qPrintable(body_name), preview_hours / 24.0);

    // The time slider, by finger
    QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(sky.findExact(QStringLiteral("Vega")))));
    settle(200);
    finger.tap("timeButton");
    report("The clock button opens the time controls", page->property("timeOpen").toBool());
    QPointF slider = finger.center_of("timeSlider");
    finger.press(slider);
    settle(50);
    report("The middle of the slider is the time now", std::fabs(offset()) < 0.09, QString::number(offset()));
    for (int i = 1; i <= 8; i++) {
        finger.move(slider + QPointF(i * 15.0, 0.0));
        settle(30);
    }
    finger.release(slider + QPointF(120.0, 0.0));
    settle(300);
    double dragged = offset();
    report("Dragging right moves the time forward", dragged > 1.0 && dragged < 12.0, QString::number(dragged));
    report("It snaps to whole 5 minutes", std::fabs(dragged * 12.0 - std::round(dragged * 12.0)) < 1e-6, QString::number(dragged * 60.0) + " minutes");
    report("The header says it isn't showing now", window->property("skyPreviewText").toString().startsWith(QStringLiteral("Showing ")),
           window->property("skyPreviewText").toString());
    finger.tap("nowButton");
    report("Now returns to the present", offset() == 0.0);
    report("And the header stops saying so", window->property("skyPreviewText").toString().isEmpty());

    // Choose the planet on the chart, at the far-off time
    QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(QVariantMap())));
    set_preview(preview_hours);
    astro::BodyReport shown = astro::body_position(chosen, sky.observer(), astro::julian_now() + preview_hours / 24.0);
    chart->setCenterAz(shown.az_deg);
    chart->setCenterAlt(shown.alt_deg);
    chart->setFieldOfView(20.0);
    settle(500);
    forget_last_tap();
    finger.tap(chart_center());
    report("A tap finds the planet where the chart shows it", target().value(QStringLiteral("name")).toString() == body_name, target().value(QStringLiteral("name")).toString());

    astro::BodyReport real = astro::body_position(chosen, sky.observer(), astro::julian_now());
    double target_error = chart::separation(target().value(QStringLiteral("ra")).toDouble(), target().value(QStringLiteral("dec")).toDouble(), real.ra_deg, real.dec_deg);
    report("The target holds its real position, not the one shown", target_error < 0.05, QString::number(target_error) + " degrees");

    // While previewing, GoTo does nothing at all, including changing the selection
    QVariantMap before = target();
    double start_ra = mount.ra();
    double start_dec = mount.dec();
    finger.tap("gotoButton");
    settle(3000);
    report("GoTo during a preview doesn't move the mount", !mount.slewing() && chart::separation(mount.ra(), mount.dec(), start_ra, start_dec) < 0.2,
           QStringLiteral("moved %1 degrees").arg(chart::separation(mount.ra(), mount.dec(), start_ra, start_dec)));
    report("Nor does the tap fall through and change the target", target().value(QStringLiteral("name")) == before.value(QStringLiteral("name")));

    // Back to now, GoTo must reach where the planet really is
    QMetaObject::invokeMethod(page, "backToNow");
    settle(300);
    finger.tap("gotoButton");
    report("GoTo starts a slew", wait_for([&]() { return mount.slewing(); }, 20000));
    report("The slew finishes", wait_for([&]() { return !mount.slewing(); }, kSlewTimeoutMs));
    settle(2000);
    real = astro::body_position(chosen, sky.observer(), astro::julian_now());
    double reached = chart::separation(mount.ra(), mount.dec(), real.ra_deg, real.dec_deg);
    double from_preview = chart::separation(mount.ra(), mount.dec(), shown.ra_deg, shown.dec_deg);
    report("The mount is on the planet's real position", reached < 0.2, QString::number(reached) + " degrees away");
    report("And not where it was previewed", from_preview > 0.5, QString::number(from_preview) + " degrees away");
    report("With no complaint shown", page->property("result").toString().isEmpty(), page->property("result").toString());
    mount.abort();

    // Tapping again moves through what's there
    QVariantMap pleiades = sky.findExact(QStringLiteral("M45"));
    double best = 0.0;
    double best_alt = -90.0;
    for (double hours = -12.0; hours < 12.0; hours += 0.25) {
        double az, alt;
        astro::equatorial_to_horizontal(pleiades.value(QStringLiteral("ra")).toDouble(), pleiades.value(QStringLiteral("dec")).toDouble(),
                                        sky.observer(), astro::julian_now() + hours / 24.0, &az, &alt);
        if (alt > best_alt) {
            best_alt = alt;
            best = hours;
        }
    }
    QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(QVariantMap())));
    set_preview(best);
    chart->centerOn(pleiades.value(QStringLiteral("ra")).toDouble(), pleiades.value(QStringLiteral("dec")).toDouble());
    settle(300);
    forget_last_tap();
    finger.tap(chart_center());
    QString first_name = target().value(QStringLiteral("name")).toString();
    int first_index = target().value(QStringLiteral("cycleIndex")).toInt();
    finger.tap(chart_center());
    QString second_name = target().value(QStringLiteral("name")).toString();
    int second_index = target().value(QStringLiteral("cycleIndex")).toInt();
    report("A tap chooses the first thing there", first_index == 1, first_name);
    report("Tapping again chooses the next", second_index == 2 && second_name != first_name, second_name);

    // Details
    finger.tap("infoButton");
    settle(300);
    report("The info button opens the details", page->property("detailsOpen").toBool() && !page->property("details").toMap().value(QStringLiteral("rows")).toList().isEmpty());
    finger.tap("infoButton");
    report("And closes them again", !page->property("detailsOpen").toBool());

    // A switch on the layers panel, by finger
    QMetaObject::invokeMethod(page, "backToNow");
    finger.tap("layersButton");
    settle(200);
    report("The layers button opens its panel", page->property("layersOpen").toBool());
    bool milky_way_was = chart->property("showMilkyWay").toBool();
    finger.tap("switch: Milky Way");
    report("A switch turns a layer off", chart->property("showMilkyWay").toBool() != milky_way_was);
    finger.tap("switch: Milky Way");
    report("And on again", chart->property("showMilkyWay").toBool() == milky_way_was);
    finger.tap("layersButton");

    // Equipment, typed into the real form. The form scrolls, so bring it into view.
    go_to_page(4);
    settle(500);
    auto* scroll = window->findChild<QObject*>(QStringLiteral("settingsScroll"));
    auto scroll_to_form = [&]() {
        scroll->setProperty("contentY", std::max(0.0, scroll->property("contentHeight").toDouble() - scroll->property("height").toDouble()));
        settle(200);
    };
    auto type_into = [&](const char* name, const QString& text) {
        window->findChild<QObject*>(QString::fromLatin1(name))->setProperty("text", text);
    };
    auto problem = [&]() { return window->findChild<QObject*>(QStringLiteral("equipmentProblem"))->property("text").toString(); };

    scroll_to_form();
    type_into("telescopeField", QStringLiteral("not a number"));
    finger.tap("saveTelescopeButton");
    report("The form refuses a bad focal length", !problem().isEmpty() && sky.telescopeFocalMm() == 0.0, problem());
    type_into("telescopeField", QStringLiteral("1000"));
    finger.tap("saveTelescopeButton");
    report("And accepts a good one", problem().isEmpty() && sky.telescopeFocalMm() == 1000.0);

    type_into("eyepieceField", QStringLiteral("25"));
    finger.tap("addEyepieceButton");
    settle(200);
    report("An eyepiece can be added", problem().isEmpty() && sky.eyepieces().size() == 1);
    scroll_to_form();
    type_into("eyepieceField", QStringLiteral("500"));
    finger.tap("addEyepieceButton");
    report("A silly one is refused", !problem().isEmpty() && sky.eyepieces().size() == 1, problem());

    // The ring it made, switched by finger on the Sky page
    go_to_page(1);
    settle(500);
    finger.tap("ringsButton");
    settle(200);
    report("The ring is listed, and on", sky.eyepieces().first().toMap().value(QStringLiteral("on")).toBool());
    report("The chart received it", chart->rings().size() == 1);
    finger.tap("switch: 25 mm, 40×, 1.30°");
    settle(200);
    report("A switch turns the ring off", !sky.eyepieces().first().toMap().value(QStringLiteral("on")).toBool());
    finger.tap("ringsButton");

    // Real keys and wheel turns, delivered the way the window system delivers them
    auto key_down = [&](int key, const QString& text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(window, QEvent::KeyPress, key, modifiers, text);
    };
    auto key_up = [&](int key, const QString& text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(window, QEvent::KeyRelease, key, modifiers, text);
    };
    auto press_key = [&](int key, const QString& text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        key_down(key, text, modifiers);
        key_up(key, text, modifiers);
        settle(80);
    };
    auto type_text = [&](const QString& text) {
        for (QChar c : text)
            press_key(c.toUpper().unicode(), QString(c));
    };
    auto wheel = [&](QPointF at, int notches) {
        QWindowSystemInterface::handleWheelEvent(window, at, window->mapToGlobal(at), QPoint(), QPoint(0, notches * 120));
        QWindowSystemInterface::flushWindowSystemEvents();
        settle(80);
    };
    auto panel_open = [&](const char* name) { return page->property(name).toBool(); };
    auto press_tab_until = [&](const char* name) {
        for (int i = 0; i < 80; i++) {
            press_key(Qt::Key_Tab);
            QQuickItem* focused = window->activeFocusItem();
            if (focused && focused->objectName() == QString::fromUtf8(name))
                return true;
        }
        return false;
    };

    // An on-screen keyboard stays out of the way once a real one is in use
    QMetaObject::invokeMethod(page, "backToNow");
    QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(QVariantMap())));
    go_to_page(1);
    settle(300);
    report("The on-screen keyboard is offered before any key is pressed", input_mode.onScreenKeyboardWanted());

    press_key(Qt::Key_Escape);
    report("A real key shows that a keyboard is in use", input_mode.keyboardInUse() && !input_mode.onScreenKeyboardWanted());
    // Straight to the app's filter, as a window needs a real touch device to accept one
    QObject touched;
    QTouchEvent touch(QEvent::TouchBegin);
    QCoreApplication::sendEvent(&touched, &touch);
    report("A touch brings the on-screen keyboard back", input_mode.onScreenKeyboardWanted() && !input_mode.keyboardInUse());
    finger.tap(QPointF(5.0, 300.0));
    report("A mouse alone doesn't take it away, as there may be no way to type", input_mode.onScreenKeyboardWanted());
    press_key(Qt::Key_Escape);
    finger.press(QPointF(5.0, 300.0));
    finger.release(QPointF(5.0, 300.0));
    settle(100);
    report("Using the mouse hides the keyboard's focus marks", !input_mode.keyboardInUse());
    report("But leaves the on-screen keyboard away", !input_mode.onScreenKeyboardWanted());

    // Pages and night mode
    press_key(Qt::Key_1, QStringLiteral("1"));
    report("1 goes to the first page", window->property("page").toInt() == 0);
    press_key(Qt::Key_5, QStringLiteral("5"));
    report("5 goes to the last page", window->property("page").toInt() == 4);
    press_key(Qt::Key_2, QStringLiteral("2"));
    report("2 goes to the Sky page", window->property("page").toInt() == 1);
    settle(300);

    bool was_night = sky.nightMode();
    press_key(Qt::Key_N, QStringLiteral("n"));
    report("N changes night mode", sky.nightMode() != was_night);
    report("The pointer turns red at night", QGuiApplication::overrideCursor() != nullptr);
    {
        QImage pointer = InputMode::nightPointerImage();
        bool all_red = true;
        int painted = 0;
        for (int y = 0; y < pointer.height(); y++) {
            for (int x = 0; x < pointer.width(); x++) {
                QColor c = pointer.pixelColor(x, y);
                if (c.alpha() == 0)
                    continue;
                painted++;
                all_red = all_red && c.green() <= c.red() * 0.3 && c.blue() <= c.red() * 0.3;
            }
        }
        report("Every pixel of it is red", all_red && painted > 50, QString::number(painted) + " pixels");
    }
    press_key(Qt::Key_N, QStringLiteral("n"));
    report("And goes back to normal by day", QGuiApplication::overrideCursor() == nullptr);

    // Looking around
    chart->setCenterAz(180.0);
    chart->setCenterAlt(40.0);
    chart->setFieldOfView(60.0);
    press_key(Qt::Key_Right);
    report("Right looks further round the sky", chart->centerAz() > 180.0, QString::number(chart->centerAz()));
    press_key(Qt::Key_Left);
    press_key(Qt::Key_Left);
    report("Left looks back the other way", chart->centerAz() < 180.0, QString::number(chart->centerAz()));
    press_key(Qt::Key_Up);
    report("Up looks higher", chart->centerAlt() > 40.0, QString::number(chart->centerAlt()));
    press_key(Qt::Key_Down);
    press_key(Qt::Key_Down);
    report("Down looks lower", chart->centerAlt() < 40.0, QString::number(chart->centerAlt()));
    double fov = chart->fieldOfView();
    press_key(Qt::Key_Plus, QStringLiteral("+"), Qt::ShiftModifier);
    report("+ zooms in", chart->fieldOfView() < fov, QString::number(chart->fieldOfView()));
    fov = chart->fieldOfView();
    press_key(Qt::Key_Minus, QStringLiteral("-"));
    report("- zooms out", chart->fieldOfView() > fov, QString::number(chart->fieldOfView()));

    // The wheel
    fov = chart->fieldOfView();
    double az_before = chart->centerAz();
    wheel(QPointF(650.0, 150.0), 2);
    report("The wheel zooms in", chart->fieldOfView() < fov, QString::number(chart->fieldOfView()));
    report("Towards the pointer", chart->centerAz() != az_before, QString::number(chart->centerAz()));
    fov = chart->fieldOfView();
    wheel(QPointF(650.0, 150.0), -2);
    report("And back out", chart->fieldOfView() > fov, QString::number(chart->fieldOfView()));

    // Searching with only a keyboard. Typing an L mustn't open the layers panel.
    press_key(Qt::Key_F, QStringLiteral("f"));
    settle(200);
    report("F opens search", panel_open("searchOpen"));
    double az_typing = chart->centerAz();
    type_text(QStringLiteral("vega"));
    press_key(Qt::Key_Backspace);
    type_text(QStringLiteral("a"));
    report("Letters go into the box, not to shortcuts", !panel_open("layersOpen") && !panel_open("ringsOpen"));
    QVariantList results = page->property("searchResults").toList();
    report("Typing searches", !results.isEmpty(), QString::number(results.size()) + " found");
    press_key(Qt::Key_Down);
    press_key(Qt::Key_Up);
    report("Arrows move through the list, not the sky", chart->centerAz() == az_typing && page->property("searchIndex").toInt() == 0);
    press_key(Qt::Key_Return);
    settle(200);
    report("Enter chooses the result", target().value(QStringLiteral("name")).toString() == QStringLiteral("Vega"), target().value(QStringLiteral("name")).toString());
    report("And closes the panel", !panel_open("searchOpen"));
    press_key(Qt::Key_L, QStringLiteral("l"));
    report("Shortcuts work again afterwards", panel_open("layersOpen"));
    press_key(Qt::Key_Escape);
    report("Esc closes a panel", !panel_open("layersOpen"));

    // Details, then Esc backing out one step at a time
    press_key(Qt::Key_I, QStringLiteral("i"));
    settle(200);
    report("I opens the details", panel_open("detailsOpen"));
    press_key(Qt::Key_Escape);
    report("Esc closes them, and keeps the target", !panel_open("detailsOpen") && target().contains(QStringLiteral("name")));
    press_key(Qt::Key_Escape);
    report("Esc again clears the target", !target().contains(QStringLiteral("name")));

    // Time
    press_key(Qt::Key_BracketRight, QStringLiteral("]"));
    report("] moves an hour ahead", std::fabs(offset() - 1.0) < 1e-9 && panel_open("timeOpen"), QString::number(offset()));
    press_key(Qt::Key_BraceRight, QStringLiteral("}"), Qt::ShiftModifier);
    report("} moves a day ahead", std::fabs(offset() - 25.0) < 1e-9, QString::number(offset()));
    press_key(Qt::Key_BracketLeft, QStringLiteral("["));
    report("[ moves an hour back", std::fabs(offset() - 24.0) < 1e-9, QString::number(offset()));
    press_key(Qt::Key_Home);
    report("Home returns to the present", offset() == 0.0);
    press_key(Qt::Key_T, QStringLiteral("t"));
    report("T closes the time controls", !panel_open("timeOpen"));

    // Selecting by keyboard: whatever is at the centre, and again for the next
    set_preview(best);
    chart->centerOn(pleiades.value(QStringLiteral("ra")).toDouble(), pleiades.value(QStringLiteral("dec")).toDouble());
    chart->setFieldOfView(20.0);
    settle(300);
    forget_last_tap();
    press_key(Qt::Key_Return);
    report("Enter selects what's at the centre", target().value(QStringLiteral("cycleIndex")).toInt() == 1, target().value(QStringLiteral("name")).toString());
    press_key(Qt::Key_Return);
    report("Enter again moves to the next", target().value(QStringLiteral("cycleIndex")).toInt() == 2, target().value(QStringLiteral("name")).toString());
    report("The centre is marked while using keys", chart->showCenterMark());
    QMetaObject::invokeMethod(page, "backToNow");

    // A stray letter can't start a slew: no key but Enter on the button itself does that
    QMetaObject::invokeMethod(window, "selectTarget", Q_ARG(QVariant, QVariant(sky.bodyMap(chosen))));
    settle(200);
    window->contentItem()->forceActiveFocus();
    for (int key : {Qt::Key_G, Qt::Key_S, Qt::Key_Y, Qt::Key_O}) {
        press_key(key, QString(QChar(key).toLower()));
    }
    settle(1000);
    report("No letter key starts a slew", !mount.slewing());

    // GoTo by Tab and Enter, then STOP by Space
    report("Tab reaches GoTo", press_tab_until("gotoButton"));
    press_key(Qt::Key_Return);
    report("Enter on it starts a slew", wait_for([&]() { return mount.slewing(); }, 20000));
    press_key(Qt::Key_Space, QStringLiteral(" "));
    report("Space stops it", wait_for([&]() { return !mount.slewing(); }, 10000));

    // A held key moves the mount, and it stops if the focus goes elsewhere while held
    mount.setTracking(false);
    go_to_page(2);
    settle(400);
    report("Tab reaches a manual move button", press_tab_until("moveNorth"));
    double dec_start = mount.dec();
    key_down(Qt::Key_Return);
    settle(3000);
    double dec_moving = mount.dec();
    report("Holding Enter moves the mount", std::fabs(dec_moving - dec_start) > 0.01, QStringLiteral("%1 degrees").arg(dec_moving - dec_start));
    press_key(Qt::Key_Tab);
    settle(1500);
    double dec_after_tab = mount.dec();
    settle(2000);
    report("Moving the focus away while held stops it", std::fabs(mount.dec() - dec_after_tab) < 0.005, QStringLiteral("%1 degrees").arg(mount.dec() - dec_after_tab));
    key_up(Qt::Key_Return);
    settle(200);

    // Text fields can be reached, and the page follows the focus off its bottom
    go_to_page(4);
    settle(400);
    report("Tab reaches the last button in Settings", press_tab_until("showKeysButton"));
    QPointF focused_at = finger.center_of("showKeysButton");
    report("And Settings scrolls to keep it in view", focused_at.y() > 52.0 && focused_at.y() < window->height() - 58.0, QString::number(focused_at.y()));
    press_key(Qt::Key_Return);
    report("Enter presses it", window->property("helpOpen").toBool());
    press_key(Qt::Key_Escape);
    report("Esc closes the list of keys", !window->property("helpOpen").toBool());

    go_to_page(1);
    settle(300);

    // Leaving the page never leaves a preview running
    set_preview(5.0);
    settle(100);
    go_to_page(0);
    settle(300);
    report("Leaving the Sky page returns to the present", offset() == 0.0, QString::number(offset()));
    report("And nothing is left saying otherwise", window->property("skyPreviewText").toString().isEmpty());

    std::printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
