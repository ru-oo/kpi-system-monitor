import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// Top-level dashboard shell.
//
// Layout (top → bottom, all fixed except the SwipeView):
//   Top Nav            — logo, status pills, theme toggle
//   KPI Controls bar   — precision/model segments, AMCL Init, E-STOP
//                        (kept ALWAYS visible — E-STOP must be reachable on
//                         every page; the navigation goal is set by tapping
//                         the 2D Route track on the Driving page)
//   Tab bar            — 4 category tabs
//   SwipeView          — one ScrollView page per category (swipe or tap)
//   PageIndicator      — dots
//
// Feedback addressed: the single-screen dense layout is split into 4
// swipeable category pages so each view is readable on the iPad.
Item {
    id: root

    // Category pages
    readonly property var tabLabels: ["AI Inference", "Driving & Perception", "KPI Validation", "System & Events", "Tactical", "Runs", "Debug"]

    // E-STOP latch. Once engaged it STAYS engaged (the worker keeps re-asserting
    // Estop_Command=1 so the vehicle holds the stop) until a deliberate
    // hold-to-reset. Prevents the old momentary "stops only while pressing".
    property bool estopEngaged: false

    // DRIVE-session clock. Starts when a destination/goal is set, resets to 0 on
    // arrival (vehicle returns to IDLE after an AUTO drive) or when the goal is
    // cleared. (Previously app-uptime, which started counting at launch.)
    property int driveSeconds: 0
    property double _driveStartMs: 0
    property bool _driveRunning: false    // counting toward an active goal
    property bool _sawAuto: false         // entered AUTO since goal set (arrival detect)
    function _resetDrive() { _driveRunning = false; _sawAuto = false; driveSeconds = 0 }
    Connections {
        target: kpiData
        function onGoalChanged() {
            if (kpiData.goalActive) {
                root._driveStartMs = kpiData.uptimeMs; root.driveSeconds = 0
                root._sawAuto = false; root._driveRunning = true
            } else {
                root._resetDrive()                       // clear goal → reset
            }
        }
        function onKpiChanged() {
            if (!root._driveRunning) return
            var ds = kpiData.driveState
            if (ds === "AUTO" || ds === "AVOID") root._sawAuto = true
            else if (root._sawAuto && ds === "IDLE") root._resetDrive()   // arrived → reset
        }
    }
    Timer {
        interval: 1000; running: root._driveRunning; repeat: true
        onTriggered: root.driveSeconds = Math.max(0, Math.floor((kpiData.uptimeMs - root._driveStartMs) / 1000))
    }
    function formatDuration(s) {
        var h = Math.floor(s / 3600);
        var m = Math.floor((s % 3600) / 60);
        var sec = s % 60;
        return (h < 10 ? "0" + h : h) + ":" + (m < 10 ? "0" + m : m) + ":" + (sec < 10 ? "0" + sec : sec);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ───────────────────────── TOP NAV ─────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: theme.lightMode ? "#ffffff" : "#000000"
            Rectangle { width: parent.width; height: 1; color: theme.hairline; anchors.bottom: parent.bottom }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 14

                Rectangle {
                    width: 32; height: 32; radius: 9
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: theme.primaryOnDark }
                        GradientStop { position: 1.0; color: theme.primary }
                    }
                    Text { anchors.centerIn: parent; text: "K"; color: "#fff"; font.pixelSize: 17; font.weight: Font.Bold }
                }
                Text { text: "KPI Monitor"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 19 }
                Text { text: "· " + config.platformLabel + " · " + config.modelFamily + " · " + config.teamLabel; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 14 }

                Item { Layout.fillWidth: true }

                Row {
                    spacing: 10

                    // DRIVE
                    Rectangle {
                        height: 30
                        implicitWidth: driveRow.implicitWidth + 20
                        width: implicitWidth
                        radius: 12
                        color: Qt.rgba(1,1,1,0.06)
                        border.color: theme.hairline
                        Row {
                            id: driveRow
                            anchors.centerIn: parent
                            spacing: 6
                            Text { text: "DRIVE"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: formatDuration(driveSeconds); color: theme.bodyText; font.family: theme.monoFont.family; font.pixelSize: 11; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }

                    // WIFI — 0x109 Network_Status. Colour reflects Network_Status
                    // enum (Online/Timeout/Error) when present, else ping band.
                    // Text appends RSSI; hover tooltip shows loss + state label.
                    Rectangle {
                        id: wifiPill
                        // Network_Status enum: 0 No_Data, 1 Online, 2 Timeout, 3 Error
                        readonly property int netState: kpiData.networkState
                        property color wifiColor: !kpiData.hasNetwork
                                                  ? theme.bodyDim
                                                  : netState === 3 ? theme.critical
                                                  : netState === 2 ? theme.warning
                                                  : (kpiData.wifiPingMs > 80 ? theme.critical
                                                     : (kpiData.wifiPingMs > 50 ? theme.warning : theme.good))
                        height: 30
                        implicitWidth: wifiRow.implicitWidth + 20
                        width: implicitWidth
                        radius: 12
                        color: "transparent"
                        border.color: wifiColor
                        Row {
                            id: wifiRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.wifiColor; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: kpiData.hasNetwork
                                      ? ("WIFI " + kpiData.wifiPingMs.toFixed(0) + "ms"
                                         + (kpiData.wifiRssiDbm !== 0 ? " · " + kpiData.wifiRssiDbm + "dBm" : ""))
                                      : "WIFI —"
                                color: theme.bodyText
                                font.family: theme.monoFont.family
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        HoverHandler { id: wifiHover }
                        ToolTip.visible: wifiHover.hovered && kpiData.hasNetwork
                        ToolTip.text: "loss " + kpiData.networkLossRatePct.toFixed(1) + "% · "
                                      + ["No data","Online","Timeout","Error"][kpiData.networkState]
                    }

                    // CAN
                    Rectangle {
                        height: 30
                        implicitWidth: canRow.implicitWidth + 20
                        width: implicitWidth
                        radius: 12
                        color: "transparent"
                        border.color: kpiData.can ? theme.bodyText : theme.critical
                        Row {
                            id: canRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: kpiData.can ? theme.good : theme.critical; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                text: kpiData.can ? ("CAN · " + Math.round(kpiData.canBitrate / 1000) + "k") : "CAN · —"
                                color: theme.bodyText
                                font.family: theme.defaultFont.family
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }

                    // FS
                    Rectangle {
                        property color fsColor: kpiData.failsafeLevel === 1 ? theme.good : (kpiData.failsafeLevel === 2 ? theme.warning : theme.critical)
                        property string fsLabel: kpiData.failsafeLevel === 1 ? "L1 Heavy" : (kpiData.failsafeLevel === 2 ? "L2 Light" : (kpiData.failsafeLevel === 3 ? "L3 AI Bypass" : "L4 E-Stop"))
                        height: 30
                        implicitWidth: fsRow.implicitWidth + 20
                        width: implicitWidth
                        radius: 12
                        color: "transparent"
                        border.color: fsColor
                        Row {
                            id: fsRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.fsColor; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "FS · " + parent.parent.fsLabel; color: theme.bodyText; font.family: theme.defaultFont.family; font.pixelSize: 11; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }

                    // Theme Toggle
                    Rectangle {
                        height: 30
                        implicitWidth: themeTxt.implicitWidth + 20
                        width: implicitWidth
                        radius: 12
                        color: "transparent"
                        border.color: theme.hairlineStrong
                        Text {
                            id: themeTxt
                            anchors.centerIn: parent
                            text: theme.lightMode ? "☀ Light" : "◐ Dark"
                            color: theme.bodyText
                            font.family: theme.defaultFont.family
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: theme.lightMode = !theme.lightMode
                        }
                    }
                }
            }
        }

        // ─────────────────── KPI CONTROLS (always visible) ───────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            color: theme.tile2
            Rectangle { width: parent.width; height: 1; color: theme.hairline; anchors.bottom: parent.bottom }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 22

                Row {
                    spacing: 10; Layout.alignment: Qt.AlignVCenter
                    Rectangle { width: 10; height: 10; radius: 5; color: theme.primaryOnDark; anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { to: 0.4; duration: 800 } NumberAnimation { to: 1.0; duration: 800 } } }
                    Text { text: "KPI Controls"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                }
                Rectangle { width: 1; height: 36; color: theme.hairline }

                // Precision segment
                Text { text: "PRECISION"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; font.letterSpacing: 0.8 }
                Rectangle {
                    width: precRow.implicitWidth + 6
                    height: 48
                    radius: 11
                    color: theme.lightMode ? Qt.rgba(0,0,0,0.06) : Qt.rgba(0,0,0,0.32)
                    border.color: theme.hairline
                    Row {
                        id: precRow
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 2
                        Repeater {
                            model: ["FP32", "FP16", "INT8"]
                            delegate: Rectangle {
                                id: precBtn
                                property bool selected: modelData === kpiData.optMode
                                height: parent.height
                                width: Math.max(64, txt1.implicitWidth + 28)
                                radius: 8
                                color: selected
                                       ? theme.primaryOnDark
                                       : (precMA.pressed
                                            ? (theme.lightMode ? Qt.rgba(0,0,0,0.08) : Qt.rgba(255,255,255,0.10))
                                            : "transparent")
                                Behavior on color { ColorAnimation { duration: 140 } }
                                scale: precMA.pressed ? 0.96 : 1.0
                                Behavior on scale { NumberAnimation { duration: 80 } }
                                Text {
                                    id: txt1
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: precBtn.selected ? "#ffffff" : theme.bodyMuted
                                    font.family: theme.monoFont.family
                                    font.weight: Font.DemiBold
                                    font.pixelSize: 13
                                    font.letterSpacing: 0.3
                                }
                                MouseArea {
                                    id: precMA
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        kpiData.optMode = modelData            // optimistic local update
                                        canBridge.sendSetPrecision(modelData)  // 0x200 UI_Command → Jetson
                                    }
                                }
                            }
                        }
                    }
                }

                // Model segment
                Text { text: "MODEL"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; font.letterSpacing: 0.8 }
                Rectangle {
                    width: modelRow.implicitWidth + 6
                    height: 48
                    radius: 11
                    color: theme.lightMode ? Qt.rgba(0,0,0,0.06) : Qt.rgba(0,0,0,0.32)
                    border.color: theme.hairline
                    Row {
                        id: modelRow
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 2
                        Repeater {
                            model: ["YOLO26s", "YOLO26n"]
                            delegate: Rectangle {
                                id: modelBtn
                                property bool selected: modelData === kpiData.yoloModel
                                height: parent.height
                                width: Math.max(80, txt2.implicitWidth + 28)
                                radius: 8
                                color: selected
                                       ? theme.primaryOnDark
                                       : (modelMA.pressed
                                            ? (theme.lightMode ? Qt.rgba(0,0,0,0.08) : Qt.rgba(255,255,255,0.10))
                                            : "transparent")
                                Behavior on color { ColorAnimation { duration: 140 } }
                                scale: modelMA.pressed ? 0.96 : 1.0
                                Behavior on scale { NumberAnimation { duration: 80 } }
                                Text {
                                    id: txt2
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: modelBtn.selected ? "#ffffff" : theme.bodyMuted
                                    font.family: theme.monoFont.family
                                    font.weight: Font.DemiBold
                                    font.pixelSize: 13
                                    font.letterSpacing: 0.3
                                }
                                MouseArea {
                                    id: modelMA
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        kpiData.yoloModel = modelData
                                        canBridge.sendSetModel(modelData)
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // ── FAIL-SAFE INDICATOR ──
                // Was a full-width banner row below this bar; when it appeared it
                // pushed the whole dashboard (incl. the SwipeView) down and the
                // page bands overlapped. Living in the controls bar's spare space
                // costs zero extra vertical room and keeps the page viewport
                // stable whether or not fail-safe is active. Full reason text is
                // on hover (desktop); level + short name read at a glance.
                Rectangle {
                    id: fsBanner
                    visible: kpiData.failsafeLevel > 1
                    width: fsBannerRow.implicitWidth + 26
                    height: 50
                    radius: 12
                    property color alertColor: kpiData.failsafeLevel >= 3 ? theme.critical : theme.warning
                    property string fsName: kpiData.failsafeLevel === 2 ? "Light · downscale"
                                          : (kpiData.failsafeLevel === 3 ? "AI Bypass" : "EMERGENCY")
                    property string fsDetail: kpiData.failsafeLevel === 2 ? "L2 Light — Heavy → Light, downscale active"
                                            : (kpiData.failsafeLevel === 3 ? "L3 AI Bypass — GPU>95%, LiDAR DBSCAN only"
                                               : "L4 EMERGENCY — Motor PWM = 0")
                    color: theme.lightMode
                           ? Qt.rgba(alertColor.r, alertColor.g, alertColor.b, 0.10)
                           : Qt.rgba(alertColor.r, alertColor.g, alertColor.b, 0.14)
                    border.color: alertColor
                    border.width: 1

                    SequentialAnimation on opacity {
                        running: fsBanner.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.8; duration: 1000; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 1000; easing.type: Easing.InOutSine }
                    }

                    Row {
                        id: fsBannerRow
                        anchors.centerIn: parent
                        spacing: 12
                        Rectangle {
                            width: 34; height: 34; radius: 10
                            anchors.verticalCenter: parent.verticalCenter
                            color: Qt.rgba(fsBanner.alertColor.r, fsBanner.alertColor.g, fsBanner.alertColor.b, 0.20)
                            Text { anchors.centerIn: parent; text: "⚠"; color: fsBanner.alertColor; font.pixelSize: 18 }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Text { text: "FAIL-SAFE ACTIVE"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 10; font.letterSpacing: 0.9 }
                            Text { text: "L" + kpiData.failsafeLevel + " · " + fsBanner.fsName; color: fsBanner.alertColor; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 16 }
                        }
                        // vertical divider + inline detail (no longer hover-only → reads at a glance)
                        Rectangle { width: 1; height: 32; color: Qt.rgba(fsBanner.alertColor.r, fsBanner.alertColor.g, fsBanner.alertColor.b, 0.35); anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: fsBanner.fsDetail
                            color: theme.bodyMuted
                            font.family: theme.defaultFont.family
                            font.pixelSize: 13
                        }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: fsBanner.alertColor
                            SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1.0; duration: 700 } }
                        }
                    }

                    HoverHandler { id: fsHover }
                    ToolTip.visible: fsHover.hovered
                    ToolTip.text: fsBanner.fsDetail
                }

                Item { Layout.fillWidth: true }

                // E-STOP
                Rectangle {
                    id: estopBtn
                    width: 124; height: 48; radius: 14
                    // Trendy depth: vertical gradient (darkens on press), softer
                    // corners, brighter ring when latched. Reuses theme.critical shades.
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: estopMA.pressed ? Qt.darker(theme.critical, 1.35) : Qt.lighter(theme.critical, 1.10) }
                        GradientStop { position: 1.0; color: estopMA.pressed ? Qt.darker(theme.critical, 1.50) : Qt.darker(theme.critical, 1.12) }
                    }
                    border.color: root.estopEngaged ? Qt.lighter(theme.critical, 1.35) : Qt.darker(theme.critical, 1.15)
                    border.width: root.estopEngaged ? 3 : 1.5
                    // Engaged → pulse to signal a LATCHED emergency stop.
                    SequentialAnimation on opacity {
                        running: root.estopEngaged
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.55; duration: 500 }
                        NumberAnimation { to: 1.0;  duration: 500 }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.estopEngaged ? "E-STOP ●" : "E-STOP"
                            color: "#fff"
                            font.family: theme.defaultFont.family
                            font.weight: Font.Bold
                            font.pixelSize: 13
                            font.letterSpacing: 0.5
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.estopEngaged
                            text: "hold to reset"
                            color: "#fff"
                            opacity: 0.85
                            font.family: theme.defaultFont.family
                            font.pixelSize: 8
                            font.letterSpacing: 0.4
                        }
                    }
                    MouseArea {
                        id: estopMA
                        property bool didHold: false
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onPressed: didHold = false
                        // First click ENGAGES (latched); the worker re-asserts until reset.
                        onClicked: {
                            if (didHold) { didHold = false; return }   // swallow click after a hold-reset
                            if (!root.estopEngaged) { root.estopEngaged = true; canBridge.sendEStop() }
                        }
                        // Deliberate ~0.8s hold RESETS — guards against accidental release.
                        onPressAndHold: {
                            if (root.estopEngaged) { didHold = true; root.estopEngaged = false; canBridge.clearEStop() }
                        }
                    }
                    scale: estopMA.pressed ? 0.95 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80 } }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width + 16
                        height: parent.height + 16
                        radius: parent.radius + 4
                        color: "transparent"
                        border.color: Qt.rgba(255/255, 69/255, 58/255, 0.5)
                        border.width: 2
                        z: -1
                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.0; duration: 1200; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 0.6; duration: 1200; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }
        }

        // ───────────────── STATUS TABS (rich vitals + selectors) ─────────────────
        Item {
            id: statusTabs
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 18
            Layout.bottomMargin: 4
            Layout.preferredHeight: 104

            // validation pass-count — same gated logic + config thresholds as
            // PageKpiValidation (single source of truth; no hardcoded numbers).
            readonly property bool valLoc:  kpiData.hasEgoPose
                                            && Math.abs(kpiData.laneCenterDeviationMm) <= config.laneCenterDevMmMax
                                            && (kpiData.totalRuns === 0 || kpiData.successRuns >= config.localizationSuccessRunsMin)
            readonly property bool valPlan: kpiData.hasPathPlan
                                            && kpiData.pathPlanSuccessRuns >= config.pathPlanSuccessRunsMin
                                            && kpiData.pathPlanLastMs <= config.pathPlanMsMax
            readonly property bool valCtrl: kpiData.hasRealtimeKpi && kpiData.pathDeviationMm <= config.targetPathDeviationMm
            readonly property bool valPerc: (kpiData.perceptionTotalRuns > 0 || kpiData.totalRuns > 0)
                                            && kpiData.perceptionDetectedRuns >= config.parkingDetectRunsMin
                                            && (!kpiData.hasRealtimeKpi || kpiData.detectLatencyMs <= config.targetDetectLatencyMs)
                                            && kpiData.falsePositiveCount < config.falsePositivePerRunMax * Math.max(kpiData.totalRuns, 1)
            readonly property int  valTotal: 6
            readonly property int  valPass: (valLoc?1:0) + (valPlan?1:0) + (valCtrl?1:0) + (valPerc?1:0)
            readonly property int  valWatch: valTotal - valPass

            RowLayout {
                anchors.fill: parent
                spacing: 12

                Repeater {
                    model: [
                        { num: "01", label: "AI Inference" },
                        { num: "02", label: "Driving" },
                        { num: "03", label: "KPI Validation" },
                        { num: "04", label: "System" },
                        { num: "05", label: "Tactical" },
                        { num: "06", label: "Runs" },
                        { num: "07", label: "Debug" }
                    ]
                    delegate: Rectangle {
                        id: stCard
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 16
                        property bool current: swipeView.currentIndex === index

                        // status: 0=good 1=warn 2=crit
                        property int statusLevel: {
                            if (index === 0) return kpiData.inferenceMs > 300 ? 2 : (kpiData.inferenceMs > 150 ? 1 : 0)
                            if (index === 1) return kpiData.failsafeLevel > 2 ? 2 : (kpiData.failsafeLevel > 1 ? 1 : (kpiData.obstacleDistM < 4 ? 1 : 0))
                            if (index === 2) return statusTabs.valWatch > 0 ? 1 : 0
                            if (index === 3) return !kpiData.pi5Online ? 2 : ((kpiData.canTxLatencyMs > 10 || kpiData.frameLossPct > 0.1) ? 1 : 0)
                            if (index === 4) return kpiData.hasVehicle ? 0 : 1   // Tactical
                            if (index === 5) return (kpiData.replaying || runRecorder.recording) ? 1 : 0   // Runs
                            return debugLink.linkUp ? 0 : 1   // 6: Debug (warn until a debug stream arrives)
                        }
                        property color statusColor: statusLevel === 2 ? theme.critical : (statusLevel === 1 ? theme.warning : theme.good)
                        property string primaryText: {
                            if (index === 0) return kpiData.inferenceMs.toFixed(0)
                            if (index === 1) return kpiData.speedKmh.toFixed(1)
                            if (index === 2) return statusTabs.valPass.toString()
                            if (index === 3) return statusLevel === 2 ? "LOST" : (statusLevel === 1 ? "DEGRADED" : "ONLINE")
                            if (index === 4) return kpiData.hasVehicle ? kpiData.progressM.toFixed(0) : "—"   // Tactical
                            if (index === 5) return kpiData.replaying ? "REPLAY" : (runRecorder.recording ? "REC" : runRecorder.runCount.toString())   // Runs
                            return debugLink.linkUp ? "LIVE" : "—"   // 6: Debug
                        }
                        property string unitText: {
                            if (index === 0) return "ms"
                            if (index === 1) return "km/h"
                            if (index === 2) return "/ " + statusTabs.valTotal
                            if (index === 3) return ""
                            if (index === 4) return "m"   // Tactical (along-route)
                            if (index === 5) return (kpiData.replaying || runRecorder.recording) ? "" : "runs"   // Runs
                            return ""   // 6: Debug
                        }
                        property string metaText: {
                            if (index === 0) return (kpiData.inferenceMs > 0 ? (config.ptBaselineMs / kpiData.inferenceMs).toFixed(2) : "—") + "× vs FP32"   // baseline = PyTorch FP32 (config.ptBaselineMs = 16.79 ms)
                            if (index === 1) return kpiData.obstacleDistM.toFixed(1) + "m · L" + kpiData.failsafeLevel
                            if (index === 2) return "PASS · " + statusTabs.valWatch + " watch"
                            if (index === 3) return "CAN " + kpiData.busLoad.toFixed(0) + "% · Pi5 " + kpiData.frameLossPct.toFixed(1) + "%"
                            if (index === 4) return kpiData.goalActive ? ("goal " + kpiData.goalDistM.toFixed(0) + "m") : (kpiData.driveState ? kpiData.driveState : "idle")   // Tactical
                            if (index === 5) return runRecorder.inRun ? (runRecorder.sampleCount + " samples") : (kpiData.replaying ? "playing back" : "record/replay")   // Runs
                            return debugLink.messageCount + " msgs"   // 6: Debug
                        }

                        color: current ? theme.tile2 : theme.tile2Dim
                        border.color: current ? statusColor : theme.hairline
                        border.width: current ? 2 : 1
                        clip: true
                        Behavior on border.color { ColorAnimation { duration: 180 } }

                        // faint top-edge tint when active (soft, contained — no
                        // hard glow disc; QML can't cheaply blur and it spilled)
                        Rectangle {
                            visible: stCard.current
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(stCard.statusColor.r, stCard.statusColor.g, stCard.statusColor.b, 0.10) }
                                GradientStop { position: 0.6; color: "transparent" }
                            }
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: stCard.modelData.num; color: theme.bodyDim; font.family: theme.monoFont.family; font.weight: Font.Bold; font.pixelSize: 11; font.letterSpacing: 0.6 }
                                Text { text: stCard.modelData.label; color: stCard.current ? theme.bodyText : theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 14 }
                                Item { Layout.fillWidth: true }
                                Rectangle {
                                    width: 9; height: 9; radius: 4.5; color: stCard.statusColor
                                    SequentialAnimation on opacity {
                                        running: stCard.statusLevel !== 0
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.35; duration: 700 } NumberAnimation { to: 1.0; duration: 700 }
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }

                            RowLayout {
                                spacing: 5
                                Text {
                                    text: stCard.primaryText
                                    color: stCard.statusLevel === 2 ? theme.critical : (stCard.statusLevel === 1 ? theme.warning : theme.bodyText)
                                    font.family: theme.defaultFont.family
                                    font.weight: Font.Bold
                                    font.pixelSize: (stCard.index === 3 || (stCard.index === 5 && (kpiData.replaying || runRecorder.recording))) ? 22 : 28
                                    Layout.alignment: Qt.AlignBottom
                                }
                                Text { visible: stCard.unitText.length > 0; text: stCard.unitText; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.pixelSize: 13; Layout.alignment: Qt.AlignBottom; bottomPadding: 3 }
                            }
                            Text { text: stCard.metaText; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12; topPadding: 4 }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: swipeView.currentIndex = stCard.index
                        }
                    }
                }
            }
        }

        // ───────────────────────── SWIPE VIEW ─────────────────────────
        SwipeView {
            id: swipeView
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0
            clip: true

            PagePerformance { }
            PageVehicle { }
            PageKpiValidation { }
            PageSystem { }
            PageTactical { }
            PageRuns { }
            PageDebug { }
        }

        // ─────────────────────── PAGE INDICATOR ───────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: theme.tile2
            Rectangle { width: parent.width; height: 1; color: theme.hairline; anchors.top: parent.top }

            PageIndicator {
                anchors.centerIn: parent
                count: swipeView.count
                currentIndex: swipeView.currentIndex
                interactive: true
                onCurrentIndexChanged: swipeView.currentIndex = currentIndex

                delegate: Rectangle {
                    property bool active: index === swipeView.currentIndex
                    implicitWidth: active ? 28 : 9
                    implicitHeight: 9
                    radius: 4.5
                    color: active ? theme.primaryOnDark : theme.dotInactive
                    Behavior on implicitWidth { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
            }
        }
    }
}
