import QtQuick
import QtQuick.Layouts

// StateDots — a run-count KPI ("X of N") shown as a row of status dots:
//   `filled` dots take `color`, the rest are inactive, and a small tick marks
//   the `targetMin` pass-gate. Reads as "how many of N", not "a bar that's
//   fuller = worse". Colour is supplied by the caller (pass=good / watch=warning
//   / no-data=bodyMuted) using existing theme colours — works in light & dark.
Item {
    id: root
    property int   filled: 0
    property int   total: 10
    property int   targetMin: 0
    property color color: theme.good
    property real  dot: 13
    property real  gap: 6

    Layout.fillWidth: true
    implicitHeight: dotsCol.implicitHeight

    Column {
        id: dotsCol
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Row {
            id: dots
            spacing: root.gap
            Repeater {
                model: Math.max(0, root.total)
                delegate: Rectangle {
                    required property int index
                    width: root.dot; height: root.dot; radius: root.dot / 2
                    color: index < root.filled ? root.color : theme.dotInactive
                }
            }
        }
        // target-gate tick, sitting just past the targetMin-th dot
        Item {
            width: dots.width; height: 6
            Rectangle {
                visible: root.targetMin > 0 && root.targetMin <= root.total
                width: 2; height: 6; radius: 1
                color: theme.bodyDim; opacity: 0.7
                x: root.targetMin * root.dot + (root.targetMin - 1) * root.gap + root.gap / 2 - 1
            }
        }
    }
}
