#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>
#include <QVector>
#include <QtSerialBus/QCanBusDevice>
#include <QtSerialBus/QCanBusFrame>
#include <QtSerialPort/QSerialPort>   // slcan (LAWICEL) over a COM port — e.g. CANable
#include "WireState.h"   // LatestValues / LogEntry (shared with StateLink, no SerialBus dep)

class KpiData;

// CanBridge — owns the CAN socket, decodes the Jetson→PC_UI frames from
// valeo_project_can.dbc, and transmits the PC_UI→Jetson UI_Command (0x200).
//
// THREADING (per mentor feedback):
//   This object is moved to its own QThread in main.cpp. Decoders run on
//   that worker thread and write into m_latest under m_mutex — they never
//   touch KpiData directly. The UI thread runs a 16 ms QTimer that pulls
//   a snapshot via takeLatest() / takePendingLogs() and feeds KpiData.
//
//   Rationale (from mentor feedback):
//     • 60 fps UI ⇒ each render frame is ~16.6 ms.
//     • CAN frames arrive every 10-20 ms; decoding must NOT contend with
//       the GUI thread or we drop frames.
//     • Worker thread decodes & buffers (queue model). UI thread polls the
//       latest values once per render frame and never blocks on I/O.
//
// RX (Jetson → PC_UI):
//   0x100 Obstacle_Detection — 20 ms
//   0x101 Vehicle_Status      — 20 ms
//   0x102 Realtime_KPI        — 100 ms
//   0x103 System_Resource     — 100 ms (memory / session / pi5 / bus load)
//   0x104 Route_Status        — 100 ms (route progress / amcl / mission)
//   0x105 Hardware_Info       — 1 s
//   0x1FF Fail_Safe_Event     — event-driven
//
// TX (PC_UI → Jetson):
//   0x200 UI_Command          — on dashboard button press
class CanBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status     READ status     NOTIFY statusChanged)
    Q_PROPERTY(bool    connected  READ isConnected NOTIFY statusChanged)
    Q_PROPERTY(bool    virtualBus READ isVirtual  NOTIFY statusChanged)
    // NOTE: CanBridge lives on the worker thread, so QML must NOT bind its
    // properties/signals (only call its invokable methods). Goal & replay
    // state for the UI lives on KpiData (main thread); replay state is pushed
    // there via the replayingChanged() signal (queued to main in main.cpp).

public:
    explicit CanBridge(KpiData *kpi, QObject *parent = nullptr);
    ~CanBridge();

    QString status() const;
    bool isConnected() const   { return m_dev != nullptr; }
    bool isVirtual() const     { return m_virt20ms.isActive(); }

    bool   isReplaying() const { return m_replaying; }

    // Call from UI thread; bounce to worker via Qt::QueuedConnection.
    Q_INVOKABLE bool openInterface(const QString &plugin, const QString &deviceName);
    // slcan / LAWICEL serial CAN (e.g. CANable in slcan mode → a COM port).
    // Qt has no slcan CAN plugin, so we speak the ASCII protocol over QSerialPort
    // directly; decoded frames feed the SAME processFrame() path as a real bus.
    Q_INVOKABLE bool openSlcan(const QString &portName, int bitrate);
    Q_INVOKABLE void closeInterface();
    Q_INVOKABLE void startVirtualBus();
    Q_INVOKABLE void stopVirtualBus();

    // Clean shutdown — to be called from main thread via
    // BlockingQueuedConnection before quitting the worker QThread. Stops
    // timers + closes device on the worker thread (where they live), then
    // migrates ourselves back to the main thread so ~CanBridge can run
    // there without "killTimer from another thread" warnings.
    Q_INVOKABLE void shutdown();

    // ── 0x200 UI_Command TX — called from QML when buttons are pressed ───
    // Command_Id: 0 No_Command, 1 Set_Precision, 2 Set_Model, 3 AMCL_Init,
    //             4 Reserved (was E_Stop), 5 Set_Destination.
    // E-stop is NOT a Command_Id — it rides the dedicated Estop_Command signal
    // (DBC CM_ BO_/SG_ 512). sendEStop() sends Command_Id=No_Command + Estop=1.
    // Safe from any thread — the body is queued onto our own event loop.
    Q_INVOKABLE void sendSetPrecision(const QString &mode);
    Q_INVOKABLE void sendSetModel(const QString &model);
    Q_INVOKABLE void sendEStop();
    // Release a latched E-stop: stop re-asserting and send Estop_Command=0 once.
    Q_INVOKABLE void clearEStop();
    // destId encodes Destination_Select (1 Start, 2 Parking, 3 Charge, 4 Garage);
    // name is the human label used only for the activity log.
    Q_INVOKABLE void sendSetDestination(int destId, const QString &name);

    // ── 0x201 UI_Goal_Pose TX — NavigateToPose goal (x, y, yaw) ──────────
    // Matches the project's actual destination mechanism (report §3.2.3:
    // NavigateToPose x/y/yaw). On the 250m straight route, distM is the
    // along-route distance, latM the lateral offset, yawDeg the final
    // heading. Jetson decodes 0x201 and issues a NavigateToPose action.
    Q_INVOKABLE void sendSetGoalPose(double distM, double latM, double yawDeg);
    // Cancel/abort the current goal: 0x201 with Goal_Valid=0 → vehicle returns to
    // IDLE (re-enabling goal-setting). Lets the operator retest without restart.
    Q_INVOKABLE void sendCancelGoal();

    // ── Replay (Task 2) — feed a recorded raw-frame CSV back through the SAME
    // processFrame() decode path at original timing. Pauses the virtual bus
    // while replaying and restores it afterwards. CSV row: ts_ms,idHex,len,hex.
    Q_INVOKABLE void startReplay(const QString &path);
    Q_INVOKABLE void stopReplay();

    void setDefaultBitrate(int hz) { m_defaultBitrate = hz; }
    void setBaselineMs(double ms)  { m_ptBaselineMs   = ms; }

    // Tell the virtual bus which Map_Info to broadcast (so the UI's map-mismatch
    // check passes against the loaded map in demo mode). Thread-safe (queued).
    Q_INVOKABLE void setMapInfoBroadcast(double ox, double oy, double res,
                                         int w, int h, int ver);
    // Tell the virtual bus which Map_Datum (WGS84 local origin) to broadcast.
    Q_INVOKABLE void setMapDatumBroadcast(double lat, double lon);
    // Demo only: place the virtual ego at an on-road start (e.g. nearest HD
    // centerline) so the simulated lane-center deviation is realistic.
    Q_INVOKABLE void setVirtualEgoStart(double x, double y, double yaw);

    // ── Thread-safe snapshot API consumed by the UI poller ──────────────
    // LatestValues / LogEntry are defined in WireState.h (shared with the UDP
    // StateLink so the iPad client can reuse the same apply path). A "dirty"
    // bit per domain says "fresh since last takeLatest()".

    // Called by UI thread once per render frame (~16 ms).
    // Returns the current snapshot and clears dirty bits in-place.
    LatestValues takeLatest();
    QVector<LogEntry> takePendingLogs();

    // Bridge forwarding: full current state with all STATE dirty bits forced
    // true (client setIf-guards on apply, so re-sending unchanged values is a
    // no-op). The one-shot Fail_Safe_Event edge is passed through and consumed
    // so it forwards exactly once. Does NOT touch the state values.
    LatestValues peekLatest();

signals:
    void statusChanged();
    void replayingChanged(bool replaying);   // worker → main (KpiData.setReplaying)
    // Emitted (worker thread) for every decoded RX frame so a recorder can
    // persist the raw bytes. Suppressed during replay to avoid re-recording.
    void frameForRecord(quint32 id, const QByteArray &payload, qint64 tsMs);

private slots:
    void onFramesReceived();
    void onSlcanReadyRead();              // slcan: parse serial bytes → processFrame
    void onFramesWritten(qint64 framesCount);
    void onErrorOccurred(QCanBusDevice::CanBusError err);
    void onStateChanged(QCanBusDevice::CanBusDeviceState state);
    void tryReconnect();
    void virtualTick20();
    void virtualTick100();
    void virtualTick1000();

private:
    // Worker-thread bodies (invoked via QueuedConnection from public API).
    bool openInterfaceImpl(const QString &plugin, const QString &deviceName);
    bool openSlcanImpl(const QString &portName, int bitrate);
    bool writeCanFrame(quint32 id, const QByteArray &payload);   // TX via slcan or QCanBus
    static QByteArray slcanEncode(quint32 id, const QByteArray &payload);
    static int        slcanBitrateCode(int bitrate);             // bps → LAWICEL S0..S8
    void closeInterfaceImpl();
    void startVirtualBusImpl();
    void stopVirtualBusImpl();
    void sendUiCommandImpl(quint8 cmdId, quint8 precision, quint8 model,
                           quint8 amcl, quint8 estop, quint8 destination,
                           const QString &logSummary);
    void sendGoalPoseImpl(double distM, double latM, double yawDeg);
    void sendCancelGoalImpl();
    void startReplayImpl(const QString &path);
    void stopReplayImpl();
    void replayTick();

    void processFrame(const QCanBusFrame &f);
    void decodeObstacle(const QByteArray &p);
    void decodeVehicleStatus(const QByteArray &p);
    void decodeRealtimeKpi(const QByteArray &p);
    void decodeSystemResource(const QByteArray &p);
    void decodeRouteStatus(const QByteArray &p);
    void decodeHardwareInfo(const QByteArray &p);
    void decodeFailsafeEvent(const QByteArray &p);
    void decodeMapInfo(const QByteArray &p);
    void decodeEgoPose(const QByteArray &p);
    void decodeMapDatum(const QByteArray &p);             // 0x108
    void decodeStm32Imu(const QByteArray &p);             // 0x21
    void decodeStm32Encoder(const QByteArray &p);         // 0x20
    void decodePlanningStatus(const QByteArray &p);       // 0x106
    void decodePerceptionValidation(const QByteArray &p); // 0x107
    void decodeNetworkStatus(const QByteArray &p);        // 0x109
    void decodeLocalizationStatus(const QByteArray &p);   // B1 (0x10A)
    void decodeBehaviorState(const QByteArray &p);        // B2 (0x109)

    // Map_Info reassembly state (worker thread): the two mux halves arrive in
    // separate frames; we cache them and publish the combined snapshot.
    double m_miOriginX = 0, m_miOriginY = 0, m_miResolution = 0;
    int    m_miWidth = 0, m_miHeight = 0, m_miVersion = 0;

    // Map_Info the virtual bus broadcasts (demo). m_bcMapValid gates emission.
    bool   m_bcMapValid = false;
    double m_bcOriginX = 0, m_bcOriginY = 0, m_bcResolution = 0;
    int    m_bcWidth = 0, m_bcHeight = 0, m_bcVersion = 0;
    quint8 m_vMapCounter = 0;
    bool   m_bcDatumValid = false;
    double m_bcDatumLat = 0, m_bcDatumLon = 0;

    void noteRxFromAlive(quint32 frameId, quint8 counter);
    void publishBusStats();
    void queueLog(const QString &code, const QString &msg,
                  const QString &severity, const QString &src);

    QByteArray buildObstacleFrame();
    QByteArray buildVehicleStatusFrame();
    QByteArray buildRealtimeKpiFrame();
    QByteArray buildSystemResourceFrame();
    QByteArray buildRouteStatusFrame();
    QByteArray buildHardwareInfoFrame();
    QByteArray buildFailsafeEventFrame(quint8 evCode, quint8 reasonCode, quint8 lvl);

    void setStatus(const QString &s);

    static void putU8 (QByteArray &b, int off, quint8 v);
    static void putU16(QByteArray &b, int off, quint16 v);
    static void putI16(QByteArray &b, int off, qint16 v);
    static void putU32(QByteArray &b, int off, quint32 v);
    static quint8  getU8 (const QByteArray &b, int off);
    static quint16 getU16(const QByteArray &b, int off);
    static qint16  getI16(const QByteArray &b, int off);
    static quint32 getU32(const QByteArray &b, int off);

    KpiData *m_kpi;                  // not touched from worker thread directly
    QCanBusDevice *m_dev = nullptr;
    QSerialPort   *m_serial = nullptr;   // slcan transport; null unless in slcan mode
    QByteArray     m_slcanRxBuf;         // partial-line accumulator for slcan RX

    // Thread-shared state. Anything in or behind m_latest / m_pendingLogs /
    // m_status / m_currentOptMode / m_currentYoloModel must be accessed under
    // m_mutex.
    mutable QMutex m_mutex;
    LatestValues   m_latest;
    QVector<LogEntry> m_pendingLogs;
    QString        m_status = "disconnected";
    // Worker reads these to encode 0x101 Yolo_Mode in the virtual bus. UI
    // thread writes them via setOpt/setModel queued invocations.
    QString        m_currentOptMode = "INT8";
    QString        m_currentYoloModel = "YOLO26s";

    // Bus stats — counters live only on worker thread.
    int m_rxFrames = 0, m_txFrames = 0;
    QElapsedTimer m_uptime;
    QElapsedTimer m_lastBusStatsPush;

    QElapsedTimer m_txTimer;
    bool   m_txPending = false;
    double m_lastTxLatencyMs = 0;

    struct AliveTrack { bool seen = false; quint8 last = 0; };
    AliveTrack m_alive100, m_alive101, m_alive102, m_alive103, m_alive104;

    QString m_lastDriveState;
    QString m_lastOptModeLog;
    QString m_lastYoloModelLog;
    int     m_lastFailsafeLevel = 0;
    qint64  m_lastPerceptLogMs = 0;

    // Bus-off / disconnect recovery
    QTimer  m_reconnectTimer;
    QString m_currentPlugin;
    QString m_currentDevice;
    bool    m_wantAutoReconnect = false;
    int     m_reconnectAttempts = 0;

    QTimer m_virt20ms;
    QTimer m_virt100ms;
    QTimer m_virt1000ms;
    // E-stop is an EVENT request (DBC 0x200 cycle=0): on engage/clear we send a
    // short BURST of frames for reliable delivery (CAN drop / UDP loss), then
    // stop — NOT a continuous stream (continuous 10 Hz made the actuators buzz).
    // The Jetson is responsible for LATCHING the actuator stop until cleared.
    QTimer m_estopRepeat;        // burst timer (fires m_estopBurst times then stops)
    bool   m_estopLatched = false;
    int    m_estopBurst = 0;     // remaining frames in the current engage/clear burst
    double m_vInfer    = 22.0;
    double m_vGpu      = 50.0;
    double m_vCpu      = 36.0;
    double m_vTemp     = 60.0;
    double m_vPathErr  = 110.0;
    double m_vSpeedMs  = 1.2;
    double m_vSteer    = 0.0;
    // Virtual mission state machine (demo): 0 IDLE, 2 AUTO. Starts IDLE so the
    // operator can set a goal (track touch / popup); a goal promotes to AUTO,
    // and arrival returns to IDLE. Lets the demo exercise the Target Lock.
    int    m_vDriveState = 0;     // IDLE on boot
    // 2D map-frame mission (demo): ego drives straight toward the goal pose and
    // returns to IDLE on arrival — works for ANY map shape, not just a line.
    bool   m_vGoalValid = false;
    double m_vGoalX = 0, m_vGoalY = 0;
    double m_vEgoX = 0, m_vEgoY = 0, m_vEgoYaw = 0;
    quint8 m_vEgoCounter = 0;
    double m_vDistM    = 9.8;
    double m_vAng      = 0.0;
    double m_vConf     = 0.92;
    double m_vRamPct   = 55.0;
    double m_vSwapPct  = 8.0;
    int    m_vSessionHz = 12;
    double m_vBusLoad  = 18.0;   // % — slewed in virtualTick100 (was a flat literal)
    double m_vCanLoss  = 0.05;   // % — slewed in virtualTick100 (was frozen 0.60)
    quint8 m_vFailsafeLevel = 1; // unified demo level (hysteresis) — 0x100 byte6 + 0x1FF use it
    double m_vRouteM   = 0.0;
    double m_vAmclErr  = 0.22;
    double m_vMissionPct = 70.0;
    quint8 m_vCounter100 = 0;
    quint8 m_vCounter101 = 0;
    quint8 m_vCounter102 = 0;
    quint8 m_vCounter103 = 0;
    quint8 m_vCounter104 = 0;
    quint8 m_vCounter105 = 0;
    quint8 m_vCounter1FF = 0;
    quint8 m_vPlanCounter = 0;   // 0x106 alive
    quint8 m_vPercCounter = 0;   // 0x107 alive
    quint8 m_vNetCounter  = 0;   // 0x109 alive
    quint8 m_txCommandCounter = 0;
    quint8 m_txGoalCounter = 0;

    // Replay engine (worker thread).
    struct ReplayRow { qint64 ts; quint32 id; QByteArray bytes; };
    QTimer        m_replayTimer;
    QElapsedTimer m_replayClock;
    QVector<ReplayRow> m_replayRows;
    int  m_replayIdx = 0;
    bool m_replaying = false;
    bool m_replayWasVirtual = false;

    int    m_defaultBitrate = 500000;
    double m_ptBaselineMs   = 68.0;
};
