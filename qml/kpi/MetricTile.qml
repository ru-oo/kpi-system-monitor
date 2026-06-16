import QtQuick
import QtQuick.Layouts

// Matches app.jsx MetricTile:
//   header row (label + warn-dot)
//   big value + small unit on baseline
//   4px progress bar (track + ring color)
//   small hint
KpiPanel {
    id: tile

    property string label: ""
    property string value: "0"
    property string unit: ""
    property real maxValue: 100        // for the bar; 0 disables bar
    property real numeric: parseFloat(value)
    property real warnAt: -1
    property real critAt: -1
    property string hint: ""
    property color ringColor: theme.primaryOnDark
    // When false the tile renders "—" and hides the progress bar fill until
    // the first CAN frame arrives for this domain.
    property bool available: true

    readonly property bool isCrit: critAt >= 0 && numeric >= critAt
    readonly property bool isWarn: !isCrit && warnAt >= 0 && numeric >= warnAt
    readonly property color activeRing: isCrit ? theme.critical : (isWarn ? theme.warning : ringColor)
    readonly property color valueColor: isCrit ? theme.critical : (isWarn ? theme.warning : theme.bodyText)
    readonly property real pct: maxValue > 0 ? Math.max(0, Math.min(1, numeric / maxValue)) : 0

    Layout.fillWidth: true
    Layout.minimumHeight: 160
    Layout.preferredHeight: 160

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        // Header
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: tile.label.toUpperCase()
                color: theme.bodyDim
                font.family: theme.defaultFont.family
                font.weight: Font.Medium
                font.pixelSize: 13
                font.letterSpacing: 0.5
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Rectangle {
                visible: tile.isWarn || tile.isCrit
                width: 8; height: 8; radius: 4
                color: tile.activeRing
                SequentialAnimation on opacity {
                    running: tile.isWarn || tile.isCrit
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 800; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0;  duration: 800; easing.type: Easing.InOutSine }
                }
            }
        }

        // Value + unit (baseline aligned)
        Item {
            Layout.fillWidth: true
            implicitHeight: valTxt.implicitHeight
            Text {
                id: valTxt
                text: tile.available ? tile.value : "—"
                color: tile.valueColor
                font.family: theme.defaultFont.family
                font.weight: Font.DemiBold
                font.pixelSize: 44
                font.letterSpacing: -0.5
            }
            Text {
                text: tile.available ? tile.unit : ""
                color: theme.bodyMuted
                font.family: theme.defaultFont.family
                font.weight: Font.Medium
                font.pixelSize: 16
                anchors.left: valTxt.right
                anchors.leftMargin: 6
                anchors.baseline: valTxt.baseline
            }
        }

        // Progress bar
        Rectangle {
            visible: tile.maxValue > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            radius: 2
            color: theme.lightMode ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.08)
            Rectangle {
                height: parent.height
                width: parent.width * (tile.available ? tile.pct : 0)
                radius: 2
                color: tile.activeRing
                Behavior on width { NumberAnimation { duration: 200 } }
            }
        }

        // Hint
        Text {
            visible: tile.hint !== ""
            text: tile.hint
            color: theme.bodyDim
            font.family: theme.defaultFont.family
            font.pixelSize: 13
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        Item { Layout.fillHeight: true }
    }
}
