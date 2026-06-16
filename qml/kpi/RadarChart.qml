import QtQuick

Canvas {
    id: root
    property double obstacleDistM: 10.0
    property double obstacleAngleDeg: 0.0
    property string obstacleClass: "car"
    property bool obstacleClose: obstacleDistM < 4.0
    property bool detected: false        // draw the blip only on a real detection

    onObstacleDistMChanged: requestPaint()
    onObstacleAngleDegChanged: requestPaint()
    onDetectedChanged: requestPaint()

    // Repaint on theme change so light-mode rings stay visible
    Connections {
        target: theme
        function onLightModeChanged() { root.requestPaint() }
    }

    property double sweepAngle: 0
    NumberAnimation on sweepAngle {
        from: 0; to: Math.PI * 2
        duration: 2500
        loops: Animation.Infinite
        running: true
    }
    onSweepAngleChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)

        var cx = width / 2
        var cy = height / 2
        var r = Math.min(cx, cy) - 20
        // During the first layout pass width/height can be 0, which makes r
        // negative → ctx.arc() throws "Incorrect argument radius". Bail until
        // the canvas has a real size.
        if (r <= 0) return

        // Theme-aware grid alphas
        // Dark: subtle white at 6-8%. Light: stronger black at 12-16% so it stays readable.
        var ringStroke  = theme.lightMode ? Qt.rgba(0, 0, 0, 0.16) : Qt.rgba(1, 1, 1, 0.08)
        var axisStroke  = theme.lightMode ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.06)
        var labelFill   = theme.lightMode ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(1, 1, 1, 0.32)

        // Concentric rings (3m, 6m, 9m, 12m)
        ctx.strokeStyle = ringStroke
        ctx.lineWidth = 1
        var rings = [0.25, 0.5, 0.75, 1.0]
        for (var i = 0; i < rings.length; i++) {
            ctx.beginPath()
            ctx.arc(cx, cy, r * rings[i], 0, 2 * Math.PI)
            ctx.stroke()
        }

        // Crosshairs
        ctx.strokeStyle = axisStroke
        ctx.beginPath()
        ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy)
        ctx.moveTo(cx, cy - r); ctx.lineTo(cx, cy + r)
        ctx.stroke()

        // Distance labels
        ctx.fillStyle = labelFill
        ctx.font = "10px " + theme.monoFont.family
        var labels = [3, 6, 9, 12]
        for (var j = 0; j < labels.length; j++) {
            ctx.fillText(labels[j] + "m", cx + 4, cy - r * (j + 1) / 4 - 2)
        }

        // Radar sweep sector (60deg wedge)
        ctx.beginPath()
        ctx.moveTo(cx, cy)
        var startAngle = sweepAngle - Math.PI/2
        var endAngle = startAngle + Math.PI/3
        ctx.arc(cx, cy, r, startAngle, endAngle)
        ctx.lineTo(cx, cy)
        ctx.fillStyle = theme.lightMode ? Qt.rgba(0/255, 102/255, 204/255, 0.08)
                                        : Qt.rgba(41/255, 151/255, 255/255, 0.08)
        ctx.fill()

        ctx.beginPath()
        ctx.arc(cx, cy, r, startAngle, endAngle)
        ctx.strokeStyle = theme.lightMode ? Qt.rgba(0/255, 102/255, 204/255, 0.22)
                                          : Qt.rgba(41/255, 151/255, 255/255, 0.18)
        ctx.stroke()

        // Center dot (vehicle)
        ctx.beginPath()
        ctx.arc(cx, cy, 4, 0, 2 * Math.PI)
        ctx.fillStyle = theme.primaryOnDark
        ctx.fill()
        ctx.beginPath()
        ctx.arc(cx, cy, 8, 0, 2 * Math.PI)
        ctx.strokeStyle = theme.primaryOnDark
        ctx.stroke()

        // Obstacle blip — only when there is a real detection (conf > 0)
        if (detected) {
            var maxDist = 12.0
            var distPx = Math.min(obstacleDistM, maxDist) / maxDist * r
            var rad = (obstacleAngleDeg - 90) * Math.PI / 180
            var ox = cx + Math.cos(rad) * distPx
            var oy = cy + Math.sin(rad) * distPx

            ctx.fillStyle = obstacleClose ? theme.warning : theme.bodyText
            ctx.beginPath()
            ctx.rect(ox - 10, oy - 6, 20, 12)
            ctx.fill()

            ctx.fillStyle = theme.bodyMuted
            ctx.fillText(obstacleClass, ox + 14, oy + 4)
        }
    }
}
