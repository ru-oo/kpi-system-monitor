import QtQuick
import QtQuick.Layouts

// Small key/value cell used inside CanStatus / Pi5Status grids.
// Matches the KV() function in app.jsx:
//   label  (UPPERCASE, 10px, dim)
//   value  (16px, demi-bold, optional warn color, monospace optional)
ColumnLayout {
    id: root
    property string label: ""
    property string value: ""
    property string unit: ""
    property bool warn: false
    property bool mono: false

    Layout.fillWidth: true
    spacing: 6

    Text {
        text: root.label.toUpperCase()
        color: theme.bodyDim
        font.family: theme.defaultFont.family
        font.weight: Font.Medium
        font.pixelSize: 12
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
            text: root.value
            color: root.warn ? theme.warning : theme.bodyText
            font.family: root.mono ? theme.monoFont.family : theme.defaultFont.family
            font.weight: Font.DemiBold
            font.pixelSize: 22
            font.letterSpacing: -0.2
        }
        Text {
            id: uTxt
            text: root.unit
            color: theme.bodyDim
            font.family: theme.defaultFont.family
            font.pixelSize: 13
            anchors.left: vTxt.right
            anchors.leftMargin: 4
            anchors.baseline: vTxt.baseline
        }
    }
}
