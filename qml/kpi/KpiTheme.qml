import QtQuick

QtObject {
    id: root
    property bool lightMode: true

    readonly property color tile1: lightMode ? "#f5f5f7" : "#1a1a1c"
    readonly property color tile2: lightMode ? "#ffffff" : "#2a2a2c"
    readonly property color tile2Dim: lightMode ? Qt.rgba(1,1,1,0.7) : Qt.rgba(0.165,0.165,0.173,0.6)
    readonly property color tile3: lightMode ? "#ebebef" : "#323234"
    readonly property color tile4: lightMode ? "#e0e0e5" : "#3a3a3c"

    readonly property color hairline: lightMode ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.08)
    readonly property color hairlineStrong: lightMode ? Qt.rgba(0,0,0,0.18) : Qt.rgba(1,1,1,0.14)

    // Redesign tokens (kpi_design v2)
    readonly property color trackDim:    lightMode ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.10)
    readonly property color dotInactive: lightMode ? Qt.rgba(0,0,0,0.22) : Qt.rgba(1,1,1,0.22)
    readonly property int    cardRadius: 22

    readonly property color primary: "#0066cc"
    readonly property color primaryOnDark: lightMode ? "#0066cc" : "#2997ff"
    
    readonly property color bodyText: lightMode ? "#1d1d1f" : "#f5f5f7"
    readonly property color bodyMuted: lightMode ? "#424245" : "#cccccc"
    readonly property color bodyDim: lightMode ? "#6e6e73" : "#86868b"

    readonly property color warning: "#ff9f0a"
    readonly property color critical: "#ff453a"
    readonly property color good: "#30d158"

    readonly property font defaultFont: Qt.font({
        family: Qt.application.font.family,
        pixelSize: 15
    })
    readonly property font monoFont: Qt.font({
        family: (Qt.platform.os === "osx" || Qt.platform.os === "ios") ? "Menlo" :
                (Qt.platform.os === "windows") ? "Consolas" : "Ubuntu Mono",
        pixelSize: 14
    })
}
