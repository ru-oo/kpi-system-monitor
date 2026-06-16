import QtQuick
import QtQuick.Layouts

// Matches app.jsx MemoryTile: header + RAM (4px bar) + Swap (3px bar)
KpiPanel {
    id: panel

    Layout.fillWidth: true
    Layout.minimumHeight: 160
    Layout.preferredHeight: 160

    readonly property bool has: kpiData.hasMemory
    readonly property real ramPct: panel.has ? kpiData.ramUsedGb / Math.max(kpiData.ramTotalGb, 0.001) : 0
    readonly property real swapPct: panel.has ? kpiData.swapUsedGb / Math.max(kpiData.swapTotalGb, 0.001) : 0
    readonly property bool ramWarn: ramPct >= 0.85
    readonly property bool ramCrit: ramPct >= 0.95
    readonly property color ramColor: ramCrit ? theme.critical : (ramWarn ? theme.warning : theme.primaryOnDark)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "MEMORY · ORIN " + (kpiData.hasHardware ? kpiData.orinMemoryGb.toFixed(0) + "GB" : "—")
                color: theme.bodyDim
                font.family: theme.defaultFont.family
                font.weight: Font.Medium
                font.pixelSize: 13
                font.letterSpacing: 0.4
                Layout.fillWidth: true
            }
            Rectangle {
                visible: panel.ramWarn || panel.ramCrit
                width: 8; height: 8; radius: 4
                color: panel.ramColor
            }
        }

        // RAM row
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Text { text: "RAM"; color: theme.bodyMuted; font.pixelSize: 13; Layout.fillWidth: true }
                Item {
                    implicitWidth: ramV.implicitWidth + 4 + ramU.implicitWidth
                    implicitHeight: ramV.implicitHeight
                    Text {
                        id: ramV
                        text: panel.has ? kpiData.ramUsedGb.toFixed(1) : "—"
                        color: panel.ramCrit ? theme.critical : (panel.ramWarn ? theme.warning : theme.bodyText)
                        font.family: theme.defaultFont.family
                        font.weight: Font.DemiBold
                        font.pixelSize: 24
                    }
                    Text {
                        id: ramU
                        text: panel.has ? ("/ " + kpiData.ramTotalGb.toFixed(0) + " GB") : ""
                        color: theme.bodyMuted
                        font.pixelSize: 13
                        anchors.left: ramV.right
                        anchors.leftMargin: 4
                        anchors.baseline: ramV.baseline
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 4
                radius: 2
                color: theme.lightMode ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.08)
                Rectangle {
                    height: parent.height
                    width: parent.width * Math.min(1, panel.ramPct)
                    radius: 2
                    color: panel.ramColor
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
        }

        // Swap row
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Swap (NVMe)"; color: theme.bodyMuted; font.pixelSize: 13; Layout.fillWidth: true }
                Item {
                    implicitWidth: swpV.implicitWidth + 4 + swpU.implicitWidth
                    implicitHeight: swpV.implicitHeight
                    Text {
                        id: swpV
                        text: panel.has ? kpiData.swapUsedGb.toFixed(1) : "—"
                        color: theme.bodyText
                        font.family: theme.defaultFont.family
                        font.weight: Font.DemiBold
                        font.pixelSize: 18
                    }
                    Text {
                        id: swpU
                        text: panel.has ? ("/ " + kpiData.swapTotalGb.toFixed(0) + " GB") : ""
                        color: theme.bodyMuted
                        font.pixelSize: 12
                        anchors.left: swpV.right
                        anchors.leftMargin: 4
                        anchors.baseline: swpV.baseline
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 3
                radius: 1.5
                color: theme.lightMode ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.08)
                Rectangle {
                    height: parent.height
                    width: parent.width * Math.min(1, panel.swapPct)
                    radius: 1.5
                    color: theme.bodyMuted
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
