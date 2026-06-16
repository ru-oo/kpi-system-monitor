import QtQuick

// Full circular steering dial with needle (slide-2-drive.jsx SteeringDial).
// ±25° range mapped to ±62.5° of needle travel (×2.5), tick marks, L/R labels.
Item {
    id: root
    property real deg: 0
    property bool available: true

    implicitWidth: 150
    implicitHeight: 150

    readonly property real clampedDeg: Math.max(-25, Math.min(25, deg))

    Canvas {
        id: cv
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d"); ctx.reset()
            var w = width, h = height
            var cx = w / 2, cy = h / 2
            var r = Math.min(w, h) / 2 - 4
            if (r <= 0) return

            // outer ring
            ctx.beginPath(); ctx.strokeStyle = theme.hairlineStrong; ctx.lineWidth = 1.2
            ctx.arc(cx, cy, r - 2, 0, 2 * Math.PI); ctx.stroke()

            // tick marks every 5° over ±25°
            for (var i = 0; i <= 10; i++) {
                var tv = -25 + i * 5
                var a = (tv * 2.5) * Math.PI / 180     // from straight up
                var inner = r - 14, outer = r - 4
                var x1 = cx + Math.sin(a) * inner, y1 = cy - Math.cos(a) * inner
                var x2 = cx + Math.sin(a) * outer, y2 = cy - Math.cos(a) * outer
                ctx.beginPath()
                ctx.strokeStyle = tv === 0 ? theme.bodyMuted : theme.bodyDim
                ctx.lineWidth = tv === 0 ? 1.6 : 0.9
                ctx.globalAlpha = (tv % 10 === 0) ? 1.0 : 0.5
                ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke()
            }
            ctx.globalAlpha = 1.0

            // needle
            var na = (root.clampedDeg * 2.5) * Math.PI / 180
            var nx = cx + Math.sin(na) * (r - 18)
            var ny = cy - Math.cos(na) * (r - 18)
            ctx.beginPath()
            ctx.strokeStyle = theme.primaryOnDark; ctx.lineWidth = 4; ctx.lineCap = "round"
            ctx.moveTo(cx, cy); ctx.lineTo(nx, ny); ctx.stroke()
            // hub
            ctx.beginPath(); ctx.fillStyle = theme.primaryOnDark; ctx.arc(cx, cy, 8, 0, 2 * Math.PI); ctx.fill()
            ctx.beginPath(); ctx.fillStyle = "#ffffff"; ctx.arc(cx, cy, 3, 0, 2 * Math.PI); ctx.fill()
        }
        Connections { target: theme; function onLightModeChanged() { cv.requestPaint() } }
    }

    onDegChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()

    // L / R labels
    Text { text: "L"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10
        anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter }
    Text { text: "R"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10
        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter }

    // value readout near the bottom
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        text: root.available ? ((root.deg >= 0 ? "+" : "") + root.deg.toFixed(1) + "°") : "—"
        color: theme.bodyText
        font.family: theme.defaultFont.family
        font.weight: Font.Bold
        font.pixelSize: 34
    }
}
