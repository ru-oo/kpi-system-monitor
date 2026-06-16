import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// SwipeView page — Jetson debug console. Receives /debug lines over UDP via
// DebugLink (each viewer registers with the Jetson; see DebugLink.h). Display
// only — no CAN, no judgment logic. Theme colours only.
Item {
    id: page

    // 0 = all, 1 = WARN+, 2 = ERROR/FATAL only
    property int levelFilter: 0
    function rank(l) { return l === "DEBUG" ? 0 : (l === "INFO" ? 1 : (l === "WARN" ? 2 : 3)) }
    function levelColor(l) {
        if (l === "ERROR" || l === "FATAL") return theme.critical
        if (l === "WARN") return theme.warning
        if (l === "DEBUG") return theme.bodyDim
        return theme.primaryOnDark   // INFO
    }
    readonly property int minVisRank: levelFilter === 0 ? 0 : (levelFilter === 1 ? 2 : 3)

    ListModel { id: dbgModel }
    property bool stick: true
    property int unseen: 0

    Connections {
        target: debugLink
        function onDebugMessage(time, level, text) {
            dbgModel.append({ t: time, lv: level, msg: text })
            if (dbgModel.count > 500) dbgModel.remove(0)
            if (page.stick) { list.positionViewAtEnd(); page.unseen = 0 }
            else page.unseen += 1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // ── Header ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                spacing: 2
                Text { text: "Debug Console"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20 }
                Text { text: "Jetson /debug · UDP"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            // link status pill
            Rectangle {
                implicitWidth: lsRow.implicitWidth + 18; implicitHeight: 26; radius: 13
                property color lc: debugLink.linkUp ? theme.good : theme.bodyDim
                color: Qt.rgba(lc.r, lc.g, lc.b, 0.12); border.color: lc
                Row { id: lsRow; anchors.centerIn: parent; spacing: 6
                    Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.lc; anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity { running: debugLink.linkUp; loops: Animation.Infinite; NumberAnimation { to: 0.4; duration: 800 } NumberAnimation { to: 1.0; duration: 800 } } }
                    Text { text: debugLink.linkUp ? "receiving" : "no data"; color: parent.parent.lc; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            // level filter chips
            Repeater {
                model: [ { l: "All", f: 0 }, { l: "WARN+", f: 1 }, { l: "ERROR", f: 2 } ]
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: fTxt.implicitWidth + 20; implicitHeight: 26; radius: 13
                    property bool sel: page.levelFilter === modelData.f
                    color: sel ? Qt.rgba(theme.primaryOnDark.r, theme.primaryOnDark.g, theme.primaryOnDark.b, 0.14) : theme.tile3
                    border.color: sel ? theme.primaryOnDark : theme.hairline
                    Text { id: fTxt; anchors.centerIn: parent; text: modelData.l; color: sel ? theme.primaryOnDark : theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.levelFilter = modelData.f }
                }
            }
            // clear
            Rectangle {
                implicitWidth: clrTxt.implicitWidth + 20; implicitHeight: 26; radius: 13
                color: clrMA.pressed ? theme.tile4 : theme.tile3; border.color: theme.hairline
                Text { id: clrTxt; anchors.centerIn: parent; text: "✕ Clear"; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11 }
                MouseArea { id: clrMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { dbgModel.clear(); page.unseen = 0; page.stick = true } }
            }
        }

        // ── Log list ──
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 14
            color: Qt.rgba(theme.tile2.r, theme.tile2.g, theme.tile2.b, 0.62)
            border.color: theme.hairline; border.width: 1
            clip: true

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 4
                model: dbgModel
                clip: true
                onMovementEnded: { page.stick = list.atYEnd; if (page.stick) page.unseen = 0 }
                onContentYChanged: { if (!moving) { var b = (contentY + height >= contentHeight - 2); page.stick = b; if (b) page.unseen = 0 } }

                Text {
                    anchors.centerIn: parent
                    visible: dbgModel.count === 0
                    text: debugLink.linkUp ? "(no messages match the filter)" : "Waiting for Jetson /debug…"
                    color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    property bool shown: page.rank(model.lv) >= page.minVisRank
                    height: shown ? Math.max(26, msgT.implicitHeight + 12) : 0
                    visible: shown
                    color: "transparent"
                    Row {
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.right: parent.right; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Text { text: model.t; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 12; width: 64 }
                        Text { text: model.lv; color: page.levelColor(model.lv); font.family: theme.monoFont.family; font.weight: Font.Bold; font.pixelSize: 12; width: 46 }
                        Text {
                            id: msgT
                            text: model.msg; color: theme.bodyText
                            font.family: theme.monoFont.family; font.pixelSize: 13
                            width: parent.width - 64 - 46 - 20
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // "↓ N new" pill
            Rectangle {
                visible: !page.stick && page.unseen > 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                implicitWidth: jr.implicitWidth + 22; implicitHeight: 30; radius: 15
                color: theme.primaryOnDark; opacity: 0.95
                Row { id: jr; anchors.centerIn: parent; spacing: 6
                    Text { text: "↓"; color: "#fff"; font.pixelSize: 14; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: page.unseen + " new"; color: "#fff"; font.pixelSize: 12; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { list.positionViewAtEnd(); page.stick = true; page.unseen = 0 } }
            }
        }

        // ── Footer ──
        Text {
            text: dbgModel.count + " messages · last 500   ·   " + debugLink.messageCount + " received"
            color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12
        }
    }
}
