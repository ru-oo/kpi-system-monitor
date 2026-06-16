import QtQuick
import QtQuick.Layouts

// Fail-safe Event Stream — driven by KpiData.logEvent signals emitted from
// CanBridge via the UI dispatcher.
//
// Auto-scroll behaviour (per user feedback):
//   • If the user is already at the bottom (or never touched the list),
//     new events scroll into view automatically.
//   • If the user has scrolled UP to inspect older entries, new events
//     are NOT force-scrolled to the bottom — we just append and let
//     them keep reading. A "↓ N new" pill appears so they know fresh
//     events are below; tapping it jumps to the bottom.
Rectangle {
    id: root
    color: Qt.rgba(theme.tile2.r, theme.tile2.g, theme.tile2.b, 0.62)
    radius: 14
    border.color: theme.hairline
    border.width: 1
    clip: true

    ListModel { id: logModel }

    // Track whether the user is currently "stuck to bottom" — i.e. the
    // viewport's end matches the content's end. atYEnd alone flickers
    // during model.append, so we also remember the last user-driven state.
    property bool stickToBottom: true
    property bool disengageOnly: false   // A4 filter
    property int unseenCount: 0

    function sevColor(s) {
        if (s === "critical")  return theme.critical;
        if (s === "disengage") return "#ff5db1";   // distinct magenta — operator-critical
        if (s === "warning")   return theme.warning;
        if (s === "good")      return theme.good;
        return theme.bodyDim;
    }

    Connections {
        target: kpiData
        function onLogEvent(time, code, msg, severity, src) {
            logModel.append({ ts: time, code: code, msg: msg, sev: severity, src: src });
            if (logModel.count > 80) logModel.remove(0);
            if (root.stickToBottom) {
                logList.positionViewAtEnd();
                root.unseenCount = 0;
            } else {
                root.unseenCount += 1;
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 56
            color: "transparent"
            Rectangle { width: parent.width; height: 1; color: theme.hairline; anchors.bottom: parent.bottom }

            RowLayout {
                anchors.fill: parent; anchors.margins: 18
                Column {
                    Text { text: "Fail-safe Event Stream"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 16 }
                    Text { text: "CAN 0x1FF · live"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }
                }
                Item { Layout.fillWidth: true }
                // DISENGAGE-only filter (A4)
                Rectangle {
                    implicitWidth: disTxt.implicitWidth + 22
                    implicitHeight: 26
                    radius: 13
                    color: root.disengageOnly ? Qt.rgba(255/255,93/255,177/255,0.16) : Qt.rgba(1,1,1,0.06)
                    border.color: root.disengageOnly ? "#ff5db1" : theme.hairline
                    Text { id: disTxt; anchors.centerIn: parent; text: "⚠ disengage"; color: root.disengageOnly ? "#ff5db1" : theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.disengageOnly = !root.disengageOnly }
                }
                Rectangle {
                    implicitWidth: recRow.implicitWidth + 18
                    implicitHeight: 26
                    radius: 13
                    color: Qt.rgba(1,1,1,0.06)
                    border.color: theme.hairline
                    Row {
                        id: recRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {
                            width: 9; height: 9; radius: 4.5; color: theme.good
                            anchors.verticalCenter: parent.verticalCenter
                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.4; duration: 800 }
                                NumberAnimation { to: 1.0; duration: 800 }
                            }
                        }
                        Text { text: "rec"; color: theme.bodyMuted; font.pixelSize: 12; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
        }

        // List area (with floating "↓ N new" pill overlay)
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true

            ListView {
                id: logList
                anchors.fill: parent
                model: logModel
                clip: true

                // Lock to bottom if the user is not actively scrolling
                // (and the bottom edge is at the content edge).
                onMovementEnded: {
                    root.stickToBottom = logList.atYEnd
                    if (root.stickToBottom) root.unseenCount = 0
                }
                // Edge case: when the model grows and the user was at the
                // bottom, atYEnd may go false for one frame. Recheck on
                // contentY changes too.
                onContentYChanged: {
                    if (!moving) {
                        // Within 2 px of the bottom counts as "at bottom".
                        const atBottom = (contentY + height >= contentHeight - 2)
                        root.stickToBottom = atBottom
                        if (atBottom) root.unseenCount = 0
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: logModel.count === 0
                    text: "Waiting for CAN events…"
                    color: theme.bodyDim
                    font.family: theme.defaultFont.family
                    font.pixelSize: 13
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    property bool shown: !root.disengageOnly || model.sev === "disengage"
                    height: shown ? Math.max(48, msgText.implicitHeight + 22) : 0
                    visible: shown
                    clip: true
                    color: "transparent"
                    Rectangle { width: parent.width; height: 1; color: theme.hairline; anchors.bottom: parent.bottom; visible: parent.shown && index < logList.count - 1 }

                    // Strict 4-column grid (v2 EventStream): time · code · msg · src.
                    // Absolute column positions guarantee every row aligns even when
                    // a cell (e.g. an empty timestamp on a TX/UI echo) is blank.
                    readonly property int colTime: 20
                    readonly property int colCode: 124
                    readonly property int colMsg:  264
                    readonly property int colSrcW: 84

                    // Timestamp
                    Text {
                        x: parent.colTime
                        anchors.verticalCenter: parent.verticalCenter
                        width: 96
                        text: model.ts
                        color: theme.bodyDim
                        font.family: theme.monoFont.family
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    // Severity dot + code
                    Row {
                        x: parent.colCode
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.colMsg - parent.colCode - 12
                        spacing: 6
                        Rectangle { width: 7; height: 7; radius: 3.5; color: root.sevColor(model.sev); anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: model.code
                            color: root.sevColor(model.sev)
                            font.family: theme.monoFont.family
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                        }
                    }
                    // Message (fills the middle, always left-aligned at colMsg)
                    Text {
                        id: msgText
                        x: parent.colMsg
                        width: parent.width - parent.colMsg - parent.colSrcW - 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.msg
                        color: theme.bodyText
                        font.family: theme.defaultFont.family
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignLeft
                    }
                    // Source (right-aligned)
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.colSrcW
                        text: model.src ? model.src : ""
                        color: theme.bodyDim
                        font.family: theme.monoFont.family
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }
                }
            }

            // "↓ N new" pill — only when there are fresh events the user
            // hasn't seen yet AND they have scrolled up.
            Rectangle {
                visible: !root.stickToBottom && root.unseenCount > 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                implicitWidth: jumpRow.implicitWidth + 22
                implicitHeight: 30
                radius: 15
                color: theme.primaryOnDark
                opacity: 0.95
                Row {
                    id: jumpRow
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "↓"; color: "#fff"; font.pixelSize: 14; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: root.unseenCount + (root.unseenCount === 1 ? " new event" : " new events")
                        color: "#fff"; font.pixelSize: 12; font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        logList.positionViewAtEnd()
                        root.stickToBottom = true
                        root.unseenCount = 0
                    }
                }
                scale: pillMA.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: 80 } }
                MouseArea { id: pillMA; anchors.fill: parent; visible: false }
            }
        }

        // Footer
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 38
            color: "transparent"
            Rectangle { width: parent.width; height: 1; color: theme.hairline; anchors.top: parent.top }

            RowLayout {
                anchors.fill: parent; anchors.margins: 14
                Text { text: logModel.count + " events · last 80"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Text { text: "rosbag2 + SavvyCAN"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 12 }
            }
        }
    }
}
