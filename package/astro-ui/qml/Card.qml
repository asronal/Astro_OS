import QtQuick

Rectangle {
    radius: 12
    border.color: theme.line
    border.width: 1
    gradient: Gradient {
        GradientStop { position: 0.0; color: theme.surfaceHigh }
        GradientStop { position: 1.0; color: theme.surface }
    }
}
