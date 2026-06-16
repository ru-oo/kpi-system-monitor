import QtQuick

// Task-Manager-style rolling resource graph: two series (GPU + CPU) on a fixed
// 0–100% scale with a faint horizontal grid. Points are pushed by the page on a
// fixed cadence so the X axis is time. Display-only (theme colours, no judgment).
Canvas {
    id: root
    property var gpuPoints: []
    property var cpuPoints: []
    property color gpuColor: theme.primaryOnDark
    property color cpuColor: theme.good

    onGpuPointsChanged: requestPaint()
    onCpuPointsChanged: requestPaint()
    Connections { target: theme; function onLightModeChanged() { root.requestPaint() } }

    onPaint: {
        var ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        var w = width, h = height
        if (w <= 1 || h <= 1) return

        // Horizontal grid at 25/50/75% (and a faint baseline at 0/100).
        ctx.strokeStyle = theme.lightMode ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.07)
        ctx.lineWidth = 1
        for (var g = 1; g < 4; g++) {
            var gy = h * g / 4
            ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke()
        }

        // GPU — filled area + line.
        if (gpuPoints && gpuPoints.length > 1) {
            var dxG = w / (gpuPoints.length - 1)
            var grad = ctx.createLinearGradient(0, 0, 0, h)
            grad.addColorStop(0, Qt.rgba(gpuColor.r, gpuColor.g, gpuColor.b, 0.22))
            grad.addColorStop(1, Qt.rgba(gpuColor.r, gpuColor.g, gpuColor.b, 0.0))
            ctx.fillStyle = grad
            ctx.beginPath(); ctx.moveTo(0, h)
            for (var i = 0; i < gpuPoints.length; i++) {
                var vg = Math.max(0, Math.min(100, gpuPoints[i]))
                ctx.lineTo(i * dxG, h - (vg / 100) * h)
            }
            ctx.lineTo(w, h); ctx.closePath(); ctx.fill()

            ctx.beginPath(); ctx.strokeStyle = gpuColor; ctx.lineWidth = 1.6; ctx.lineJoin = "round"
            for (var j = 0; j < gpuPoints.length; j++) {
                var vg2 = Math.max(0, Math.min(100, gpuPoints[j]))
                var yg = h - (vg2 / 100) * h
                if (j === 0) ctx.moveTo(0, yg); else ctx.lineTo(j * dxG, yg)
            }
            ctx.stroke()
            var lastG = Math.max(0, Math.min(100, gpuPoints[gpuPoints.length - 1]))
            ctx.beginPath(); ctx.fillStyle = gpuColor; ctx.arc(w, h - (lastG / 100) * h, 2.5, 0, 2 * Math.PI); ctx.fill()
        }

        // CPU — line only (so it stays readable over the GPU fill).
        if (cpuPoints && cpuPoints.length > 1) {
            var dxC = w / (cpuPoints.length - 1)
            ctx.beginPath(); ctx.strokeStyle = cpuColor; ctx.lineWidth = 1.6; ctx.lineJoin = "round"
            for (var k = 0; k < cpuPoints.length; k++) {
                var vc = Math.max(0, Math.min(100, cpuPoints[k]))
                var yc = h - (vc / 100) * h
                if (k === 0) ctx.moveTo(0, yc); else ctx.lineTo(k * dxC, yc)
            }
            ctx.stroke()
            var lastC = Math.max(0, Math.min(100, cpuPoints[cpuPoints.length - 1]))
            ctx.beginPath(); ctx.fillStyle = cpuColor; ctx.arc(w, h - (lastC / 100) * h, 2.5, 0, 2 * Math.PI); ctx.fill()
        }
    }
}
