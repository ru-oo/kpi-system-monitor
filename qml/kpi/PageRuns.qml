import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// SwipeView page 6 — Runs (Task 2: record / replay / aggregate).
// Fully offline; drives runRecorder + canBridge.startReplay. Layout:
//   left  — record controls + run list (replay)
//   right — across-run §4.2 KPI aggregate table
Item {
    id: page

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // ── LEFT: record + run list ──
        KpiPanel {
            Layout.preferredWidth: parent.width * 0.42
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Text { text: "Run Recorder"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 18 }
                Text { text: "REC records the live feed now (manual, any drive state) · a goal also auto-records the AUTO drive → CSV · offline replay"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13 }

                // Record / replay state row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 180; Layout.preferredHeight: 46
                        radius: 11
                        property bool on: runRecorder.recording
                        color: recMA.pressed ? Qt.darker(theme.tile3, 1.15)
                                             : (on ? Qt.rgba(48/255,209/255,88/255,0.12) : theme.tile3)
                        border.color: on ? theme.good : theme.hairlineStrong
                        Row {
                            anchors.centerIn: parent; spacing: 8
                            Rectangle {
                                width: 11; height: 11; radius: 5.5
                                color: parent.parent.on ? theme.good : theme.bodyDim
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: parent.parent.on ? "REC · ON" : "REC · OFF"
                                color: parent.parent.on ? theme.good : theme.bodyMuted
                                font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea { id: recMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: runRecorder.toggleRecording() }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 46
                        radius: 11
                        color: runRecorder.inRun ? Qt.rgba(48/255,209/255,88/255,0.12) : theme.tile3
                        border.color: runRecorder.inRun ? theme.good : theme.hairline
                        Text {
                            anchors.centerIn: parent
                            text: runRecorder.inRun
                                  ? ("● REC RUN · " + runRecorder.sampleCount + " samples")
                                  : (kpiData.replaying ? "▶ REPLAYING…"
                                     : (runRecorder.recording ? "armed · waiting for AUTO drive"
                                        : "press REC to record now · or set a goal to auto-record"))
                            color: runRecorder.inRun ? theme.good : (kpiData.replaying ? theme.primaryOnDark : theme.bodyDim)
                            font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 13
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "RECORDED RUNS · " + runRecorder.runCount; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 12; font.letterSpacing: 0.6 }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: kpiData.replaying ? "■ stop" : "↻ refresh"
                        color: theme.primaryOnDark; font.family: theme.defaultFont.family; font.pixelSize: 12
                        MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor
                            onClicked: kpiData.replaying ? canBridge.stopReplay() : runRecorder.refreshRuns() }
                    }
                }

                // run list
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    model: runRecorder.runList
                    spacing: 6
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 44
                        radius: 9
                        color: theme.tile3
                        border.color: theme.hairline
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12; anchors.rightMargin: 8
                            spacing: 8
                            Text { text: modelData; color: theme.bodyText; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideMiddle }
                            Rectangle {
                                Layout.preferredWidth: 78; Layout.preferredHeight: 30; radius: 8
                                color: playMA.pressed ? Qt.rgba(41/255,151/255,255/255,0.28) : Qt.rgba(41/255,151/255,255/255,0.12)
                                border.color: theme.primaryOnDark
                                Text { anchors.centerIn: parent; text: "▶ Replay"; color: theme.primaryOnDark; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                                MouseArea { id: playMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    enabled: !kpiData.replaying
                                    onClicked: canBridge.startReplay(runRecorder.runPath(modelData)) }
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: runRecorder.runCount === 0
                        text: "no runs yet — arm Record, then drive (AUTO)"
                        color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13
                    }
                }
            }
        }

        // ── RIGHT: across-run aggregate ──
        KpiPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Text { text: "KPI Aggregate"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 18 }
                        Text { text: runRecorder.lastSummary !== "" ? runRecorder.lastSummary : ("across-run mean ± std · N target " + config.runsTotal); color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13 }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        Layout.preferredWidth: 130; Layout.preferredHeight: 38; radius: 10
                        color: expMA.pressed ? Qt.rgba(48/255,209/255,88/255,0.24) : Qt.rgba(48/255,209/255,88/255,0.12)
                        border.color: theme.good
                        Text { anchors.centerIn: parent; text: "⤓ Export CSV"; color: theme.good; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 13 }
                        MouseArea { id: expMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: runRecorder.exportSummary() }
                    }
                }

                // ── campaign filter (A6) — only matching runs feed the aggregate ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "CAMPAIGN"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.5 }
                    Repeater {
                        model: runRecorder.campaigns
                        delegate: Rectangle {
                            required property var modelData
                            Layout.preferredHeight: 28
                            implicitWidth: campTxt.implicitWidth + 22
                            radius: 14
                            property bool sel: modelData === runRecorder.campaign
                            color: sel ? Qt.rgba(41/255,151/255,255/255,0.16) : theme.tile3
                            border.color: sel ? theme.primaryOnDark : theme.hairline
                            Text { id: campTxt; anchors.centerIn: parent; text: modelData; color: sel ? theme.primaryOnDark : theme.bodyMuted; font.family: theme.monoFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: runRecorder.campaign = modelData }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: runRecorder.runsInScope + " in scope"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 12 }
                }

                // table header
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "KPI"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; Layout.preferredWidth: 110 }
                    Text { text: "MEAN ± STD"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; Layout.fillWidth: true }
                    Text { text: "TARGET"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; Layout.preferredWidth: 110 }
                    Text { text: "RESULT"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 11; Layout.preferredWidth: 70 }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }

                Repeater {
                    model: runRecorder.kpiResults
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        property bool has: modelData.runs > 0
                        Text { text: modelData.name; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 14; Layout.preferredWidth: 110 }
                        Text {
                            text: has ? (modelData.mean.toFixed(2) + " ± " + modelData.std.toFixed(2) + " " + modelData.unit) : "—"
                            color: theme.bodyText; font.family: theme.monoFont.family; font.pixelSize: 13; Layout.fillWidth: true
                        }
                        Text {
                            text: (modelData.higher ? "≥ " : "≤ ") + Number(modelData.target).toFixed(2) + " " + modelData.unit
                            color: theme.bodyMuted; font.family: theme.monoFont.family; font.pixelSize: 12; Layout.preferredWidth: 110
                        }
                        Rectangle {
                            Layout.preferredWidth: 64; Layout.preferredHeight: 24; radius: 12
                            color: !parent.has ? "transparent"
                                   : (modelData.pass ? Qt.rgba(48/255,209/255,88/255,0.14) : Qt.rgba(255/255,159/255,10/255,0.14))
                            border.color: !parent.has ? theme.hairline : (modelData.pass ? theme.good : theme.warning)
                            Text { anchors.centerIn: parent; text: !parent.parent.has ? "—" : (modelData.pass ? "PASS" : "FAIL"); color: !parent.parent.has ? theme.bodyDim : (modelData.pass ? theme.good : theme.warning); font.family: theme.monoFont.family; font.weight: Font.Bold; font.pixelSize: 11 }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
