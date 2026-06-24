import QtQuick
import QtQuick.Layouts

// Matches app.jsx CanStatus:
//   header (title + "online" pill)
//   GridLayout 5 columns of KV pairs
//   "target TX latency ≤ 10 ms" hint
//   Flow of 4 ID badges (wraps if needed)
KpiPanel {
    id: panel
    Layout.fillWidth: true
    Layout.minimumHeight: 180

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Item { Layout.fillHeight: true }   // top spacer → centres block

        // Header
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Text { text: "CAN Bus"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 16 }
                Text { text: "500 kbps · 120Ω terminated"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                readonly property color stColor: kpiData.can ? theme.good : theme.bodyDim
                implicitWidth: onlineRow.implicitWidth + 16
                implicitHeight: 22
                radius: 11
                color: kpiData.can ? Qt.rgba(48/255, 209/255, 88/255, 0.10) : Qt.rgba(1,1,1,0.04)
                border.color: kpiData.can ? Qt.rgba(48/255, 209/255, 88/255, 0.32) : theme.hairline
                Row {
                    id: onlineRow
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: parent.parent.stColor
                        anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity {
                            running: kpiData.can
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.4; duration: 800 }
                            NumberAnimation { to: 1.0; duration: 800 }
                        }
                    }
                    Text {
                        text: kpiData.can ? "online" : "no data"
                        color: parent.parent.stColor
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // 5-column KV grid
        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 12
            rowSpacing: 0

            KvCell { label: "TX latency"; value: kpiData.can ? kpiData.canTxLatencyMs.toFixed(1) : "—"; unit: kpiData.can ? "ms" : ""; warn: kpiData.canTxLatencyMs > 10 }
            KvCell { label: "Bus load";   value: kpiData.can ? kpiData.busLoad.toFixed(0) : "—";       unit: kpiData.can ? "%"  : "" }
            KvCell { label: "Loss";       value: kpiData.can ? kpiData.frameLossPct.toFixed(2) : "—";  unit: kpiData.can ? "%"  : ""; warn: kpiData.frameLossPct > 0.1 }
            KvCell { label: "TX"; value: kpiData.can ? kpiData.framesTx.toLocaleString(Qt.locale("en_US"), 'f', 0) : "—"; mono: true }
            KvCell { label: "RX"; value: kpiData.can ? kpiData.framesRx.toLocaleString(Qt.locale("en_US"), 'f', 0) : "—"; mono: true }
        }

        Text { text: "target TX latency ≤ " + config.targetCanTxLatencyMs.toFixed(0) + " ms"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }

        // ── Every message ID in valeo_project_can.dbc ──
        // RX (Jetson → PC_UI) plus the one TX frame (0x200, PC_UI → Jetson).
        // The dot is green once that ID has actually been seen on the bus.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [
                    { id: "0x100", name: "obstacle",  seen: kpiData.hasObstacle,      tx: false },
                    { id: "0x101", name: "vehicle",   seen: kpiData.hasVehicle,       tx: false },
                    { id: "0x102", name: "kpi",       seen: kpiData.hasRealtimeKpi,   tx: false },
                    { id: "0x103", name: "system",    seen: kpiData.hasMemory,        tx: false },
                    { id: "0x104", name: "route",     seen: kpiData.hasMission,       tx: false },
                    { id: "0x105", name: "hardware",  seen: kpiData.hasHardware,      tx: false },
                    { id: "0x106", name: "planning",  seen: kpiData.hasPathPlan,            tx: false },
                    { id: "0x107", name: "perception",seen: kpiData.perceptionTotalRuns > 0,tx: false },
                    { id: "0x108", name: "datum",     seen: kpiData.hasDatum,               tx: false },
                    { id: "0x109", name: "network",   seen: kpiData.hasNetwork,             tx: false },
                    { id: "0x10A", name: "loc",       seen: kpiData.hasLocalization,        tx: false },
                    { id: "0x10B", name: "behavior",  seen: kpiData.behaviorFromCan,        tx: false },
                    { id: "0x10C", name: "map-info",  seen: kpiData.hasMapInfo,             tx: false },
                    { id: "0x10D", name: "ego-pose",  seen: kpiData.hasEgoPose,             tx: false },
                    { id: "0x20",  name: "encoder",   seen: kpiData.hasEncoder,             tx: false },
                    { id: "0x21",  name: "imu",       seen: kpiData.hasImu,                 tx: false },
                    { id: "0x1FF", name: "failsafe",  seen: kpiData.hasFailsafeEvent, tx: false },
                    { id: "0x200", name: "ui-cmd",    seen: kpiData.framesTx > 0,     tx: true }
                ]
                delegate: Rectangle {
                    height: 24
                    width: badgeRow.implicitWidth + 16
                    radius: 6
                    color: modelData.tx
                           ? Qt.rgba(41/255, 151/255, 255/255, 0.10)
                           : theme.tile3
                    border.color: modelData.tx ? Qt.rgba(41/255, 151/255, 255/255, 0.32) : theme.hairline
                    border.width: 1
                    Row {
                        id: badgeRow
                        anchors.centerIn: parent
                        spacing: 5
                        Rectangle {
                            width: 6; height: 6; radius: 3
                            anchors.verticalCenter: parent.verticalCenter
                            color: modelData.seen
                                   ? (modelData.tx ? theme.primaryOnDark : theme.good)
                                   : theme.hairlineStrong
                        }
                        Text {
                            text: modelData.id + " " + modelData.name
                            color: modelData.tx ? theme.primaryOnDark : theme.bodyMuted
                            font.family: theme.monoFont.family
                            font.pixelSize: 11
                            font.letterSpacing: 0.2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.tx ? "TX" : "RX"
                            color: theme.bodyDim
                            font.family: theme.monoFont.family
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
