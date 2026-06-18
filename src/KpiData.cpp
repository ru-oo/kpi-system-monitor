#include "KpiData.h"
#include <QTime>

// KpiData is a passive sink. All values arrive via apply*() slots invoked by
// CanBridge / GPS / IMU bridges after they decode raw input.
//
// ─────────────────────────────────────────────────────────────────────────
//  GUARD VALVES — change-detection in every apply*() function
// ─────────────────────────────────────────────────────────────────────────
//  Without guards, every CAN frame (every 10–100 ms per domain) emits a
//  signal even if the value is identical to the previous one. Every QML
//  binding bound to that property re-evaluates, every Text re-formats its
//  string, and the scene graph may schedule a redraw. With the UI poll
//  running at 8 ms (≈125 Hz) and 0x100/0x101 arriving at 50 Hz each, that
//  is several hundred unnecessary binding evaluations per second per panel.
//
//  Guard pattern: for each member field, only write + flag changed when
//  the new value actually differs. Emit the domain's signal only if at
//  least one of its fields changed. Side effects that must run every call
//  (edge-trigger mission counter, has-flag flip) are kept outside the
//  guard so transitions are still detected.
// ─────────────────────────────────────────────────────────────────────────

namespace {
// setIf(slot, value, changed) — writes `value` into `slot` only when it
// differs and OR-s into `changed`. Works for any type with operator!=.
template <typename T>
inline void setIf(T &slot, const T &v, bool &changed) {
    if (slot != v) { slot = v; changed = true; }
}
} // namespace

KpiData::KpiData(QObject *parent) : QObject(parent) {
    // Sensor watchdog — flips lidarOk / imuOk based on freshness of the
    // last received frame from each sensor. The QML binding for "online"
    // pills updates whenever this fires sensorHealthChanged.
    m_sensorWatchdog.setParent(this);
    m_sensorWatchdog.setInterval(500);
    connect(&m_sensorWatchdog, &QTimer::timeout, this, [this]() {
        const bool lidarFresh = m_lastObstacleRxTimer.isValid() &&
                                m_lastObstacleRxTimer.elapsed() < m_lidarStaleMs;
        const bool imuFresh   = m_lastImuRxTimer.isValid() &&
                                m_lastImuRxTimer.elapsed() < m_imuStaleMs;
        const bool encFresh   = m_lastEncRxTimer.isValid() &&
                                m_lastEncRxTimer.elapsed() < m_encStaleMs;
        const bool egoFresh   = m_lastEgoRxTimer.isValid() &&
                                m_lastEgoRxTimer.elapsed() < m_egoStaleMs;
        const bool locFresh   = m_lastLocRxTimer.isValid() &&
                                m_lastLocRxTimer.elapsed() < m_locStaleMs;
        m_lidarOk   = lidarFresh;
        m_imuOk     = imuFresh;
        m_encoderOk = encFresh;
        m_egoOk     = egoFresh;
        m_piLaneOk  = locFresh;
        // Emit once per tick — bindings for ok/silentMs both refresh together.
        // 500 ms cadence is well below the human perception threshold and far
        // cheaper than re-eval at every CAN/IMU frame.
        emit sensorHealthChanged();
    });
    m_sensorWatchdog.start();
}

// ── Derived helpers ──────────────────────────────────────────────────────
QString KpiData::pi5StatusText() const {
    switch (m_pi5StatusCode) {
        case 1:  return "online";
        case 2:  return "timeout";
        case 3:  return "error";
        default: return "no data";
    }
}

int KpiData::cameraOnline() const {
    int n = 0;
    for (int b = 0; b < 8; ++b) if (m_cameraStatus & (1 << b)) ++n;
    return n;
}

int KpiData::missionSuccess() const {
    return qRound(m_missionSuccessPct / 100.0 * m_missionTotal);
}

QString KpiData::jetsonModelName() const {
    return m_jetsonModelCode == 1 ? "Orin" : "Unknown";
}

// ── 0x102 Realtime_KPI ───────────────────────────────────────────────────
void KpiData::applyRealtimeKpi(double inferenceMs, double gpuPct, double cpuPct,
                               double gpuTempC, double pathErrorMm)
{
    bool changed = false;
    setIf(m_inferenceMs,     inferenceMs, changed);
    setIf(m_gpuPct,          gpuPct,      changed);
    setIf(m_cpuPct,          cpuPct,      changed);
    setIf(m_gpuTempC,        gpuTempC,    changed);
    setIf(m_pathDeviationMm, pathErrorMm, changed);
    // Detection response = the real measured YOLO inference latency (0x102
    // Inference_Latency). There is no separate Detect_Latency signal in the DBC,
    // so we use the real value directly (was a fabricated inferenceMs*1.4+30).
    setIf(m_detectLatencyMs, inferenceMs, changed);
    if (!m_hasRealtimeKpi) { m_hasRealtimeKpi = true; changed = true; }
    if (changed) emit kpiChanged();

    // ── Fail-safe transition latency, half 1 ─────────────────────────────
    // Watch for the overload-trigger condition becoming true while we are
    // still in L1. Start the stopwatch; applyFailsafeEvent / applyObstacle
    // stop it when failsafeLevel actually changes. (Spec §4.x: target
    // transition ≤ 100 ms.)
    const bool overloadNow =
        (gpuPct > 95 && inferenceMs > 300) ||   // L3 trigger
        (gpuPct > 85 && inferenceMs > 150);     // L2 trigger
    if (overloadNow && !m_failsafeTriggerActive && m_failsafeLevel == 1) {
        m_failsafeTriggerTimer.start();
        m_failsafeTriggerActive = true;
    }
}

// ── 0x101 Vehicle_Status ─────────────────────────────────────────────────
void KpiData::applyVehicleStatus(double speedKmh, double steeringDeg,
                                 const QString &drivingStateName,
                                 const QString &optMode, const QString &yoloModel,
                                 bool controlEnable)
{
    bool changed = false;
    setIf(m_speedKmh,       speedKmh,         changed);
    setIf(m_steeringDeg,    steeringDeg,      changed);
    setIf(m_driveState,     drivingStateName, changed);
    setIf(m_controlEnable,  controlEnable,    changed);
    if (!m_hasVehicle) { m_hasVehicle = true; changed = true; }

    // ── Drive-state transition logic (edge-trigger, runs unconditionally) ──
    const bool isTransition = (drivingStateName != m_prevDriveStateName);

    // Mission completion counter — AUTO → STOP success, AUTO → ERROR fail.
    if (m_prevDriveStateName == "AUTO" && isTransition) {
        if (drivingStateName == "STOP") {
            ++m_successRuns; ++m_totalRuns; emit runsChanged();
        } else if (drivingStateName == "ERROR") {
            ++m_totalRuns; emit runsChanged();
        }
        // Disengagement = leaving AUTO into STOP/ERROR. Log it with the most
        // recent Fail_Safe_Event Reason_Code so the AV-ops console shows WHY.
        if (drivingStateName == "STOP" || drivingStateName == "ERROR") {
            const QString why = m_lastReasonLabel.isEmpty() ? "None" : m_lastReasonLabel;
            pushLogEvent("DISENGAGE",
                         "AUTO → " + drivingStateName + " · reason: " + why,
                         "disengage", "0x1FF");
        }
        // Perception cumulative: did we detect a parked car this run?
        if (m_perceptDuringRun) ++m_perceptionDetectedRuns;
        m_perceptDuringRun = false;
        emit perceptionStatsChanged();
    }

    // Path-planning timing: spec §4.2 (target ≤ 2000 ms, success ≥ 9/10).
    // The Jetson plans while IDLE/STOP and then promotes to AUTO when
    // a route is ready. So the wall-clock time from leaving AUTO until
    // re-entering AUTO is our local proxy for "planning latency".
    // Transition into IDLE/STOP from anywhere = planning begins.
    if (isTransition &&
        (drivingStateName == "IDLE" || drivingStateName == "STOP") &&
        !m_pathPlanInProgress)
    {
        m_pathPlanTimer.start();
        m_pathPlanInProgress = true;
    }
    // Transition into AUTO while planning was in progress = success.
    if (isTransition && drivingStateName == "AUTO" && m_pathPlanInProgress) {
        m_pathPlanLastMs = m_pathPlanTimer.elapsed();
        ++m_pathPlanSuccessRuns; ++m_pathPlanTotalRuns;
        m_pathPlanInProgress = false;
        m_hasPathPlan = true;
        emit pathPlanChanged();
    }
    // Transition into ERROR while planning was in progress = failed plan.
    if (isTransition && drivingStateName == "ERROR" && m_pathPlanInProgress) {
        ++m_pathPlanTotalRuns;
        m_pathPlanInProgress = false;
        m_hasPathPlan = true;
        emit pathPlanChanged();
    }

    m_prevDriveStateName = drivingStateName;
    recomputeBehavior();   // B2 (drive-state changed)

    bool modeChanged = false;
    if (!optMode.isEmpty() && optMode != "OFF" && m_optMode != optMode) {
        m_optMode = optMode; modeChanged = true;
    }
    if (!yoloModel.isEmpty() && yoloModel != "LiDAR" && m_yoloModel != yoloModel) {
        m_yoloModel = yoloModel; modeChanged = true;
    }

    if (changed) emit kpiChanged();
    if (modeChanged) { emit optModeChanged(); emit yoloModelChanged(); }
}

// ── 0x100 Obstacle_Detection ─────────────────────────────────────────────
void KpiData::applyObstacle(double distM, double angleDeg, double confidence01,
                            const QString &className, int failsafeLevel)
{
    // LiDAR heartbeat — 0x100 is the only frame the LiDAR/DBSCAN pipeline
    // produces, so a fresh decode means LiDAR is alive. The sensor
    // watchdog QTimer flips m_lidarOk based on this freshness window.
    m_lastObstacleRxTimer.start();

    // Perception cumulative: any high-confidence detection while AUTO
    // counts this run as "successful detection".
    if (m_driveState == "AUTO" && confidence01 > 0.5) {
        m_perceptDuringRun = true;
    }

    bool changed = false;
    setIf(m_obstacleDistM,    distM,        changed);
    setIf(m_obstacleAngleDeg, angleDeg,     changed);
    setIf(m_obstacleConf,     confidence01, changed);
    setIf(m_obstacleClass,    className,    changed);
    if (!m_hasObstacle) { m_hasObstacle = true; changed = true; }

    proposeFailsafeLevel(failsafeLevel, changed);   // debounced (0x100 path)
    if (changed) emit kpiChanged();
    recomputeBehavior();   // B2 (obstacle proximity changed)
}

// ── 0x103 System_Resource ────────────────────────────────────────────────
void KpiData::applySystemResource(double ramUsagePct, double swapUsagePct,
                                  int sessionRateHz, double busLoadPct,
                                  double canLossPct, int pi5StatusCode,
                                  int cameraStatus)
{
    bool changed = false;
    setIf(m_ramUsagePct,   ramUsagePct,   changed);
    setIf(m_swapUsagePct,  swapUsagePct,  changed);
    setIf(m_sessionRateHz, sessionRateHz, changed);
    setIf(m_busLoad,       busLoadPct,    changed);
    setIf(m_frameLossPct,  canLossPct,    changed);
    setIf(m_pi5StatusCode, pi5StatusCode, changed);
    setIf(m_cameraStatus,  cameraStatus,  changed);
    if (!m_hasSysResource) { m_hasSysResource = true; changed = true; }
    if (changed) emit sysResourceChanged();
}

// ── 0x104 Route_Status ───────────────────────────────────────────────────
void KpiData::applyRouteStatus(double routeProgressM, double amclErrorM,
                               double missionSuccessPct, int routeState)
{
    bool changed = false;
    setIf(m_progressM,         routeProgressM,    changed);
    setIf(m_amclErrorM,        amclErrorM,        changed);
    setIf(m_missionSuccessPct, missionSuccessPct, changed);
    setIf(m_routeState,        routeState,        changed);
    if (!m_hasRoute) { m_hasRoute = true; changed = true; }
    if (changed) emit routeChanged();
}

// B1 — localization source/confidence. Post-AMCL the live source is EKF via
// 0x10A Localization_Status (applyLocalizationStatus). Until that arrives we
// only show a coarse GPS/INIT placeholder — no AMCL/ODOM derivation anymore.
void KpiData::recomputeLocalization() {
    if (m_locFromCan) return;     // real EKF signal (0x10A) takes over once present
    QString mode; double q;
    if (m_gpsHasFix) {
        mode = "GPS"; q = 0.5;
    } else {
        mode = "INIT"; q = 0.0;
    }
    if (mode != m_locMode || !qFuzzyCompare(q + 1.0, m_locQuality + 1.0)) {
        m_locMode = mode; m_locQuality = q;
        emit localizationChanged();
    }
}

// B2 — derive a coarse behavior state from drive-state + obstacle proximity
// until a real 0x109 Behavior_State exists (then applyBehaviorState wins).
void KpiData::recomputeBehavior() {
    if (m_behaviorFromCan) return;
    QString b;
    if (m_driveState == "STOP") {
        b = "OBSTACLE_STOP";
    } else if (m_driveState == "AVOIDANCE") {
        b = "NUDGE";
    } else if (m_driveState == "AUTO") {
        if (m_hasObstacle && m_obstacleDistM < 2.5)      b = "OBSTACLE_STOP";
        else if (m_hasObstacle && m_obstacleDistM < 4.5) b = "NUDGE";
        else                                             b = "LANE_FOLLOW";
    } else {
        b = "STANDBY";   // IDLE / MANUAL / ERROR
    }
    if (b != m_behaviorState) { m_behaviorState = b; emit behaviorChanged(); }
}

// ── 0x105 Hardware_Info ──────────────────────────────────────────────────
void KpiData::applyHardwareInfo(int jetsonModelCode, double orinMemoryGb)
{
    bool changed = false;
    setIf(m_jetsonModelCode, jetsonModelCode, changed);
    setIf(m_orinMemoryGb,    orinMemoryGb,    changed);
    if (!m_hasHardware) { m_hasHardware = true; changed = true; }
    if (changed) {
        emit hardwareChanged();
        // ramTotalGb is derived from orinMemoryGb — refresh the memory tile.
        emit sysResourceChanged();
    }
}

// ── 0x1FF Fail_Safe_Event ────────────────────────────────────────────────
// Event-driven (not periodic). Still guard for code symmetry.
// Reason_Code_Table (valeo_project_can.dbc).
static QString reasonLabel(quint8 code) {
    switch (code) {
        case 8: return "Pi5_Fail";       case 7: return "Path_Fail";
        case 6: return "AMCL_Unstable";  case 5: return "UART_Fail";
        case 4: return "Camera_Fail";    case 3: return "LiDAR_Fail";
        case 2: return "Inference_Delay";case 1: return "GPU_Overload";
        default: return "None";
    }
}

void KpiData::applyFailsafeEvent(quint8 eventCode, quint8 reasonCode, quint8 level)
{
    Q_UNUSED(eventCode)
    m_lastReasonCode  = reasonCode;            // remembered for disengagement log
    m_lastReasonLabel = reasonLabel(reasonCode);
    bool changed = false;
    if (!m_hasFailsafeEvent) { m_hasFailsafeEvent = true; changed = true; }
    proposeFailsafeLevel(int(level), changed);   // debounced (0x1FF path)
    if (changed) emit kpiChanged();
}

// ── Fail-safe level commit + debounce ────────────────────────────────────
// Escalation (more severe) applies immediately — never delay a real fail-safe,
// and keep the §4.x ≤100 ms transition-latency KPI honest. De-escalation must
// hold the lower level for a dwell window, so a flapping source (demo jitter or
// a real Jetson near a threshold) can't thrash the banner; the worst level is
// shown until things genuinely settle.
void KpiData::commitFailsafeLevel(int level, bool &changed) {
    if (m_failsafeTriggerActive && level != m_failsafeLevel) {
        m_failsafeLastTransitionMs = m_failsafeTriggerTimer.nsecsElapsed() / 1.0e6;
        if (m_failsafeLastTransitionMs > m_failsafeMaxTransitionMs)
            m_failsafeMaxTransitionMs = m_failsafeLastTransitionMs;
        m_failsafeTriggerActive = false;
    }
    setIf(m_failsafeLevel, level, changed);
}

void KpiData::proposeFailsafeLevel(int level, bool &changed) {
    if (level < 1 || level > 4) return;
    if (level >= m_failsafeLevel) {
        m_pendingFailsafeLevel = level;
        commitFailsafeLevel(level, changed);          // escalate / reaffirm now
    } else if (level != m_pendingFailsafeLevel) {
        m_pendingFailsafeLevel = level;               // new lower target → start dwell
        m_failsafePendingSince.restart();
    } else if (!m_failsafePendingSince.isValid()
               || m_failsafePendingSince.elapsed() >= m_failsafeDwellMs) {
        commitFailsafeLevel(level, changed);          // lower level held → settle
    }
}

// ── Bus statistics ───────────────────────────────────────────────────────
// framesRx/framesTx/uptimeMs almost always change — the guard rarely skips,
// but applying it costs nothing and avoids a redundant emit during long
// silent periods (e.g. virtual bus disabled and no real frames coming in).
void KpiData::applyBusStats(double txLatencyMs, int framesRx, int framesTx,
                            qint64 uptimeMs)
{
    bool changed = false;
    setIf(m_canTxLatencyMs, txLatencyMs, changed);
    setIf(m_framesRx,       framesRx,    changed);
    setIf(m_framesTx,       framesTx,    changed);
    setIf(m_uptimeMs,       uptimeMs,    changed);
    if (!m_canOnline) { m_canOnline = true; changed = true; }
    if (changed) emit busStatsChanged();
}

void KpiData::applyAccuracy(double fp32mAP, double fp16mAP, double int8mAP) {
    bool changed = false;
    setIf(m_accFp32, fp32mAP, changed);
    setIf(m_accFp16, fp16mAP, changed);
    setIf(m_accInt8, int8mAP, changed);
    if (!m_hasAccuracy) { m_hasAccuracy = true; changed = true; }
    if (changed) emit accuracyChanged();
}

// ── External sensors ────────────────────────────────────────────────────
void KpiData::applyGps(bool hasFix, int satCount, double latDeg, double lonDeg,
                       double altitudeM, double speedKmh, double headingDeg)
{
    bool changed = false;
    setIf(m_gpsHasFix,     hasFix,     changed);
    setIf(m_gpsSatCount,   satCount,   changed);
    setIf(m_gpsLatDeg,     latDeg,     changed);
    setIf(m_gpsLonDeg,     lonDeg,     changed);
    setIf(m_gpsAltitudeM,  altitudeM,  changed);
    setIf(m_gpsSpeedKmh,   speedKmh,   changed);
    setIf(m_gpsHeadingDeg, headingDeg, changed);
    if (!m_hasGps) { m_hasGps = true; changed = true; }
    if (changed) emit gpsChanged();
    recomputeLocalization();   // B1 (gpsHasFix may have changed)
}

void KpiData::applyImu(int calSys, int calGyro, int calAccel, int calMag,
                       double headingDeg, double rollDeg, double pitchDeg)
{
    m_lastImuRxTimer.start();   // heartbeat → sensor watchdog flips imuOk
    bool changed = false;
    setIf(m_imuCalSys,     calSys,     changed);
    setIf(m_imuCalGyro,    calGyro,    changed);
    setIf(m_imuCalAccel,   calAccel,   changed);
    setIf(m_imuCalMag,     calMag,     changed);
    setIf(m_imuHeadingDeg, headingDeg, changed);
    setIf(m_imuRollDeg,    rollDeg,    changed);
    setIf(m_imuPitchDeg,   pitchDeg,   changed);
    if (!m_hasImu) { m_hasImu = true; changed = true; }
    if (changed) emit imuChanged();
}

// 0x21 STM32_IMU_Feedback — chassis IMU over CAN. No BNO055 calibration bytes;
// yaw maps to heading. Starts the IMU watchdog so the "online" pill lights up.
void KpiData::applyImuFromCan(double yawDeg, double gyroZDps, double rollDeg, double pitchDeg)
{
    Q_UNUSED(gyroZDps);   // not surfaced in the UI yet (kept in DBC for completeness)
    m_lastImuRxTimer.start();
    bool changed = false;
    setIf(m_imuHeadingDeg, yawDeg,   changed);
    setIf(m_imuRollDeg,    rollDeg,  changed);
    setIf(m_imuPitchDeg,   pitchDeg, changed);
    if (!m_hasImu) { m_hasImu = true; changed = true; }
    if (changed) emit imuChanged();
}

// 0x20 STM32_Encoder_Feedback — wheel tick counts. Drives the drivetrain
// freshness watchdog (encoderOk) so the monitoring page shows it connected.
void KpiData::applyEncoder(int leftCount, int rightCount)
{
    m_lastEncRxTimer.start();
    bool changed = false;
    setIf(m_encLeft,  leftCount,  changed);
    setIf(m_encRight, rightCount, changed);
    if (!m_hasEncoder) { m_hasEncoder = true; changed = true; }
    if (changed) emit encoderChanged();
}

// 0x106 Planning_Status — CAN-authoritative path-plan stats. Overwrites the
// locally-derived (drive-state timer) values; firmware total/success are
// absolute cumulative counts, so we SET rather than increment.
void KpiData::applyPlanningStatus(double lastMs, int successRuns, int totalRuns, int state)
{
    bool changed = false;
    setIf(m_pathPlanLastMs,      lastMs,      changed);
    setIf(m_pathPlanSuccessRuns, successRuns, changed);
    setIf(m_pathPlanTotalRuns,   totalRuns,   changed);
    setIf(m_planningState,       state,       changed);
    if (!m_hasPathPlan) { m_hasPathPlan = true; changed = true; }
    if (changed) emit pathPlanChanged();
}

// 0x107 Perception_Validation — detection runs, false positives, trigger acc.
void KpiData::applyPerceptionValidation(int detectedRuns, int totalRuns,
                                        int falsePositives, double triggerAccuracyPct)
{
    bool changed = false;
    setIf(m_perceptionDetectedRuns, detectedRuns,       changed);
    setIf(m_perceptionTotalRuns,    totalRuns,          changed);
    setIf(m_falsePositiveCount,     falsePositives,     changed);
    setIf(m_triggerAccuracyPct,     triggerAccuracyPct, changed);
    if (changed) emit perceptionStatsChanged();
}

// 0x109 Network_Status — wifi link health (drives the header WIFI pill).
void KpiData::applyNetworkStatus(double wifiPingMs, double lossRatePct, int rssiDbm, int state)
{
    bool changed = false;
    setIf(m_wifiPingMs,         wifiPingMs,  changed);
    setIf(m_networkLossRatePct, lossRatePct, changed);
    setIf(m_wifiRssiDbm,        rssiDbm,     changed);
    setIf(m_networkState,       state,       changed);
    if (!m_hasNetwork) { m_hasNetwork = true; changed = true; }
    if (changed) emit networkChanged();
}

// Log events are always meaningful (one entry per unique event) — no guard.
void KpiData::pushLogEvent(const QString &code, const QString &msg,
                           const QString &severity, const QString &src)
{
    const QTime now = QTime::currentTime();
    const QString ts = now.toString("HH:mm:ss") + "." +
                       QString::number(now.msec() / 10).rightJustified(2, '0');
    emit logEvent(ts, code, msg, severity, src);
}
