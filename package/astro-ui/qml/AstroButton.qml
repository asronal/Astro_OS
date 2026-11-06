import QtQuick

Rectangle {
    id: button

    property string text: ""
    property bool active: false
    property bool danger: false
    signal clicked()
    signal pressed()
    signal released()

    implicitWidth: Math.max(label.implicitWidth + 28, 56)
    implicitHeight: 48
    radius: 6
    opacity: enabled ? 1.0 : 0.4
    color: danger ? theme.danger : (touch.pressed || active ? theme.accent : theme.panel)
    border.color: theme.line
    border.width: 1

    Text {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: button.danger || button.active || touch.pressed ? theme.accentText : theme.text
        font.pixelSize: 16
        font.bold: button.danger
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
