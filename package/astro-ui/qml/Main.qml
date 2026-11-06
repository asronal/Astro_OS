import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.VirtualKeyboard

Window {
    id: root

    property var target: ({})
    property bool solarFilterFitted: false
    property int page: sky.locationSet ? 0 : 4

    function selectTarget(object) {
        target = object
    }

    visible: true
    visibility: runWindowed ? Window.Windowed : Window.FullScreen
    width: 800
    height: 480
    title: "Astro OS"
    color: theme.background

    QtObject {
        id: theme
        readonly property bool night: sky.nightMode
        readonly property color background: night ? "#0a0000" : "#0b1020"
        readonly property color panel: night ? "#1c0303" : "#182036"
        readonly property color line: night ? "#4d0d0a" : "#33415f"
        readonly property color text: night ? "#e0392c" : "#e8edf8"
        readonly property color dim: night ? "#a12a20" : "#9aa8c4"
        readonly property color accent: night ? "#a81f15" : "#3b6fd4"
        readonly property color accentText: night ? "#ffc9c2" : "#ffffff"
        readonly property color danger: night ? "#d81b10" : "#c8281e"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: keyboard.active ? keyboard.height : 0
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            spacing: 8

            Text {
                text: "Astro OS"
                color: theme.text
                font.pixelSize: 18
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: mount.mountConnected
                      ? (mount.slewing ? "Slewing" : (mount.tracking ? "Tracking" : "Mount ready"))
                      : "No mount"
                color: theme.dim
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Text {
                text: sky.clockText
                color: theme.text
                font.pixelSize: 18
            }
            AstroButton {
                text: sky.nightMode ? "Day" : "Night"
                implicitHeight: 38
                onClicked: sky.nightMode = !sky.nightMode
            }
            AstroButton {
                text: "STOP"
                danger: true
                implicitHeight: 38
                implicitWidth: 92
                onClicked: mount.abort()
            }
        }

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

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.margins: 4
            spacing: 4

            Repeater {
                model: ["Tonight", "Sky", "Mount", "Tools", "Settings"]
                delegate: AstroButton {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    active: root.page === index
                    onClicked: root.page = index
                }
            }
        }
    }

    InputPanel {
        id: keyboard
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: active
    }
}
