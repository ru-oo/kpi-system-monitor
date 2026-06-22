import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// SwipeView page 1 — AI Inference.
// Three stacked bands that together fill the whole viewport. Heights are
// computed as explicit fractions of the available height (NOT Layout.fillHeight
// ratios — QML distributes fillHeight extra space equally, not by ratio, which
// collapsed the panels in an earlier revision).
Item {
    id: page

    property var historyInference: []
    property var historyRatio: []

    // GPU/CPU usage are bursty instantaneous values (0x102 @10Hz). Smooth the
    // DISPLAY with an EMA (~1s) and keep a 30s rolling history for the graph
    // below. Judgment/thresholds are unaffected — these feed only the gauge +
    // graph (inference latency + GPU temp stay RAW, see below).
    property real gpuSmooth: 0
    property real cpuSmooth: 0
    property var  gpuHist: []
    property var  cpuHist: []

    // iPad's landscape page viewport (~520px after the fixed top chrome) is far
    // shorter than this page's content needs (~880px), so the fixed-fraction
    // bands collapsed and the hero / metric / accuracy bands overlapped. The
    // content lives in a Flickable that keeps a comfortable minimum height and
    // scrolls when the viewport is shorter — nothing overlaps on iPad, and on a
    // tall desktop window the bands still fill the viewport exactly.
    readonly property real minContentHeight: 1120   // +220 for the resource-history band

    Connections {
        target: kpiData
        function onKpiChanged() {
            var maxPts = 50
            var inf = page.historyInference.slice()
            inf.push(kpiData.inferenceMs)
            if (inf.length > maxPts) inf.shift()
            page.historyInference = inf

            var ratio = config.ptBaselineMs / Math.max(kpiData.inferenceMs, 1)
            var rat = page.historyRatio.slice()
            rat.push(ratio)
            if (rat.length > maxPts) rat.shift()
            page.historyRatio = rat

            infChart.dataPoints = page.historyInference
            ratioChart.dataPoints = page.historyRatio
        }
    }

    // EMA-smooth GPU/CPU (~0.8s) and append to the rolling history @5Hz.
    Timer {
        interval: 200; running: true; repeat: true
        onTriggered: {
            if (!kpiData.hasRealtimeKpi) return
            var a = 0.25   // EMA factor → ~0.8s time constant at 200ms cadence
            page.gpuSmooth += a * (kpiData.gpuPct - page.gpuSmooth)
            page.cpuSmooth += a * (kpiData.cpuPct - page.cpuSmooth)
            var maxPts = 150   // 150 × 200ms = 30s window
            var g = page.gpuHist.slice(); g.push(page.gpuSmooth); if (g.length > maxPts) g.shift(); page.gpuHist = g
            var c = page.cpuHist.slice(); c.push(page.cpuSmooth); if (c.length > maxPts) c.shift(); page.cpuHist = c
            resGraph.gpuPoints = page.gpuHist
            resGraph.cpuPoints = page.cpuHist
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, page.minContentHeight)
        flickableDirection: Flickable.VerticalFlick   // leave horizontal swipes for the SwipeView
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: col
            x: 20; y: 20
            width: flick.width - 40
            height: flick.contentHeight - 40
            spacing: 20   // wider gap so the hardware hints aren't glued to the band below
            // 4 stacked items now (hero, metrics, graph, accuracy) → 3 gaps; the
            // graph takes a fixed slice and the other 3 share the rest by ratio.
            // (minContentHeight was bumped so hero/metrics/accuracy keep their size.)
            readonly property real graphH: 200
            readonly property real band: (height - 3 * spacing - graphH)

            // ── HERO ROW (absorbs the height freed by the smaller accuracy card) ──
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: col.band * 0.48
                spacing: 16

                // INFERENCE DELAY
                KpiPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "INFERENCE DELAY"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 13; font.letterSpacing: 0.5 }
                            Item { Layout.fillWidth: true }
                            Rectangle { width: 116; height: 22; radius: 11; color: Qt.rgba(255,255,255,0.04); Text { anchors.centerIn: parent; text: "target < " + config.targetInferenceMs.toFixed(0) + " ms"; color: theme.bodyDim; font.pixelSize: 11 } }
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: val1.implicitHeight
                            Text { id: val1; text: kpiData.hasRealtimeKpi ? kpiData.inferenceMs.toFixed(0) : "—"; color: kpiData.inferenceMs > config.targetInferenceMs ? theme.warning : theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 100 }
                            Text { text: kpiData.hasRealtimeKpi ? "ms" : ""; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 26; anchors.left: val1.right; anchors.leftMargin: 12; anchors.baseline: val1.baseline }
                        }
                        SparklineChart {
                            id: infChart
                            Layout.fillWidth: true; Layout.fillHeight: true
                            minValue: 0; maxValue: 300
                            lineColor: kpiData.inferenceMs > config.targetInferenceMs ? theme.warning : theme.bodyText
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                width: 62; height: 22; radius: 11; color: Qt.rgba(41/255, 151/255, 255/255, 0.10); border.color: Qt.rgba(41/255, 151/255, 255/255, 0.32); border.width: 1
                                Row { anchors.centerIn: parent; spacing: 4
                                    Rectangle { width: 6; height: 6; radius: 3; color: theme.primaryOnDark; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: kpiData.optMode; color: theme.primaryOnDark; font.pixelSize: 11; font.family: theme.monoFont.family; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                            Text { text: "vs FP32 baseline " + config.ptBaselineMs.toFixed(1) + " ms"; color: theme.bodyDim; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            Text { text: config.realtimeKpiHz + "Hz · 0x102"; color: theme.bodyDim; font.pixelSize: 12; font.family: theme.monoFont.family }
                        }
                    }
                }

                // SPEED-UP RATIO
                KpiPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "SPEED-UP RATIO"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 13; font.letterSpacing: 0.5 }
                            Item { Layout.fillWidth: true }
                            Rectangle { width: 110; height: 22; radius: 11; color: Qt.rgba(255,255,255,0.04); Text { anchors.centerIn: parent; text: "target ≥ " + config.targetSpeedupRatio.toFixed(1) + "×"; color: theme.bodyDim; font.pixelSize: 11 } }
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: val2.implicitHeight
                            property double ratio: config.ptBaselineMs / Math.max(kpiData.inferenceMs, 1)
                            Text { id: val2; text: kpiData.hasRealtimeKpi ? parent.ratio.toFixed(2) : "—"; color: parent.ratio < config.targetSpeedupRatio ? theme.warning : theme.good; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 100 }
                            Text { text: kpiData.hasRealtimeKpi ? "×" : ""; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 26; anchors.left: val2.right; anchors.leftMargin: 12; anchors.baseline: val2.baseline }
                        }
                        SparklineChart {
                            id: ratioChart
                            Layout.fillWidth: true; Layout.fillHeight: true
                            minValue: 0; maxValue: 5.0
                            property double ratio: config.ptBaselineMs / Math.max(kpiData.inferenceMs, 1)
                            lineColor: ratio < config.targetSpeedupRatio ? theme.warning : theme.good
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: kpiData.optMode + " vs FP32 (" + kpiData.yoloModel + " · " + config.ptBaselineMs.toFixed(1) + " ms)"; color: theme.bodyDim; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            Text { text: "live"; color: theme.bodyDim; font.pixelSize: 12; font.family: theme.monoFont.family }
                        }
                    }
                }
            }

            // ── SYSTEM METRICS (25%) ──
            GridLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: col.band * 0.20
                columns: 4
                rowSpacing: 16
                columnSpacing: 16

                MetricTile {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    label: "GPU Usage"; value: page.gpuSmooth.toFixed(0); unit: "%"
                    maxValue: 100; warnAt: 85; critAt: 95
                    hint: "Ampere · 1024 CUDA · avg"; available: kpiData.hasRealtimeKpi
                }
                MetricTile {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    label: "CPU Usage"; value: page.cpuSmooth.toFixed(0); unit: "%"
                    maxValue: 100; warnAt: 85; critAt: 95
                    hint: "Cortex-A78AE × 6 · avg"; available: kpiData.hasRealtimeKpi
                }
                MetricTile {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    label: "GPU Temp"; value: kpiData.gpuTempC.toFixed(0); unit: "°C"
                    maxValue: 95; warnAt: 75; critAt: 85
                    hint: "active cooling"; available: kpiData.hasRealtimeKpi
                }
                MemoryTile { Layout.fillWidth: true; Layout.fillHeight: true }
            }

            // ── GPU / CPU RESOURCE HISTORY (Task-Manager style, fixed slice) ──
            KpiPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: col.graphH
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "RESOURCE HISTORY"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 13; font.letterSpacing: 0.5 }
                        Item { Layout.fillWidth: true }
                        Row { spacing: 12; Layout.alignment: Qt.AlignVCenter
                            Row { spacing: 5
                                Rectangle { width: 10; height: 3; radius: 1.5; color: theme.primaryOnDark; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "GPU " + page.gpuSmooth.toFixed(0) + "%"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            }
                            Row { spacing: 5
                                Rectangle { width: 10; height: 3; radius: 1.5; color: theme.good; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "CPU " + page.cpuSmooth.toFixed(0) + "%"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            }
                            Text { text: "30s · 0x102"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ResourceGraph {
                            id: resGraph
                            anchors.fill: parent
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !kpiData.hasRealtimeKpi
                            text: "waiting for 0x102…"
                            color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13
                        }
                    }
                }
            }

            // ── ACCURACY TRACKER (smaller — was 0.40, shrunk per request) ──
            AccuracyTracker {
                Layout.fillWidth: true
                Layout.preferredHeight: col.band * 0.32
                currentOptMode: kpiData.optMode
            }
        }
    }
}
