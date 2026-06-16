import QtQuick
import QtQuick.Window
import "kpi"

Window {
    id: root
    width : 1680 // Full width to match app.jsx max-width
    height: 1050
    visibility: Qt.platform.os === "ios" ? Window.FullScreen : Window.Windowed
    title  : "KPI Monitoring System"
    color  : theme.tile1

    KpiTheme {
        id: theme
    }

    // ── Fullscreen toggle
    Shortcut { sequence: "F11"; onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
    Shortcut { sequence: "Escape"; onActivated: root.showNormal() }

    KpiDashboardApp { 
        anchors.fill: parent
        // We will access 'theme' by id globally from within KpiDashboardApp since it's in the root context of main.qml
    }
}

