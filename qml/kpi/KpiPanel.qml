import QtQuick

// Card chrome (kpi_design v2). Children anchors.fill + own margins (~24).
// Rounded 22px, hairline border, faint top-edge highlight gradient to match
// the redesigned cardBase. (Drop-shadow from the JSX is intentionally
// omitted — layer effects fight `clip: true` and add GPU cost on Jetson;
// the larger radius + highlight already carry the new look.)
Rectangle {
    id: root
    color: theme.tile2
    radius: theme.cardRadius
    border.color: theme.hairline
    border.width: 1
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        z: -1
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.lightMode ? Qt.rgba(0,0,0,0.012) : Qt.rgba(1,1,1,0.025) }
            GradientStop { position: 0.45; color: "transparent" }
        }
    }
}
