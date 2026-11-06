import QtQuick

// Where Polaris should sit in a polar scope, as on a 12-hour clock face
Canvas {
    id: dial
    property real hourAngle: 0
    property real size: 118
    width: size
    height: size
    onHourAngleChanged: requestPaint()
    onWidthChanged: requestPaint()
    Component.onCompleted: requestPaint()
    Connections { target: sky; function onSettingsChanged() { dial.requestPaint() } }
    onPaint: {
        if (size < 8)
            return
        var c = getContext("2d")
        var r = size / 2 - 3
        c.reset()
        c.translate(size / 2, size / 2)
        c.strokeStyle = theme.line
        c.lineWidth = 1
        c.beginPath(); c.arc(0, 0, r, 0, Math.PI * 2); c.stroke()
        c.beginPath(); c.arc(0, 0, r * 0.68, 0, Math.PI * 2); c.stroke()
        c.strokeStyle = theme.dim
        for (var h = 0; h < 12; h++) {
            var t = h / 12 * Math.PI * 2
            c.beginPath()
            c.moveTo(Math.sin(t) * r, -Math.cos(t) * r)
            c.lineTo(Math.sin(t) * (r - (h % 3 === 0 ? 8 : 4)), -Math.cos(t) * (r - (h % 3 === 0 ? 8 : 4)))
            c.stroke()
        }
        // The pole
        c.strokeStyle = theme.dim
        c.beginPath(); c.moveTo(-5, 0); c.lineTo(5, 0); c.moveTo(0, -5); c.lineTo(0, 5); c.stroke()
        // Polaris
        var a = hourAngle / 360 * Math.PI * 2
        c.fillStyle = theme.accent
        c.beginPath()
        c.arc(Math.sin(a) * r * 0.68, -Math.cos(a) * r * 0.68, 5, 0, Math.PI * 2)
        c.fill()
    }
}
