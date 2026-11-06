import QtQuick

// The Astro OS mark: a star on a tilted orbit
Canvas {
    id: logo
    property real size: 28
    property color tint: theme.accent
    width: size
    height: size
    onTintChanged: requestPaint()
    onPaint: {
        var c = getContext("2d")
        var s = logo.size
        c.reset()
        c.translate(s / 2, s / 2)
        c.rotate(-0.5)
        c.strokeStyle = logo.tint
        c.lineWidth = s * 0.06
        c.beginPath()
        c.ellipse(-s * 0.46, -s * 0.2, s * 0.92, s * 0.4)
        c.stroke()
        c.rotate(0.5)
        // Four-point star
        c.fillStyle = logo.tint
        var r = s * 0.3
        var w = s * 0.055
        c.beginPath()
        c.moveTo(0, -r); c.quadraticCurveTo(w, -w, r, 0)
        c.quadraticCurveTo(w, w, 0, r); c.quadraticCurveTo(-w, w, -r, 0)
        c.quadraticCurveTo(-w, -w, 0, -r)
        c.fill()
        // The planet on its orbit
        c.beginPath()
        c.arc(s * 0.36, -s * 0.2, s * 0.07, 0, Math.PI * 2)
        c.fill()
    }
}
