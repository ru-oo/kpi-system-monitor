#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>
#ifndef Q_OS_IOS
#include <QtSerialBus/QCanBus>
#include <QtSerialBus/QCanBusDeviceInfo>
#endif
#include <QtMath>
#include <QElapsedTimer>
#include <memory>
#include <QDebug>

#include "KpiData.h"
#ifndef Q_OS_IOS
#include "CanBridge.h"
#include "SerialGpsBridge.h"
#include "I2cImuBridge.h"
#endif
#include "RunRecorder.h"
#include "MapModel.h"
#include "LaneMapModel.h"
#include "RawFrameModel.h"
#include "DebugLink.h"
#include "Config.h"
#include "StateLink.h"

// ─────────────────────────────────────────────────────────────────────────
//  THREADING MODEL (mentor feedback applied):
//
//    ┌──────────────────────────────┐         ┌──────────────────────────┐
//    │  CAN worker thread           │         │  GUI thread (Qt main)    │
//    │  ──────────────────          │         │  ────────────────────    │
//    │  • QCanBusDevice signals     │         │  • QML rendering         │
//    │  • Virtual bus timers        │         │  • KpiData mutations     │
//    │  • Decoders write to:        │ poll    │  • UI poll QTimer 16 ms  │
//    │      LatestValues (mutex)    │ ◄──────▶│      ↓                   │
//    │      pendingLogs queue       │         │      takeLatest()        │
//    │  • TX (0x200) writeFrame     │         │      takePendingLogs()   │
//    └──────────────────────────────┘         │      → kpiData.apply*()  │
//                                              └──────────────────────────┘
//
//    Rationale: 60 fps UI needs ~16.6 ms/frame. CAN frames arrive every
//    10-20 ms; doing decode+parse on the GUI thread risks frame drops. The
//    worker thread decodes and buffers; the UI thread polls once per
//    render frame, never blocks on I/O, and only sees fully-formed values.
// ─────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    // Headless bridge: force the offscreen QPA plugin BEFORE QGuiApplication
    // latches a platform, so the laptop/WSL bridge runs with no X11/Wayland and
    // never spins up software GL (llvmpipe). Keyed off the env var because
    // config.json isn't loaded yet here — launch the headless bridge with
    // KPI_MODE=bridge. Respects a user-pinned QT_QPA_PLATFORM.
    {
        const QByteArray envMode = qgetenv("KPI_MODE").toLower();
        if (envMode == "bridge" && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QSurfaceFormat fmt;
    fmt.setSamples(4);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName("KPI Monitoring System");

    Config config;
    config.loadFromDisk();

    // ── Deployment mode ────────────────────────────────────────────────
    // standalone: CAN + UI in one process (default).
    // bridge:     laptop reads CAN, forwards coalesced snapshots over UDP, no UI.
    // client:     iPad receives snapshots → UI, sends commands back over UDP.
    // Env KPI_MODE overrides config.link.mode.
    QString linkMode = config.linkMode().toLower();
    const QByteArray envMode = qgetenv("KPI_MODE");
    if (!envMode.isEmpty()) linkMode = QString::fromLocal8Bit(envMode).toLower();
#ifdef Q_OS_IOS
    // iOS is built without the CAN/serial modules (see CMakeLists) and has no
    // such hardware, so it is ALWAYS the UDP client — it receives decoded
    // snapshots from the laptop bridge. Ignore any other configured mode.
    linkMode = "client";
#endif
    const bool clientMode = (linkMode == "client");
    const bool bridgeMode = (linkMode == "bridge");
    qInfo().nospace() << "[main] link mode: " << linkMode;
    StateLink *stateLink = nullptr;   // created below per mode

    KpiData         kpiData;
#ifndef Q_OS_IOS
    CanBridge       canBridge(&kpiData);
    SerialGpsBridge gpsBridge;
    I2cImuBridge    imuBridge;
#endif
    RunRecorder     runRecorder(&kpiData, &config);   // record/replay/aggregate (Task 2)

    RawFrameModel   rawFrames;                        // bus-level frame monitor (A5)
    DebugLink       debugLink;                         // Jetson /debug console (UDP)

#ifndef Q_OS_IOS
    // Recording lives on the bridge/standalone side ONLY — the client (iPad)
    // records nothing locally (it has no raw frames and mirrors the bridge's run
    // list instead), so wire the recorder's inputs only when NOT a client.
    if (!clientMode) {
        // Raw RX frames (worker thread) → recorder + raw monitor (main): queued.
        QObject::connect(&canBridge, &CanBridge::frameForRecord,
                         &runRecorder, &RunRecorder::onFrame);
        QObject::connect(&canBridge, &CanBridge::frameForRecord,
                         &rawFrames, &RawFrameModel::onFrame);
        // KPI cadence drives run boundaries (Driving_State) + sampling.
        QObject::connect(&kpiData, &KpiData::kpiChanged,
                         &runRecorder, &RunRecorder::onKpiTick);
        // Replay state (worker) → KpiData (main) so QML can bind it safely.
        QObject::connect(&canBridge, &CanBridge::replayingChanged,
                         &kpiData, &KpiData::setReplaying);
        // Setting a navigation goal arms auto-record (recorded on the AUTO drive).
        QObject::connect(&kpiData, &KpiData::goalChanged,
                         &runRecorder, &RunRecorder::onGoalSet);
    }
#endif

    // ── Tactical SLAM map (loaded from disk, NOT over CAN) ──────────────
    auto *mapProvider = new MapImageProvider();      // ownership → QML engine below
    MapModel mapModel(mapProvider);
    if (!config.tacticalMapPath().isEmpty())
        mapModel.loadMap(config.tacticalMapPath());
    // Tell the virtual bus to broadcast Map_Info matching the loaded map so the
    // mismatch check passes in demo mode (real Jetson sends the real one).
#ifndef Q_OS_IOS
    if (mapModel.loaded())
        canBridge.setMapInfoBroadcast(mapModel.originX(), mapModel.originY(),
                                      mapModel.resolution(), mapModel.widthPx(),
                                      mapModel.heightPx(), config.tacticalMapVersion());
#endif

    // HD lane network (WGS84 GeoJSON; ENU-converted at runtime against the datum).
    LaneMapModel laneMap;
    if (!config.tacticalLanePath().isEmpty())
        laneMap.loadLanes(config.tacticalLanePath());
    // Map_Datum (0x108): the local-frame origin. Demo broadcasts the configured
    // datum; the lane model converts WGS84 → ENU when it arrives / changes.
    // (Client seeds the datum from config below + receives live 0x108 over UDP.)
#ifndef Q_OS_IOS
    canBridge.setMapDatumBroadcast(config.datumLat(), config.datumLon());
#endif
    QObject::connect(&kpiData, &KpiData::datumChanged, &laneMap, [&]() {
        laneMap.setDatum(kpiData.datumLat(), kpiData.datumLon());
    });
    // Seed the datum from config NOW so WGS84 HD lanes render immediately, even
    // with no CAN / before the live Map_Datum (0x108) arrives. A live 0x108
    // overrides this via datumChanged above (only if it's plausible — see
    // setReferenceDatum/applyMapDatum, which reject (0,0)/garbage datums).
    laneMap.setDatum(config.datumLat(), config.datumLon());
    kpiData.setReferenceDatum(config.datumLat(), config.datumLon());

    // ── Lane-center deviation from the HD map (replaces raw 0x10A) ──────
    // Signed lateral distance of the ego to the nearest HD centerline. The raw
    // 0x10A Loc_Lane_Dev was showing garbage (-7737 mm); this derives the real
    // value from data the UI already has. Throttled — nearestCenterlinePoint
    // scans the centerline polylines.
    auto laneDevTimer = std::make_shared<QElapsedTimer>(); laneDevTimer->start();
    auto updateLaneDev = [&, laneDevTimer]() {
        if (laneDevTimer->elapsed() < 150) return;
        if (!(kpiData.hasEgoPose() && laneMap.loaded() && laneMap.hasDatum())) return;
        laneDevTimer->restart();
        const QVariantMap np = laneMap.nearestCenterlinePoint(kpiData.egoX(), kpiData.egoY());
        if (!np.value("found").toBool()) return;
        const double hr = qDegreesToRadians(np.value("headingDeg").toDouble());
        const double ox = kpiData.egoX() - np.value("x").toDouble();
        const double oy = kpiData.egoY() - np.value("y").toDouble();
        const double cross = qCos(hr) * oy - qSin(hr) * ox;   // + left / - right of lane
        kpiData.applyLaneCenterDeviation((cross >= 0 ? 1.0 : -1.0)
                                         * np.value("dist").toDouble() * 1000.0);
    };
    QObject::connect(&kpiData, &KpiData::egoPoseChanged, &kpiData, updateLaneDev);
    QObject::connect(&laneMap, &LaneMapModel::laneChanged, &kpiData, updateLaneDev);
    // Demo polish: start the virtual ego ON the nearest centerline to the datum
    // (the datum itself can be metres off-road), so the simulated lane-center
    // deviation is small/realistic. No effect on real CAN (ego comes from 0x10D).
#ifndef Q_OS_IOS
    if (laneMap.loaded() && laneMap.hasDatum()) {
        const QVariantMap np = laneMap.nearestCenterlinePoint(0.0, 0.0);
        if (np.value("found").toBool())
            canBridge.setVirtualEgoStart(np.value("x").toDouble(), np.value("y").toDouble(),
                                         np.value("headingDeg").toDouble());
    }
#endif

    // ── UI-thread initial state (runs before worker thread starts) ──────
    kpiData.setOptMode(config.defaultOptMode());
    kpiData.setYoloModel(config.defaultYoloModel());
    // Accuracy is model-aware: load the active model's map50-95 triple, and
    // re-apply whenever the selected Yolo_Mode model (0x101) changes so the
    // Safety/AI card's mAP loss tracks YOLO26s ↔ YOLO26n.
    auto applyModelAccuracy = [&config, &kpiData]() {
        const QString m = kpiData.yoloModel();
        kpiData.applyAccuracy(config.accMap5095(m, 0), config.accMap5095(m, 1),
                              config.accMap5095(m, 2));
    };
    applyModelAccuracy();
    QObject::connect(&kpiData, &KpiData::yoloModelChanged, &kpiData, applyModelAccuracy);
    kpiData.setRamTotalGbFallback(config.ramTotalGbFallback());
    kpiData.setSwapTotalGb(config.swapTotalGb());
    kpiData.setMissionTotal(config.missionTotal());
    kpiData.setSensorStaleMs(config.sensorStaleLidarMs(), config.sensorStaleImuMs(),
                             config.sensorStaleEncMs(), config.sensorStaleEgoMs());
    kpiData.setFailsafeDwellMs(config.failsafeDwellMs());

#ifndef Q_OS_IOS
    canBridge.setDefaultBitrate(config.canDefaultBitrate());
    canBridge.setBaselineMs(config.ptBaselineMs());

    // ── Move CanBridge to a dedicated worker thread ─────────────────────
    // Client (iPad) has no CAN hardware and gets its data over UDP, so the
    // worker (and its virtual-bus fallback) is not started there.
    QThread canThread;
    canThread.setObjectName("CanWorker");
    if (!clientMode) {
    canBridge.moveToThread(&canThread);

    QObject::connect(&canThread, &QThread::started, &canBridge, [&]() {
        // Now executing on the worker thread.
        bool opened = false;
        // Adapter selection: env (KPI_CAN_PLUGIN/KPI_CAN_DEVICE) overrides, else
        // config.json can.plugin/can.device. This lets a bare double-click pick
        // up a fixed adapter (e.g. CANable slcan on COM3) with no env vars.
        const QString canPlugin = qEnvironmentVariable("KPI_CAN_PLUGIN", config.canPlugin());
        const QString canDevice = qEnvironmentVariable("KPI_CAN_DEVICE", config.canDevice());
        if (!canPlugin.isEmpty() && !canDevice.isEmpty()) {
            const QString plug = canPlugin.toLower();
            if (plug == "slcan" || plug == "serialcan") {
                // CANable (slcan firmware) → a COM port; no Qt CAN plugin for it,
                // so use our serial slcan transport. e.g. device "COM3".
                opened = canBridge.openSlcan(canDevice, config.canDefaultBitrate());
            } else {
                opened = canBridge.openInterface(canPlugin, canDevice);
            }
        } else {
            for (const QString &plugin : QCanBus::instance()->plugins()) {
                if (plugin == QLatin1String("virtualcan")) continue;
                QString err;
                const auto devs = QCanBus::instance()->availableDevices(plugin.toLocal8Bit(), &err);
                if (devs.isEmpty()) continue;
                if (canBridge.openInterface(plugin, devs.first().name())) {
                    opened = true;
                    break;
                }
            }
        }
        if (!opened) {
            // The simulated demo bus produces fake sensor heartbeats (LiDAR/IMU
            // "online", bus traffic). That's fine for a standalone UI demo, but in
            // BRIDGE mode it would forward fake data to the client and make sensors
            // look online before the real Jetson/CAN is connected. So in bridge mode
            // do NOT auto-start it — the client honestly shows offline/no-data until
            // real CAN arrives. (Opt back in with KPI_DEMO=1 for link testing.)
            const bool allowDemo = !bridgeMode || qEnvironmentVariableIntValue("KPI_DEMO") == 1;
            if (allowDemo) {
                qInfo() << "[main] No CAN hardware detected — starting in-process virtual bus.";
                canBridge.startVirtualBus();
            } else {
                qInfo() << "[main] BRIDGE: no real CAN adapter — demo bus DISABLED "
                           "(sensors stay offline until real CAN arrives; set KPI_DEMO=1 to force the demo bus).";
            }
        } else {
            qInfo().nospace() << "[main] CAN online: " << canBridge.status();
        }
    });

    canThread.start();
    }   // !clientMode

    // ── GPS worker thread (M10 NMEA over USB-serial) ────────────────────
    // Env override: KPI_GPS_PORT=COM5 (Win) or /dev/ttyUSB0 (Linux). Skip
    // if unset — the bridge stays idle and the UI shows "—" for GPS.
    QThread gpsThread;
    gpsThread.setObjectName("GpsWorker");
    gpsBridge.moveToThread(&gpsThread);
    QObject::connect(&gpsThread, &QThread::started, &gpsBridge, [&]() {
        const QByteArray port = qgetenv("KPI_GPS_PORT");
        const QByteArray baud = qgetenv("KPI_GPS_BAUD");
        if (!port.isEmpty()) {
            gpsBridge.openPort(QString::fromLocal8Bit(port),
                               baud.isEmpty() ? 9600 : baud.toInt());
        } else {
            qInfo() << "[main] KPI_GPS_PORT not set — GPS bridge idle.";
        }
    });
    gpsThread.start();

    // ── IMU worker thread (BNO055 over I²C, Linux only) ─────────────────
    // Env override: KPI_IMU_I2C=/dev/i2c-1, KPI_IMU_ADDR=0x28 (default).
    QThread imuThread;
    imuThread.setObjectName("ImuWorker");
    imuBridge.moveToThread(&imuThread);
    QObject::connect(&imuThread, &QThread::started, &imuBridge, [&]() {
        const QByteArray dev = qgetenv("KPI_IMU_I2C");
        const QByteArray addrEnv = qgetenv("KPI_IMU_ADDR");
        const int addr = addrEnv.isEmpty() ? 0x28 : addrEnv.toInt(nullptr, 0);
        if (!dev.isEmpty()) {
            imuBridge.openBus(QString::fromLocal8Bit(dev), addr);
        } else {
            qInfo() << "[main] KPI_IMU_I2C not set — IMU bridge idle.";
        }
    });
    imuThread.start();
#endif   // !Q_OS_IOS — CAN / GPS / IMU worker threads

    // ── UDP transport for the split deployment ──────────────────────────
#ifndef Q_OS_IOS
    if (bridgeMode) {
        stateLink = new StateLink(StateLink::Role::Bridge, &app);
        stateLink->attachBridge(&canBridge);
        stateLink->attachRecorder(&runRecorder);   // advertise runs + resolve replay
        stateLink->start(QString(), config.linkSnapshotPort(),
                         config.linkCommandPort(), config.linkSnapshotHz());
    } else
#endif
    if (clientMode) {
        stateLink = new StateLink(StateLink::Role::Client, &app);
        // Mirror the bridge's recorded-run list into the local recorder so the
        // shared Runs page lists + replays them; the ▶ Replay button's
        // canBridge.startReplay() is StateLink here → CMD_REPLAY to the bridge.
        QObject::connect(stateLink, &StateLink::runListChanged, &runRecorder, [&]() {
            runRecorder.setExternalRunList(stateLink->bridgeRunList());
        });
        // Raw CAN frames forwarded by the bridge → feed the Raw CAN Monitor (the
        // client has no CAN of its own; this is the only path that fills it).
        QObject::connect(stateLink, &StateLink::rawFrameReceived,
                         &rawFrames, &RawFrameModel::onFrame);
        // Bridge host = where the client sends commands/HELLO (the bridge
        // laptop's IP). KPI_BRIDGE_HOST overrides config.link.bridge_host so a
        // viewer laptop can be pointed at the bridge WITHOUT editing config.json
        // (used by Run-Client.bat <bridge-ip>).
        const QString bridgeHost = qEnvironmentVariable("KPI_BRIDGE_HOST",
                                                         config.linkBridgeHost());
        qInfo().noquote() << "[main] client → bridge host:" << bridgeHost;
        stateLink->start(bridgeHost, config.linkSnapshotPort(),
                         config.linkCommandPort(), config.linkSnapshotHz());
    }

    // ── UI-thread dispatcher: polls the worker faster than the frame rate ──
    // Per mentor feedback: spending the whole 16 ms frame budget on data
    // processing leaves zero headroom for rendering and risks a bottleneck.
    // We default to 8 ms (≈ half a 60 fps frame) so the dispatcher runs
    // twice per frame and the GPU always sees the most recent buffer when
    // it goes to redraw. The interval is configurable in config.json
    // (ui.poll_interval_ms) — drop to 16 / 33 on lower-end Jetson if 8 ms
    // is too aggressive for that board's CPU budget.
    QTimer uiPoll;
    uiPoll.setInterval(config.uiPollIntervalMs());
    qInfo().nospace() << "[main] UI dispatcher poll interval: "
                      << config.uiPollIntervalMs() << " ms";
    QObject::connect(&uiPoll, &QTimer::timeout, &app, [&]() {
        // Source: CAN snapshot (standalone) or UDP snapshot (client).
#ifdef Q_OS_IOS
        const auto v = stateLink->takeLatest();
#else
        // Bridge: PEEK (non-destructive) — the snapshot forwarder (StateLink) must
        // still see the dirty bits to ship to the client; if we TOOK here we'd
        // clear them out from under it and the iPad would freeze. Standalone /
        // client: TAKE (we're the only consumer; clearing dirty is correct).
        const auto v = clientMode ? stateLink->takeLatest()
                     : (bridgeMode ? canBridge.peekLatest() : canBridge.takeLatest());
#endif
        // Replay state rides the snapshot so the client knows the bridge is
        // replaying (idempotent setter — only fires on change).
        kpiData.setReplaying(v.replaying);
        if (v.kpiDirty)
            kpiData.applyRealtimeKpi(v.inferenceMs, v.gpuPct, v.cpuPct,
                                     v.gpuTempC, v.pathErrorMm);
        if (v.vehicleDirty)
            kpiData.applyVehicleStatus(v.speedKmh, v.steeringDeg,
                                       v.driveStateName, v.optMode,
                                       v.yoloModel, v.controlEnable);
        if (v.obstacleDirty)
            kpiData.applyObstacle(v.obstacleDistM, v.obstacleAngleDeg,
                                  v.obstacleConf01, v.obstacleClassName,
                                  v.failsafeLevel);
        if (v.sysDirty)
            kpiData.applySystemResource(v.ramPct, v.swapPct, v.sessionRateHz,
                                        v.busLoadPct, v.canLossPct,
                                        v.pi5StatusCode, v.cameraStatus);
        if (v.routeDirty)
            kpiData.applyRouteStatus(v.routeProgressM, v.amclErrorM,
                                     v.missionSuccessPct, v.routeState);
        if (v.hardwareDirty)
            kpiData.applyHardwareInfo(v.jetsonModelCode, v.orinMemoryGb);
        if (v.mapInfoDirty)
            kpiData.applyMapInfo(v.mapOriginX, v.mapOriginY, v.mapResolution,
                                 v.mapWidth, v.mapHeight, v.mapVersion);
        if (v.egoDirty)
            kpiData.applyEgoPose(v.egoX, v.egoY, v.egoYaw);
        if (v.datumDirty)
            kpiData.applyMapDatum(v.datumLat, v.datumLon);
        if (v.locDirty)
            kpiData.applyLocalizationStatus(v.locMode, v.locQuality);
        // NOTE: lane-center deviation is NOT taken from the raw 0x10A Loc_Lane_Dev
        // (it showed garbage like -7737 mm); it is computed from the HD map below
        // (ego → nearest centerline). v.locLaneDevMm is intentionally ignored.
        if (v.behaviorDirty)
            kpiData.applyBehaviorState(v.behaviorMode);
        if (v.imuDirty)
            kpiData.applyImuFromCan(v.imuYaw, v.imuGyroZ, v.imuRoll, v.imuPitch);
        if (v.encDirty)
            kpiData.applyEncoder(v.encLeft, v.encRight);
        if (v.planningDirty)
            kpiData.applyPlanningStatus(v.planLastMs, v.planSuccessRuns,
                                        v.planTotalRuns, v.planState);
        if (v.perceptionDirty)
            kpiData.applyPerceptionValidation(v.percDetectedRuns, v.percTotalRuns,
                                              v.percFalsePos, v.percTriggerAccPct);
        if (v.networkDirty)
            kpiData.applyNetworkStatus(v.netWifiPingMs, v.netLossRatePct,
                                       v.netRssiDbm, v.netState);
        if (v.failsafeEventDirty)
            kpiData.applyFailsafeEvent(v.fsEventCode, v.fsReasonCode, v.fsLevel);
        if (v.busStatsDirty) {
            kpiData.setCanBitrate(v.canBitrate);
            kpiData.applyBusStats(v.txLatencyMs, v.framesRx, v.framesTx, v.uptimeMs);
        }
        // Drain logs for the UI log panel. In bridge mode the snapshot forwarder
        // owns the pending logs (it ships them to the client) — don't steal them.
        if (!bridgeMode) {
#ifdef Q_OS_IOS
            const auto logs = stateLink->takePendingLogs();
#else
            const auto logs = clientMode ? stateLink->takePendingLogs() : canBridge.takePendingLogs();
#endif
            for (const auto &e : logs)
                kpiData.pushLogEvent(e.code, e.msg, e.severity, e.src);
        }

#ifndef Q_OS_IOS
        // External sensors — same dirty-bit pattern. Bridges populate their
        // snapshots on their own worker threads; we just drain them here.
        // (Client gets GPS/IMU folded into the UDP snapshot, not local serial.)
        const auto g = gpsBridge.takeLatest();
        if (g.dirty) {
            kpiData.applyGps(g.hasFix, g.satCount, g.latDeg, g.lonDeg,
                             g.altitudeM, g.speedKmh, g.headingDeg);
        }
        const auto i = imuBridge.takeLatest();
        if (i.dirty) {
            kpiData.applyImu(i.calSys, i.calGyro, i.calAccel, i.calMag,
                             i.headingDeg, i.rollDeg, i.pitchDeg);
        }
#endif
    });
    // Start the UI dispatcher in every mode. In bridge mode there's no QML to
    // feed, but the poll still drives kpiData (and thus the RunRecorder, which
    // is bound to kpiData.kpiChanged) so the laptop bridge CAPTURES real AUTO
    // drives to CSV — recording is a bridge-side concern. StateLink forwards to
    // the iPad from its own peekLatest cadence, independent of this poll.
    uiPoll.start();

#ifndef Q_OS_IOS
    // Headless bridge: no QML — just run the CAN worker + UDP forwarder loop.
    if (bridgeMode) {
        qInfo() << "[main] headless bridge running — forwarding CAN → UDP.";
        const int rcb = app.exec();
        QMetaObject::invokeMethod(&canBridge, "shutdown", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(&gpsBridge, "shutdown", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(&imuBridge, "shutdown", Qt::BlockingQueuedConnection);
        canThread.quit(); canThread.wait();
        gpsThread.quit(); gpsThread.wait();
        imuThread.quit(); imuThread.wait();
        return rcb;
    }
#endif

    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    engine.addImageProvider("tacticalmap", mapProvider);   // engine takes ownership
    ctx->setContextProperty("kpiData",     &kpiData);
    // Client binds StateLink as "canBridge" — it mirrors CanBridge's invokable
    // method names, so the QML (sendEStop / sendSetGoalPose / …) is unchanged.
#ifdef Q_OS_IOS
    ctx->setContextProperty("canBridge", static_cast<QObject *>(stateLink));
#else
    ctx->setContextProperty("canBridge",
                            clientMode ? static_cast<QObject *>(stateLink)
                                       : static_cast<QObject *>(&canBridge));
#endif
    ctx->setContextProperty("config",      &config);
    ctx->setContextProperty("runRecorder", &runRecorder);
    ctx->setContextProperty("mapModel",    &mapModel);
    ctx->setContextProperty("laneMapModel", &laneMap);
    ctx->setContextProperty("rawFrames",   &rawFrames);
    ctx->setContextProperty("debugLink",   &debugLink);

    // Jetson debug console — register with the Jetson and receive /debug lines.
    // Reached only in UI modes (bridge mode returns headless earlier). Host can be
    // overridden per machine with KPI_DEBUG_HOST; empty host = channel disabled.
    debugLink.start(qEnvironmentVariable("KPI_DEBUG_HOST", config.debugHost()),
                    static_cast<quint16>(config.debugPort()),
                    static_cast<quint16>(config.debugLocalPort()));

    const QUrl url(QStringLiteral("qrc:/qt/qml/KpiProject/qml/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
#ifndef Q_OS_IOS
        if (!clientMode)
            QMetaObject::invokeMethod(&canBridge, "shutdown", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(&gpsBridge, "shutdown", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(&imuBridge, "shutdown", Qt::BlockingQueuedConnection);
        canThread.quit(); canThread.wait();
        gpsThread.quit(); gpsThread.wait();
        imuThread.quit(); imuThread.wait();
#endif
        return -1;
    }

    const int rc = app.exec();

    // Ask each worker to tear down on its own thread (stop timers, close
    // device) and migrate back to main, then join all worker threads.
    // Order: don't matter — workers are independent. (Client has no CAN worker.)
#ifndef Q_OS_IOS
    if (!clientMode)
        QMetaObject::invokeMethod(&canBridge, "shutdown", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(&gpsBridge, "shutdown", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(&imuBridge, "shutdown", Qt::BlockingQueuedConnection);

    canThread.quit(); canThread.wait();
    gpsThread.quit(); gpsThread.wait();
    imuThread.quit(); imuThread.wait();
#endif
    return rc;
}
