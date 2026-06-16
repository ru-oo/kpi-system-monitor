#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// Config — static project configuration loaded once at startup from
// config.json. Values that come from offline YOLO benchmarks (mAP per
// precision, baseline inference latency) and operational targets/labels
// (thresholds, route length, team name) live here. Runtime sensor data
// stays on CAN.
//
// Path resolution order:
//   1) env var KPI_CONFIG                (explicit override)
//   2) <executable dir>/config.json      (deployed alongside binary)
//   3) <source dir>/config.json          (Qt Creator dev convenience)
// If none found, compiled-in defaults equal to the JSX demo are used.
class Config : public QObject {
    Q_OBJECT

    // accuracy.*  — YOLO project benchmark per model & precision (map50 / map50-95).
    // The single accFp32/16/Int8 getters return the DEFAULT model's map50-95
    // (back-compat); use accMap5095()/accMap50() for a specific model.
    Q_PROPERTY(double accFp32 READ accFp32 CONSTANT)
    Q_PROPERTY(double accFp16 READ accFp16 CONSTANT)
    Q_PROPERTY(double accInt8 READ accInt8 CONSTANT)

    // model.*
    Q_PROPERTY(double  ptBaselineMs       READ ptBaselineMs       CONSTANT)
    Q_PROPERTY(QString defaultOptMode     READ defaultOptMode     CONSTANT)
    Q_PROPERTY(QString defaultYoloModel   READ defaultYoloModel   CONSTANT)
    Q_PROPERTY(int     perceptionHz       READ perceptionHz       CONSTANT)
    Q_PROPERTY(int     realtimeKpiHz      READ realtimeKpiHz      CONSTANT)
    Q_PROPERTY(int     vehicleStatusHz    READ vehicleStatusHz    CONSTANT)
    Q_PROPERTY(double  fp16SpeedupRef     READ fp16SpeedupRef     CONSTANT)

    // targets.*
    Q_PROPERTY(double targetInferenceMs       READ targetInferenceMs       CONSTANT)
    Q_PROPERTY(double targetSpeedupRatio      READ targetSpeedupRatio      CONSTANT)
    Q_PROPERTY(double targetMapLossPct        READ targetMapLossPct        CONSTANT)
    Q_PROPERTY(double targetPathDeviationMm   READ targetPathDeviationMm   CONSTANT)
    Q_PROPERTY(double targetCanTxLatencyMs    READ targetCanTxLatencyMs    CONSTANT)
    Q_PROPERTY(double targetDetectLatencyMs   READ targetDetectLatencyMs   CONSTANT)
    Q_PROPERTY(int    targetMissionSuccessPct READ targetMissionSuccessPct CONSTANT)
    Q_PROPERTY(double targetSpeedKmhMin       READ targetSpeedKmhMin       CONSTANT)
    Q_PROPERTY(double targetSpeedKmhMax       READ targetSpeedKmhMax       CONSTANT)
    Q_PROPERTY(double routeLengthM            READ routeLengthM            CONSTANT)

    // ── Tactical (SLAM-map page) ──────────────────────────────────────────
    Q_PROPERTY(QString tacticalMapPath        READ tacticalMapPath         CONSTANT)
    Q_PROPERTY(QString tacticalLanePath       READ tacticalLanePath        CONSTANT)
    Q_PROPERTY(int     tacticalMapVersion     READ tacticalMapVersion      CONSTANT)
    Q_PROPERTY(QString campaign               READ campaign                CONSTANT)
    Q_PROPERTY(double  routeStartX            READ routeStartX             CONSTANT)
    Q_PROPERTY(double  routeStartY            READ routeStartY             CONSTANT)
    Q_PROPERTY(double  routeHeadingDeg        READ routeHeadingDeg         CONSTANT)

    // ── Additional §4.2 KPI targets (report row-for-row) ──────────────────
    Q_PROPERTY(int    localizationSuccessRunsMin READ localizationSuccessRunsMin CONSTANT)
    Q_PROPERTY(int    pathPlanMsMax              READ pathPlanMsMax              CONSTANT)
    Q_PROPERTY(int    pathPlanSuccessRunsMin     READ pathPlanSuccessRunsMin     CONSTANT)
    Q_PROPERTY(int    parkingDetectRunsMin       READ parkingDetectRunsMin       CONSTANT)
    Q_PROPERTY(int    falsePositivePerRunMax     READ falsePositivePerRunMax     CONSTANT)
    Q_PROPERTY(int    triggerAccuracyPctMin      READ triggerAccuracyPctMin      CONSTANT)
    Q_PROPERTY(int    fsTransitionMsMax          READ fsTransitionMsMax          CONSTANT)
    Q_PROPERTY(double canLossPctMax              READ canLossPctMax              CONSTANT)
    Q_PROPERTY(int    runsTotal                  READ runsTotal                  CONSTANT)
    Q_PROPERTY(double perceptionConfidenceMin    READ perceptionConfidenceMin    CONSTANT)
    Q_PROPERTY(int    pathPlanChartMaxMs         READ pathPlanChartMaxMs         CONSTANT)
    Q_PROPERTY(int    pathDeviationChartMaxMm    READ pathDeviationChartMaxMm    CONSTANT)
    Q_PROPERTY(int    failsafeDwellMs            READ failsafeDwellMs            CONSTANT)
    Q_PROPERTY(double laneCenterDevMmMax         READ laneCenterDevMmMax         CONSTANT)

    // ── Tactical map view (mismatch tolerances, zoom bounds, first-load focus) ──
    Q_PROPERTY(double mapOriginTolM       READ mapOriginTolM       CONSTANT)
    Q_PROPERTY(double mapResolutionTolM   READ mapResolutionTolM   CONSTANT)
    Q_PROPERTY(double mapTextureLimitPx   READ mapTextureLimitPx   CONSTANT)
    Q_PROPERTY(double mapZoomMin          READ mapZoomMin          CONSTANT)
    Q_PROPERTY(double mapZoomMax          READ mapZoomMax          CONSTANT)
    Q_PROPERTY(double mapFocusSpanM       READ mapFocusSpanM       CONSTANT)

    // ── Sensor stale timeouts (ms) ─────────────────────────────────────────
    Q_PROPERTY(int    sensorStaleLidarMs  READ sensorStaleLidarMs  CONSTANT)
    Q_PROPERTY(int    sensorStaleImuMs    READ sensorStaleImuMs    CONSTANT)
    Q_PROPERTY(int    sensorStaleEncMs    READ sensorStaleEncMs    CONSTANT)
    Q_PROPERTY(int    sensorStaleEgoMs    READ sensorStaleEgoMs    CONSTANT)

    // can.*
    Q_PROPERTY(int canDefaultBitrate READ canDefaultBitrate CONSTANT)
    Q_PROPERTY(int canTerminationOhm READ canTerminationOhm CONSTANT)
    // Optional fixed adapter so a bare double-click picks up the CANable without
    // env vars: plugin "slcan"/"serialcan" + device "COM3" (or a QCanBus plugin).
    Q_PROPERTY(QString canPlugin READ canPlugin CONSTANT)
    Q_PROPERTY(QString canDevice READ canDevice CONSTANT)

    // link.*  — split bridge/client UDP deployment
    Q_PROPERTY(QString linkMode        READ linkMode        CONSTANT)
    Q_PROPERTY(QString linkBridgeHost  READ linkBridgeHost  CONSTANT)
    Q_PROPERTY(int     linkSnapshotPort READ linkSnapshotPort CONSTANT)
    Q_PROPERTY(int     linkCommandPort READ linkCommandPort CONSTANT)
    Q_PROPERTY(int     linkSnapshotHz  READ linkSnapshotHz  CONSTANT)

    // ── Jetson debug console (UDP) ──────────────────────────────────────────
    Q_PROPERTY(QString debugHost      READ debugHost      CONSTANT)
    Q_PROPERTY(int     debugPort      READ debugPort      CONSTANT)
    Q_PROPERTY(int     debugLocalPort READ debugLocalPort CONSTANT)

    // hardware.*  — fallbacks for values not always on CAN
    Q_PROPERTY(double ramTotalGbFallback READ ramTotalGbFallback CONSTANT)
    Q_PROPERTY(double swapTotalGb        READ swapTotalGb        CONSTANT)
    Q_PROPERTY(int    missionTotal       READ missionTotal       CONSTANT)

    // ui.* — runtime tunables
    Q_PROPERTY(int uiPollIntervalMs READ uiPollIntervalMs CONSTANT)

    // ui.*
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString modelFamily   READ modelFamily   CONSTANT)
    Q_PROPERTY(QString teamLabel     READ teamLabel     CONSTANT)

    // Where the file was loaded from (for diagnostic display); empty if defaults.
    Q_PROPERTY(QString sourcePath READ sourcePath CONSTANT)

public:
    explicit Config(QObject *parent = nullptr);

    // Tries the path resolution order described above. Returns true if a
    // file was found and parsed; false means compiled-in defaults are used.
    bool loadFromDisk();

    // Direct load from explicit path (used by tests or when KPI_CONFIG set).
    bool loadFromFile(const QString &absolutePath);

    double  accFp32() const                  { return m_accFp32; }
    double  accFp16() const                  { return m_accFp16; }
    double  accInt8() const                  { return m_accInt8; }
    // model: "YOLO26s"/"YOLO26n" (matched by "26n" substring, else 26s).
    // prec: 0 FP32, 1 FP16, 2 INT8. Returns that model's benchmark mAP (%).
    Q_INVOKABLE double accMap5095(const QString &model, int prec) const {
        if (prec < 0 || prec > 2) return 0.0;
        return model.contains("26n", Qt::CaseInsensitive) ? m_accN5095[prec] : m_accS5095[prec];
    }
    Q_INVOKABLE double accMap50(const QString &model, int prec) const {
        if (prec < 0 || prec > 2) return 0.0;
        return model.contains("26n", Qt::CaseInsensitive) ? m_accN50[prec] : m_accS50[prec];
    }

    double  ptBaselineMs() const             { return m_ptBaselineMs; }
    QString defaultOptMode() const           { return m_defaultOptMode; }
    QString defaultYoloModel() const         { return m_defaultYoloModel; }
    int     perceptionHz() const             { return m_perceptionHz; }
    int     realtimeKpiHz() const            { return m_realtimeKpiHz; }
    int     vehicleStatusHz() const          { return m_vehicleStatusHz; }
    double  fp16SpeedupRef() const           { return m_fp16SpeedupRef; }

    double  targetInferenceMs() const        { return m_targetInferenceMs; }
    double  targetSpeedupRatio() const       { return m_targetSpeedupRatio; }
    double  targetMapLossPct() const         { return m_targetMapLossPct; }
    double  targetPathDeviationMm() const    { return m_targetPathDeviationMm; }
    double  targetCanTxLatencyMs() const     { return m_targetCanTxLatencyMs; }
    double  targetDetectLatencyMs() const    { return m_targetDetectLatencyMs; }
    int     targetMissionSuccessPct() const  { return m_targetMissionSuccessPct; }
    double  targetSpeedKmhMin() const        { return m_targetSpeedKmhMin; }
    double  targetSpeedKmhMax() const        { return m_targetSpeedKmhMax; }
    double  routeLengthM() const             { return m_routeLengthM; }
    QString tacticalMapPath() const          { return m_tacticalMapPath; }
    QString tacticalLanePath() const         { return m_tacticalLanePath; }
    int     tacticalMapVersion() const       { return m_tacticalMapVersion; }
    QString campaign() const                 { return m_campaign; }
    double  datumLat() const                 { return m_datumLat; }
    double  datumLon() const                 { return m_datumLon; }
    double  routeStartX() const              { return m_routeStartX; }
    double  routeStartY() const              { return m_routeStartY; }
    double  routeHeadingDeg() const          { return m_routeHeadingDeg; }

    int     localizationSuccessRunsMin() const { return m_locSuccessRunsMin; }
    int     pathPlanMsMax() const              { return m_pathPlanMsMax; }
    int     pathPlanSuccessRunsMin() const     { return m_pathPlanSuccessRunsMin; }
    int     parkingDetectRunsMin() const       { return m_parkingDetectRunsMin; }
    int     falsePositivePerRunMax() const     { return m_falsePositivePerRunMax; }
    int     triggerAccuracyPctMin() const      { return m_triggerAccuracyPctMin; }
    int     fsTransitionMsMax() const          { return m_fsTransitionMsMax; }
    double  canLossPctMax() const              { return m_canLossPctMax; }
    int     runsTotal() const                  { return m_runsTotal; }
    double  perceptionConfidenceMin() const    { return m_perceptionConfidenceMin; }
    int     pathPlanChartMaxMs() const         { return m_pathPlanChartMaxMs; }
    int     pathDeviationChartMaxMm() const    { return m_pathDeviationChartMaxMm; }
    int     failsafeDwellMs() const            { return m_failsafeDwellMs; }
    double  laneCenterDevMmMax() const         { return m_laneCenterDevMmMax; }

    double  mapOriginTolM() const              { return m_mapOriginTolM; }
    double  mapResolutionTolM() const          { return m_mapResolutionTolM; }
    double  mapTextureLimitPx() const          { return m_mapTextureLimitPx; }
    double  mapZoomMin() const                 { return m_mapZoomMin; }
    double  mapZoomMax() const                 { return m_mapZoomMax; }
    double  mapFocusSpanM() const              { return m_mapFocusSpanM; }

    int     sensorStaleLidarMs() const         { return m_sensorStaleLidarMs; }
    int     sensorStaleImuMs() const           { return m_sensorStaleImuMs; }
    int     sensorStaleEncMs() const           { return m_sensorStaleEncMs; }
    int     sensorStaleEgoMs() const           { return m_sensorStaleEgoMs; }

    int     canDefaultBitrate() const        { return m_canDefaultBitrate; }
    int     canTerminationOhm() const        { return m_canTerminationOhm; }
    QString canPlugin() const                { return m_canPlugin; }
    QString canDevice() const                { return m_canDevice; }

    QString linkMode() const                 { return m_linkMode; }
    QString linkBridgeHost() const           { return m_linkBridgeHost; }
    int     linkSnapshotPort() const         { return m_linkSnapshotPort; }
    int     linkCommandPort() const          { return m_linkCommandPort; }
    QString debugHost() const                { return m_debugHost; }
    int     debugPort() const                { return m_debugPort; }
    int     debugLocalPort() const           { return m_debugLocalPort; }
    int     linkSnapshotHz() const           { return m_linkSnapshotHz; }

    double  ramTotalGbFallback() const       { return m_ramTotalGbFallback; }
    double  swapTotalGb() const              { return m_swapTotalGb; }
    int     missionTotal() const             { return m_missionTotal; }

    int     uiPollIntervalMs() const         { return m_uiPollIntervalMs; }

    QString platformLabel() const            { return m_platformLabel; }
    QString modelFamily() const              { return m_modelFamily; }
    QString teamLabel() const                { return m_teamLabel; }

    QString sourcePath() const               { return m_sourcePath; }

private:
    // Defaults match config.json so the app still runs without it. Per model,
    // index 0 FP32 / 1 FP16 / 2 INT8.
    double  m_accS5095[3] = { 40.91, 40.89, 30.04 };   // YOLO26s map50-95
    double  m_accN5095[3] = { 37.64, 37.57, 29.54 };   // YOLO26n map50-95
    double  m_accS50[3]   = { 62.33, 62.35, 52.79 };   // YOLO26s map50
    double  m_accN50[3]   = { 59.22, 59.16, 52.73 };   // YOLO26n map50
    // Back-compat single getters = DEFAULT model's map50-95 (set in finalize).
    double  m_accFp32 = 40.91;
    double  m_accFp16 = 40.89;
    double  m_accInt8 = 30.04;

    double  m_ptBaselineMs = 68.0;
    QString m_defaultOptMode = "INT8";
    QString m_defaultYoloModel = "YOLO26s";
    int     m_perceptionHz = 10;
    int     m_realtimeKpiHz = 10;
    int     m_vehicleStatusHz = 50;
    double  m_fp16SpeedupRef = 2.2;

    double  m_targetInferenceMs = 150;
    double  m_targetSpeedupRatio = 3.0;
    double  m_targetMapLossPct = 1.5;
    double  m_targetPathDeviationMm = 300;
    double  m_targetCanTxLatencyMs = 10;
    double  m_targetDetectLatencyMs = 200;
    int     m_targetMissionSuccessPct = 70;
    double  m_targetSpeedKmhMin = 3;
    double  m_targetSpeedKmhMax = 5;
    double  m_routeLengthM = 250;
    QString m_tacticalMapPath = "maps/straight_road.yaml";
    QString m_tacticalLanePath = "maps/testarea_all_local.json";
    int     m_tacticalMapVersion = 1;
    QString m_campaign = "dev";
    double  m_datumLat = 35.8341190;
    double  m_datumLon = 128.6862139;
    double  m_routeStartX = 0.0;
    double  m_routeStartY = 0.0;
    double  m_routeHeadingDeg = 0.0;

    // §4.2 KPI targets
    int     m_locSuccessRunsMin   = 7;
    int     m_pathPlanMsMax       = 2000;
    int     m_pathPlanSuccessRunsMin = 9;
    int     m_parkingDetectRunsMin = 8;
    int     m_falsePositivePerRunMax = 1;
    int     m_triggerAccuracyPctMin = 90;
    int     m_fsTransitionMsMax   = 100;
    double  m_canLossPctMax       = 0.1;
    int     m_runsTotal           = 10;
    double  m_perceptionConfidenceMin = 0.70;
    int     m_pathPlanChartMaxMs      = 2400;
    int     m_pathDeviationChartMaxMm = 500;
    int     m_failsafeDwellMs         = 1500;
    double  m_laneCenterDevMmMax      = 500;

    double  m_mapOriginTolM       = 0.05;
    double  m_mapResolutionTolM   = 0.001;
    double  m_mapTextureLimitPx   = 15000;
    double  m_mapZoomMin          = 1.0;
    double  m_mapZoomMax          = 12.0;
    double  m_mapFocusSpanM       = 150;

    int     m_sensorStaleLidarMs  = 1000;
    int     m_sensorStaleImuMs    = 500;
    int     m_sensorStaleEncMs    = 500;
    int     m_sensorStaleEgoMs    = 700;

    int     m_canDefaultBitrate = 500000;
    int     m_canTerminationOhm = 120;
    QString m_canPlugin;   // e.g. "slcan" (empty → env/auto-detect)
    QString m_canDevice;   // e.g. "COM3"

    QString m_linkMode         = "standalone";
    QString m_linkBridgeHost   = "192.168.137.1";   // typical Windows hotspot gateway
    int     m_linkSnapshotPort = 45100;
    int     m_linkCommandPort  = 45101;

    QString m_debugHost        = "";       // Jetson IP for the debug console (empty = off)
    int     m_debugPort        = 45103;    // Jetson node's listen port (HELLO + send)
    int     m_debugLocalPort   = 45102;    // where this dashboard receives debug lines
    int     m_linkSnapshotHz   = 50;

    double  m_ramTotalGbFallback = 8;
    double  m_swapTotalGb = 4;
    int     m_missionTotal = 10;

    int     m_uiPollIntervalMs = 8;   // mentor: half the 60fps frame budget

    QString m_platformLabel = "Jetson Orin Nano";
    QString m_modelFamily   = "YOLO26";
    QString m_teamLabel     = "Team 02";

    QString m_sourcePath;
};
