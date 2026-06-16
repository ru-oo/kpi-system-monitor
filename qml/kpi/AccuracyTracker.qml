import QtQuick
import QtQuick.Layouts

// v2 AccuracyTracker (slide-1-ai.jsx):
//   Header (title + subtitle) | target-loss pill
//   3 comparison cards FP32 / FP16 / INT8  (big mAP + loss badge, corner glow on current)
//   bottom Trend strip (mAP curve across the 3 stages)
KpiPanel {
    id: panel
    property string currentOptMode: "INT8"

    Layout.fillWidth: true
    Layout.minimumHeight: 232   // fits the iPad band; content scales below this

    // mAP values come from KpiData (CAN-driven). Until applyAccuracy() fires
    // hasAccuracy is false and we render "—" instead of stale numbers.
    readonly property bool hasAcc: kpiData.hasAccuracy
    readonly property var stages: [
        { id: "FP32", mAP: kpiData.accFp32, baseline: true },
        { id: "FP16", mAP: kpiData.accFp16, baseline: false },
        { id: "INT8", mAP: kpiData.accInt8, baseline: false }
    ]
    readonly property real baselineMap: kpiData.accFp32 > 0 ? kpiData.accFp32 : 1.0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // ── Header ──
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Text { text: "Accuracy Tracker"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20 }
                Text { text: "mAP across quantization · COCO val"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: pillTxt.implicitWidth + 24
                implicitHeight: 26
                radius: 13
                color: Qt.rgba(1,1,1,0.04)
                border.color: theme.hairline
                Text {
                    id: pillTxt
                    anchors.centerIn: parent
                    text: "target loss ≤ " + config.targetMapLossPct.toFixed(1) + "%"
                    color: theme.bodyMuted
                    font.family: theme.defaultFont.family
                    font.pixelSize: 13
                }
            }
        }

        // ── 3 comparison cards ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Repeater {
                model: panel.stages
                delegate: Rectangle {
                    id: stage
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 16

                    property bool cur: panel.currentOptMode === modelData.id
                    property real lossPct: (panel.baselineMap - modelData.mAP) / panel.baselineMap * 100
                    property bool lossWarn: lossPct > config.targetMapLossPct
                    property color accent: modelData.baseline ? theme.primaryOnDark : (lossWarn ? theme.warning : theme.good)

                    color: cur ? Qt.rgba(accent.r, accent.g, accent.b, 0.10) : theme.tile3
                    border.color: cur ? Qt.rgba(accent.r, accent.g, accent.b, 0.40) : theme.hairline
                    border.width: 1
                    clip: true

                    // faint top tint on current (soft, contained)
                    Rectangle {
                        visible: stage.cur
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.rgba(stage.accent.r, stage.accent.g, stage.accent.b, 0.12) }
                            GradientStop { position: 0.7; color: "transparent" }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        // header row: id + baseline/loss-target badge
                        RowLayout {
                            Layout.fillWidth: true
                            Row {
                                spacing: 7
                                Rectangle {
                                    visible: stage.cur; width: 8; height: 8; radius: 4; color: stage.accent
                                    anchors.verticalCenter: parent.verticalCenter
                                    SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { to: 0.35; duration: 700 } NumberAnimation { to: 1.0; duration: 700 } }
                                }
                                Text {
                                    text: stage.modelData.id
                                    color: stage.cur ? stage.accent : theme.bodyMuted
                                    font.family: theme.monoFont.family
                                    font.weight: Font.Bold
                                    font.pixelSize: 15
                                    font.letterSpacing: 0.6
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            Item { Layout.fillWidth: true }
                            // baseline label OR loss-target badge
                            Text {
                                visible: stage.modelData.baseline
                                text: "BASELINE"
                                color: theme.bodyDim
                                font.family: theme.defaultFont.family
                                font.weight: Font.DemiBold
                                font.pixelSize: 11
                                font.letterSpacing: 0.6
                            }
                            Rectangle {
                                visible: !stage.modelData.baseline
                                implicitWidth: badgeTxt.implicitWidth + 20
                                implicitHeight: 22
                                radius: 11
                                color: stage.lossWarn ? Qt.rgba(255/255,159/255,10/255,0.12) : Qt.rgba(48/255,209/255,88/255,0.10)
                                border.color: stage.lossWarn ? Qt.rgba(255/255,159/255,10/255,0.30) : Qt.rgba(48/255,209/255,88/255,0.30)
                                border.width: 1
                                Text {
                                    id: badgeTxt
                                    anchors.centerIn: parent
                                    text: (stage.lossWarn ? "> " : "≤ ") + config.targetMapLossPct.toFixed(1) + "% target"
                                    color: stage.lossWarn ? theme.warning : theme.good
                                    font.family: theme.defaultFont.family
                                    font.weight: Font.Bold
                                    font.pixelSize: 11
                                }
                            }
                        }

                        // a little breathing room so the big number isn't glued
                        // to the ID label (tight on the narrower iPad card)
                        Item { Layout.preferredHeight: 2 }

                        // big mAP value
                        Row {
                            spacing: 6
                            Text {
                                text: panel.hasAcc ? stage.modelData.mAP.toFixed(2) : "—"
                                color: theme.bodyText
                                font.family: theme.defaultFont.family
                                font.weight: Font.Bold
                                font.pixelSize: 28
                                anchors.baseline: mapUnit.baseline
                            }
                            Text { id: mapUnit; text: "mAP"; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.pixelSize: 13 }
                        }

                        // loss row (packed directly under mAP so it never clips
                        // when the panel is short)
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                visible: stage.modelData.baseline
                                text: "reference accuracy"
                                color: theme.bodyDim
                                font.family: theme.defaultFont.family
                                font.pixelSize: 13
                            }
                            Text {
                                visible: !stage.modelData.baseline
                                text: "↓ loss from baseline"
                                color: theme.bodyMuted
                                font.family: theme.defaultFont.family
                                font.pixelSize: 13
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: !stage.modelData.baseline && panel.hasAcc
                                text: "−" + stage.lossPct.toFixed(2) + "%"
                                color: stage.lossWarn ? theme.warning : theme.good
                                font.family: theme.defaultFont.family
                                font.weight: Font.Bold
                                font.pixelSize: 16
                            }
                        }
                    }
                }
            }
        }

        // ── Trend strip ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.minimumHeight: 30      // never let the layout squeeze it → no clipped trend
            radius: 12
            color: theme.tile3
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18
                Text { text: "TREND"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12; font.letterSpacing: 0.6 }
                Canvas {
                    id: trend
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20
                    Layout.alignment: Qt.AlignVCenter
                    property color stroke: theme.primaryOnDark
                    property color dotOff: theme.bodyDim
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        if (!panel.hasAcc) return
                        var pad = 6, w = width, h = height
                        var min = 44, max = 50
                        var stages = panel.stages, xs = [], ys = []
                        for (var i = 0; i < stages.length; i++) {
                            xs.push(pad + i * (w - pad * 2) / (stages.length - 1))
                            ys.push(pad + (1 - (stages[i].mAP - min) / (max - min)) * (h - pad * 2))
                        }
                        ctx.strokeStyle = stroke; ctx.lineWidth = 1.6; ctx.lineJoin = "round"; ctx.lineCap = "round"
                        ctx.beginPath(); ctx.moveTo(xs[0], ys[0])
                        for (var j = 1; j < stages.length; j++) ctx.lineTo(xs[j], ys[j])
                        ctx.stroke()
                        for (var k = 0; k < stages.length; k++) {
                            var isCur = panel.currentOptMode === stages[k].id
                            ctx.beginPath(); ctx.arc(xs[k], ys[k], isCur ? 3.6 : 2.4, 0, 2 * Math.PI)
                            ctx.fillStyle = isCur ? stroke : dotOff; ctx.fill()
                        }
                    }
                    Connections { target: panel; function onCurrentOptModeChanged() { trend.requestPaint() } }
                    Connections { target: theme; function onLightModeChanged() { trend.requestPaint() } }
                    Connections { target: kpiData; function onAccuracyChanged() { trend.requestPaint() } }
                }
                Text {
                    text: panel.hasAcc ? (panel.stages[0].mAP.toFixed(1) + " → " + panel.stages[2].mAP.toFixed(1) + " mAP") : "—"
                    color: theme.bodyDim
                    font.family: theme.monoFont.family
                    font.pixelSize: 11
                }
            }
        }
    }
}
