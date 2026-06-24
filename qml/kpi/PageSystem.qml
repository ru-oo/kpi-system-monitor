import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// SwipeView page 3 — System & Events.
// CAN Bus + Pi5 (top) · Raw CAN-frame monitor (middle) · Fail-safe stream (bottom).
Item {
    id: page

    // iPad's landscape page viewport (~520px after the fixed top chrome) is far
    // shorter than this page's content needs (~880px), so the fixed-fraction
    // bands collapsed and the CAN / Pi5 / sensor panels were cut off at the
    // bottom. The content lives in a Flickable that keeps a comfortable minimum
    // height and scrolls when the viewport is shorter — nothing is clipped on
    // iPad, and on a tall desktop window the bands still fill the viewport.
    readonly property real minContentHeight: 900

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
            spacing: 16
            readonly property real band: height - 2 * spacing

            // ── CAN BUS + PI5 ──
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: col.band * 0.30
                spacing: 16
                readonly property real avail: width - spacing

                CanStatus {
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.avail * 0.6
                }
                Pi5Status {
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.avail * 0.4
                }
            }

            // ── RAW CAN-FRAME MONITOR (bus-level observation evidence) ──
            KpiPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: col.band * 0.32

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Raw CAN Monitor"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 16 }
                        Text { text: "bus-level · last " + rawFrames.frames.length + " frames"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                        // 0x10D Ego_Pose reception: live / stale (frozen) / none.
                        Rectangle {
                            implicitWidth: egoR.implicitWidth + 18; implicitHeight: 24; radius: 12
                            property color ec: !kpiData.hasEgoPose ? theme.critical
                                             : kpiData.egoOk ? theme.good : theme.warning
                            color: Qt.rgba(ec.r, ec.g, ec.b, 0.12); border.color: ec
                            Row { id: egoR; anchors.centerIn: parent; spacing: 6
                                Rectangle { width: 7; height: 7; radius: 3.5; color: parent.parent.ec; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "0x10D " + (!kpiData.hasEgoPose ? "none" : kpiData.egoOk ? "live" : "stale"); color: parent.parent.ec; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            }
                            HoverHandler { id: egoHov }
                            ToolTip.visible: egoHov.hovered
                            ToolTip.text: !kpiData.hasEgoPose ? "Ego_Pose(0x10D) 미수신 — 직선 스톱갭 표시 중"
                                        : kpiData.egoOk ? "Ego_Pose 수신 중 (live)"
                                        : ("Ego_Pose 끊김 — 마지막 위치 고정(stale), " + kpiData.egoSilentMs + "ms 무신호")
                        }
                        // 0x108 Map_Datum source: live CAN datum vs config fallback.
                        // Config fallback ≠ Jetson's localization origin → ego/lane misalignment.
                        Rectangle {
                            implicitWidth: datR.implicitWidth + 18; implicitHeight: 24; radius: 12
                            property color dc: kpiData.hasDatum ? theme.good : theme.warning
                            color: Qt.rgba(dc.r, dc.g, dc.b, 0.12); border.color: dc
                            Row { id: datR; anchors.centerIn: parent; spacing: 6
                                Rectangle { width: 7; height: 7; radius: 3.5; color: parent.parent.dc; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "0x108 " + (kpiData.hasDatum ? "CAN" : "cfg"); color: parent.parent.dc; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            }
                            HoverHandler { id: datHov }
                            ToolTip.visible: datHov.hovered
                            ToolTip.text: kpiData.hasDatum ? "Map_Datum(0x108) CAN 수신 — Jetson datum으로 ENU 정렬"
                                                           : "Map_Datum(0x108) 미수신 — config 폴백 datum 사용 (Jetson 원점과 다르면 ego/차선 어긋남)"
                        }
                        Text { text: rawFrames.total + " rx"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 12 }
                    }

                    // column header
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "ID";    color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; Layout.preferredWidth: 70 }
                        Text { text: "DLC";   color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; Layout.preferredWidth: 40 }
                        Text { text: "DATA";  color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; Layout.fillWidth: true }
                        Text { text: "RATE";  color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; Layout.preferredWidth: 64 }
                        Text { text: "CNT";   color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 10; Layout.preferredWidth: 40 }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }

                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        model: rawFrames.frames
                        delegate: RowLayout {
                            required property var modelData
                            width: ListView.view.width
                            height: 22
                            Text { text: modelData.idHex; color: theme.primaryOnDark; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.preferredWidth: 70 }
                            Text { text: modelData.dlc; color: theme.bodyMuted; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.preferredWidth: 40 }
                            Text { text: modelData.hex; color: theme.bodyText; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { text: modelData.rateHz > 0 ? modelData.rateHz.toFixed(0) + "Hz" : "—"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.preferredWidth: 64 }
                            Text { text: modelData.counter; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.preferredWidth: 40 }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: rawFrames.frames.length === 0
                            text: "waiting for CAN frames…"
                            color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13
                        }
                    }
                }
            }

            // ── FAIL-SAFE EVENT STREAM ──
            LogPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: col.band * 0.38
            }
        }
    }
}
