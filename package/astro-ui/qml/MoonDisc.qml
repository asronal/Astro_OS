import QtQuick

// The Moon's current shape, lit from the right while waxing
Canvas {
    id: disc
    property real lit: 0.5
    property bool waxing: true
    property real size: 72
    width: size
    height: size
    onLitChanged: requestPaint()
    onWaxingChanged: requestPaint()
    Connections { target: sky; function onSettingsChanged() { disc.requestPaint() } }
    onPaint: {
        if (size < 8)
            return
        var c = getContext("2d")
        var r = size / 2 - 1
        c.reset()
        c.translate(size / 2, size / 2)
        c.fillStyle = theme.moonDark
        c.beginPath(); c.arc(0, 0, r, 0, Math.PI * 2); c.fill()

        // The terminator is half an ellipse, from pole to pole
        c.fillStyle = theme.moonLit
        c.save()
        if (!waxing)
            c.scale(-1, 1)
        c.beginPath()
        c.arc(0, 0, r, -Math.PI / 2, Math.PI / 2, false)
        var bulge = r * (1 - 2 * lit)
        c.bezierCurveTo(bulge * 1.33, r, bulge * 1.33, -r, 0, -r)
        c.closePath()
        c.fill()
        c.restore()
    }
}
