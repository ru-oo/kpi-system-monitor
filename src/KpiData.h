#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QTimer>

// KpiData — passive value holder driven exclusively by CAN frames decoded in
// CanBridge (valeo_project_can.dbc). No simulation.
//
// DBC → apply*() mapping:
//   0x100 Obstacle_Detection  → applyObstacle()        → hasObstacle
//   0x101 Vehicle_Status      → applyVehicleStatus()   → hasVehicle
//   0x102 Realtime_KPI        → applyRealtimeKpi()     → hasRealtimeKpi
//   0x103 System_Resource     → applySystemResource()  → hasSysResource (memory/session/pi5/busload)
//   0x104 Route_Status        → applyRouteStatus()     → hasRoute (route progress / amcl / mission)
//   0x105 Hardware_Info       → applyHardwareInfo()    → hasHardware
//   0x1FF Fail_Safe_Event     → applyFailsafeEvent()
//
// 0x200 UI_Command is the only TX frame (PC_UI → Jetson) — built & sent by
// CanBridge when dashboard buttons are pressed.
//
// No CAN signal: wifiPingMs (hasNetwork stays false → "—"), per-Pi5 latency/
// loss/lastSeen (only a status enum exists), canTxLatencyMs.
class KpiData : public QObject {
    Q_OBJECT

    // ── 0x102 Realtime_KPI ───────────────────────────────────────────────
    Q_PROPERTY(double inferenceMs     READ inferenceMs     NOTIFY kpiChanged)
    Q_PROPERTY(double gpuPct          READ gpuPct          NOTIFY kpiChanged)
    Q_PROPERTY(double cpuPct          READ cpuPct          NOTIFY kpiChanged)
    Q_PROPERTY(double gpuTempC        READ gpuTempC        NOTIFY kpiChanged)
    Q_PROPERTY(double pathDeviationMm READ pathDeviationMm NOTIFY kpiChanged)
    Q_PROPERTY(double detectLatencyMs READ detectLatencyMs NOTIFY kpiChanged)
    Q_PROPERTY(bool   hasRealtimeKpi  READ hasRealtimeKpi  NOTIFY kpiChanged)

    // ── 0x101 Vehicle_Status ─────────────────────────────────────────────
    Q_PROPERTY(double  speedKmh      READ speedKmh      NOTIFY kpiChanged)
    Q_PROPERTY(double  steeringDeg   READ steeringDeg   NOTIFY kpiChanged)
    Q_PROPERTY(QString driveState    READ driveState    NOTIFY kpiChanged)
    Q_PROPERTY(bool    controlEnable READ controlEnable NOTIFY kpiChanged)
    Q_PROPERTY(bool    hasVehicle    READ hasVehicle    NOTIFY kpiChanged)

    // ── 0x100 Obstacle_Detection ─────────────────────────────────────────
    Q_PROPERTY(double  obstacleDistM    READ obstacleDistM    NOTIFY kpiChanged)
    Q_PROPERTY(double  obstacleAngleDeg READ obstacleAngleDeg NOTIFY kpiChanged)
    Q_PROPERTY(double  obstacleConf     READ obstacleConf     NOTIFY kpiChanged)
    Q_PROPERTY(QString obstacleClass    READ obstacleClass    NOTIFY kpiChanged)
    Q_PROPERTY(bool    hasObstacle      READ hasObstacle      NOTIFY kpiChanged)
    // True only for a REAL detection (conf > 0). A "no detection" 0x100 frame is
    // all-zero → class 0 = "person", dist 0, conf 0; gate markers on this so we
    // don't draw a phantom obstacle on the car. (hasObstacle = "0x100 seen".)
    Q_PROPERTY(bool    obstacleDetected READ obstacleDetected NOTIFY kpiChanged)

    Q_PROPERTY(int  failsafeLevel    READ failsafeLevel    NOTIFY kpiChanged)
    Q_PROPERTY(bool hasFailsafeEvent READ hasFailsafeEvent NOTIFY kpiChanged)

    // Yolo_Mode (0x101) decoded → two display fields. Writeable from QML so
    // button presses update local state immediately; the 0x101 echo confirms.
    Q_PROPERTY(QString optMode    READ optMode    WRITE setOptMode    NOTIFY optModeChanged)
    Q_PROPERTY(QString yoloModel  READ yoloModel  WRITE setYoloModel  NOTIFY yoloModelChanged)

    // ── 0x103 System_Resource (memory / session / pi5 / bus stats) ───────
    Q_PROPERTY(double  ramUsedGb      READ ramUsedGb      NOTIFY sysResourceChanged)
    Q_PROPERTY(double  ramTotalGb     READ ramTotalGb     NOTIFY sysResourceChanged)
    Q_PROPERTY(double  swapUsedGb     READ swapUsedGb     NOTIFY sysResourceChanged)
    Q_PROPERTY(double  swapTotalGb    READ swapTotalGb    NOTIFY sysResourceChanged)
    Q_PROPERTY(bool    hasMemory      READ hasMemory      NOTIFY sysResourceChanged)
    Q_PROPERTY(int     sessionRateHz  READ sessionRateHz  NOTIFY sysResourceChanged)
    Q_PROPERTY(bool    hasSession     READ hasSession     NOTIFY sysResourceChanged)
    Q_PROPERTY(double  busLoad        READ busLoad        NOTIFY sysResourceChanged)
    Q_PROPERTY(double  frameLossPct   READ frameLossPct   NOTIFY sysResourceChanged)
    Q_PROPERTY(int     pi5StatusCode  READ pi5StatusCode  NOTIFY sysResourceChanged)
    Q_PROPERTY(QString pi5StatusText  READ pi5StatusText  NOTIFY sysResourceChanged)
    Q_PROPERTY(bool    pi5Online      READ pi5Online      NOTIFY sysResourceChanged)
    Q_PROPERTY(int     cameraStatus   READ cameraStatus   NOTIFY sysResourceChanged)
    Q_PROPERTY(int     cameraOnline   READ cameraOnline   NOTIFY sysResourceChanged)
    Q_PROPERTY(bool    hasPi5         READ hasPi5         NOTIFY sysResourceChanged)

    // ── 0x104 Route_Status (route / amcl / mission) ──────────────────────
    Q_PROPERTY(double progressM         READ progressM         NOTIFY routeChanged)
    Q_PROPERTY(double amclErrorM        READ amclErrorM        NOTIFY routeChanged)
    Q_PROPERTY(double missionSuccessPct READ missionSuccessPct NOTIFY routeChanged)
    Q_PROPERTY(int    missionSuccess    READ missionSuccess    NOTIFY routeChanged)
    Q_PROPERTY(int    missionTotal      READ missionTotal      NOTIFY routeChanged)
    Q_PROPERTY(int    routeState        READ routeState        NOTIFY routeChanged)
    Q_PROPERTY(bool   hasMission        READ hasMission        NOTIFY routeChanged)
    Q_PROPERTY(bool   hasLocalization   READ hasLocalization   NOTIFY routeChanged)

    // Edge-trigger mission counter — locally computed from drive-state
    // transitions on 0x101. AUTO → STOP counts as success; AUTO → ERROR as
    // failure. Cross-checks the Jetson-reported Mission_Success(%) on 0x104.
    Q_PROPERTY(int    successRuns         READ successRuns       NOTIFY runsChanged)
    Q_PROPERTY(int    totalRuns           READ totalRuns         NOTIFY runsChanged)
    Q_PROPERTY(double localSuccessRatePct READ localSuccessRatePct NOTIFY runsChanged)

    // ── Spec §4.2 KPIs not in current DBC ────────────────────────────────
    // Path-planning timing (target ≤ 2000 ms, success ≥ 9/10). Locally
    // approximated from IDLE→AUTO transitions on 0x101 until the firmware
    // ships a dedicated Planning_Status message.
    Q_PROPERTY(double pathPlanLastMs         READ pathPlanLastMs         NOTIFY pathPlanChanged)
    Q_PROPERTY(int    pathPlanSuccessRuns    READ pathPlanSuccessRuns    NOTIFY pathPlanChanged)
    Q_PROPERTY(int    pathPlanTotalRuns      READ pathPlanTotalRuns      NOTIFY pathPlanChanged)
    Q_PROPERTY(double pathPlanSuccessRatePct READ pathPlanSuccessRatePct NOTIFY pathPlanChanged)
    Q_PROPERTY(bool   hasPathPlan            READ hasPathPlan            NOTIFY pathPlanChanged)
    Q_PROPERTY(int    planningState          READ planningState          NOTIFY pathPlanChanged)  // 0x106 Planning_State [0..3]

    // Perception cumulative — "Was a parked car detected during this run?"
    // (target ≥ 8/10). Counted on each AUTO → STOP/ERROR transition; a run
    // is "detected" if confidence ever exceeded 0.5 while AUTO.
    Q_PROPERTY(int    perceptionDetectedRuns READ perceptionDetectedRuns NOTIFY perceptionStatsChanged)
    Q_PROPERTY(int    perceptionTotalRuns    READ perceptionTotalRuns    NOTIFY perceptionStatsChanged)  // 0x107 own total
    Q_PROPERTY(double perceptionDetectRatePct READ perceptionDetectRatePct NOTIFY perceptionStatsChanged)
    Q_PROPERTY(int    falsePositiveCount     READ falsePositiveCount     NOTIFY perceptionStatsChanged)
    Q_PROPERTY(double triggerAccuracyPct     READ triggerAccuracyPct     NOTIFY perceptionStatsChanged)  // 0x107 Trigger_Accuracy

    // Fail-safe transition latency — time from trigger condition (overload)
    // first met to failsafeLevel actually changing (target ≤ 100 ms).
    // last = most recent measurement; max = worst-case observed.
    Q_PROPERTY(double failsafeLastTransitionMs READ failsafeLastTransitionMs NOTIFY kpiChanged)
    Q_PROPERTY(double failsafeMaxTransitionMs  READ failsafeMaxTransitionMs  NOTIFY kpiChanged)

    // Critical-sensor health — independent of CAN frame counters. LiDAR
    // is inferred from 0x100 heartbeat freshness (no signal in DBC yet);
    // IMU comes from I2cImuBridge having produced a frame.
    Q_PROPERTY(bool   lidarOk           READ lidarOk           NOTIFY sensorHealthChanged)
    Q_PROPERTY(qint64 lidarSilentMs     READ lidarSilentMs     NOTIFY sensorHealthChanged)
    Q_PROPERTY(bool   imuOk             READ imuOk             NOTIFY sensorHealthChanged)
    Q_PROPERTY(qint64 imuSilentMs       READ imuSilentMs       NOTIFY sensorHealthChanged)
    // Drivetrain encoder freshness from 0x20 STM32_Encoder_Feedback heartbeat.
    Q_PROPERTY(bool   encoderOk         READ encoderOk         NOTIFY sensorHealthChanged)
    Q_PROPERTY(qint64 encoderSilentMs   READ encoderSilentMs   NOTIFY sensorHealthChanged)
    Q_PROPERTY(bool   hasEncoder        READ hasEncoder        NOTIFY encoderChanged)
    // Ego-pose (0x10D) freshness — distinguishes a LIVE localization stream from a
    // FROZEN last-known pose. hasEgoPose latches true forever; egoOk goes false when
    // 0x10D stops arriving (watchdog), so the UI can flag a stale/frozen ego.
    Q_PROPERTY(bool   egoOk             READ egoOk             NOTIFY sensorHealthChanged)
    Q_PROPERTY(qint64 egoSilentMs       READ egoSilentMs       NOTIFY sensorHealthChanged)
    Q_PROPERTY(int    encLeftCount      READ encLeftCount      NOTIFY encoderChanged)
    Q_PROPERTY(int    encRightCount     READ encRightCount     NOTIFY encoderChanged)

    // ── 0x105 Hardware_Info ──────────────────────────────────────────────
    Q_PROPERTY(int     jetsonModelCode READ jetsonModelCode NOTIFY hardwareChanged)
    Q_PROPERTY(QString jetsonModelName READ jetsonModelName NOTIFY hardwareChanged)
    Q_PROPERTY(double  orinMemoryGb    READ orinMemoryGb    NOTIFY hardwareChanged)
    Q_PROPERTY(bool    hasHardware     READ hasHardware     NOTIFY hardwareChanged)

    // ── Bus statistics (CanBridge frame counting + device info) ──────────
    Q_PROPERTY(double canTxLatencyMs READ canTxLatencyMs NOTIFY busStatsChanged)
    Q_PROPERTY(int    framesRx       READ framesRx       NOTIFY busStatsChanged)
    Q_PROPERTY(int    framesTx       READ framesTx       NOTIFY busStatsChanged)
    Q_PROPERTY(qint64 uptimeMs       READ uptimeMs       NOTIFY busStatsChanged)
    Q_PROPERTY(bool   can            READ canOnline      NOTIFY busStatsChanged)
    Q_PROPERTY(int    canBitrate     READ canBitrate     NOTIFY busStatsChanged)

    // ── Accuracy mAP (config.json — offline benchmark) ───────────────────
    Q_PROPERTY(double accFp32     READ accFp32     NOTIFY accuracyChanged)
    Q_PROPERTY(double accFp16     READ accFp16     NOTIFY accuracyChanged)
    Q_PROPERTY(double accInt8     READ accInt8     NOTIFY accuracyChanged)
    Q_PROPERTY(bool   hasAccuracy READ hasAccuracy NOTIFY accuracyChanged)

    // ── 0x109 Network_Status (wifi link health) ──────────────────────────
    Q_PROPERTY(double wifiPingMs        READ wifiPingMs        NOTIFY networkChanged)
    Q_PROPERTY(bool   hasNetwork        READ hasNetwork        NOTIFY networkChanged)
    Q_PROPERTY(double networkLossRatePct READ networkLossRatePct NOTIFY networkChanged)
    Q_PROPERTY(int    wifiRssiDbm       READ wifiRssiDbm       NOTIFY networkChanged)
    Q_PROPERTY(int    networkState      READ networkState      NOTIFY networkChanged)  // [0..3]

    // ── External sensors (independent of CAN — cross-validate route) ─────
    Q_PROPERTY(bool   hasGps         READ hasGps         NOTIFY gpsChanged)
    Q_PROPERTY(bool   gpsHasFix      READ gpsHasFix      NOTIFY gpsChanged)
    Q_PROPERTY(int    gpsSatCount    READ gpsSatCount    NOTIFY gpsChanged)
    Q_PROPERTY(double gpsLatDeg      READ gpsLatDeg      NOTIFY gpsChanged)
    Q_PROPERTY(double gpsLonDeg      READ gpsLonDeg      NOTIFY gpsChanged)
    Q_PROPERTY(double gpsAltitudeM   READ gpsAltitudeM   NOTIFY gpsChanged)
    Q_PROPERTY(double gpsSpeedKmh    READ gpsSpeedKmh    NOTIFY gpsChanged)
    Q_PROPERTY(double gpsHeadingDeg  READ gpsHeadingDeg  NOTIFY gpsChanged)

    Q_PROPERTY(bool   hasImu         READ hasImu         NOTIFY imuChanged)
    Q_PROPERTY(int    imuCalSys      READ imuCalSys      NOTIFY imuChanged)
    Q_PROPERTY(int    imuCalGyro     READ imuCalGyro     NOTIFY imuChanged)
    Q_PROPERTY(int    imuCalAccel    READ imuCalAccel    NOTIFY imuChanged)
    Q_PROPERTY(int    imuCalMag      READ imuCalMag      NOTIFY imuChanged)
    Q_PROPERTY(double imuHeadingDeg  READ imuHeadingDeg  NOTIFY imuChanged)
    Q_PROPERTY(double imuRollDeg     READ imuRollDeg     NOTIFY imuChanged)
    Q_PROPERTY(double imuPitchDeg    READ imuPitchDeg    NOTIFY imuChanged)

    // ── UI command/replay state (main thread; set from QML / via CanBridge
    //    signals). Lives here because QML must bind main-thread objects only —
    //    canBridge runs on the worker thread and cannot be a binding target. ──
    Q_PROPERTY(bool   goalActive READ goalActive NOTIFY goalChanged)
    Q_PROPERTY(double goalDistM  READ goalDistM  NOTIFY goalChanged)
    Q_PROPERTY(double goalLatM   READ goalLatM   NOTIFY goalChanged)
    Q_PROPERTY(double goalYawDeg READ goalYawDeg NOTIFY goalChanged)
    Q_PROPERTY(bool   replaying  READ replaying  NOTIFY replayingChanged)

    // Map_Info (0x106) — metadata of the SLAM map the Jetson is using, for the
    // Tactical page's map-mismatch verification. Image never travels on CAN.
    Q_PROPERTY(bool   hasMapInfo      READ hasMapInfo      NOTIFY mapInfoChanged)
    Q_PROPERTY(double mapInfoOriginX  READ mapInfoOriginX  NOTIFY mapInfoChanged)
    Q_PROPERTY(double mapInfoOriginY  READ mapInfoOriginY  NOTIFY mapInfoChanged)
    Q_PROPERTY(double mapInfoResolution READ mapInfoResolution NOTIFY mapInfoChanged)
    Q_PROPERTY(int    mapInfoWidth    READ mapInfoWidth    NOTIFY mapInfoChanged)
    Q_PROPERTY(int    mapInfoHeight   READ mapInfoHeight   NOTIFY mapInfoChanged)
    Q_PROPERTY(int    mapInfoVersion  READ mapInfoVersion  NOTIFY mapInfoChanged)

    // Ego_Pose (0x107) — map-frame vehicle pose (the recommended signal; the
    // Tactical page uses this when present, else the straight-route stopgap).
    Q_PROPERTY(bool   hasEgoPose READ hasEgoPose NOTIFY egoPoseChanged)
    Q_PROPERTY(double egoX       READ egoX       NOTIFY egoPoseChanged)
    Q_PROPERTY(double egoY       READ egoY       NOTIFY egoPoseChanged)
    Q_PROPERTY(double egoYaw     READ egoYaw     NOTIFY egoPoseChanged)
    Q_PROPERTY(bool   hasDatum   READ hasDatum   NOTIFY datumChanged)
    Q_PROPERTY(double datumLat   READ datumLat   NOTIFY datumChanged)
    Q_PROPERTY(double datumLon   READ datumLon   NOTIFY datumChanged)

    // B1 — localization source/confidence. Derived placeholder until a real
    // 0x108 Localization_Status arrives (then applyLocalizationStatus overrides).
    Q_PROPERTY(QString localizationMode    READ localizationMode    NOTIFY localizationChanged)  // AMCL/ODOM/GPS/INIT
    Q_PROPERTY(double  localizationQuality READ localizationQuality NOTIFY localizationChanged)  // 0..1
    Q_PROPERTY(bool    localizationFromCan READ localizationFromCan NOTIFY localizationChanged)

    // B2 — behavior FSM state. Derived placeholder until a real 0x109
    // Behavior_State arrives (then applyBehaviorState overrides).
    Q_PROPERTY(QString behaviorState   READ behaviorState   NOTIFY behaviorChanged)  // LANE_FOLLOW/OBSTACLE_STOP/NUDGE/OVERTAKE/RESUME
    Q_PROPERTY(bool    behaviorFromCan READ behaviorFromCan NOTIFY behaviorChanged)

    // B3 — lane-center deviation. Currently DERIVED in main.cpp (ego → nearest HD
    // centerline cross-track), NOT a raw CAN measurement — laneDevFromCan reflects
    // that a value has been pushed, but it is computed. TODO(CAN): replace with a
    // real Lane_Center_Deviation signal when firmware provides one. Until then the
    // UI marks it derived ("~"). Falls back to pathDeviationMm if nothing pushed.
    Q_PROPERTY(double laneCenterDeviationMm READ laneCenterDeviationMm NOTIFY kpiChanged)
    Q_PROPERTY(bool   laneDevFromCan        READ laneDevFromCan        NOTIFY kpiChanged)

public:
    explicit KpiData(QObject *parent = nullptr);

    bool   hasEgoPose() const { return m_hasEgoPose; }
    double egoX() const       { return m_egoX; }
    double egoY() const       { return m_egoY; }
    double egoYaw() const     { return m_egoYaw; }
    void   applyEgoPose(double x, double y, double yaw) {
        m_hasEgoPose = true; m_egoX = x; m_egoY = y; m_egoYaw = yaw;
        m_lastEgoRxTimer.start();   // heartbeat → sensor watchdog flips egoOk
        emit egoPoseChanged();
    }

    // 0x108 Map_Datum — local-frame origin (WGS84). Drives runtime ENU of HD lanes.
    bool   hasDatum() const { return m_hasDatum; }
    double datumLat() const { return m_datumLat; }
    double datumLon() const { return m_datumLon; }
    void   applyMapDatum(double lat, double lon) {
        // Reject an implausible live datum. Before localization initializes the
        // Jetson can send (0,0) or a corrupted value; applying it would re-project
        // the HD lanes thousands of km away and push the ego off the map (and make
        // lane-center deviation astronomical). Keep the config / last-good datum
        // until a sane one (near the expected test-area datum) arrives.
        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
        if (m_hasRefDatum && (qAbs(lat - m_refDatumLat) > 0.5 || qAbs(lon - m_refDatumLon) > 0.5))
            return;   // > ~50 km from the expected datum → garbage, ignore
        if (m_hasDatum && qFuzzyCompare(lat, m_datumLat) && qFuzzyCompare(lon, m_datumLon)) return;
        m_hasDatum = true; m_datumLat = lat; m_datumLon = lon;
        emit datumChanged();
    }
    // Expected datum (from config) — used to sanity-check live 0x108 datums above.
    void   setReferenceDatum(double lat, double lon) {
        m_refDatumLat = lat; m_refDatumLon = lon; m_hasRefDatum = true;
    }


    // B1 — localization source/confidence
    QString localizationMode() const    { return m_locMode; }
    double  localizationQuality() const { return m_locQuality; }
    bool    localizationFromCan() const { return m_locFromCan; }
    // Real-signal entry point (TODO(CAN): 0x108 Localization_Status). Mode enum:
    // DBC Loc_Mode: 0 INIT, 1 (localized/EKF), 2 ODOM, 3 GPS. quality 0..1.
    // Post-AMCL the "localized" state is the EKF fusion output, so we label it
    // "EKF" (no "AMCL" string in the UI). The DBC enum value is unchanged.
    void applyLocalizationStatus(int mode, double quality) {
        m_locFromCan = true;
        m_locMode = (mode == 1) ? "EKF" : (mode == 2) ? "ODOM" : (mode == 3) ? "GPS" : "INIT";
        m_locQuality = quality;
        emit localizationChanged();
    }

    // B2 — behavior state
    QString behaviorState() const   { return m_behaviorState; }
    bool    behaviorFromCan() const { return m_behaviorFromCan; }
    // Real-signal entry point (TODO(CAN): 0x109 Behavior_State). Enum:
    // 0 LANE_FOLLOW, 1 OBSTACLE_STOP, 2 NUDGE, 3 OVERTAKE, 4 RESUME.
    void applyBehaviorState(int mode) {
        m_behaviorFromCan = true;
        static const char *names[] = { "LANE_FOLLOW","OBSTACLE_STOP","NUDGE","OVERTAKE","RESUME" };
        m_behaviorState = (mode >= 0 && mode < 5) ? names[mode] : "LANE_FOLLOW";
        emit behaviorChanged();
    }

    // B3 — lane-center deviation. TODO(CAN): real Lane_Center_Deviation signal;
    // until then the getter falls back to pathDeviationMm as a marked stand-in.
    bool   laneDevFromCan() const        { return m_laneDevFromCan; }
    double laneCenterDeviationMm() const { return m_laneDevFromCan ? m_laneCenterDeviationMm
                                                                   : m_pathDeviationMm; }
    void   applyLaneCenterDeviation(double mm) {
        m_laneDevFromCan = true; m_laneCenterDeviationMm = mm; emit kpiChanged();
    }

    bool   hasMapInfo() const       { return m_hasMapInfo; }
    double mapInfoOriginX() const   { return m_mapInfoOriginX; }
    double mapInfoOriginY() const   { return m_mapInfoOriginY; }
    double mapInfoResolution() const{ return m_mapInfoResolution; }
    int    mapInfoWidth() const     { return m_mapInfoWidth; }
    int    mapInfoHeight() const    { return m_mapInfoHeight; }
    int    mapInfoVersion() const   { return m_mapInfoVersion; }
    void   applyMapInfo(double ox, double oy, double res, int w, int h, int ver) {
        m_hasMapInfo = true;
        m_mapInfoOriginX = ox; m_mapInfoOriginY = oy; m_mapInfoResolution = res;
        m_mapInfoWidth = w; m_mapInfoHeight = h; m_mapInfoVersion = ver;
        emit mapInfoChanged();
    }

    // ── Goal / replay (UI state) ─────────────────────────────────────────
    bool   goalActive() const { return m_goalActive; }
    double goalDistM() const  { return m_goalDistM; }
    double goalLatM() const   { return m_goalLatM; }
    double goalYawDeg() const { return m_goalYawDeg; }
    bool   replaying() const  { return m_replaying; }
    Q_INVOKABLE void setGoal(double distM, double latM, double yawDeg) {
        m_goalActive = true; m_goalDistM = distM; m_goalLatM = latM; m_goalYawDeg = yawDeg;
        emit goalChanged();
    }
    // Clear the active-goal marker (paired with CanBridge::sendCancelGoal, which
    // tells the vehicle to abort → IDLE). Lets the operator pick a new goal
    // without restarting the app.
    Q_INVOKABLE void clearGoal() {
        m_goalActive = false; m_goalDistM = 0; m_goalLatM = 0; m_goalYawDeg = 0;
        emit goalChanged();
    }
    // Connected to CanBridge::replayingChanged (queued, worker→main).
    void setReplaying(bool r) {
        if (m_replaying == r) return;
        m_replaying = r; emit replayingChanged();
    }

    // ── Getters ──────────────────────────────────────────────────────────
    double inferenceMs() const     { return m_inferenceMs; }
    double gpuPct() const          { return m_gpuPct; }
    double cpuPct() const          { return m_cpuPct; }
    double gpuTempC() const        { return m_gpuTempC; }
    double pathDeviationMm() const { return m_pathDeviationMm; }
    double detectLatencyMs() const { return m_detectLatencyMs; }
    bool   hasRealtimeKpi() const  { return m_hasRealtimeKpi; }

    double speedKmh() const        { return m_speedKmh; }
    double steeringDeg() const     { return m_steeringDeg; }
    QString driveState() const     { return m_driveState; }
    bool   controlEnable() const   { return m_controlEnable; }
    bool   hasVehicle() const      { return m_hasVehicle; }

    double obstacleDistM() const   { return m_obstacleDistM; }
    double obstacleAngleDeg() const{ return m_obstacleAngleDeg; }
    double obstacleConf() const    { return m_obstacleConf; }
    QString obstacleClass() const  { return m_obstacleClass; }
    bool   hasObstacle() const     { return m_hasObstacle; }
    bool   obstacleDetected() const { return m_hasObstacle && m_obstacleConf > 0.0; }

    int    failsafeLevel() const    { return m_failsafeLevel; }
    bool   hasFailsafeEvent() const { return m_hasFailsafeEvent; }
    QString optMode() const        { return m_optMode; }
    QString yoloModel() const      { return m_yoloModel; }

    // 0x103-derived
    double ramTotalGb() const  { return m_hasHardware ? m_orinMemoryGb : m_ramTotalGbFallback; }
    double ramUsedGb() const   { return m_ramUsagePct / 100.0 * ramTotalGb(); }
    double swapTotalGb() const { return m_swapTotalGb; }
    double swapUsedGb() const  { return m_swapUsagePct / 100.0 * m_swapTotalGb; }
    bool   hasMemory() const   { return m_hasSysResource; }
    int    sessionRateHz() const { return m_sessionRateHz; }
    bool   hasSession() const  { return m_hasSysResource; }
    double busLoad() const     { return m_busLoad; }
    double frameLossPct() const{ return m_frameLossPct; }
    int    pi5StatusCode() const { return m_pi5StatusCode; }
    QString pi5StatusText() const;
    bool   pi5Online() const   { return m_pi5StatusCode == 1; }   // 1 = Online
    int    cameraStatus() const{ return m_cameraStatus; }
    int    cameraOnline() const;                                  // popcount of bitflag
    bool   hasPi5() const      { return m_hasSysResource; }

    // 0x104-derived
    double progressM() const         { return m_progressM; }
    double amclErrorM() const        { return m_amclErrorM; }
    double missionSuccessPct() const { return m_missionSuccessPct; }
    int    missionSuccess() const;                                // round(pct/100 * total)
    int    missionTotal() const      { return m_missionTotal; }
    int    routeState() const        { return m_routeState; }
    bool   hasMission() const        { return m_hasRoute; }
    bool   hasLocalization() const   { return m_hasRoute; }

    int    successRuns() const       { return m_successRuns; }
    int    totalRuns() const         { return m_totalRuns; }
    double localSuccessRatePct() const {
        return m_totalRuns > 0 ? 100.0 * m_successRuns / m_totalRuns : 0.0;
    }

    double pathPlanLastMs() const         { return m_pathPlanLastMs; }
    int    pathPlanSuccessRuns() const    { return m_pathPlanSuccessRuns; }
    int    pathPlanTotalRuns() const      { return m_pathPlanTotalRuns; }
    double pathPlanSuccessRatePct() const {
        return m_pathPlanTotalRuns > 0
               ? 100.0 * m_pathPlanSuccessRuns / m_pathPlanTotalRuns
               : 0.0;
    }
    bool   hasPathPlan() const            { return m_hasPathPlan; }
    int    planningState() const          { return m_planningState; }

    int    perceptionDetectedRuns() const  { return m_perceptionDetectedRuns; }
    int    perceptionTotalRuns() const     { return m_perceptionTotalRuns; }
    double perceptionDetectRatePct() const {
        // Prefer the perception-own total (0x107). Fall back to the local
        // mission counter (m_totalRuns) when no CAN total has arrived.
        const int denom = m_perceptionTotalRuns > 0 ? m_perceptionTotalRuns : m_totalRuns;
        return denom > 0 ? 100.0 * m_perceptionDetectedRuns / denom : 0.0;
    }
    int    falsePositiveCount() const      { return m_falsePositiveCount; }
    double triggerAccuracyPct() const      { return m_triggerAccuracyPct; }

    double failsafeLastTransitionMs() const { return m_failsafeLastTransitionMs; }
    double failsafeMaxTransitionMs() const  { return m_failsafeMaxTransitionMs; }

    bool   lidarOk() const           { return m_lidarOk; }
    qint64 lidarSilentMs() const {
        return m_lastObstacleRxTimer.isValid() ? m_lastObstacleRxTimer.elapsed() : 0;
    }
    bool   imuOk() const             { return m_imuOk; }
    qint64 imuSilentMs() const {
        return m_lastImuRxTimer.isValid() ? m_lastImuRxTimer.elapsed() : 0;
    }
    bool   encoderOk() const         { return m_encoderOk; }
    qint64 encoderSilentMs() const {
        return m_lastEncRxTimer.isValid() ? m_lastEncRxTimer.elapsed() : 0;
    }
    bool   hasEncoder() const        { return m_hasEncoder; }
    int    encLeftCount() const      { return m_encLeft; }
    int    encRightCount() const     { return m_encRight; }
    bool   egoOk() const             { return m_egoOk; }
    qint64 egoSilentMs() const {
        return m_lastEgoRxTimer.isValid() ? m_lastEgoRxTimer.elapsed() : 0;
    }

    // 0x105-derived
    int    jetsonModelCode() const   { return m_jetsonModelCode; }
    QString jetsonModelName() const;
    double orinMemoryGb() const      { return m_orinMemoryGb; }
    bool   hasHardware() const       { return m_hasHardware; }

    // bus stats
    double canTxLatencyMs() const { return m_canTxLatencyMs; }
    int    framesRx() const       { return m_framesRx; }
    int    framesTx() const       { return m_framesTx; }
    qint64 uptimeMs() const       { return m_uptimeMs; }
    bool   canOnline() const      { return m_canOnline; }
    int    canBitrate() const     { return m_canBitrate; }

    double accFp32() const        { return m_accFp32; }
    double accFp16() const        { return m_accFp16; }
    double accInt8() const        { return m_accInt8; }
    bool   hasAccuracy() const    { return m_hasAccuracy; }

    double wifiPingMs() const        { return m_wifiPingMs; }
    bool   hasNetwork() const        { return m_hasNetwork; }
    double networkLossRatePct() const { return m_networkLossRatePct; }
    int    wifiRssiDbm() const       { return m_wifiRssiDbm; }
    int    networkState() const      { return m_networkState; }

    bool   hasGps() const         { return m_hasGps; }
    bool   gpsHasFix() const      { return m_gpsHasFix; }
    int    gpsSatCount() const    { return m_gpsSatCount; }
    double gpsLatDeg() const      { return m_gpsLatDeg; }
    double gpsLonDeg() const      { return m_gpsLonDeg; }
    double gpsAltitudeM() const   { return m_gpsAltitudeM; }
    double gpsSpeedKmh() const    { return m_gpsSpeedKmh; }
    double gpsHeadingDeg() const  { return m_gpsHeadingDeg; }

    bool   hasImu() const         { return m_hasImu; }
    int    imuCalSys() const      { return m_imuCalSys; }
    int    imuCalGyro() const     { return m_imuCalGyro; }
    int    imuCalAccel() const    { return m_imuCalAccel; }
    int    imuCalMag() const      { return m_imuCalMag; }
    double imuHeadingDeg() const  { return m_imuHeadingDeg; }
    double imuRollDeg() const     { return m_imuRollDeg; }
    double imuPitchDeg() const    { return m_imuPitchDeg; }

    // ── WRITE setters (QML buttons — optimistic local update) ────────────
    void setOptMode(const QString &v) {
        if (m_optMode == v) return;
        m_optMode = v; emit optModeChanged();
    }
    void setYoloModel(const QString &v) {
        if (m_yoloModel == v) return;
        m_yoloModel = v; emit yoloModelChanged();
    }
    void setCanBitrate(int v) { if (m_canBitrate==v) return; m_canBitrate = v; emit busStatsChanged(); }

    // ── Config injection (main.cpp, once at startup) ─────────────────────
    void setRamTotalGbFallback(double v) { m_ramTotalGbFallback = v; }
    void setSwapTotalGb(double v)        { m_swapTotalGb = v; }
    void setMissionTotal(int v)          { m_missionTotal = v; }
    void setSensorStaleMs(qint64 lidar, qint64 imu, qint64 enc, qint64 ego = 700) {
        m_lidarStaleMs = lidar; m_imuStaleMs = imu; m_encStaleMs = enc; m_egoStaleMs = ego;
    }
    void setFailsafeDwellMs(qint64 ms) { m_failsafeDwellMs = ms; }

    // ── Slots invoked by CanBridge ───────────────────────────────────────
    void applyRealtimeKpi(double inferenceMs, double gpuPct, double cpuPct,
                          double gpuTempC, double pathErrorMm);
    void applyVehicleStatus(double speedKmh, double steeringDeg,
                            const QString &drivingStateName,
                            const QString &optMode, const QString &yoloModel,
                            bool controlEnable);
    void applyObstacle(double distM, double angleDeg, double confidence01,
                       const QString &className, int failsafeLevel);
    void applySystemResource(double ramUsagePct, double swapUsagePct,
                             int sessionRateHz, double busLoadPct,
                             double canLossPct, int pi5StatusCode,
                             int cameraStatus);
    void applyRouteStatus(double routeProgressM, double amclErrorM,
                          double missionSuccessPct, int routeState);
    void applyHardwareInfo(int jetsonModelCode, double orinMemoryGb);
    void applyFailsafeEvent(quint8 eventCode, quint8 reasonCode, quint8 level);
    void applyBusStats(double txLatencyMs, int framesRx, int framesTx,
                       qint64 uptimeMs);
    void applyAccuracy(double fp32mAP, double fp16mAP, double int8mAP);

    // External-sensor pushes — called by UI dispatcher after polling each
    // bridge. Same pattern as the CAN applyXxx slots.
    void applyGps(bool hasFix, int satCount, double latDeg, double lonDeg,
                  double altitudeM, double speedKmh, double headingDeg);
    void applyImu(int calSys, int calGyro, int calAccel, int calMag,
                  double headingDeg, double rollDeg, double pitchDeg);
    // Chassis IMU over CAN (0x21 STM32_IMU_Feedback). No BNO055 calibration
    // bytes — feeds heading/roll/pitch and the same freshness watchdog.
    void applyImuFromCan(double yawDeg, double gyroZDps, double rollDeg, double pitchDeg);
    // Drivetrain encoder over CAN (0x20 STM32_Encoder_Feedback) — tick counts +
    // freshness for the drivetrain "online" indicator.
    void applyEncoder(int leftCount, int rightCount);
    // 0x106 Planning_Status — CAN-authoritative; overwrites the locally-derived
    // path-plan stats (drive-state fallback) when firmware sends this frame.
    void applyPlanningStatus(double lastMs, int successRuns, int totalRuns, int state);
    // 0x107 Perception_Validation — detection runs, false positives, trigger acc.
    void applyPerceptionValidation(int detectedRuns, int totalRuns,
                                   int falsePositives, double triggerAccuracyPct);
    // 0x109 Network_Status — wifi link health.
    void applyNetworkStatus(double wifiPingMs, double lossRatePct, int rssiDbm, int state);

    void pushLogEvent(const QString &code, const QString &msg,
                      const QString &severity, const QString &src = QString());

signals:
    void kpiChanged();
    void sysResourceChanged();
    void routeChanged();
    void hardwareChanged();
    void busStatsChanged();
    void optModeChanged();
    void yoloModelChanged();
    void networkChanged();
    void accuracyChanged();
    void runsChanged();
    void gpsChanged();
    void imuChanged();
    void encoderChanged();
    void pathPlanChanged();
    void perceptionStatsChanged();
    void sensorHealthChanged();
    void goalChanged();
    void replayingChanged();
    void mapInfoChanged();
    void egoPoseChanged();
    void datumChanged();
    void localizationChanged();
    void behaviorChanged();
    void logEvent(QString time, QString code, QString msg,
                  QString severity, QString src);

private:
    // UI command/replay state
    bool   m_goalActive = false;
    double m_goalDistM = 0, m_goalLatM = 0, m_goalYawDeg = 0;
    bool   m_replaying = false;

    // Map_Info (0x106)
    bool   m_hasMapInfo = false;
    double m_mapInfoOriginX = 0, m_mapInfoOriginY = 0, m_mapInfoResolution = 0;
    int    m_mapInfoWidth = 0, m_mapInfoHeight = 0, m_mapInfoVersion = 0;

    // Ego_Pose (0x107)
    bool   m_hasEgoPose = false;
    double m_egoX = 0, m_egoY = 0, m_egoYaw = 0;

    // Map_Datum (0x108)
    bool   m_hasDatum = false;
    double m_datumLat = 0, m_datumLon = 0;
    bool   m_hasRefDatum = false;            // expected datum (config) for sanity-checking live ones
    double m_refDatumLat = 0, m_refDatumLon = 0;

    // A7 — AMCL error binned by feature/gap zone

    // B1 — localization mode/quality (derived until 0x108 exists)
    QString m_locMode = "INIT";
    double  m_locQuality = 0.0;
    bool    m_locFromCan = false;
    void    recomputeLocalization();

    // B2 — behavior state (derived until 0x109 exists)
    QString m_behaviorState = "LANE_FOLLOW";
    bool    m_behaviorFromCan = false;
    void    recomputeBehavior();

    // B3 — lane-center deviation (placeholder uses pathDeviationMm)
    double  m_laneCenterDeviationMm = 0.0;
    bool    m_laneDevFromCan = false;

    // 0x102
    double m_inferenceMs = 0;
    double m_gpuPct = 0;
    double m_cpuPct = 0;
    double m_gpuTempC = 0;
    double m_pathDeviationMm = 0;
    double m_detectLatencyMs = 0;
    bool   m_hasRealtimeKpi = false;

    // 0x101
    double  m_speedKmh = 0;
    double  m_steeringDeg = 0;
    QString m_driveState = "";
    bool    m_controlEnable = false;
    bool    m_hasVehicle = false;
    QString m_optMode = "INT8";
    QString m_yoloModel = "YOLO26s";

    // 0x100
    double  m_obstacleDistM = 0;
    double  m_obstacleAngleDeg = 0;
    double  m_obstacleConf = 0;
    QString m_obstacleClass = "";
    bool    m_hasObstacle = false;
    int     m_failsafeLevel = 1;
    bool    m_hasFailsafeEvent = false;

    // 0x103
    double m_ramUsagePct = 0;
    double m_swapUsagePct = 0;
    int    m_sessionRateHz = 0;
    double m_busLoad = 0;
    double m_frameLossPct = 0;
    int    m_pi5StatusCode = 0;     // 0 No_Data, 1 Online, 2 Timeout, 3 Error
    int    m_cameraStatus = 0;      // bitflag
    bool   m_hasSysResource = false;

    // 0x104
    double m_progressM = 0;
    double m_amclErrorM = 0;
    double m_missionSuccessPct = 0;
    int    m_routeState = 0;
    bool   m_hasRoute = false;

    // 0x105
    int    m_jetsonModelCode = 0;   // 0 Unknown, 1 Orin
    double m_orinMemoryGb = 0;
    bool   m_hasHardware = false;

    // Bus stats
    double m_canTxLatencyMs = 0;
    int    m_framesRx = 0;
    int    m_framesTx = 0;
    qint64 m_uptimeMs = 0;
    bool   m_canOnline = false;
    int    m_canBitrate = 0;

    // Accuracy (config.json)
    double m_accFp32 = 0;
    double m_accFp16 = 0;
    double m_accInt8 = 0;
    bool   m_hasAccuracy = false;

    // 0x109 Network_Status
    double m_wifiPingMs = 0;
    bool   m_hasNetwork = false;
    double m_networkLossRatePct = 0;
    int    m_wifiRssiDbm = 0;
    int    m_networkState = 0;

    // Config-injected fallbacks
    double m_ramTotalGbFallback = 8;
    double m_swapTotalGb = 4;
    int    m_missionTotal = 10;

    // Edge-trigger mission counters — only incremented when drive state
    // transitions out of AUTO. Cross-validates Jetson's Mission_Success%.
    QString m_prevDriveStateName;
    int     m_successRuns = 0;
    int     m_totalRuns = 0;

    // Latest Fail_Safe_Event reason (for the disengagement log, A4).
    quint8  m_lastReasonCode = 0;
    QString m_lastReasonLabel;

    // External sensors (M10 GPS + BNO055 IMU) — pushed by their bridges
    // through the same UI-poll dispatcher used for CAN.
    bool    m_hasGps = false;
    bool    m_gpsHasFix = false;
    int     m_gpsSatCount = 0;
    double  m_gpsLatDeg = 0, m_gpsLonDeg = 0, m_gpsAltitudeM = 0;
    double  m_gpsSpeedKmh = 0, m_gpsHeadingDeg = 0;

    bool    m_hasImu = false;
    int     m_imuCalSys = 0, m_imuCalGyro = 0, m_imuCalAccel = 0, m_imuCalMag = 0;
    double  m_imuHeadingDeg = 0, m_imuRollDeg = 0, m_imuPitchDeg = 0;

    // ── §4.2 KPI tracking ────────────────────────────────────────────────
    // Path-planning timing — measured as the gap between a non-AUTO drive
    // state and the next AUTO. Resets/captures on drive-state transitions
    // in applyVehicleStatus(). True source-of-truth should be a future
    // 0x106 Planning_Status message from Jetson.
    bool          m_hasPathPlan = false;
    double        m_pathPlanLastMs = 0;
    int           m_pathPlanSuccessRuns = 0;
    int           m_pathPlanTotalRuns = 0;
    int           m_planningState = 0;        // 0x106 Planning_State [0..3]
    QElapsedTimer m_pathPlanTimer;
    bool          m_pathPlanInProgress = false;

    // Perception cumulative — one bit per run, OR'd in applyObstacle when
    // confidence > 0.5 while AUTO; flushed at AUTO→STOP/ERROR transitions.
    // m_perceptionTotalRuns is the 0x107-own total (kept separate from the
    // localization mission counter m_totalRuns to avoid corrupting it).
    int           m_perceptionDetectedRuns = 0;
    int           m_perceptionTotalRuns = 0;
    bool          m_perceptDuringRun = false;
    int           m_falsePositiveCount = 0;   // 0x107 False_Positive_Count (was reserved)
    double        m_triggerAccuracyPct = 0;   // 0x107 Trigger_Accuracy

    // Fail-safe transition latency — trigger condition (overload met)
    // → failsafeLevel actually changes. Measured locally via QElapsedTimer.
    double        m_failsafeLastTransitionMs = 0;
    double        m_failsafeMaxTransitionMs = 0;
    QElapsedTimer m_failsafeTriggerTimer;
    bool          m_failsafeTriggerActive = false;
    // Displayed-level debounce (anti-flicker). De-escalation must hold the dwell.
    int           m_pendingFailsafeLevel = 1;
    QElapsedTimer m_failsafePendingSince;
    qint64        m_failsafeDwellMs = 1500;   // config: targets.failsafe_dwell_ms
    void proposeFailsafeLevel(int level, bool &changed);
    void commitFailsafeLevel(int level, bool &changed);

    // Sensor health watchdog — LiDAR freshness from 0x100 heartbeat,
    // IMU freshness from I2cImuBridge applyImu() calls. A QTimer ticks
    // every 500 ms to flip the boolean health flags + notify QML.
    QTimer        m_sensorWatchdog;
    QElapsedTimer m_lastObstacleRxTimer;
    QElapsedTimer m_lastImuRxTimer;
    QElapsedTimer m_lastEncRxTimer;
    QElapsedTimer m_lastEgoRxTimer;
    bool          m_lidarOk = false;
    bool          m_imuOk = false;
    bool          m_encoderOk = false;
    bool          m_egoOk = false;
    bool          m_hasEncoder = false;
    int           m_encLeft = 0, m_encRight = 0;
    // Sensor stale windows (ms). Config-injected from main.cpp (config.hardware);
    // defaults match the historical constants. Raise if firmware publishes the
    // sensor's heartbeat frame less often than expected.
    qint64 m_lidarStaleMs = 1000;   // without 0x100 = LiDAR stale
    qint64 m_imuStaleMs   = 500;    // without 0x21
    qint64 m_encStaleMs   = 500;    // without 0x20
    qint64 m_egoStaleMs   = 700;    // without 0x10D (100ms cycle) = ego pose stale/frozen
};
