import QtQuick

// 270° speedometer gauge (slide-2-drive.jsx VelocityArc).
// Track + green target band + value arc + ticks, big value in the centre.
Item {
    id: root
    property real speed: 0
    property real maxSpeed: 6
    property real targetMin: 3
    property real targetMax: 5
    property bool available: true

    implicitWidth: 150
    implicitHeight: 150

    readonly property color valueColor: (speed >= targetMin && speed <= targetMax)
                                         ? theme.good
                                         : (speed > targetMax ? theme.warning : theme.primaryOnDark)

    Canvas {
        id: cv
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d"); ctx.reset()
            var w = width, h = height
            var cx = w / 2, cy = h / 2 + 4
            var r = Math.min(w, h) / 2 - 22
            if (r <= 0) return
            // -135°..+135° measured from straight up, clockwise
            var startA = -135, endA = 135, sweep = endA - startA
            function aToRad(a) { return (a - 90) * Math.PI / 180 }   // 0°=up
            function arc(a1, a2, color, lw) {
                ctx.beginPath()
                ctx.strokeStyle = color; ctx.lineWidth = lw; ctx.lineCap = "round"
                ctx.arc(cx, cy, r, aToRad(a1), aToRad(a2), false)
                ctx.stroke()
            }
            // track
            arc(startA, endA, theme.trackDim, 10)
            // target band
            var tA1 = startA + root.targetMin / root.maxSpeed * sweep
            var tA2 = startA + root.targetMax / root.maxSpeed * sweep
            arc(tA1, tA2, Qt.rgba(48/255,209/255,88/255,0.22), 10)
            // value arc
            if (root.available) {
                var frac = Math.max(0, Math.min(1, root.speed / root.maxSpeed))
                arc(startA, startA + frac * sweep, root.valueColor, 10)
            }
            // ticks
            ctx.strokeStyle = theme.bodyDim
            for (var v = 0; v <= root.maxSpeed; v++) {
                var a = aToRad(startA + v / root.maxSpeed * sweep)
                var x1 = cx + Math.cos(a) * (r + 7), y1 = cy + Math.sin(a) * (r + 7)
                var x2 = cx + Math.cos(a) * (r + 13), y2 = cy + Math.sin(a) * (r + 13)
                ctx.lineWidth = 1.2; ctx.globalAlpha = 0.7
                ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke()
            }
            ctx.globalAlpha = 1.0

            // number labels at 0 / mid / max
            ctx.fillStyle = theme.bodyDim
            ctx.font = "10px " + theme.monoFont.family
            ctx.textAlign = "center"; ctx.textBaseline = "middle"
            var labels = [0, root.maxSpeed / 2, root.maxSpeed]
            for (var li = 0; li < labels.length; li++) {
                var la = aToRad(startA + labels[li] / root.maxSpeed * sweep)
                var lx = cx + Math.cos(la) * (r + 22), ly = cy + Math.sin(la) * (r + 22)
                ctx.fillText(labels[li].toFixed(0), lx, ly)
            }
        }
        Connections { target: theme; function onLightModeChanged() { cv.requestPaint() } }
    }

    onSpeedChanged: cv.requestPaint()
    onAvailableChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 6
        spacing: 1
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.available ? root.speed.toFixed(1) : "—"
            color: root.valueColor
            font.family: theme.defaultFont.family
            font.weight: Font.Bold
            font.pixelSize: 56
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.available ? "km/h" : ""
            color: theme.bodyMuted
            font.family: theme.defaultFont.family
            font.pixelSize: 14
        }
    }
}
