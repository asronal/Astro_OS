import QtQuick

// Shows where the keyboard's focus is. Only while a key was the last thing used, as it
// would just be clutter to someone using a finger or a mouse.
Rectangle {
    anchors.fill: parent
    anchors.margins: -2
    z: 5
    radius: parent.radius !== undefined ? parent.radius + 2 : 8
    color: "transparent"
    border.color: theme.accentHigh
    border.width: 2
    visible: parent.activeFocus && inputMode.keyboardInUse
}
