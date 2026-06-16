import QtQuick

// SwipeView page 5 — Tactical (SLAM-map-backed bird's-eye + map-frame goals).
Item {
    id: page
    TacticalMap {
        anchors.fill: parent
        anchors.margins: 20
    }
}
