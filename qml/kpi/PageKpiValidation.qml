import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// SwipeView page 3 — KPI Validation (§4.2).
//
// Summary strip + 3×2 grid of category cards. Each metric is encoded by its
// TYPE (not one uniform progress bar), via 3 reusable components:
//   • StateDots  — run-count KPIs (X of N): reach/plan/detect success
//   • AxisDot    — smaller-is-better continuous values: lane-center dev, plan
//                  time, tracking error, detect response, mAP loss, frame loss,
//                  CAN TX delay  (dot on an axis with the in-target zone shaded)
//   • BigStat    — ratio / representative values: INT8 speed-up, trigger accuracy
// Control keeps a sparkline (time series). Colour comes from the unchanged
// 3-state result (pass=good / watch=warning / no-data=bodyMuted) via
// page.statusColor() — theme colours only, light & dark safe. The PASS/WATCH/
// NO DATA judgment (okXxx/hasXxx) is unchanged; only the visual encoding.
Item {
    id: page

    // ── rolling histories (real telemetry) ──
    property var ctrlHist: []   // pathDeviationMm,   last 20
    property var planHist: []   // pathPlanLastMs,    last 10
    property var percHist: []   // obstacleConf,      last 10

    Connections {
        target: kpiData
        function onKpiChanged() {
            if (kpiData.hasRealtimeKpi) {
                var c = page.ctrlHist.slice(); c.push(kpiData.pathDeviationMm)
                if (c.length > 20) c.shift(); page.ctrlHist = c
            }
            if (kpiData.hasObstacle) {
                var p = page.percHist.slice(); p.push(kpiData.obstacleConf)
                if (p.length > 10) p.shift(); page.percHist = p
            }
        }
        function onPathPlanChanged() {
            if (kpiData.hasPathPlan) {
                var q = page.planHist.slice(); q.push(kpiData.pathPlanLastMs)
                if (q.length > 10) q.shift(); page.planHist = q
            }
        }
    }

    // ── §4.2 evaluation (tri-state) ─────────────────────────────────────
    // A category only PASSes when its DATA is present AND meets target. With no
    // CAN data yet it must NOT read PASS — it's NO DATA (neutral), not green.
    readonly property bool hasLoc:    kpiData.hasEgoPose   // EKF: lane-center dev needs a pose
    readonly property bool hasPlan:   kpiData.hasPathPlan
    readonly property bool hasCtrl:   kpiData.hasRealtimeKpi
    readonly property bool hasPerc:   kpiData.hasRealtimeKpi || kpiData.totalRuns > 0
    readonly property bool hasSafety: kpiData.hasRealtimeKpi
    readonly property bool hasMon:    kpiData.can

    // Post-AMCL: localization accuracy = HD lane-center deviation ≤ threshold.
    readonly property bool okLoc:  hasLoc
                                   && Math.abs(kpiData.laneCenterDeviationMm) <= config.laneCenterDevMmMax
                                   && (kpiData.totalRuns === 0 || kpiData.successRuns >= config.localizationSuccessRunsMin)
    readonly property bool okPlan: hasPlan
                                   && kpiData.pathPlanLastMs <= config.pathPlanMsMax
                                   && kpiData.pathPlanSuccessRuns >= config.pathPlanSuccessRunsMin
    readonly property bool okCtrl: hasCtrl && kpiData.pathDeviationMm <= config.targetPathDeviationMm
    readonly property bool okPerc: hasPerc
                                   && (kpiData.totalRuns === 0 || kpiData.perceptionDetectedRuns >= config.parkingDetectRunsMin)
                                   && (!kpiData.hasRealtimeKpi || kpiData.detectLatencyMs <= config.targetDetectLatencyMs)
                                   && kpiData.falsePositiveCount < config.falsePositivePerRunMax * Math.max(kpiData.totalRuns, 1)
                                   // §4.2 trigger accuracy now gates too (was display-only). No 0x107 yet → not failed.
                                   && (kpiData.perceptionTotalRuns === 0 || kpiData.triggerAccuracyPct >= config.triggerAccuracyPctMin)
    readonly property bool okSafety: hasSafety
                                     && (config.ptBaselineMs / Math.max(kpiData.inferenceMs, 1)) >= config.targetSpeedupRatio
                                     && (!kpiData.hasAccuracy || ((kpiData.accFp32 - kpiData.accInt8) / Math.max(kpiData.accFp32, 1) * 100) <= config.targetMapLossPct)
    readonly property bool okMon: hasMon
                                  && kpiData.canTxLatencyMs <= config.targetCanTxLatencyMs
                                  && (!kpiData.hasMemory || kpiData.frameLossPct <= config.canLossPctMax)

    readonly property int passCount:  (okLoc?1:0)+(okPlan?1:0)+(okCtrl?1:0)+(okPerc?1:0)+(okSafety?1:0)+(okMon?1:0)
    readonly property int evalCount:  (hasLoc?1:0)+(hasPlan?1:0)+(hasCtrl?1:0)+(hasPerc?1:0)+(hasSafety?1:0)+(hasMon?1:0)
    readonly property int watchCount: evalCount - passCount     // data present, failing
    readonly property int noDataCount: 6 - evalCount

    // ── per-METRIC has-data / ok (for the per-metric encodings; the card-level
    // okXxx above are unchanged). Defined here so card bindings stay simple and
    // avoid fragile parent chains. Logic mirrors the okXxx clauses, per metric.
    readonly property bool mLocRunsData: kpiData.totalRuns > 0
    readonly property bool mLocRunsOk:   kpiData.successRuns >= config.localizationSuccessRunsMin
    readonly property bool mLocDevOk:    Math.abs(kpiData.laneCenterDeviationMm) <= config.laneCenterDevMmMax
    readonly property bool mPlanRunsOk:  kpiData.pathPlanSuccessRuns >= config.pathPlanSuccessRunsMin
    readonly property bool mPlanTimeOk:  kpiData.pathPlanLastMs <= config.pathPlanMsMax
    readonly property bool mCtrlOk:      kpiData.pathDeviationMm <= config.targetPathDeviationMm
    readonly property bool mPercDetData: kpiData.perceptionTotalRuns > 0 || kpiData.totalRuns > 0
    readonly property int  mPercDetTotal: kpiData.perceptionTotalRuns > 0 ? kpiData.perceptionTotalRuns : config.runsTotal
    readonly property bool mPercDetOk:   kpiData.perceptionDetectedRuns >= config.parkingDetectRunsMin
    readonly property bool mPercLatOk:   kpiData.detectLatencyMs <= config.targetDetectLatencyMs
    readonly property bool mPercTrigData: kpiData.perceptionTotalRuns > 0
    readonly property bool mPercTrigOk:  kpiData.triggerAccuracyPct >= config.triggerAccuracyPctMin
    readonly property real mInt8Speed:   config.ptBaselineMs / Math.max(kpiData.inferenceMs, 1)
    readonly property real mInt8Loss:    kpiData.accFp32 > 0 ? ((kpiData.accFp32 - kpiData.accInt8) / kpiData.accFp32 * 100) : 0
    readonly property bool mSafeSpdOk:   mInt8Speed >= config.targetSpeedupRatio
    readonly property bool mSafeLossOk:  mInt8Loss <= config.targetMapLossPct
    readonly property bool mMonLossOk:   kpiData.frameLossPct <= config.canLossPctMax
    readonly property bool mMonTxOk:     kpiData.canTxLatencyMs <= config.targetCanTxLatencyMs

    // ── small reusable bits ──────────────────────────────────

    // category icon (monochrome dingbat in a tinted rounded box)
    component StatusBadge : Rectangle {
        id: badge
        property bool pass: true
        property bool hasData: true                  // false → NO DATA (neutral, not PASS)
        readonly property color hue: !hasData ? theme.bodyDim : (pass ? theme.good : theme.warning)
        readonly property string label: !hasData ? "NO DATA" : (pass ? "PASS" : "WATCH")
        implicitWidth: badgeRow.implicitWidth + 22
        implicitHeight: 26
        radius: 13
        color: !hasData ? Qt.rgba(theme.bodyDim.r,theme.bodyDim.g,theme.bodyDim.b,0.08)
                        : (pass ? Qt.rgba(48/255,209/255,88/255,0.10) : Qt.rgba(255/255,159/255,10/255,0.12))
        border.color: hue
        border.width: 1
        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 7
            Rectangle { width: 6; height: 6; radius: 3; color: badge.hue; anchors.verticalCenter: parent.verticalCenter
                SequentialAnimation on opacity { running: badge.hasData && !badge.pass; loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1.0; duration: 700 } } }
            Text { text: badge.label; color: badge.hue; font.family: theme.monoFont.family; font.weight: Font.Bold; font.pixelSize: 12; font.letterSpacing: 0.8; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // category card chrome with a content slot. No dingbat icons — status is
    // carried by a soft top wash (status-tinted gradient), a small leading dot,
    // and the PASS/WATCH badge. Matches the StatusTabs / AccuracyTracker look.
    component CategoryCard : Rectangle {
        id: cc
        property string title: ""
        property string subtitle: ""
        property bool pass: true
        property bool hasData: true
        property color accent: !hasData ? theme.bodyDim : (pass ? theme.good : theme.warning)
        default property alias content: body.children

        Layout.fillWidth: true
        Layout.preferredHeight: 392
        radius: 18
        color: theme.tile2
        border.color: theme.hairline
        border.width: 1
        clip: true

        // soft status wash fading down from the top edge (no hard shapes)
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(cc.accent.r, cc.accent.g, cc.accent.b, 0.12) }
                GradientStop { position: 0.32; color: "transparent" }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    width: 10; height: 10; radius: 5; color: cc.accent
                    Layout.alignment: Qt.AlignVCenter
                    layer.enabled: true
                    SequentialAnimation on opacity {
                        running: cc.hasData && !cc.pass; loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 700 } NumberAnimation { to: 1.0; duration: 700 }
                    }
                }
                ColumnLayout {
                    spacing: 1
                    Text { text: cc.title; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20 }
                    Text { visible: cc.subtitle !== ""; text: cc.subtitle; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }
                }
                Item { Layout.fillWidth: true }
                StatusBadge { pass: cc.pass; hasData: cc.hasData; Layout.alignment: Qt.AlignTop }
            }

            // hairline separating header from the visualization
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline; opacity: 0.6 }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14
            }
        }
    }

    // chart panel (inner tile-3 box)
    component ChartBox : Rectangle {
        default property alias inner: innerCol.children
        Layout.fillWidth: true
        radius: 14
        color: theme.tile3
        border.color: theme.hairline
        border.width: 1
        implicitHeight: innerCol.implicitHeight + 32
        ColumnLayout {
            id: innerCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 16
            spacing: 8
        }
    }

    // chart title row: left uppercase label + right slot text
    component ChartTitle : RowLayout {
        property string label: ""
        property string rightText: ""
        property color rightColor: theme.bodyDim
        Layout.fillWidth: true
        Text { text: parent.label; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; font.letterSpacing: 0.6 }
        Item { Layout.fillWidth: true }
        Text { text: parent.rightText; color: parent.rightColor; font.family: theme.monoFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
    }

    // footer metric: label + big value + sub
    component MetricFoot : ColumnLayout {
        property string label: ""
        property string value: ""
        property string sub: ""
        property color valueColor: theme.bodyText
        Layout.fillWidth: true
        spacing: 4
        Text { text: parent.label; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.5 }
        Row {
            spacing: 4
            Text { text: parent.parent.value; color: parent.parent.valueColor; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 24; anchors.baseline: footSub.baseline }
            Text { id: footSub; text: parent.parent.sub; color: theme.bodyMuted; font.pixelSize: 12 }
        }
    }

    // horizontal value bar with a target tick (lane dev, tx delay, ...)
    component TargetBar : ColumnLayout {
        property real value: 0
        property real barMax: 100
        property real targetValue: 0
        property bool pass: true
        property string loLabel: "0"
        property string midLabel: ""
        property string hiLabel: ""
        Layout.fillWidth: true
        spacing: 6
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 10
            Rectangle { anchors.fill: parent; radius: 5; color: theme.tile4 }
            Rectangle {
                height: parent.height; radius: 5
                width: parent.width * Math.max(0, Math.min(1, parent.parent.value / parent.parent.barMax))
                color: parent.parent.pass ? theme.good : theme.warning
                Behavior on width { NumberAnimation { duration: 200 } }
            }
            Rectangle {
                visible: parent.parent.targetValue > 0
                x: parent.width * (parent.parent.targetValue / parent.parent.barMax) - 1
                y: -3; width: 2; height: parent.height + 6
                color: theme.bodyText; opacity: 0.55
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: parent.parent.loLabel; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Text { text: parent.parent.midLabel; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Text { text: parent.parent.hiLabel; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
        }
    }

    // status colour from a metric's state: meets target → good, misses →
    // warning, no data → bodyMuted (never a false green). Theme colours only.
    function statusColor(hasData, ok) {
        return !hasData ? theme.bodyMuted : (ok ? theme.good : theme.warning)
    }
    function statusIcon(hasData, ok) {
        return !hasData ? "" : (ok ? "✓" : "▲")
    }

    // MetricBlock — a labelled metric: name (left) + value (right) over a
    // visual slot (StateDots / AxisDot / BigStat / sparkline). Keeps every card
    // consistent. NO judgment logic here — callers pass value/colour.
    component MetricBlock : ColumnLayout {
        property string name: ""
        property string valueText: ""
        property color  valueColor: theme.bodyText
        default property alias content: holder.children
        Layout.fillWidth: true
        spacing: 7
        RowLayout {
            Layout.fillWidth: true
            Text { text: name; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12; font.letterSpacing: 0.3 }
            Item { Layout.fillWidth: true }
            Text { text: valueText; color: valueColor; font.family: theme.monoFont.family; font.weight: Font.DemiBold; font.pixelSize: 13 }
        }
        ColumnLayout { id: holder; Layout.fillWidth: true; spacing: 8 }
    }

    // ── scrollable content ───────────────────────────────────
    Flickable {
        anchors.fill: parent
        anchors.margins: 20
        contentWidth: width
        contentHeight: outer.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: outer
            width: parent.width
            spacing: 18

            // ── Summary strip ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                radius: 20
                color: theme.tile2
                border.color: theme.hairline
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24; anchors.rightMargin: 24
                    spacing: 26
                    Row {
                        spacing: 10
                        Text { text: page.passCount.toString(); color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 42; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "/ 6"; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: "CATEGORIES"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; font.letterSpacing: 0.8 }
                            Text { text: "passing target"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 13 }
                        }
                    }
                    Rectangle { width: 1; height: 44; color: theme.hairline }
                    Row {
                        spacing: 8
                        Rectangle { width: 9; height: 9; radius: 4.5; color: theme.good; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: page.passCount.toString(); color: theme.good; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "PASS"; color: theme.bodyMuted; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Row {
                        spacing: 8
                        Rectangle { width: 9; height: 9; radius: 4.5; color: theme.warning; anchors.verticalCenter: parent.verticalCenter
                            SequentialAnimation on opacity { running: page.watchCount > 0; loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1.0; duration: 700 } } }
                        Text { text: page.watchCount.toString(); color: theme.warning; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "WATCH"; color: theme.bodyMuted; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Row {
                        spacing: 8
                        visible: page.noDataCount > 0
                        Rectangle { width: 9; height: 9; radius: 4.5; color: theme.bodyDim; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: page.noDataCount.toString(); color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "NO DATA"; color: theme.bodyMuted; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Item { Layout.fillWidth: true }
                    Column {
                        Text { anchors.right: parent.right; text: "LAST RUN"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; font.letterSpacing: 0.6 }
                        Text { anchors.right: parent.right; text: config.routeLengthM ? (config.routeLengthM + "m · L" + kpiData.failsafeLevel + (kpiData.failsafeLevel === 1 ? " Heavy" : "")) : ("250m · L" + kpiData.failsafeLevel); color: theme.bodyText; font.family: theme.monoFont.family; font.weight: Font.DemiBold; font.pixelSize: 13 }
                    }
                }
            }

            // ── 3 × 2 category grid ──
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 18
                columnSpacing: 18

                // 1. LOCALIZATION — reach-success (dots) + lane-center dev (axis)
                CategoryCard {
                    title: "Localization"; subtitle: "도달 성공률 · 차로중심 편차"; pass: page.okLoc; hasData: page.hasLoc
                    ChartBox {
                        // 운행 횟수형 → StateDots
                        MetricBlock {
                            name: "도달 성공률"
                            valueText: kpiData.totalRuns > 0 ? (kpiData.successRuns + " / " + config.runsTotal) : "데이터 없음"
                            valueColor: page.statusColor(page.mLocRunsData, page.mLocRunsOk)
                            StateDots {
                                filled: kpiData.successRuns; total: config.runsTotal
                                targetMin: config.localizationSuccessRunsMin
                                color: page.statusColor(page.mLocRunsData, page.mLocRunsOk)
                            }
                            Text { text: "target ≥ " + config.localizationSuccessRunsMin + " / " + config.runsTotal; color: theme.bodyDim; font.pixelSize: 11 }
                        }
                    }
                    ChartBox {
                        // 작을수록 좋은 연속값 → AxisDot. "(~)" = HD-map 파생값.
                        MetricBlock {
                            name: "차로중심 편차 (~)"
                            valueText: kpiData.hasEgoPose ? ((kpiData.laneCenterDeviationMm / 1000).toFixed(2) + " m") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasEgoPose, page.mLocDevOk)
                            AxisDot {
                                value: Math.abs(kpiData.laneCenterDeviationMm); max: config.laneCenterDevMmMax * 2; targetMax: config.laneCenterDevMmMax
                                color: page.statusColor(kpiData.hasEgoPose, page.mLocDevOk)
                                loLabel: "0"; midLabel: "↑ target " + (config.laneCenterDevMmMax / 1000).toFixed(2); hiLabel: (config.laneCenterDevMmMax * 2 / 1000).toFixed(2) + " m"
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 2. PLANNING — success (dots) + this-search time (axis) + state chip
                CategoryCard {
                    title: "Planning"; subtitle: "경로 탐색 성공률 · 시간"; pass: page.okPlan; hasData: page.hasPlan
                    ChartBox {
                        MetricBlock {
                            name: "경로 탐색 성공률"
                            valueText: kpiData.hasPathPlan ? (kpiData.pathPlanSuccessRuns + " / " + config.runsTotal) : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasPathPlan, page.mPlanRunsOk)
                            StateDots {
                                filled: kpiData.pathPlanSuccessRuns; total: config.runsTotal
                                targetMin: config.pathPlanSuccessRunsMin
                                color: page.statusColor(kpiData.hasPathPlan, page.mPlanRunsOk)
                            }
                            Text { text: "target ≥ " + config.pathPlanSuccessRunsMin + " / " + config.runsTotal; color: theme.bodyDim; font.pixelSize: 11 }
                        }
                    }
                    ChartBox {
                        MetricBlock {
                            name: "이번 탐색 시간"
                            valueText: kpiData.hasPathPlan ? ((kpiData.pathPlanLastMs/1000).toFixed(2) + " s") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasPathPlan, page.mPlanTimeOk)
                            AxisDot {
                                value: kpiData.pathPlanLastMs; max: config.pathPlanChartMaxMs; targetMax: config.pathPlanMsMax
                                color: page.statusColor(kpiData.hasPathPlan, page.mPlanTimeOk)
                                loLabel: "0"; midLabel: "↑ target " + (config.pathPlanMsMax/1000).toFixed(1) + "s"; hiLabel: (config.pathPlanChartMaxMs/1000).toFixed(1) + "s"
                            }
                        }
                        // 상태 라벨 (0x106 Planning_State enum) → 작은 칩 그대로
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "상태"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: kpiData.hasPathPlan ? ["대기","계획중","성공","실패"][kpiData.planningState] : "데이터 없음"
                                color: !kpiData.hasPathPlan ? theme.bodyMuted
                                       : kpiData.planningState === 2 ? theme.good
                                       : kpiData.planningState === 3 ? theme.warning : theme.bodyText
                                font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 13
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 3. CONTROL — line chart + 2 metrics
                CategoryCard {
                    title: "Control"; subtitle: "추종 오차 (path deviation)"; pass: page.okCtrl; hasData: page.hasCtrl
                    ChartBox {
                        ChartTitle { label: "추종 오차 trend"; rightText: "last 20 samples" }
                        Canvas {
                            id: ctrlChart
                            Layout.fillWidth: true
                            Layout.preferredHeight: 150
                            property color line: kpiData.pathDeviationMm <= config.targetPathDeviationMm ? theme.good : theme.warning
                            onPaint: {
                                var ctx = getContext("2d"); ctx.reset()
                                var W = width, H = height
                                var padL = 28, padR = 6, padT = 8, padB = 16
                                var maxV = config.pathDeviationChartMaxMm, target = config.targetPathDeviationMm
                                function yFor(v){ return H - padB - v/maxV*(H-padT-padB) }
                                function xFor(i,n){ return padL + i/(Math.max(1,n-1))*(W-padL-padR) }
                                // grid
                                ctx.strokeStyle = theme.hairline; ctx.lineWidth = 0.6
                                ctx.fillStyle = theme.bodyDim; ctx.font = "8px " + theme.monoFont.family; ctx.textAlign = "end"; ctx.textBaseline = "middle"
                                var gstep = maxV / 5, grids = [gstep, 2*gstep, 3*gstep, 4*gstep]
                                for (var g=0; g<grids.length; g++){
                                    var gy = yFor(grids[g])
                                    ctx.beginPath(); ctx.moveTo(padL, gy); ctx.lineTo(W-padR, gy); ctx.stroke()
                                    ctx.fillText(grids[g].toString(), padL-4, gy)
                                }
                                // target dashed
                                ctx.strokeStyle = theme.good; ctx.lineWidth = 1; ctx.setLineDash([4,3]); ctx.globalAlpha = 0.7
                                ctx.beginPath(); ctx.moveTo(padL, yFor(target)); ctx.lineTo(W-padR, yFor(target)); ctx.stroke()
                                ctx.setLineDash([]); ctx.globalAlpha = 1.0
                                ctx.fillStyle = theme.good; ctx.textAlign = "end"; ctx.fillText("target " + target, W-padR-2, yFor(target)-8)
                                // line
                                var hist = page.ctrlHist
                                if (hist.length >= 2) {
                                    ctx.strokeStyle = line; ctx.lineWidth = 1.8; ctx.lineJoin = "round"; ctx.lineCap = "round"
                                    ctx.beginPath()
                                    for (var i=0;i<hist.length;i++){ var x=xFor(i,hist.length), y=yFor(Math.min(maxV,hist[i])); if(i===0)ctx.moveTo(x,y); else ctx.lineTo(x,y) }
                                    ctx.stroke()
                                    var lx=xFor(hist.length-1,hist.length), ly=yFor(Math.min(maxV,hist[hist.length-1]))
                                    ctx.beginPath(); ctx.fillStyle=line; ctx.arc(lx,ly,3.5,0,2*Math.PI); ctx.fill()
                                }
                            }
                            Connections { target: page; function onCtrlHistChanged() { ctrlChart.requestPaint() } }
                            Connections { target: theme; function onLightModeChanged() { ctrlChart.requestPaint() } }
                        }
                    }
                    // current tracking error on the axis (trend above stays a sparkline)
                    ChartBox {
                        MetricBlock {
                            name: "현재 추종 오차"
                            valueText: kpiData.hasRealtimeKpi ? ((kpiData.pathDeviationMm / 1000).toFixed(2) + " m") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasRealtimeKpi, page.mCtrlOk)
                            AxisDot {
                                value: kpiData.pathDeviationMm; max: config.pathDeviationChartMaxMm; targetMax: config.targetPathDeviationMm
                                color: page.statusColor(kpiData.hasRealtimeKpi, page.mCtrlOk)
                                loLabel: "0"; midLabel: "↑ target " + (config.targetPathDeviationMm / 1000).toFixed(2); hiLabel: (config.pathDeviationChartMaxMm / 1000).toFixed(2) + " m"
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 4. PERCEPTION — detect rate (dots) + response (axis) + trigger (bigstat) + FP chip
                CategoryCard {
                    title: "Perception"; subtitle: "주차 감지율 · 응답 · 트리거"; pass: page.okPerc; hasData: page.hasPerc
                    ChartBox {
                        MetricBlock {
                            name: "주차 감지율"
                            valueText: page.mPercDetData ? (kpiData.perceptionDetectedRuns + " / " + page.mPercDetTotal) : "데이터 없음"
                            valueColor: page.statusColor(page.mPercDetData, page.mPercDetOk)
                            StateDots {
                                filled: kpiData.perceptionDetectedRuns; total: page.mPercDetTotal
                                targetMin: config.parkingDetectRunsMin
                                color: page.statusColor(page.mPercDetData, page.mPercDetOk)
                            }
                            Text { text: "target ≥ " + config.parkingDetectRunsMin + " / " + page.mPercDetTotal; color: theme.bodyDim; font.pixelSize: 11 }
                        }
                    }
                    ChartBox {
                        MetricBlock {
                            name: "감지 응답"
                            valueText: kpiData.hasRealtimeKpi ? (kpiData.detectLatencyMs.toFixed(0) + " ms") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasRealtimeKpi, page.mPercLatOk)
                            AxisDot {
                                value: kpiData.detectLatencyMs; max: config.targetDetectLatencyMs * 1.5; targetMax: config.targetDetectLatencyMs
                                color: page.statusColor(kpiData.hasRealtimeKpi, page.mPercLatOk)
                                loLabel: "0"; midLabel: "↑ target " + config.targetDetectLatencyMs; hiLabel: (config.targetDetectLatencyMs*1.5).toFixed(0) + " ms"
                            }
                        }
                        // 트리거 정확도(대표값) → BigStat · 오탐 → 작은 칩
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 2
                                Text { text: "트리거 정확도"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                                BigStat {
                                    value: kpiData.perceptionTotalRuns > 0 ? kpiData.triggerAccuracyPct.toFixed(0) : "—"
                                    unit: "%"; valueSize: 22
                                    statusColor: page.statusColor(kpiData.perceptionTotalRuns > 0, kpiData.triggerAccuracyPct >= config.triggerAccuracyPctMin)
                                    icon: page.statusIcon(kpiData.perceptionTotalRuns > 0, kpiData.triggerAccuracyPct >= config.triggerAccuracyPctMin)
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ColumnLayout {
                                spacing: 2
                                Text { text: "오탐"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12; Layout.alignment: Qt.AlignRight }
                                Text {
                                    text: kpiData.falsePositiveCount + " 회"
                                    color: kpiData.falsePositiveCount === 0 ? theme.good : theme.warning
                                    font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 18
                                    Layout.alignment: Qt.AlignRight
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 5. SAFETY / AI — grouped speed/loss bars + 2 metrics
                CategoryCard {
                    title: "Safety / AI"; subtitle: "INT8 가속 · mAP@.5:.95 손실 · " + kpiData.yoloModel; pass: page.okSafety; hasData: page.hasSafety
                    property real int8Speed: config.ptBaselineMs / Math.max(kpiData.inferenceMs, 1)
                    // mAP loss vs FP32 baseline, on the active model's map50-95 (0x101).
                    property real int8Loss: kpiData.accFp32 > 0 ? ((kpiData.accFp32 - kpiData.accInt8) / kpiData.accFp32 * 100) : 0
                    property real fp16Loss: kpiData.accFp32 > 0 ? ((kpiData.accFp32 - kpiData.accFp16) / kpiData.accFp32 * 100) : 0
                    id: safetyCard
                    ChartBox {
                        // INT8 가속 (대표 비율값) → BigStat (larger-is-better)
                        MetricBlock {
                            name: "INT8 가속"
                            valueText: "target ≥ " + config.targetSpeedupRatio.toFixed(1) + "×"
                            valueColor: theme.bodyDim
                            BigStat {
                                value: kpiData.hasRealtimeKpi ? safetyCard.int8Speed.toFixed(2) : "—"
                                unit: "×"; valueSize: 30
                                statusColor: page.statusColor(kpiData.hasRealtimeKpi, page.mSafeSpdOk)
                                icon: page.statusIcon(kpiData.hasRealtimeKpi, page.mSafeSpdOk)
                            }
                        }
                    }
                    ChartBox {
                        // INT8 mAP 손실 (작을수록 좋음) → AxisDot
                        MetricBlock {
                            name: "mAP@.5:.95 손실"
                            valueText: kpiData.hasAccuracy ? (safetyCard.int8Loss.toFixed(2) + " %") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasAccuracy, page.mSafeLossOk)
                            AxisDot {
                                value: safetyCard.int8Loss; max: 30; targetMax: config.targetMapLossPct
                                color: page.statusColor(kpiData.hasAccuracy, page.mSafeLossOk)
                                loLabel: "0"; midLabel: "↑ target " + config.targetMapLossPct + "%"; hiLabel: "30 %"
                            }
                            Text {
                                text: kpiData.hasAccuracy ? ("INT8 mAP " + kpiData.accFp32.toFixed(1) + " → " + kpiData.accInt8.toFixed(1) + " (" + kpiData.yoloModel + ")") : ""
                                color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 6. MONITORING — frame loss vs target + tx delay
                CategoryCard {
                    title: "Monitoring"; subtitle: "프레임 손실 · CAN 송출 지연"; pass: page.okMon; hasData: page.hasMon
                    ChartBox {
                        // 프레임 손실 (작을수록 좋음) → AxisDot
                        MetricBlock {
                            name: "프레임 손실"
                            valueText: kpiData.hasMemory ? (kpiData.frameLossPct.toFixed(2) + " %  ·  " + kpiData.framesRx.toLocaleString(Qt.locale("en_US"), 'f', 0) + " rx") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.hasMemory, page.mMonLossOk)
                            AxisDot {
                                value: kpiData.frameLossPct; max: config.canLossPctMax * 5; targetMax: config.canLossPctMax
                                color: page.statusColor(kpiData.hasMemory, page.mMonLossOk)
                                loLabel: "0"; midLabel: "↑ target " + config.canLossPctMax.toFixed(2) + "%"; hiLabel: (config.canLossPctMax*5).toFixed(2) + "%"
                            }
                        }
                    }
                    ChartBox {
                        // CAN 송출 지연 (작을수록 좋음) → AxisDot
                        MetricBlock {
                            name: "CAN 송출 지연"
                            valueText: kpiData.can ? (kpiData.canTxLatencyMs.toFixed(1) + " ms") : "데이터 없음"
                            valueColor: page.statusColor(kpiData.can, page.mMonTxOk)
                            AxisDot {
                                value: kpiData.canTxLatencyMs; max: 30; targetMax: config.targetCanTxLatencyMs
                                color: page.statusColor(kpiData.can, page.mMonTxOk)
                                loLabel: "0"; midLabel: "↑ target " + config.targetCanTxLatencyMs; hiLabel: "30 ms"
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
