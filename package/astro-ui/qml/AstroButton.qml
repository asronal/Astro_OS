import QtQuick

// style: "plain", "primary", "danger" or "quiet"
Rectangle {
    id: button

    property string text: ""
    property string glyph: ""
    property string style: "plain"
    property bool active: false
    signal clicked()
    signal pressed()
    signal released()

    readonly property bool lit: style === "primary" || style === "danger" || active || touch.pressed
    readonly property color contentColor: style === "danger" ? theme.dangerInk : (lit ? theme.accentInk : theme.text)

    implicitWidth: Math.max(row.implicitWidth + 28, 48)
    implicitHeight: 44
    radius: 10
    opacity: enabled ? 1.0 : 0.38
    color: style === "danger" ? (touch.pressed ? theme.dangerHigh : theme.danger)
         : lit ? (touch.pressed && !active ? theme.accentHigh : theme.accent)
         : style === "quiet" ? "transparent" : theme.surfaceHigh
    border.color: style === "quiet" || lit ? "transparent" : theme.line
    border.width: 1
    scale: touch.pressed ? 0.97 : 1.0

    Behavior on scale { NumberAnimation { duration: 80 } }
    Behavior on color { ColorAnimation { duration: 120 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Glyph {
            visible: button.glyph.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: button.glyph
            size: 18
            tint: button.contentColor
        }
        AText {
            visible: button.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: button.contentColor
            font.pixelSize: 14
            weight: button.lit ? 600 : 500
        }
    }

    MouseArea {
        id: touch
        anchors.fill: parent
        onClicked: button.clicked()
        onPressed: button.pressed()
        onReleased: button.released()
        onCanceled: button.released()
    }
}
