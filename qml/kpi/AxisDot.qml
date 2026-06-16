import QtQuick
import QtQuick.Layouts

// AxisDot — a "smaller-is-better" (or larger-is-better) continuous KPI shown as
// a single dot on a thin axis, with the IN-TARGET zone shaded. Deliberately not
// a fill bar, so a value near target doesn't read as an alarming "full bar".
//   value/max position the dot; targetMax is the pass threshold; lowerIsBetter
//   shades 0..targetMax (else targetMax..max). Colour supplied by the caller
//   (good / warning / bodyMuted) — theme-only, light & dark safe.
Item {
    id: root
    property real  value: 0
    property real  max: 100
    property real  targetMax: 50
    property bool  lowerIsBetter: true
    property color color: theme.good
    property string loLabel: "0"
    property string midLabel: ""
    property string hiLabel: ""

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight

    readonly property real frac:  Math.max(0, Math.min(1, root.max > 0 ? root.value / root.max : 0))
    readonly property real tfrac: Math.max(0, Math.min(1, root.max > 0 ? root.targetMax / root.max : 0))

    Column {
        id: col
        anchors.left: parent.left; anchors.right: parent.right
        spacing: 6

        Item {
            id: axis
            width: parent.width; height: 12
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 4; radius: 2; color: theme.trackDim }
            // in-target (good) zone, subtle
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: root.lowerIsBetter ? 0 : parent.width * root.tfrac
                width: parent.width * (root.lowerIsBetter ? root.tfrac : (1 - root.tfrac))
                height: 4; radius: 2
                color: Qt.rgba(theme.good.r, theme.good.g, theme.good.b, 0.22)
            }
            // target boundary tick
            Rectangle { x: parent.width * root.tfrac - 1; y: -1; width: 2; height: parent.height + 2; color: theme.bodyDim; opacity: 0.6 }
            // value dot (ringed so it reads on any track colour)
            Rectangle {
                width: 13; height: 13; radius: 6.5
                x: parent.width * root.frac - 6.5
                anchors.verticalCenter: parent.verticalCenter
                color: root.color
                border.color: theme.tile2; border.width: 2
                Behavior on x { NumberAnimation { duration: 200 } }
            }
        }
        RowLayout {
            width: parent.width
            Text { text: root.loLabel; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Text { visible: root.midLabel !== ""; text: root.midLabel; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Text { text: root.hiLabel; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
        }
    }
}
