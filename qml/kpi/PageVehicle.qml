import QtQuick
import QtQuick.Layouts

// SwipeView page 2 — Driving & Perception.
//   left  — Perception radar (55% width): header / radar (fills) / mini stats (fixed)
//   right — Vehicle State / Route / Mission (45% width)
Item {
    id: page

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16
        readonly property real avail: width - spacing

        // ── LEFT: PERCEPTION RADAR ──
        KpiPanel {
            Layout.fillHeight: true
            Layout.preferredWidth: row.avail * 0.55

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                // Header
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Text { text: "Perception · Detection"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 16 }
                        Text { text: "YOLO26 + LiDAR DBSCAN · 0x100"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13 }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        implicitWidth: hzTxt.implicitWidth + 20
                        implicitHeight: 22
                        radius: 11
                        color: Qt.rgba(1,1,1,0.06)
                        border.color: theme.hairline
                        Text { id: hzTxt; anchors.centerIn: parent; text: config.perceptionHz + " Hz"; color: theme.bodyMuted; font.pixelSize: 12 }
                    }
                }

                // Square radar — takes ALL the remaining vertical space, centred.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(parent.width, parent.height))
                        height: width
                        color: theme.tile3
                        radius: 16
                        border.color: theme.hairline
                        border.width: 1
                        clip: true
                        RadarChart {
                            anchors.fill: parent
                            obstacleDistM: kpiData.obstacleDistM
                            obstacleAngleDeg: kpiData.obstacleAngleDeg
                            obstacleClass: kpiData.obstacleClass
                            detected: kpiData.obstacleDetected   // hide blip when no real detection
                        }

                        // Range overlay (top-right glass panel)
                        Rectangle {
                            anchors.top: parent.top; anchors.right: parent.right
                            anchors.topMargin: 16; anchors.rightMargin: 16
                            implicitWidth: rangeCol.implicitWidth + 28
                            implicitHeight: rangeCol.implicitHeight + 20
                            radius: 12
                            color: theme.lightMode ? Qt.rgba(1,1,1,0.82) : Qt.rgba(0.16,0.16,0.17,0.82)
                            border.color: theme.hairlineStrong
                            border.width: 1
                            visible: kpiData.obstacleDetected
                            Column {
                                id: rangeCol
                                anchors.centerIn: parent
                                spacing: 3
                                Text { anchors.right: parent.right; text: "RANGE"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.6 }
                                Row {
                                    anchors.right: parent.right
                                    spacing: 4
                                    Text { text: kpiData.obstacleDistM.toFixed(1); color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 28; anchors.baseline: rangeUnit.baseline }
                                    Text { id: rangeUnit; text: "m"; color: theme.bodyMuted; font.pixelSize: 13 }
                                }
                                Text {
                                    anchors.right: parent.right
                                    text: (kpiData.obstacleAngleDeg >= 0 ? "+" : "") + kpiData.obstacleAngleDeg.toFixed(0) + "° · conf " + (kpiData.obstacleConf * 100).toFixed(0) + "%"
                                    color: theme.bodyMuted; font.family: theme.monoFont.family; font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                // Mini stats row — sizes to MiniStat's fixed preferredHeight (96).
                // No fillHeight anywhere in this subtree, so it can't propagate
                // greedy sizing up and steal the radar's space.
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 10

                    MiniStat {
                        label: "Live Confidence"
                        value: kpiData.hasObstacle ? (kpiData.obstacleConf * 100).toFixed(0) : "—"
                        unit:  kpiData.hasObstacle ? "%" : ""
                    }
                    MiniStat {
                        label: "Session Rate"
                        value: kpiData.hasSession ? kpiData.sessionRateHz : "—"
                        unit:  kpiData.hasSession ? "Hz" : ""
                        good:  kpiData.hasSession && kpiData.sessionRateHz >= 10
                    }
                    MiniStat {
                        label: "Detect Latency"
                        value: kpiData.hasRealtimeKpi ? kpiData.detectLatencyMs.toFixed(0) : "—"
                        unit:  kpiData.hasRealtimeKpi ? "ms" : ""
                        warn:  kpiData.hasRealtimeKpi && kpiData.detectLatencyMs > config.targetDetectLatencyMs
                    }
                    MiniStat {
                        label: "Distance"
                        value: kpiData.hasObstacle ? kpiData.obstacleDistM.toFixed(1) : "—"
                        unit:  kpiData.hasObstacle ? "m" : ""
                        warn:  kpiData.hasObstacle && kpiData.obstacleDistM < 4
                    }
                }
            }
        }

        // ── RIGHT: VEHICLE / ROUTE / MISSION ──
        ColumnLayout {
            id: rightCol
            Layout.fillHeight: true
            Layout.preferredWidth: row.avail * 0.45
            spacing: 16
            // Derive band from the STABLE parent height (row is anchors.fill,
            // so row.height never depends on us). Referencing rightCol.height
            // here instead would feed back into our own implicit-height
            // calculation → "Detected recursive rearrange".
            readonly property real band: row.height - 2 * spacing

            // VEHICLE STATE — now fills the whole right column (RouteProgress
            // moved to the Tactical page; its 1D track was redundant with the
            // BEV corridor). The freed space makes the velocity/steering
            // gauges noticeably larger.
            KpiPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Clean stack: gauges (flexible) → compact engine tile → compact
                // fail-safe ladder. No fillHeight stretching of text cells.
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 16

                    // ── gauges row (takes the flexible upper space) ──
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 24

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 4
                            Text { text: "LINEAR VELOCITY"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12; font.letterSpacing: 0.6 }
                            VelocityArc {
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter
                                speed: kpiData.speedKmh; maxSpeed: 6
                                targetMin: config.targetSpeedKmhMin; targetMax: config.targetSpeedKmhMax
                                available: kpiData.hasVehicle
                            }
                            Text { Layout.alignment: Qt.AlignHCenter; text: "target " + config.targetSpeedKmhMin + " – " + config.targetSpeedKmhMax + " km/h"; color: theme.bodyDim; font.pixelSize: 12 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 4
                            Text { text: "STEERING ANGLE"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12; font.letterSpacing: 0.6 }
                            SteeringDial {
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter
                                deg: kpiData.steeringDeg; available: kpiData.hasVehicle
                            }
                            Text { Layout.alignment: Qt.AlignHCenter; text: "±25° range"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11 }
                        }
                    }

                    // ── engine tile (full-width, compact) ──
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        radius: 12
                        color: theme.lightMode ? Qt.rgba(41/255,151/255,255/255,0.08) : Qt.rgba(41/255,151/255,255/255,0.10)
                        border.color: Qt.rgba(41/255,151/255,255/255,0.22)
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                            ColumnLayout {
                                spacing: 1
                                Text { text: "ENGINE"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.6 }
                                Row {
                                    spacing: 6
                                    Text { text: kpiData.optMode; color: theme.primaryOnDark; font.family: theme.monoFont.family; font.weight: Font.Bold; font.pixelSize: 22; anchors.baseline: engM.baseline }
                                    Text { id: engM; text: "· " + kpiData.yoloModel; color: theme.bodyMuted; font.pixelSize: 13 }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ColumnLayout {
                                spacing: 1
                                Text { text: "RUNTIME"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.6; Layout.alignment: Qt.AlignRight }
                                Text { text: (kpiData.yoloModel === "YOLO26n" ? "Light" : "Heavy") + " · TensorRT"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 13; Layout.alignment: Qt.AlignRight }
                            }
                        }
                    }

                    // ── fail-safe ladder (compact fixed-height rows) ──
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "DRIVE STATE · FAIL-SAFE"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.6 }
                        // B2 — behavior FSM pill (derived until 0x109)
                        Rectangle {
                            implicitWidth: bvRow.implicitWidth + 16; implicitHeight: 22; radius: 11
                            property color bc: kpiData.behaviorState === "OBSTACLE_STOP" ? theme.critical
                                             : (kpiData.behaviorState === "NUDGE" || kpiData.behaviorState === "OVERTAKE") ? theme.warning
                                             : kpiData.behaviorState === "LANE_FOLLOW" ? theme.good : theme.bodyDim
                            color: Qt.rgba(bc.r, bc.g, bc.b, 0.12); border.color: bc
                            Row { id: bvRow; anchors.centerIn: parent; spacing: 5
                                Text { text: "▸"; color: parent.parent.bc; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: kpiData.behaviorState; color: parent.parent.bc; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "FS Δ " + (kpiData.failsafeMaxTransitionMs > 0 ? kpiData.failsafeLastTransitionMs.toFixed(0) + " ms · ≤100" : "— ms · ≤100")
                            color: kpiData.failsafeLastTransitionMs > 100 ? theme.warning : theme.bodyMuted
                            font.family: theme.monoFont.family; font.pixelSize: 11
                        }
                    }
                    Repeater {
                        model: [
                            { id: 1, label: "Heavy",     color: theme.good },
                            { id: 2, label: "Light",     color: theme.warning },
                            { id: 3, label: "AI Bypass", color: theme.critical },
                            { id: 4, label: "E-Stop",    color: theme.critical }
                        ]
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            radius: 8
                            property bool isActive: kpiData.failsafeLevel === modelData.id
                            property bool isPast: modelData.id < kpiData.failsafeLevel
                            color: isActive ? Qt.rgba(modelData.color.r, modelData.color.g, modelData.color.b, 0.18)
                                            : (theme.lightMode ? Qt.rgba(0,0,0,0.03) : Qt.rgba(1,1,1,0.03))
                            border.color: isActive ? modelData.color : "transparent"
                            border.width: isActive ? 1 : 0
                            opacity: isPast ? 0.45 : 1.0
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left; anchors.leftMargin: 12
                                spacing: 10
                                Text { text: "L" + modelData.id; color: parent.parent.isActive ? modelData.color : theme.bodyDim; font.family: theme.monoFont.family; font.weight: Font.Bold; font.pixelSize: 12; width: 18; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData.label; color: parent.parent.isActive ? theme.bodyText : theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
                            }
                            Rectangle {
                                visible: parent.isActive
                                anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                                width: 8; height: 8; radius: 4; color: modelData.color
                            }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.preferredHeight: 1 }
                }
            }
        }
    }
}
