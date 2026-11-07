import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.VirtualKeyboard

Window {
    id: root

    property var target: ({})
    property bool solarFilterFitted: false
    property int page: sky.locationSet ? 0 : 4
    readonly property bool skyPage: page === 1
    // Tapping empty sky hides the navigation, for an uninterrupted view
    property bool skyFullView: false
    readonly property bool navigationShown: !keyboard.active && !(skyPage && skyFullView)
    readonly property int headerHeight: 52
    readonly property int navigationHeight: navigationShown ? 58 : 0
    // Set while the Sky page shows a time other than now
    property string skyPreviewText: ""
    signal timeControlsRequested()

    // A target always holds its real position for now, wherever it came from, so that
    // a time preview on the chart can never be what gets sent to the mount
    function selectTarget(object) {
        target = object.name !== undefined ? sky.livePosition(object) : object
    }

    visible: true
    visibility: runWindowed ? Window.Windowed : Window.FullScreen
    width: 800
    height: 480
    title: "Astro OS"
    color: theme.background

    FontLoader { id: sansFont; source: "qrc:/data/fonts/IBMPlexSans.ttf" }
    FontLoader { id: monoFont; source: "qrc:/data/fonts/IBMPlexMono-Regular.ttf" }
    FontLoader { source: "qrc:/data/fonts/IBMPlexMono-Medium.ttf" }
    FontLoader { id: displayFont; source: "qrc:/data/fonts/SpaceGrotesk.ttf" }

    // Midnight and brass by day. At night every colour is red, because any other
    // colour would spoil the eye's adaptation to the dark.
    QtObject {
        id: theme
        readonly property bool night: sky.nightMode
        readonly property string sansFamily: sansFont.name
        readonly property string monoFamily: monoFont.name
        readonly property string displayFamily: displayFont.name
        readonly property color background: night ? "#060000" : "#070a14"
        readonly property color backgroundHigh: night ? "#100101" : "#0d1324"
        readonly property color surface: night ? "#110202" : "#10162a"
        readonly property color surfaceHigh: night ? "#1a0404" : "#171e36"
        readonly property color line: night ? "#3c0b08" : "#262f4b"
        readonly property color text: night ? "#e23a2c" : "#eef1f8"
        readonly property color dim: night ? "#c2382a" : "#8e99b3"
        readonly property color accent: night ? "#d02c1c" : "#e9b872"
        readonly property color accentHigh: night ? "#e83726" : "#f3cb90"
        readonly property color accentInk: night ? "#ffd5cf" : "#17110a"
        readonly property color danger: night ? "#e01f12" : "#e5484d"
        readonly property color dangerHigh: night ? "#ff3323" : "#f2666a"
        readonly property color dangerInk: night ? "#ffe0dc" : "#ffffff"
        // Darker than the text colours, so that a button's label stands out on it
        readonly property color dangerFill: night ? "#b8180c" : "#cf3036"
        readonly property color dangerFillHigh: night ? "#d42014" : "#e0474c"
        readonly property color good: night ? "#c9301f" : "#76c4ba"
        readonly property color moonLit: night ? "#c9301f" : "#e8e4d8"
        readonly property color moonDark: night ? "#1a0404" : "#1b2138"
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.backgroundHigh }
            GradientStop { position: 0.6; color: theme.background }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: keyboard.active ? keyboard.height : 0
        spacing: 0

        // Header. On the Sky page the chart continues underneath it.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: root.headerHeight
            z: 2

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.height + 18
                visible: root.skyPage
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.82) }
                    GradientStop { position: 1.0; color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.0) }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 10

                Logo { size: 30 }
                AText {
                    visible: !root.skyPage
                    text: "ASTRO"
                    style: "display"
                    weight: 600
                    font.pixelSize: 17
                    font.letterSpacing: 3.2
                }

                Rectangle {
                    Layout.leftMargin: 6
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: statusRow.implicitWidth + 20
                    radius: 13
                    color: theme.surface
                    border.color: theme.line
                    // Goes straight to where the clock can be set
                    MouseArea {
                        anchors.fill: parent
                        enabled: !sky.clockSet
                        onClicked: root.page = 4
                    }
                    Row {
                        id: statusRow
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 7; height: 7; radius: 4
                            color: !sky.clockSet ? theme.danger : mount.mountConnected ? (mount.slewing ? theme.accent : theme.good) : theme.dim
                        }
                        AText {
                            text: !sky.clockSet ? "Clock not set"
                                : !mount.mountConnected ? "No mount"
                                : mount.slewing ? "Slewing"
                                : mount.tracking ? "Tracking" : "Mount ready"
                            color: theme.dim
                            font.pixelSize: 12
                            weight: 500
                        }
                    }
                }
                // Up here, beside the real clock, so that no panel can ever cover it
                Rectangle {
                    objectName: "previewChip"
                    visible: root.skyPage && root.skyPreviewText.length > 0
                    Layout.preferredHeight: 30
                    Layout.preferredWidth: previewText.implicitWidth + 24
                    radius: 15
                    color: theme.accent
                    AText {
                        id: previewText
                        anchors.centerIn: parent
                        color: theme.accentInk
                        font.pixelSize: 13
                        weight: 600
                        text: root.skyPreviewText
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.timeControlsRequested()
                    }
                }

                Item { Layout.fillWidth: true }

                AText {
                    text: sky.clockText
                    style: "data"
                    font.pixelSize: 19
                }
                AstroButton {
                    glyph: "eye"
                    style: "quiet"
                    active: sky.nightMode
                    implicitWidth: 46
                    implicitHeight: 44
                    onClicked: sky.nightMode = !sky.nightMode
                }
                AstroButton {
                    glyph: "stop"
                    text: "STOP"
                    style: "danger"
                    implicitHeight: 44
                    implicitWidth: 104
                    onClicked: mount.abort()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.line; visible: !root.skyPage }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.page

            TonightPage { }
            SkyPage { }
            MountPage { }
            ToolsPage { }
            SettingsPage { }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.line; visible: root.navigationShown && !root.skyPage }

        // Navigation
        RowLayout {
            visible: root.navigationShown
            z: 2
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: 58
            spacing: 0

            Repeater {
                model: [
                    { label: "Tonight", glyph: "moon" },
                    { label: "Sky", glyph: "sky" },
                    { label: "Mount", glyph: "mount" },
                    { label: "Tools", glyph: "tools" },
                    { label: "Settings", glyph: "settings" }
                ]
                delegate: Rectangle {
                    id: tab
                    color: root.skyPage ? Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.88) : "transparent"
                    required property int index
                    required property var modelData
                    readonly property bool current: root.page === index
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Rectangle {
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 34
                        height: 2
                        radius: 1
                        color: theme.accent
                        visible: tab.current
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Glyph {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: tab.modelData.glyph
                            size: 22
                            tint: tab.current ? theme.accent : theme.dim
                        }
                        AText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: tab.modelData.label
                            font.pixelSize: 12
                            weight: tab.current ? 600 : 400
                            color: tab.current ? theme.accent : theme.dim
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.page = tab.index
                    }
                }
            }
        }
    }

    // A brief branded start
    Rectangle {
        id: splash
        anchors.fill: parent
        color: theme.background
        z: 10
        Column {
            anchors.centerIn: parent
            spacing: 14
            Logo { anchors.horizontalCenter: parent.horizontalCenter; size: 84 }
            AText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "ASTRO"
                style: "display"
                weight: 600
                font.pixelSize: 30
                font.letterSpacing: 8
            }
            AText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "An observatory in your hands"
                color: theme.dim
                font.pixelSize: 13
            }
        }
        NumberAnimation on opacity { id: splashFade; running: false; to: 0; duration: 500 }
        onOpacityChanged: if (opacity === 0) visible = false
        Timer { interval: 1300; running: true; onTriggered: splashFade.start() }
    }

    InputPanel {
        id: keyboard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: active
    }
}
