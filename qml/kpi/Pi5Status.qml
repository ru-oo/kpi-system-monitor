import QtQuick
import QtQuick.Layouts

// Raspberry Pi 5 aux-camera link. The new DBC (0x103 System_Resource) only
// carries a Pi5_Status enum + a Camera_Status bitflag — there is no per-link
// latency / loss / last-seen signal, so those tiles are gone. We show the
// status text and the live camera count instead.
KpiPanel {
    id: panel
    Layout.fillWidth: true
    Layout.minimumHeight: 180

    readonly property bool has: kpiData.hasPi5
    // Pi5_Status: 0 No_Data, 1 Online, 2 Timeout, 3 Error
    readonly property color stColor: !has ? theme.bodyDim
                                   : kpiData.pi5StatusCode === 1 ? theme.good
                                   : kpiData.pi5StatusCode === 2 ? theme.warning
                                   : theme.critical

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
                Text { text: "Raspberry Pi 5 · Aux Cameras ×2"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 16 }
                Text { text: "UDP · fail-safe trigger if > 500ms silent"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: stRow.implicitWidth + 16
                implicitHeight: 22
                radius: 11
                color: "transparent"
                border.color: panel.stColor
                Row {
                    id: stRow
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4; color: panel.stColor
                        anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity {
                            running: panel.has && kpiData.pi5StatusCode === 1
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.4; duration: 800 }
                            NumberAnimation { to: 1.0; duration: 800 }
                        }
                    }
                    Text {
                        text: panel.has ? kpiData.pi5StatusText : "no data"
                        color: panel.stColor
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // Link state + camera count
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 0

            KvCell {
                label: "Link status"
                value: panel.has ? kpiData.pi5StatusText : "—"
                warn:  panel.has && kpiData.pi5StatusCode !== 1
            }
            KvCell {
                label: "Cameras online"
                value: panel.has ? (kpiData.cameraOnline + "/2") : "—"
                warn:  panel.has && kpiData.cameraOnline < 2
            }
            // ── Critical-sensor health (engineering review §4) ───────────
            // LiDAR freshness derived from 0x100 heartbeat in KpiData's
            // watchdog timer. IMU freshness from I2cImuBridge applyImu calls.
            KvCell {
                label: "LiDAR"
                value: kpiData.lidarOk ? "online" : "stale"
                warn:  !kpiData.lidarOk
            }
            KvCell {
                label: "IMU"
                value: kpiData.imuOk ? "online" : "stale"
                warn:  !kpiData.imuOk
            }
            // Drivetrain encoder freshness from 0x20 STM32_Encoder_Feedback.
            KvCell {
                label: "Encoder"
                value: kpiData.encoderOk ? "online" : "stale"
                warn:  !kpiData.encoderOk
            }
            // Ego-pose (0x10D) localization freshness — "no data" (never seen),
            // "live" (streaming), or "stale" (frozen: last pose held, stream dropped).
            KvCell {
                label: "Ego pose"
                value: !kpiData.hasEgoPose ? "no data" : (kpiData.egoOk ? "live" : "stale")
                warn:  !kpiData.egoOk
            }
        }

        // Per-source status dots — cameras (2) + LiDAR + IMU
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: 2
                delegate: RowLayout {
                    spacing: 6
                    Rectangle {
                        width: 9; height: 9; radius: 4.5
                        color: panel.has && (kpiData.cameraStatus & (1 << index))
                               ? theme.good
                               : (theme.lightMode ? Qt.rgba(0,0,0,0.15) : Qt.rgba(1,1,1,0.12))
                    }
                    Text {
                        text: "CAM" + (index + 1)
                        color: theme.bodyDim
                        font.family: theme.monoFont.family
                        font.pixelSize: 11
                    }
                }
            }

            // LiDAR + IMU heartbeat dots — driven by KpiData's sensor watchdog
            RowLayout {
                spacing: 6
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: kpiData.lidarOk ? theme.good
                                           : (theme.lightMode ? Qt.rgba(0,0,0,0.15) : Qt.rgba(1,1,1,0.12))
                    SequentialAnimation on opacity {
                        running: kpiData.lidarOk
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.5; duration: 900 }
                        NumberAnimation { to: 1.0; duration: 900 }
                    }
                }
                Text {
                    text: "LIDAR"
                    color: theme.bodyDim
                    font.family: theme.monoFont.family
                    font.pixelSize: 11
                }
            }
            RowLayout {
                spacing: 6
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: kpiData.imuOk ? theme.good
                                         : (theme.lightMode ? Qt.rgba(0,0,0,0.15) : Qt.rgba(1,1,1,0.12))
                    SequentialAnimation on opacity {
                        running: kpiData.imuOk
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.5; duration: 900 }
                        NumberAnimation { to: 1.0; duration: 900 }
                    }
                }
                Text {
                    text: "IMU"
                    color: theme.bodyDim
                    font.family: theme.monoFont.family
                    font.pixelSize: 11
                }
            }
            RowLayout {
                spacing: 6
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: kpiData.encoderOk ? theme.good
                                             : (theme.lightMode ? Qt.rgba(0,0,0,0.15) : Qt.rgba(1,1,1,0.12))
                    SequentialAnimation on opacity {
                        running: kpiData.encoderOk
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.5; duration: 900 }
                        NumberAnimation { to: 1.0; duration: 900 }
                    }
                }
                Text {
                    text: "ENC"
                    color: theme.bodyDim
                    font.family: theme.monoFont.family
                    font.pixelSize: 11
                }
            }
            RowLayout {
                spacing: 6
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: kpiData.egoOk ? theme.good
                                         : (theme.lightMode ? Qt.rgba(0,0,0,0.15) : Qt.rgba(1,1,1,0.12))
                    SequentialAnimation on opacity {
                        running: kpiData.egoOk
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.5; duration: 900 }
                        NumberAnimation { to: 1.0; duration: 900 }
                    }
                }
                Text {
                    text: "EGO"
                    color: theme.bodyDim
                    font.family: theme.monoFont.family
                    font.pixelSize: 11
                }
            }

            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}
