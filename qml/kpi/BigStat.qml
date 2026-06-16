import QtQuick
import QtQuick.Layouts

// BigStat — a ratio / representative KPI as a big number + unit + a status icon,
// coloured by the caller's status colour (pass=good / watch=warning /
// fail=critical / no-data=bodyMuted). Icon: "✓" pass, "▲" watch, "✗" fail, ""
// none. Theme colours only — light & dark safe.
RowLayout {
    id: root
    property string value: "—"
    property string unit: ""
    property color  statusColor: theme.good
    property string icon: ""
    property int    valueSize: 26

    spacing: 6

    Text {
        visible: root.icon !== ""
        text: root.icon
        color: root.statusColor
        font.family: theme.defaultFont.family
        font.weight: Font.Bold
        font.pixelSize: Math.round(root.valueSize * 0.62)
        Layout.alignment: Qt.AlignBaseline
    }
    Text {
        text: root.value
        color: root.statusColor
        font.family: theme.defaultFont.family
        font.weight: Font.Bold
        font.pixelSize: root.valueSize
        Layout.alignment: Qt.AlignBaseline
    }
    Text {
        visible: root.unit !== ""
        text: root.unit
        color: theme.bodyMuted
        font.family: theme.defaultFont.family
        font.pixelSize: Math.round(root.valueSize * 0.5)
        Layout.alignment: Qt.AlignBaseline
    }
}
