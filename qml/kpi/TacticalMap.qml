import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs

// SLAM-map-backed tactical view. The saved occupancy grid (MapModel) is the
// single source of spatial truth; goals are map-frame poses (x,y,yaw) sent on
// 0x201. Course shape lives entirely in the loaded map → nothing here assumes a
// straight road. Pixel↔map conversion is delegated to MapModel so overlays and
// click-handling never disagree.
KpiPanel {
    id: panel
    Layout.fillWidth: true
    Layout.fillHeight: true

    // ── map-vs-Jetson verification (§6) ──
    readonly property bool mapMismatch:
        kpiData.hasMapInfo && mapModel.loaded &&
        (kpiData.mapInfoVersion !== config.tacticalMapVersion
         || Math.abs(mapModel.originX - kpiData.mapInfoOriginX) > config.mapOriginTolM
         || Math.abs(mapModel.resolution - kpiData.mapInfoResolution) > config.mapResolutionTolM)
    // HD lanes ignore the occupancy MAP MISMATCH (different frame); goal is
    // settable when an HD map OR a (matching) occupancy grid is available.
    readonly property bool goalUnlocked:
        (laneMapModel.loaded || (mapModel.loaded && !mapMismatch)) &&
        (kpiData.driveState === "IDLE" || kpiData.driveState === "STOP" || kpiData.driveState === "")

    // SLAM occupancy grid is a supporting layer. It auto-hides once the HD
    // metre frame is active (different frame); shown otherwise (incl. the brief
    // window before the datum arrives, and the no-HD case).
    property bool showGrid: true
    // HD layer category toggles (context OFF by default)
    property bool showMarkings: true
    property bool showCues: true
    property bool showContext: false

    // map zoom / pan
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    function setZoom(z) {
        // Cap so the content canvas (fitW × zoom) stays within the texture limit.
        var maxByTexture = mapArea.fitW > 1 ? Math.max(config.mapZoomMin, config.mapTextureLimitPx / mapArea.fitW) : config.mapZoomMax
        zoom = Math.max(config.mapZoomMin, Math.min(Math.min(config.mapZoomMax, maxByTexture), z))
    }
    function resetView() { zoom = config.mapZoomMin; panX = 0; panY = 0 }

    // First-load focus: open the HD map zoomed on the DATUM (ENU 0,0 = our
    // reference point), not fitted to the whole ~2.2 km extent (which made the
    // initial ego a speck). One-shot — never overrides later manual pan/zoom.
    property bool didInitialFocus: false
    function focusDatum() {
        if (!mapArea.hd) return
        // show ~map_focus_span_m across the plot width, clamped by setZoom's caps
        setZoom((mapArea.width / config.mapFocusSpanM) / mapArea.fitSc)
        // center ENU (0,0): datum content-local px is lx(0)=-minX*effSc, ly(0)=maxY*effSc
        panX = mapArea.contentW / 2 + laneMapModel.minX * mapArea.effSc
        panY = mapArea.contentH / 2 - laneMapModel.maxY * mapArea.effSc
    }
    // One-shot: fire as soon as the HD frame is ready AND the plot has a real
    // size. Covers both startup (lanes/datum preloaded in main() before the
    // page is laid out → triggered by mapArea's size hooks) and a live datum
    // arriving later (→ triggered by laneChanged). Never re-runs, so manual
    // pan/zoom is preserved; the ⟲ reset button still returns to full-extent.
    function maybeInitialFocus() {
        if (!didInitialFocus && mapArea.hd && mapArea.width > 1 && mapArea.height > 1) {
            didInitialFocus = true
            focusDatum()
        }
    }
    Connections {
        target: laneMapModel
        function onLaneChanged() { panel.maybeInitialFocus() }
    }

    // pending (un-sent) goal
    property bool  pending: false
    property real  pendingX: 0
    property real  pendingY: 0
    property real  pendingYaw: 0
    property string rejectMsg: ""

    function sendGoal(mx, my, yaw) {
        kpiData.setGoal(mx, my, yaw)            // main-thread marker state
        canBridge.sendSetGoalPose(mx, my, yaw)  // 0x201 TX (worker)
    }

    // Cancel/clear the goal so the operator can retest without restarting the
    // app. A sent goal is aborted on the vehicle (0x201 Goal_Valid=0 → IDLE,
    // which re-unlocks goal-setting); an un-sent map click is just discarded.
    function cancelGoal() {
        if (kpiData.goalActive) {
            kpiData.clearGoal()         // drop the marker (main thread)
            canBridge.sendCancelGoal()  // 0x201 Goal_Valid=0 → vehicle → IDLE
        }
        panel.pending = false           // discard any un-sent pending goal
        ov.requestPaint()               // redraw overlay (marker gone)
    }

    // Runtime map / HD-lane file pickers removed — the HD lane map loads from
    // config.json (tactical.lane_path) at startup; SLAM now does obstacle
    // detection only (no occupancy-map generation), so no manual map swap.

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        // ── header ──
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Text { text: "Tactical · " + (laneMapModel.loaded ? laneMapModel.name : (mapModel.loaded ? mapModel.name : "no map")); color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 18 }
                Text { text: laneMapModel.loaded ? "HD lane map · click snaps goal to centerline (0x201)" : "SLAM map · map-frame goal (0x201) · click drivable space"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }

            // B1 — localization source/confidence pill (derived until 0x108)
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: locRow.implicitWidth + 20; implicitHeight: 26; radius: 13
                // EKF localization quality (post-AMCL): colour by Loc_Quality.
                property color locColor: kpiData.localizationQuality >= 0.7 ? theme.good
                                       : kpiData.localizationQuality >= 0.4 ? theme.warning : theme.critical
                color: Qt.rgba(locColor.r, locColor.g, locColor.b, 0.12)
                border.color: locColor
                Row {
                    id: locRow; anchors.centerIn: parent; spacing: 6
                    Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.locColor; anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity { running: kpiData.localizationQuality < 0.4; loops: Animation.Infinite; NumberAnimation { to: 0.35; duration: 700 } NumberAnimation { to: 1.0; duration: 700 } } }
                    Text { text: "LOC " + kpiData.localizationMode; color: parent.parent.locColor; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: (kpiData.localizationQuality * 100).toFixed(0) + "%"; color: theme.bodyMuted; font.family: theme.monoFont.family; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                }
            }

            // B2 — behavior FSM pill (derived until 0x109)
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: behRow.implicitWidth + 20; implicitHeight: 26; radius: 13
                property color behColor: kpiData.behaviorState === "OBSTACLE_STOP" ? theme.critical
                                       : (kpiData.behaviorState === "NUDGE" || kpiData.behaviorState === "OVERTAKE") ? theme.warning
                                       : kpiData.behaviorState === "LANE_FOLLOW" ? theme.good : theme.bodyDim
                color: Qt.rgba(behColor.r, behColor.g, behColor.b, 0.12)
                border.color: behColor
                Row {
                    id: behRow; anchors.centerIn: parent; spacing: 6
                    Text { text: "▸"; color: parent.parent.behColor; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: kpiData.behaviorState; color: parent.parent.behColor; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                }
            }

            // route stats (carried over from Route & Mission)
            Row {
                spacing: 18; Layout.alignment: Qt.AlignVCenter
                Row {
                    spacing: 5; anchors.verticalCenter: parent.verticalCenter
                    // Lane-center deviation (HD map): ego's lateral distance to the
                    // nearest centerline — replaces the AMCL static-error readout.
                    Text { text: "Lane dev"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: kpiData.hasEgoPose ? (kpiData.laneCenterDeviationMm / 1000).toFixed(2) : "—"; color: !kpiData.hasEgoPose ? theme.bodyDim : (Math.abs(kpiData.laneCenterDeviationMm) > config.laneCenterDevMmMax ? theme.warning : theme.good); font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "m"; color: theme.bodyMuted; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                }
                Rectangle {
                    visible: kpiData.hasPathPlan; anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: pr.implicitWidth + 16; implicitHeight: 24; radius: 12
                    color: kpiData.pathPlanSuccessRatePct >= 90 ? Qt.rgba(48/255,209/255,88/255,0.12) : Qt.rgba(255/255,159/255,10/255,0.12)
                    border.color: kpiData.pathPlanSuccessRatePct >= 90 ? Qt.rgba(48/255,209/255,88/255,0.40) : Qt.rgba(255/255,159/255,10/255,0.40)
                    Row { id: pr; anchors.centerIn: parent; spacing: 5
                        Text { text: "PLAN"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.Medium; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: kpiData.pathPlanSuccessRuns + "/" + kpiData.pathPlanTotalRuns; color: kpiData.pathPlanSuccessRatePct >= 90 ? theme.good : theme.warning; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
        }

        // ── map load error banner ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 44 : 0
            visible: mapModel.loadError !== ""
            radius: 10
            color: Qt.rgba(255/255,69/255,58/255,0.14)
            border.color: theme.critical
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 10
                Text { text: "⚠"; color: theme.critical; font.pixelSize: 18 }
                Text { Layout.fillWidth: true; text: "MAP LOAD FAILED — " + mapModel.loadError; color: theme.critical; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 13; elide: Text.ElideRight }
            }
        }

        // ── MAP MISMATCH banner (§6) ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 44 : 0
            visible: panel.mapMismatch && !laneMapModel.loaded
            radius: 10
            color: Qt.rgba(255/255,69/255,58/255,0.14)
            border.color: theme.critical
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 10
                Text { text: "⚠"; color: theme.critical; font.pixelSize: 18 }
                Text {
                    Layout.fillWidth: true
                    text: "MAP MISMATCH — loaded map ≠ Jetson Map_Info (v" + kpiData.mapInfoVersion + " vs v" + config.tacticalMapVersion + "). Goal-setting disabled."
                    color: theme.critical; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 13
                }
            }
        }

        // ── HD layer toggles (context categories OFF by default) ──
        RowLayout {
            Layout.fillWidth: true
            visible: laneMapModel.loaded
            spacing: 8
            Text { text: "LAYERS"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.5 }
            Repeater {
                model: [
                    { key: "markings", label: "Markings" },
                    { key: "cues",     label: "Stop/Marks" },
                    { key: "context",  label: "Context" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.preferredHeight: 26; implicitWidth: tgT.implicitWidth + 22; radius: 13
                    property bool on: modelData.key === "markings" ? panel.showMarkings
                                    : modelData.key === "cues"     ? panel.showCues : panel.showContext
                    color: on ? Qt.rgba(41/255,151/255,255/255,0.14) : theme.tile3
                    border.color: on ? theme.primaryOnDark : theme.hairline
                    Text { id: tgT; anchors.centerIn: parent; text: modelData.label; color: parent.on ? theme.primaryOnDark : theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.key === "markings") panel.showMarkings = !panel.showMarkings
                            else if (modelData.key === "cues") panel.showCues = !panel.showCues
                            else panel.showContext = !panel.showContext
                        } }
                }
            }
            Item { Layout.fillWidth: true }
            Text { text: laneMapModel.featureCount + " HD features"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 11 }
        }

        // ── map plot ──
        Item {
            id: mapArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // First valid layout size → run the one-shot datum focus (startup case).
            onWidthChanged:  panel.maybeInitialFocus()
            onHeightChanged: panel.maybeInitialFocus()

            // HD metre frame is canonical only once the datum has arrived and
            // ENU has been computed; otherwise fall back to the occupancy grid.
            readonly property bool hd: laneMapModel.loaded && laneMapModel.hasDatum

            // base (zoom=1) fit factors
            readonly property real imgAR: mapModel.loaded && mapModel.heightPx > 0 ? mapModel.widthPx / mapModel.heightPx : 1
            readonly property real areaAR: height > 0 ? width / height : 1
            readonly property real iDispW: !mapModel.loaded ? 0 : (areaAR > imgAR ? height * imgAR : width)
            readonly property real iSc: mapModel.loaded && mapModel.widthPx > 0 ? iDispW / mapModel.widthPx : 1

            readonly property real hdW: hd ? Math.max(0.001, laneMapModel.maxX - laneMapModel.minX) : 1
            readonly property real hdH: hd ? Math.max(0.001, laneMapModel.maxY - laneMapModel.minY) : 1
            // Guard the fit denominators: during a SwipeView transition the plot
            // width/height can transiently be ~0, and (width-16) would go negative
            // → a negative canvas size → qnumeric ASSERT. Floor at 1px.
            readonly property real hSc0: hd ? Math.min(Math.max(1, width - 16) / hdW,
                                                       Math.max(1, height - 16) / hdH) : 1

            // fit (zoom = 1) content size, used to cap zoom so the content canvas
            // never exceeds the GPU/backing-store limit.
            readonly property real fitSc: hd ? hSc0 : iSc
            readonly property real fitW: Math.max(1, hd ? hdW * hSc0 : mapModel.widthPx * iSc)
            readonly property real fitH: Math.max(1, hd ? hdH * hSc0 : mapModel.heightPx * iSc)

            // effective scale (with zoom) + content rect (with pan). The STATIC HD
            // canvas is sized to contentW/H and positioned at originX/originY:
            // panning just moves the item (no repaint); only zoom resizes/repaints.
            readonly property real effSc: fitSc * panel.zoom
            readonly property real contentW: Math.max(0, fitW * panel.zoom)
            readonly property real contentH: Math.max(0, fitH * panel.zoom)
            readonly property real originX: (width  - contentW) / 2 + panel.panX
            readonly property real originY: (height - contentH) / 2 + panel.panY

            // metre → content-local (no origin), then ax/ay add the (pan'd) origin.
            // Canvases are FIXED at plot size and draw at origin+content so the
            // backing store never grows with zoom (avoids the qnumeric ASSERT).
            function lx(mx) { return (mx - laneMapModel.minX) * effSc }          // HD east
            function ly(my) { return (laneMapModel.maxY - my) * effSc }          // HD north (y-up flip)
            function cx(mx, my) { return mapModel.mapToPixel(mx, my).x * effSc } // pixel*zoom
            function cy(mx, my) { return mapModel.mapToPixel(mx, my).y * effSc }
            function ax(mx, my) { return originX + (hd ? lx(mx) : cx(mx, my)) }
            function ay(mx, my) { return originY + (hd ? ly(my) : cy(mx, my)) }
            // mapArea-relative pixel → metre (click/hover inverse; account for origin)
            function toMx(sx) { var px = sx - originX
                return hd ? laneMapModel.minX + px / effSc : mapModel.pixelToMap(px / effSc, 0).x }
            function toMy(sx, sy) { var px = sx - originX, py = sy - originY
                return hd ? laneMapModel.maxY - py / effSc : mapModel.pixelToMap(px / effSc, py / effSc).y }

            // Ego pose: prefer real Ego_Pose (0x10D), else straight-route stopgap
            // (route_start + progress along heading; no AMCL lateral term anymore).
            readonly property real egoH: kpiData.hasEgoPose ? kpiData.egoYaw * Math.PI / 180
                                                            : config.routeHeadingDeg * Math.PI / 180
            readonly property real egoMx: kpiData.hasEgoPose ? kpiData.egoX
                : config.routeStartX + kpiData.progressM * Math.cos(config.routeHeadingDeg * Math.PI / 180)
            readonly property real egoMy: kpiData.hasEgoPose ? kpiData.egoY
                : config.routeStartY + kpiData.progressM * Math.sin(config.routeHeadingDeg * Math.PI / 180)

            readonly property bool active: hd || mapModel.loaded

            Text {
                anchors.centerIn: parent
                visible: !mapArea.active
                text: "no map — set tactical.map_path / lane_path or use Load…"
                color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 14
            }

            // occupancy backdrop (OFF by default once an HD lane map is loaded —
            // pixel frame won't align with the metre frame unless origins match)
            Image {
                id: mapImg
                visible: mapModel.loaded && panel.showGrid && !mapArea.hd
                opacity: 0.7
                x: mapArea.originX; y: mapArea.originY
                width: mapArea.contentW; height: mapArea.contentH
                source: mapModel.source
                smooth: false; fillMode: Image.Stretch; cache: false
            }

            // ── STATIC HD layer (10k+ features) — content-sized canvas at the
            //    origin. PAN just moves the item (x/y bound to origin → no
            //    repaint = smooth). Only ZOOM resizes it (→ onWidthChanged
            //    repaint). NEVER repaints per telemetry frame. ──
            Canvas {
                id: hdLayer
                visible: mapArea.hd
                x: mapArea.originX; y: mapArea.originY
                width: mapArea.contentW; height: mapArea.contentH
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset()
                    if (!mapArea.hd) return
                    ctx.lineCap = "round"; ctx.lineJoin = "round"

                    function strokeLines(cat, col, lw, closed) {
                        var fs = laneMapModel.featuresByCategory(cat)
                        ctx.strokeStyle = col; ctx.lineWidth = lw
                        for (var i = 0; i < fs.length; i++) {
                            var c = fs[i].coords
                            ctx.beginPath()
                            for (var j = 0; j < c.length; j++) {
                                var X = mapArea.lx(c[j].x), Y = mapArea.ly(c[j].y)
                                if (j === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y)
                            }
                            if (closed) ctx.closePath()
                            ctx.stroke()
                        }
                    }
                    function dots(cat, col, r) {
                        var fs = laneMapModel.featuresByCategory(cat)
                        ctx.fillStyle = col
                        for (var i = 0; i < fs.length; i++) {
                            var c = fs[i].coords
                            if (c.length < 1) continue
                            ctx.beginPath(); ctx.arc(mapArea.lx(c[0].x), mapArea.ly(c[0].y), r, 0, 2*Math.PI); ctx.fill()
                        }
                    }

                    // context (off by default)
                    if (panel.showContext) {
                        strokeLines("guardrail", theme.lightMode ? Qt.rgba(0,0,0,0.18) : Qt.rgba(1,1,1,0.16), 1.0, false)
                        strokeLines("lane_area", theme.lightMode ? Qt.rgba(0,0,0,0.12) : Qt.rgba(1,1,1,0.10), 0.8, true)
                        dots("pole", theme.bodyDim, 2)
                        dots("node", theme.bodyDim, 1.5)
                        dots("sign", theme.warning, 3)
                        dots("traffic_light", theme.critical, 3)
                    }
                    // cues
                    if (panel.showCues) {
                        strokeLines("stop_line", theme.warning, 2.0, false)
                        strokeLines("surface_mark", theme.bodyMuted, 1.4, true)
                        dots("surface_mark", theme.bodyMuted, 2)
                    }
                    // lane markings (painted lines)
                    if (panel.showMarkings)
                        strokeLines("lane_marking", theme.lightMode ? Qt.rgba(0,0,0,0.30) : Qt.rgba(1,1,1,0.28), 1.2, false)
                    // centerlines = the drivable reference path → accent, on top, always
                    strokeLines("centerline", theme.primaryOnDark, 2.6, false)
                }
                Connections { target: laneMapModel; function onLaneChanged()    { hdLayer.requestPaint() } }
                // NOTE: do NOT repaint the HD lane layer on goalChanged — the goal
                // marker is on the overlay (ov), and the lanes (10k+ features) don't
                // move with the goal. Repainting them on every send/clear-goal was
                // the lag. The overlay repaints itself on goalChanged (cheap).
                Connections { target: theme;        function onLightModeChanged(){ hdLayer.requestPaint() } }
                onWidthChanged: requestPaint()   // zoom resizes the canvas → redraw crisp
                onHeightChanged: requestPaint()
                onVisibleChanged: requestPaint()
                Connections { target: panel
                    function onShowMarkingsChanged() { hdLayer.requestPaint() }
                    function onShowCuesChanged()     { hdLayer.requestPaint() }
                    function onShowContextChanged()  { hdLayer.requestPaint() }
                }
            }

            // ── DYNAMIC layer — ego / goal / obstacle. Fixed at plot size; draws
            //    via ax/ay which include the origin (zoom/pan offsets). ──
            Canvas {
                id: ov
                anchors.fill: parent
                visible: mapArea.active
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset()
                    if (!mapArea.active) return

                    // obstacles (ego-relative → frame coords; list-ready)
                    // Real detection only (conf > 0) — no phantom marker on a zero frame.
                    if (kpiData.obstacleDetected) {
                        var aw = mapArea.egoH + kpiData.obstacleAngleDeg * Math.PI / 180
                        var omx = mapArea.egoMx + kpiData.obstacleDistM * Math.cos(aw)
                        var omy = mapArea.egoMy + kpiData.obstacleDistM * Math.sin(aw)
                        var ox = mapArea.ax(omx, omy), oy = mapArea.ay(omx, omy)
                        var oc = kpiData.obstacleDistM < 4 ? theme.critical : theme.warning
                        ctx.fillStyle = oc
                        ctx.save(); ctx.translate(ox, oy); ctx.rotate(Math.PI/4)
                        var s = 5 + kpiData.obstacleConf * 5
                        ctx.fillRect(-s, -s, 2*s, 2*s); ctx.restore()
                        ctx.fillStyle = theme.bodyText; ctx.font = "10px " + theme.monoFont.family
                        ctx.textAlign = "center"; ctx.textBaseline = "bottom"
                        ctx.fillText(kpiData.obstacleClass + " " + (kpiData.obstacleConf*100).toFixed(0) + "%", ox, oy - s - 2)
                    }
                    if (kpiData.goalActive)
                        drawGoal(ctx, mapArea.ax(kpiData.goalDistM, kpiData.goalLatM), mapArea.ay(kpiData.goalDistM, kpiData.goalLatM), kpiData.goalYawDeg, theme.good)
                    if (panel.pending)
                        drawGoal(ctx, mapArea.ax(panel.pendingX, panel.pendingY), mapArea.ay(panel.pendingX, panel.pendingY), panel.pendingYaw, theme.primaryOnDark)

                    // Show the ego whenever we have a pose (0x10D Ego_Pose) OR the
                    // vehicle is alive (0x101 → straight-route stopgap). Was gated on
                    // hasVehicle only, so a robot sending Ego_Pose without Vehicle_Status
                    // showed no ego on the map.
                    if (kpiData.hasEgoPose || kpiData.hasVehicle) {
                        var ex = mapArea.ax(mapArea.egoMx, mapArea.egoMy)
                        var ey = mapArea.ay(mapArea.egoMx, mapArea.egoMy)
                        // True heading = ego yaw (0x10D). Draw a triangular arrow
                        // pointing in the heading so the facing direction is obvious;
                        // a thin secondary tick shows the steered-wheel direction.
                        var hh = mapArea.egoH                                  // heading (yaw)
                        var sh = mapArea.egoH - kpiData.steeringDeg * Math.PI / 180  // steer
                        // steering tick (subtle)
                        ctx.strokeStyle = theme.bodyDim; ctx.lineWidth = 2; ctx.lineCap = "round"
                        ctx.beginPath(); ctx.moveTo(ex, ey)
                        ctx.lineTo(ex + Math.cos(sh) * 16, ey - Math.sin(sh) * 16); ctx.stroke()
                        // heading arrow. 0x10D Ego_X/Y is the vehicle reference
                        // point (base_link), not the front bumper. Keep the
                        // triangle's visual centroid on ex/ey so the marker does
                        // not appear ahead of the real vehicle.
                        var tipPx = 18
                        var rearPx = 9
                        var halfW = 8
                        var ax0 = ex + Math.cos(hh) * tipPx, ay0 = ey - Math.sin(hh) * tipPx
                        var lh = hh + Math.PI * 0.5, rh = hh - Math.PI * 0.5
                        var bx = ex - Math.cos(hh) * rearPx
                        var by = ey + Math.sin(hh) * rearPx
                        ctx.fillStyle = theme.primaryOnDark
                        ctx.beginPath()
                        ctx.moveTo(ax0, ay0)
                        ctx.lineTo(bx + Math.cos(lh) * halfW, by - Math.sin(lh) * halfW)
                        ctx.lineTo(bx + Math.cos(rh) * halfW, by - Math.sin(rh) * halfW)
                        ctx.closePath(); ctx.fill()
                        // base_link reference point
                        ctx.fillStyle = theme.primaryOnDark
                        ctx.beginPath(); ctx.arc(ex, ey, 7, 0, 2*Math.PI); ctx.fill()
                        ctx.fillStyle = theme.tile1
                        ctx.beginPath(); ctx.arc(ex, ey, 3, 0, 2*Math.PI); ctx.fill()
                    }
                }
                function drawGoal(ctx, gx, gy, yawDeg, col) {
                    var yr = yawDeg * Math.PI / 180
                    ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.lineCap = "round"
                    ctx.beginPath(); ctx.moveTo(gx, gy); ctx.lineTo(gx + Math.cos(yr)*18, gy - Math.sin(yr)*18); ctx.stroke()
                    ctx.fillStyle = col
                    ctx.beginPath(); ctx.arc(gx, gy, 5, 0, 2*Math.PI); ctx.fill()
                    ctx.strokeStyle = col; ctx.lineWidth = 1.5
                    ctx.beginPath(); ctx.arc(gx, gy, 10, 0, 2*Math.PI); ctx.stroke()
                }
                Connections { target: kpiData;  function onKpiChanged()      { ov.requestPaint() } }
                Connections { target: kpiData;  function onRouteChanged()    { ov.requestPaint() } }
                Connections { target: kpiData;  function onGoalChanged()     { ov.requestPaint() } }
                Connections { target: kpiData;  function onEgoPoseChanged()  { ov.requestPaint() } }
                Connections { target: laneMapModel; function onLaneChanged() { ov.requestPaint() } }
                Connections { target: mapModel; function onMapChanged()      { ov.requestPaint() } }
                Connections { target: theme;    function onLightModeChanged(){ ov.requestPaint() } }
                Connections { target: mapArea
                    function onOriginXChanged() { ov.requestPaint() }
                    function onOriginYChanged() { ov.requestPaint() }
                    function onEffScChanged()   { ov.requestPaint() }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
            }

            // wheel zoom (about the cursor)
            WheelHandler {
                target: null
                enabled: mapArea.active
                onWheel: function(ev) {
                    var factor = ev.angleDelta.y > 0 ? 1.15 : 1/1.15
                    var z0 = panel.zoom
                    panel.setZoom(z0 * factor)
                    var k = panel.zoom / z0
                    // keep the point under the cursor fixed
                    panel.panX = ev.x - k * (ev.x - panel.panX)
                    panel.panY = ev.y - k * (ev.y - panel.panY)
                }
            }

            // hover + click + drag-pan
            MouseArea {
                id: ma
                anchors.fill: parent          // whole plot (pan anywhere)
                hoverEnabled: true
                enabled: mapArea.active
                // Keep the drag once pressed so the parent SwipeView can't steal
                // a horizontal pan and flip the page instead.
                preventStealing: true
                cursorShape: dragging ? Qt.ClosedHandCursor
                                      : (panel.goalUnlocked ? Qt.PointingHandCursor : Qt.OpenHandCursor)
                property real hovMx: 0
                property real hovMy: 0
                property bool hovValid: false
                property real pressX: 0
                property real pressY: 0
                property real pressPanX: 0
                property real pressPanY: 0
                property bool dragging: false
                onPressed: function(m) { pressX = m.x; pressY = m.y; pressPanX = panel.panX; pressPanY = panel.panY; dragging = false }
                onPositionChanged: function(m) {
                    if (pressed) {
                        var dx = m.x - pressX, dy = m.y - pressY
                        if (!dragging && (Math.abs(dx) + Math.abs(dy)) > 4) dragging = true
                        if (dragging) { panel.panX = pressPanX + dx; panel.panY = pressPanY + dy }
                    } else {
                        hovMx = mapArea.toMx(m.x); hovMy = mapArea.toMy(m.x, m.y); hovValid = true
                    }
                }
                onExited: hovValid = false
                onClicked: function(m) {
                    if (dragging) { dragging = false; return }   // it was a pan, not a click
                    if (!panel.goalUnlocked) return
                    var mx = mapArea.toMx(m.x), my = mapArea.toMy(m.x, m.y)
                    if (mapArea.hd) {
                        var np = laneMapModel.nearestCenterlinePoint(mx, my)
                        if (!np.found) { panel.rejectMsg = "no centerline near that point"; rejectTimer.restart(); return }
                        panel.rejectMsg = ""
                        panel.pendingX = np.x; panel.pendingY = np.y; panel.pendingYaw = np.headingDeg
                        panel.pending = true
                    } else {
                        if (!mapModel.isFree(mx, my)) { panel.rejectMsg = "occupied/unknown cell — pick drivable space"; rejectTimer.restart(); return }
                        panel.rejectMsg = ""
                        panel.pendingX = mx; panel.pendingY = my; panel.pending = true
                    }
                    yawSl.value = panel.pendingYaw
                    ov.requestPaint()
                }
            }
            Timer { id: rejectTimer; interval: 2200; onTriggered: panel.rejectMsg = "" }

            // ── zoom controls (overlay, top-right) ──
            Column {
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                spacing: 6
                visible: mapArea.active
                Repeater {
                    model: [ { t: "＋", a: "in" }, { t: "－", a: "out" }, { t: "⟲", a: "reset" } ]
                    delegate: Rectangle {
                        required property var modelData
                        width: 32; height: 32; radius: 8
                        color: zMA.pressed ? Qt.rgba(1,1,1,0.14) : (theme.lightMode ? Qt.rgba(1,1,1,0.85) : Qt.rgba(0.16,0.16,0.17,0.9))
                        border.color: theme.hairlineStrong
                        Text { anchors.centerIn: parent; text: modelData.t; color: theme.bodyText; font.pixelSize: 16; font.weight: Font.Bold }
                        MouseArea { id: zMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.a === "in") panel.setZoom(panel.zoom * 1.3)
                                else if (modelData.a === "out") panel.setZoom(panel.zoom / 1.3)
                                else panel.resetView()
                            } }
                    }
                }
                Rectangle {
                    width: 32; height: 22; radius: 6
                    color: theme.lightMode ? Qt.rgba(1,1,1,0.85) : Qt.rgba(0.16,0.16,0.17,0.9)
                    border.color: theme.hairline
                    Text { anchors.centerIn: parent; text: panel.zoom.toFixed(1) + "×"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
                }
            }

            // hover coordinate readout
            Rectangle {
                visible: ma.hovValid && mapArea.active
                anchors.left: parent.left; anchors.bottom: parent.bottom
                anchors.margins: 6
                implicitWidth: hovTxt.implicitWidth + 16; implicitHeight: 24; radius: 6
                color: theme.lightMode ? Qt.rgba(1,1,1,0.85) : Qt.rgba(0.16,0.16,0.17,0.85)
                border.color: theme.hairline
                Text { id: hovTxt; anchors.centerIn: parent
                    text: "x " + ma.hovMx.toFixed(1) + "  y " + ma.hovMy.toFixed(1) + " m"
                    color: theme.bodyText; font.family: theme.monoFont.family; font.pixelSize: 11 }
            }

            // reject tooltip
            Rectangle {
                visible: panel.rejectMsg !== ""
                anchors.centerIn: parent
                implicitWidth: rejTxt.implicitWidth + 24; implicitHeight: 32; radius: 8
                color: Qt.rgba(255/255,159/255,10/255,0.16); border.color: theme.warning
                Text { id: rejTxt; anchors.centerIn: parent; text: panel.rejectMsg; color: theme.warning; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
            }

            // lock veil while driving
            Rectangle {
                anchors.fill: parent
                visible: mapArea.active && !panel.goalUnlocked && !panel.mapMismatch
                color: theme.lightMode ? Qt.rgba(0,0,0,0.05) : Qt.rgba(0,0,0,0.28)
                Text { anchors.centerIn: parent; text: "🔒 " + kpiData.driveState + " · goal locked"; color: theme.bodyMuted; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 13 }
            }
        }

        // ── presets (per-map) + confirm bar ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text { text: "PRESETS"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 11; font.letterSpacing: 0.5 }
            Repeater {
                model: mapModel.presets
                delegate: Rectangle {
                    required property var modelData
                    Layout.preferredHeight: 34
                    implicitWidth: presetTxt.implicitWidth + 22
                    radius: 9
                    color: presetMA.pressed ? Qt.rgba(48/255,209/255,88/255,0.22) : Qt.rgba(48/255,209/255,88/255,0.10)
                    border.color: theme.hairlineStrong
                    opacity: panel.goalUnlocked ? 1.0 : 0.4
                    Text { id: presetTxt; anchors.centerIn: parent; text: "⚑ " + modelData.name; color: theme.good; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                    MouseArea { id: presetMA; anchors.fill: parent; enabled: panel.goalUnlocked; cursorShape: Qt.PointingHandCursor
                        onClicked: panel.sendGoal(modelData.x, modelData.y, modelData.yaw) }
                }
            }

            Item { Layout.fillWidth: true }

            // pending yaw + send
            Row {
                visible: panel.pending
                spacing: 8
                Layout.alignment: Qt.AlignVCenter
                Text { text: "yaw"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                Slider { id: yawSl; width: 130; from: -180; to: 180; stepSize: 5; value: panel.pendingYaw
                    anchors.verticalCenter: parent.verticalCenter
                    onMoved: { panel.pendingYaw = value; ov.requestPaint() } }
                Text { text: panel.pendingYaw.toFixed(0) + "°"; color: theme.bodyText; font.family: theme.monoFont.family; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            }
            Rectangle {
                visible: panel.pending
                Layout.preferredWidth: 130; Layout.preferredHeight: 38; radius: 10
                color: sendMA.pressed ? Qt.rgba(48/255,209/255,88/255,0.30) : Qt.rgba(48/255,209/255,88/255,0.16)
                border.color: theme.good
                Text { anchors.centerIn: parent; text: "⚑ Send goal"; color: theme.good; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 13 }
                MouseArea { id: sendMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: { panel.sendGoal(panel.pendingX, panel.pendingY, panel.pendingYaw); panel.pending = false } }
            }

            // Clear / cancel the goal (or an un-sent map click) so you can retest
            // without restarting. A sent goal is aborted on the vehicle → IDLE.
            Rectangle {
                visible: kpiData.goalActive || panel.pending
                Layout.preferredWidth: 120; Layout.preferredHeight: 38; radius: 10
                color: cancelMA.pressed ? Qt.rgba(theme.critical.r, theme.critical.g, theme.critical.b, 0.28)
                                        : Qt.rgba(theme.critical.r, theme.critical.g, theme.critical.b, 0.12)
                border.color: theme.critical
                Text { anchors.centerIn: parent; text: "✕ Clear goal"; color: theme.critical; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 13 }
                MouseArea { id: cancelMA; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.cancelGoal() }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }

        // ── Mission + Path Deviation + goal readout (carried over) ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                spacing: 6; Layout.preferredWidth: 300
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "MISSION"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                    Text { text: kpiData.hasMission ? kpiData.missionSuccess : "—"; color: theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20 }
                    Text { text: kpiData.hasMission ? ("/ " + kpiData.missionTotal) : ""; color: theme.bodyMuted; font.pixelSize: 13; Layout.alignment: Qt.AlignBottom; bottomPadding: 2 }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 4
                    Repeater {
                        model: kpiData.hasMission ? kpiData.missionTotal : 10
                        delegate: Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 8; radius: 4
                            color: kpiData.hasMission && index < kpiData.missionSuccess ? theme.good : (theme.lightMode ? Qt.rgba(0,0,0,0.10) : Qt.rgba(1,1,1,0.08)) }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 32; color: theme.hairline }
            ColumnLayout {
                spacing: 4
                Text { text: "PATH DEVIATION"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                Row { spacing: 4
                    Text { text: kpiData.hasRealtimeKpi ? (kpiData.pathDeviationMm / 1000).toFixed(2) : "—"; color: kpiData.pathDeviationMm > config.targetPathDeviationMm ? theme.warning : theme.bodyText; font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20; anchors.baseline: pdu.baseline }
                    Text { id: pdu; text: "m · < " + (config.targetPathDeviationMm / 1000).toFixed(2); color: theme.bodyMuted; font.pixelSize: 12 }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 32; color: theme.hairline }
            // B3 — lane-center deviation (vision); placeholder until real signal
            ColumnLayout {
                spacing: 4
                Row { spacing: 5
                    Text { text: "LANE-CENTER DEV"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                    // "~" = derived (computed from the HD map, not a raw CAN signal yet)
                    Text { text: "(~)"; color: theme.bodyDim; font.family: theme.monoFont.family; font.pixelSize: 10 }
                }
                Row { spacing: 4
                    Text { text: kpiData.hasEgoPose ? (kpiData.laneCenterDeviationMm / 1000).toFixed(2) : "—"; color: !kpiData.hasEgoPose ? theme.bodyMuted : (Math.abs(kpiData.laneCenterDeviationMm) > config.laneCenterDevMmMax ? theme.warning : theme.bodyText); font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20; anchors.baseline: ldu.baseline }
                    Text { id: ldu; text: "m"; color: theme.bodyMuted; font.pixelSize: 12 }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 32; color: theme.hairline }
            // Raspberry-Pi camera lane deviation (0x10A Loc_Lane_Dev) — a distinct
            // measurement from the HD-map LANE-CENTER DEV; "no data" when silent.
            ColumnLayout {
                spacing: 4
                Text { text: "RPi LANE DEV"; color: theme.bodyDim; font.family: theme.defaultFont.family; font.weight: Font.DemiBold; font.pixelSize: 12 }
                Row { spacing: 4
                    Text { text: kpiData.piLaneOk ? (kpiData.piLaneDevMm / 1000).toFixed(2)
                                                  : (kpiData.hasPiLaneDev ? "stale" : "no data")
                           color: !kpiData.piLaneOk ? theme.bodyMuted
                                : (Math.abs(kpiData.piLaneDevMm) > config.laneCenterDevMmMax ? theme.warning : theme.bodyText)
                           font.family: theme.defaultFont.family; font.weight: Font.Bold; font.pixelSize: 20; anchors.baseline: rpu.baseline }
                    Text { id: rpu; text: kpiData.piLaneOk ? "m" : ""; color: theme.bodyMuted; font.pixelSize: 12 }
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: kpiData.goalActive
                      ? ("⚑ goal  x " + kpiData.goalDistM.toFixed(1) + "  y " + kpiData.goalLatM.toFixed(1) + " m → 0x201")
                      : (panel.goalUnlocked ? "click map to set goal" : "")
                color: kpiData.goalActive ? theme.good : theme.bodyDim
                font.family: theme.monoFont.family; font.pixelSize: 12
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
