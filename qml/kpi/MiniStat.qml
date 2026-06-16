import QtQuick
import QtQuick.Layouts

// Matches app.jsx MiniStat — inner tile inside the radar grid.
// label (10px upper) + value baseline + unit + optional hint
KpiPanel {
    id: cell
    property string label: ""
    property string value: ""
    property string unit: ""
    property bool warn: false
    property bool good: false
    property string hint: ""

    Layout.fillWidth: true
    // fillHeight is intentionally NEVER used (not baked in, not set at
    // use-site). A fillHeight cell propagates greedy vertical sizing up to
    // the enclosing GridLayout, which then competes with the radar for
    // space — that is what made the radar collapse. Instead the cell has a
    // fixed preferredHeight, so the mini-stat strip stays a fixed band and
    // the radar gets all the remaining height.
    Layout.minimumHeight: 84
    Layout.preferredHeight: 96

    // Inner tile uses tile3 so it visually sinks slightly vs the parent radar card
    color: theme.tile3
    radius: 12

    readonly property color valColor: warn ? theme.warning : (good ? theme.good : theme.bodyText)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        Item { Layout.fillHeight: true }   // top spacer — centres content

        Text {
            text: cell.label.toUpperCase()
            color: theme.bodyDim
            font.family: theme.defaultFont.family
            font.weight: Font.Medium
            font.pixelSize: 10
            font.letterSpacing: 0.4
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Item {
            implicitWidth: vTxt.implicitWidth + (uTxt.text === "" ? 0 : 4 + uTxt.implicitWidth)
            implicitHeight: vTxt.implicitHeight
            Layout.fillWidth: true
            Text {
                id: vTxt
                text: cell.value
                color: cell.valColor
                font.family: theme.defaultFont.family
                font.weight: Font.DemiBold
                font.pixelSize: 18
                font.letterSpacing: -0.2
            }
            Text {
                id: uTxt
                text: cell.unit
                color: theme.bodyMuted
                font.pixelSize: 11
                anchors.left: vTxt.right
                anchors.leftMargin: 4
                anchors.baseline: vTxt.baseline
            }
        }
        Text {
            visible: cell.hint !== ""
            text: cell.hint
            color: theme.bodyDim
            font.pixelSize: 10
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Item { Layout.fillHeight: true }
    }
}
