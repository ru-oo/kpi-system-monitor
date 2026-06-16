import QtQuick

Canvas {
    id: root
    property var dataPoints: []
    property color lineColor: theme.primary
    property double minValue: 0
    property double maxValue: 100

    onDataPointsChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)

        if (dataPoints.length < 2) return

        var pts = dataPoints
        var w = width
        var h = height

        var min = minValue
        var max = maxValue
        
        // Auto-scale if needed
        if (max === min) {
            max = min + 1
        }

        var range = max - min
        var dx = w / (pts.length - 1)

        // Draw filled area gradient
        ctx.beginPath()
        var grad = ctx.createLinearGradient(0, 0, 0, h)
        var c = lineColor
        grad.addColorStop(0, Qt.rgba(c.r, c.g, c.b, 0.25))
        grad.addColorStop(1, Qt.rgba(c.r, c.g, c.b, 0.0))
        ctx.fillStyle = grad

        ctx.moveTo(0, h)
        for (var i = 0; i < pts.length; i++) {
            var normalizedVal = (pts[i] - min) / range
            if (normalizedVal > 1.0) normalizedVal = 1.0
            if (normalizedVal < 0.0) normalizedVal = 0.0
            ctx.lineTo(i * dx, h - (normalizedVal * h))
        }
        ctx.lineTo(w, h)
        ctx.closePath()
        ctx.fill()

        // Draw line
        ctx.beginPath()
        ctx.strokeStyle = lineColor
        ctx.lineWidth = 1.4
        ctx.lineJoin = "round"

        var lastY = 0
        for (var j = 0; j < pts.length; j++) {
            var normVal = (pts[j] - min) / range
            if (normVal > 1.0) normVal = 1.0
            if (normVal < 0.0) normVal = 0.0
            var y = h - (normVal * h)
            
            if (j === 0) {
                ctx.moveTo(j * dx, y)
            } else {
                ctx.lineTo(j * dx, y)
            }
            if (j === pts.length - 1) lastY = y
        }
        ctx.stroke()

        // Draw final dot
        ctx.beginPath()
        ctx.fillStyle = lineColor
        ctx.arc(w, lastY, 2.5, 0, 2 * Math.PI)
        ctx.fill()
    }
}
