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

    // Held down from the keyboard, the same as a finger on it
    property bool keyHeld: false
    readonly property bool down: touch.pressed || keyHeld
    readonly property bool lit: style === "primary" || style === "danger" || active || down
    readonly property color contentColor: style === "danger" ? theme.dangerInk : (lit ? theme.accentInk : theme.text)

    implicitWidth: Math.max(row.implicitWidth + 28, 48)
    implicitHeight: 44
    radius: 10
    opacity: enabled ? 1.0 : 0.38
    color: style === "danger" ? (down || touch.containsMouse ? theme.dangerFillHigh : theme.dangerFill)
         : lit ? (down && !active || touch.containsMouse ? theme.accentHigh : theme.accent)
         : style === "quiet" ? (touch.containsMouse ? theme.surfaceHigh : "transparent")
         : (touch.containsMouse ? theme.line : theme.surfaceHigh)
    border.color: style === "quiet" || lit ? "transparent" : theme.line
    border.width: 1
    scale: down ? 0.97 : 1.0
    activeFocusOnTab: true

    function isActivateKey(key) {
        return key === Qt.Key_Return || key === Qt.Key_Enter
    }

    // Letting go must never be missed. A manual move runs until released, so it has to
    // stop if the focus goes elsewhere while the key is held down.
    function letGo() {
        if (keyHeld) {
            keyHeld = false
            released()
        }
    }

    onActiveFocusChanged: if (!activeFocus) letGo()
    onEnabledChanged: if (!enabled) letGo()
    onVisibleChanged: if (!visible) letGo()

    Keys.onShortcutOverride: (event) => event.accepted = isActivateKey(event.key)
    Keys.onPressed: (event) => {
        if (!isActivateKey(event.key))
            return
        event.accepted = true
        if (!event.isAutoRepeat) {
            keyHeld = true
            pressed()
        }
    }
    Keys.onReleased: (event) => {
        if (!isActivateKey(event.key) || event.isAutoRepeat || !keyHeld)
            return
        event.accepted = true
        letGo()
        clicked()
    }

    FocusRing { }

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
        hoverEnabled: true
        onClicked: button.clicked()
        onPressed: button.pressed()
        onReleased: button.released()
        onCanceled: button.released()
    }
}
