import QtQuick

// Icons drawn as strokes, so that they take on the day and night colours
Canvas {
    id: glyph
    property string name: ""
    property real size: 22
    property color tint: theme.text
    width: size
    height: size
    onTintChanged: requestPaint()
    onNameChanged: requestPaint()
    onPaint: {
        var c = getContext("2d")
        var s = size / 24
        c.reset()
        c.scale(s, s)
        c.strokeStyle = tint
        c.fillStyle = tint
        c.lineWidth = 1.7
        c.lineCap = "round"
        c.lineJoin = "round"
        c.beginPath()
        if (name === "moon") {
            c.arc(12, 12, 8.5, -2.2, 2.2, false)
            c.quadraticCurveTo(16, 12, 7, 5.3)
            c.stroke()
        } else if (name === "sky") {
            var pts = [[4, 17], [9, 9], [14, 13], [20, 5]]
            c.moveTo(4, 17); c.lineTo(9, 9); c.lineTo(14, 13); c.lineTo(20, 5)
            c.stroke()
            for (var i = 0; i < pts.length; i++) {
                c.beginPath()
                c.arc(pts[i][0], pts[i][1], 2, 0, Math.PI * 2)
                c.fill()
            }
        } else if (name === "mount") {
            c.moveTo(4, 12); c.lineTo(18, 5); c.lineTo(20, 9); c.lineTo(6, 16); c.closePath()
            c.moveTo(12, 12.5); c.lineTo(8, 21)
            c.moveTo(12, 12.5); c.lineTo(16, 21)
            c.stroke()
        } else if (name === "tools") {
            c.moveTo(4, 7); c.lineTo(20, 7)
            c.moveTo(4, 12); c.lineTo(20, 12)
            c.moveTo(4, 17); c.lineTo(20, 17)
            c.stroke()
            var dots = [[9, 7], [16, 12], [7, 17]]
            for (var j = 0; j < dots.length; j++) {
                c.beginPath()
                c.arc(dots[j][0], dots[j][1], 2.4, 0, Math.PI * 2)
                c.fill()
            }
        } else if (name === "settings") {
            c.arc(12, 12, 3.2, 0, Math.PI * 2)
            for (var a = 0; a < 8; a++) {
                var t = a * Math.PI / 4
                c.moveTo(12 + 6.4 * Math.cos(t), 12 + 6.4 * Math.sin(t))
                c.lineTo(12 + 9 * Math.cos(t), 12 + 9 * Math.sin(t))
            }
            c.stroke()
            c.beginPath()
            c.arc(12, 12, 6.4, 0, Math.PI * 2)
            c.stroke()
        } else if (name === "eye") {
            c.moveTo(2.5, 12); c.quadraticCurveTo(12, 3, 21.5, 12); c.quadraticCurveTo(12, 21, 2.5, 12)
            c.stroke()
            c.beginPath()
            c.arc(12, 12, 2.6, 0, Math.PI * 2)
            c.fill()
        } else if (name === "search") {
            c.arc(10.5, 10.5, 6, 0, Math.PI * 2)
            c.moveTo(15, 15); c.lineTo(20.5, 20.5)
            c.stroke()
        } else if (name === "layers") {
            c.moveTo(12, 4); c.lineTo(21, 9); c.lineTo(12, 14); c.lineTo(3, 9); c.closePath()
            c.moveTo(3, 13.5); c.lineTo(12, 18.5); c.lineTo(21, 13.5)
            c.stroke()
        } else if (name === "clock") {
            c.arc(12, 12, 8.5, 0, Math.PI * 2)
            c.moveTo(12, 7); c.lineTo(12, 12); c.lineTo(15.5, 14.5)
            c.stroke()
        } else if (name === "rings") {
            c.arc(12, 12, 8.5, 0, Math.PI * 2)
            c.stroke()
            c.beginPath()
            c.arc(12, 12, 4, 0, Math.PI * 2)
            c.stroke()
        } else if (name === "info") {
            c.arc(12, 12, 8.5, 0, Math.PI * 2)
            c.moveTo(12, 11); c.lineTo(12, 16.5)
            c.stroke()
            c.beginPath()
            c.arc(12, 7.8, 1.1, 0, Math.PI * 2)
            c.fill()
        } else if (name === "close") {
            c.moveTo(6, 6); c.lineTo(18, 18)
            c.moveTo(18, 6); c.lineTo(6, 18)
            c.stroke()
        } else if (name === "stop") {
            c.moveTo(7, 7); c.lineTo(17, 7); c.lineTo(17, 17); c.lineTo(7, 17); c.closePath()
            c.fill()
        } else if (name === "plus" || name === "minus") {
            c.moveTo(6, 12); c.lineTo(18, 12)
            if (name === "plus") { c.moveTo(12, 6); c.lineTo(12, 18) }
            c.stroke()
        } else if (name === "up" || name === "down" || name === "left" || name === "right") {
            var turn = { up: 0, right: Math.PI / 2, down: Math.PI, left: -Math.PI / 2 }[name]
            c.translate(12, 12); c.rotate(turn)
            c.moveTo(-6, 3.5); c.lineTo(0, -3.5); c.lineTo(6, 3.5)
            c.stroke()
        }
    }
}
