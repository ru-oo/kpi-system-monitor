#include "Config.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

Config::Config(QObject *parent) : QObject(parent) {}

bool Config::loadFromDisk() {
    // 1) KPI_CONFIG env override
    const QByteArray envPath = qgetenv("KPI_CONFIG");
    if (!envPath.isEmpty()) {
        if (loadFromFile(QString::fromLocal8Bit(envPath))) return true;
        qWarning() << "[Config] KPI_CONFIG set but file unreadable:" << envPath;
    }

    // 2) <appDir>/config.json
    QString next = QCoreApplication::applicationDirPath() + "/config.json";
    if (QFile::exists(next) && loadFromFile(next)) return true;

    // 3) Walk up a few levels from appDir to find a source-tree config.json.
    //    Useful for Qt Creator dev builds where the binary lives under
    //    build/<kit>/Desktop_Qt_..._Debug/KpiProjectApp.exe.
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        const QString candidate = d.filePath("config.json");
        if (QFile::exists(candidate) && loadFromFile(candidate)) return true;
        if (!d.cdUp()) break;
    }

    qInfo() << "[Config] No config.json found — using compiled defaults.";
    return false;
}

static double readD(const QJsonObject &o, const char *k, double def) {
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toDouble() : def;
}
static int readI(const QJsonObject &o, const char *k, int def) {
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toInt() : def;
}
static QString readS(const QJsonObject &o, const char *k, const QString &def) {
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isString() ? v.toString() : def;
}

bool Config::loadFromFile(const QString &absolutePath) {
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[Config] cannot open" << absolutePath << ":" << f.errorString();
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "[Config] parse error in" << absolutePath << ":" << err.errorString();
        return false;
    }
    const QJsonObject root = doc.object();

    // accuracy: per model {yolo26s,yolo26n} × {fp32,fp16,int8} × {map50,map50_95}.
    const QJsonObject acc = root.value("accuracy").toObject();
    auto readModelAcc = [&](const char *key, double m5095[3], double m50[3]) {
        const QJsonObject mo = acc.value(key).toObject();
        const char *precs[3] = { "fp32", "fp16", "int8" };
        for (int i = 0; i < 3; ++i) {
            const QJsonObject po = mo.value(precs[i]).toObject();
            m5095[i] = readD(po, "map50_95", m5095[i]);
            m50[i]   = readD(po, "map50",    m50[i]);
        }
    };
    readModelAcc("yolo26s", m_accS5095, m_accS50);
    readModelAcc("yolo26n", m_accN5095, m_accN50);

    const QJsonObject mdl = root.value("model").toObject();
    m_ptBaselineMs     = readD(mdl, "pt_baseline_ms",      m_ptBaselineMs);
    m_defaultOptMode   = readS(mdl, "default_opt_mode",    m_defaultOptMode);
    m_defaultYoloModel = readS(mdl, "default_yolo_model",  m_defaultYoloModel);

    // Back-compat singles = default model's map50-95 (now that model is known).
    m_accFp32 = accMap5095(m_defaultYoloModel, 0);
    m_accFp16 = accMap5095(m_defaultYoloModel, 1);
    m_accInt8 = accMap5095(m_defaultYoloModel, 2);
    m_perceptionHz     = readI(mdl, "perception_hz",       m_perceptionHz);
    m_realtimeKpiHz    = readI(mdl, "realtime_kpi_hz",     m_realtimeKpiHz);
    m_vehicleStatusHz  = readI(mdl, "vehicle_status_hz",   m_vehicleStatusHz);
    m_fp16SpeedupRef   = readD(mdl, "fp16_speedup_ref",    m_fp16SpeedupRef);

    const QJsonObject tgt = root.value("targets").toObject();
    m_targetInferenceMs       = readD(tgt, "inference_ms_max",        m_targetInferenceMs);
    m_targetSpeedupRatio      = readD(tgt, "speedup_ratio_min",       m_targetSpeedupRatio);
    m_targetMapLossPct        = readD(tgt, "map_loss_pct_max",        m_targetMapLossPct);
    m_targetPathDeviationMm   = readD(tgt, "path_deviation_mm_max",   m_targetPathDeviationMm);
    m_targetCanTxLatencyMs    = readD(tgt, "can_tx_latency_ms_max",   m_targetCanTxLatencyMs);
    m_targetDetectLatencyMs   = readD(tgt, "detect_latency_ms_max",   m_targetDetectLatencyMs);
    m_targetMissionSuccessPct = readI(tgt, "mission_success_pct_min", m_targetMissionSuccessPct);
    m_targetSpeedKmhMin       = readD(tgt, "speed_kmh_min",           m_targetSpeedKmhMin);
    m_targetSpeedKmhMax       = readD(tgt, "speed_kmh_max",           m_targetSpeedKmhMax);
    m_routeLengthM            = readD(tgt, "route_length_m",          m_routeLengthM);

    // §4.2 KPI targets
    m_locSuccessRunsMin       = readI(tgt, "localization_success_runs_min", m_locSuccessRunsMin);
    m_pathPlanMsMax           = readI(tgt, "path_plan_ms_max",          m_pathPlanMsMax);
    m_pathPlanSuccessRunsMin  = readI(tgt, "path_plan_success_runs_min", m_pathPlanSuccessRunsMin);
    m_parkingDetectRunsMin    = readI(tgt, "parking_detect_runs_min",   m_parkingDetectRunsMin);
    m_falsePositivePerRunMax  = readI(tgt, "false_positive_per_run_max", m_falsePositivePerRunMax);
    m_triggerAccuracyPctMin   = readI(tgt, "trigger_accuracy_pct_min",  m_triggerAccuracyPctMin);
    m_fsTransitionMsMax       = readI(tgt, "fs_transition_ms_max",      m_fsTransitionMsMax);
    m_canLossPctMax           = readD(tgt, "can_loss_pct_max",          m_canLossPctMax);
    m_runsTotal               = readI(tgt, "runs_total",                m_runsTotal);
    m_perceptionConfidenceMin = readD(tgt, "perception_confidence_min", m_perceptionConfidenceMin);
    m_pathPlanChartMaxMs      = readI(tgt, "path_plan_chart_max_ms",    m_pathPlanChartMaxMs);
    m_pathDeviationChartMaxMm = readI(tgt, "path_deviation_chart_max_mm", m_pathDeviationChartMaxMm);
    m_failsafeDwellMs         = readI(tgt, "failsafe_dwell_ms",         m_failsafeDwellMs);
    m_laneCenterDevMmMax      = readD(tgt, "lane_center_dev_mm_max",    m_laneCenterDevMmMax);

    const QJsonObject canc = root.value("can").toObject();
    m_canDefaultBitrate = readI(canc, "default_bitrate", m_canDefaultBitrate);
    m_canTerminationOhm = readI(canc, "termination_ohm", m_canTerminationOhm);
    m_canPlugin         = readS(canc, "plugin", m_canPlugin);   // e.g. "slcan" (CANable)
    m_canDevice         = readS(canc, "device", m_canDevice);   // e.g. "COM3"

    const QJsonObject lnk = root.value("link").toObject();
    m_linkMode         = readS(lnk, "mode",          m_linkMode);
    m_linkBridgeHost   = readS(lnk, "bridge_host",   m_linkBridgeHost);
    m_linkSnapshotPort = readI(lnk, "snapshot_port", m_linkSnapshotPort);
    m_linkCommandPort  = readI(lnk, "command_port",  m_linkCommandPort);
    m_linkSnapshotHz   = readI(lnk, "snapshot_hz",   m_linkSnapshotHz);

    const QJsonObject dbg = root.value("debug").toObject();
    m_debugHost      = readS(dbg, "host",       m_debugHost);
    m_debugPort      = readI(dbg, "port",       m_debugPort);
    m_debugLocalPort = readI(dbg, "local_port", m_debugLocalPort);

    const QJsonObject hw = root.value("hardware").toObject();
    m_ramTotalGbFallback = readD(hw, "ram_total_gb_fallback", m_ramTotalGbFallback);
    m_swapTotalGb        = readD(hw, "swap_total_gb",         m_swapTotalGb);
    m_missionTotal       = readI(hw, "mission_total",         m_missionTotal);
    m_sensorStaleLidarMs = readI(hw, "sensor_stale_lidar_ms", m_sensorStaleLidarMs);
    m_sensorStaleImuMs   = readI(hw, "sensor_stale_imu_ms",   m_sensorStaleImuMs);
    m_sensorStaleEncMs   = readI(hw, "sensor_stale_enc_ms",   m_sensorStaleEncMs);
    m_sensorStaleEgoMs   = readI(hw, "sensor_stale_ego_ms",   m_sensorStaleEgoMs);

    const QJsonObject uiCfg = root.value("ui").toObject();
    m_uiPollIntervalMs = readI(uiCfg, "poll_interval_ms", m_uiPollIntervalMs);
    if (m_uiPollIntervalMs < 1)   m_uiPollIntervalMs = 1;     // sanity floor
    if (m_uiPollIntervalMs > 100) m_uiPollIntervalMs = 100;   // sanity ceiling

    const QJsonObject ui = root.value("ui").toObject();
    m_platformLabel = readS(ui, "platform_label", m_platformLabel);
    m_modelFamily   = readS(ui, "model_family",   m_modelFamily);
    m_teamLabel     = readS(ui, "team_label",     m_teamLabel);

    // Tactical (SLAM-map page). map_path is resolved relative to the config
    // file's directory if not absolute.
    const QJsonObject tac = root.value("tactical").toObject();
    QString mp = readS(tac, "map_path", m_tacticalMapPath);
    if (!mp.isEmpty() && !QFileInfo(mp).isAbsolute())
        mp = QDir(QFileInfo(absolutePath).absolutePath()).filePath(mp);
    m_tacticalMapPath   = mp;
    QString lp = readS(tac, "lane_path", m_tacticalLanePath);
    if (!lp.isEmpty() && !QFileInfo(lp).isAbsolute())
        lp = QDir(QFileInfo(absolutePath).absolutePath()).filePath(lp);
    m_tacticalLanePath  = lp;
    m_tacticalMapVersion = readI(tac, "map_version", m_tacticalMapVersion);
    m_campaign           = readS(tac, "campaign", m_campaign);
    m_mapOriginTolM      = readD(tac, "map_origin_tol_m",     m_mapOriginTolM);
    m_mapResolutionTolM  = readD(tac, "map_resolution_tol_m", m_mapResolutionTolM);
    m_mapTextureLimitPx  = readD(tac, "map_texture_limit_px", m_mapTextureLimitPx);
    m_mapZoomMin         = readD(tac, "map_zoom_min",         m_mapZoomMin);
    m_mapZoomMax         = readD(tac, "map_zoom_max",         m_mapZoomMax);
    m_mapFocusSpanM      = readD(tac, "map_focus_span_m",     m_mapFocusSpanM);
    m_datumLat          = readD(tac, "datum_lat", m_datumLat);
    m_datumLon          = readD(tac, "datum_lon", m_datumLon);
    m_routeStartX       = readD(tac, "route_start_x",   m_routeStartX);
    m_routeStartY       = readD(tac, "route_start_y",   m_routeStartY);
    m_routeHeadingDeg   = readD(tac, "route_heading_deg", m_routeHeadingDeg);

    m_sourcePath = QFileInfo(absolutePath).absoluteFilePath();
    qInfo().noquote() << "[Config] loaded" << m_sourcePath;
    return true;
}
