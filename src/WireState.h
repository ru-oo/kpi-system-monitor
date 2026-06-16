#pragma once

#include <QString>
#include <QtGlobal>

// WireState — the decoded "physical data" snapshot shared between the CAN
// decoder (CanBridge) and the UDP transport (StateLink). It deliberately has
// NO QtSerialBus dependency so it compiles on iOS (where SerialBus is absent).
//
// LatestValues is the coalesced "latest value per CAN domain" snapshot. In the
// split deployment the bridge decodes CAN into this struct and ships it over
// UDP; the iPad client receives it and feeds KpiData through the SAME apply*()
// path. A "dirty" bit per domain says "fresh since the last drain".

struct LatestValues {
    // 0x102 Realtime_KPI
    bool   kpiDirty = false;
    double inferenceMs = 0, gpuPct = 0, cpuPct = 0, gpuTempC = 0, pathErrorMm = 0;

    // 0x101 Vehicle_Status
    bool    vehicleDirty = false;
    double  speedKmh = 0, steeringDeg = 0;
    QString driveStateName, optMode, yoloModel;
    bool    controlEnable = false;

    // 0x100 Obstacle_Detection
    bool    obstacleDirty = false;
    double  obstacleDistM = 0, obstacleAngleDeg = 0, obstacleConf01 = 0;
    QString obstacleClassName;
    int     failsafeLevel = 1;

    // 0x103 System_Resource
    bool   sysDirty = false;
    double ramPct = 0, swapPct = 0, busLoadPct = 0, canLossPct = 0;
    int    sessionRateHz = 0, pi5StatusCode = 0, cameraStatus = 0;

    // 0x104 Route_Status
    bool   routeDirty = false;
    double routeProgressM = 0, amclErrorM = 0, missionSuccessPct = 0;
    int    routeState = 0;

    // 0x105 Hardware_Info
    bool   hardwareDirty = false;
    int    jetsonModelCode = 0;
    double orinMemoryGb = 0;

    // 0x1FF Fail_Safe_Event
    bool   failsafeEventDirty = false;
    quint8 fsEventCode = 0, fsReasonCode = 0, fsLevel = 0;

    // 0x10C Map_Info (multiplexed; reassembled across m0/m1 frames)
    bool   mapInfoDirty = false;
    double mapOriginX = 0, mapOriginY = 0, mapResolution = 0;
    int    mapWidth = 0, mapHeight = 0, mapVersion = 0;

    // 0x10D Ego_Pose (map frame)
    bool   egoDirty = false;
    double egoX = 0, egoY = 0, egoYaw = 0;

    // 0x108 Map_Datum — local-frame origin (WGS84). Drives runtime ENU.
    bool   datumDirty = false;
    double datumLat = 0, datumLon = 0;

    // 0x21 STM32_IMU_Feedback — chassis IMU over CAN
    bool   imuDirty = false;
    double imuYaw = 0, imuGyroZ = 0, imuRoll = 0, imuPitch = 0;

    // 0x20 STM32_Encoder_Feedback — drivetrain wheel tick counts
    bool   encDirty = false;
    qint32 encLeft = 0, encRight = 0;

    // 0x106 Planning_Status
    bool   planningDirty = false;
    double planLastMs = 0;
    int    planSuccessRuns = 0, planTotalRuns = 0, planState = 0;

    // 0x107 Perception_Validation
    bool   perceptionDirty = false;
    int    percDetectedRuns = 0, percTotalRuns = 0, percFalsePos = 0;
    double percTriggerAccPct = 0;

    // 0x109 Network_Status
    bool   networkDirty = false;
    double netWifiPingMs = 0, netLossRatePct = 0;
    int    netRssiDbm = 0, netState = 0;

    // 0x10A Localization_Status
    bool   locDirty = false;
    int    locMode = 0;
    double locQuality = 0;
    double locLaneDevMm = 0;

    // 0x10B Behavior_State
    bool   behaviorDirty = false;
    int    behaviorMode = 0;

    // Bus stats (TX latency + counters)
    bool   busStatsDirty = false;
    double txLatencyMs = 0;
    int    framesRx = 0, framesTx = 0;
    qint64 uptimeMs = 0;
    int    canBitrate = 0;
    bool   canOnline = false;

    // Replay state — true while the bridge is replaying a recorded run. Forwarded
    // so the iPad client can show "REPLAYING" and the replayed data flows through
    // the normal snapshot path. NOT a dirty bit (it's a level, not an edge).
    bool   replaying = false;
};

struct LogEntry {
    QString code;
    QString msg;
    QString severity;
    QString src;
};
