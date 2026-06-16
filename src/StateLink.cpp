#include "StateLink.h"

#include <QUdpSocket>
#include <QTimer>
#include <QDataStream>
#include <QNetworkDatagram>
#include <QFileInfo>
#include <QDebug>

#include "RunRecorder.h"   // bridge resolves run name → path (iOS-safe: no SerialBus)

// CanBridge is only referenced by the BRIDGE role (desktop). It pulls in
// QtSerialBus, which does not exist on iOS — so keep it out of the iPad build.
#if !defined(Q_OS_IOS)
#  include "CanBridge.h"
#endif

namespace {
constexpr quint32 MAGIC_SNAPSHOT = 0x4B504953;   // 'KPIS'
constexpr quint32 MAGIC_COMMAND  = 0x4B504943;   // 'KPIC'
constexpr quint32 MAGIC_RUNLIST  = 0x4B504952;   // 'KPIR' — bridge → client run names
constexpr quint32 MAGIC_RAWFRAMES = 0x4B504946;  // 'KPIF' — bridge → client raw CAN frames
constexpr quint16 WIRE_VERSION   = 2;            // v2: +snapshot.replaying, run-list, replay cmds

// Command types (client → bridge).
enum CmdType : quint8 {
    CMD_HELLO = 0,   // register client address (no payload)
    CMD_PRECISION,   // QString mode
    CMD_MODEL,       // QString model
    CMD_AMCL,        // reserved (AMCL removed; kept so wire numbering is stable)
    CMD_ESTOP,       // (none)
    CMD_DEST,        // qint32 destId, QString name
    CMD_GOAL,        // double distM, latM, yawDeg
    CMD_REPLAY,      // QString run name → bridge resolves path + plays it back
    CMD_STOP_REPLAY, // (none)
    CMD_CANCEL_GOAL, // (none) → bridge aborts goal (0x201 Goal_Valid=0 → IDLE)
    CMD_CLEAR_ESTOP, // (none) → bridge releases the latched E-stop (Estop=0)
};

// Fixed stream version so the bridge and iPad agree byte-for-byte.
// Pin a fixed stream version so the bridge and client agree byte-for-byte even
// across Qt versions (Linux bridge may be Qt 6.4, iPad/Windows 6.11). Qt_6_4 is
// the lowest common baseline and exists in all of them.
inline void setVer(QDataStream &s) { s.setVersion(QDataStream::Qt_6_4); }

// ── Snapshot (bridge → client): every LatestValues field, in a fixed order,
// followed by the pending log entries. ──────────────────────────────────────
QByteArray serializeSnapshot(const LatestValues &v, const QVector<LogEntry> &logs) {
    QByteArray buf;
    QDataStream s(&buf, QIODevice::WriteOnly); setVer(s);
    s << MAGIC_SNAPSHOT << WIRE_VERSION;
    s << v.kpiDirty << v.inferenceMs << v.gpuPct << v.cpuPct << v.gpuTempC << v.pathErrorMm;
    s << v.vehicleDirty << v.speedKmh << v.steeringDeg << v.driveStateName << v.optMode << v.yoloModel << v.controlEnable;
    s << v.obstacleDirty << v.obstacleDistM << v.obstacleAngleDeg << v.obstacleConf01 << v.obstacleClassName << v.failsafeLevel;
    s << v.sysDirty << v.ramPct << v.swapPct << v.busLoadPct << v.canLossPct << v.sessionRateHz << v.pi5StatusCode << v.cameraStatus;
    s << v.routeDirty << v.routeProgressM << v.amclErrorM << v.missionSuccessPct << v.routeState;
    s << v.hardwareDirty << v.jetsonModelCode << v.orinMemoryGb;
    s << v.failsafeEventDirty << v.fsEventCode << v.fsReasonCode << v.fsLevel;
    s << v.mapInfoDirty << v.mapOriginX << v.mapOriginY << v.mapResolution << v.mapWidth << v.mapHeight << v.mapVersion;
    s << v.egoDirty << v.egoX << v.egoY << v.egoYaw;
    s << v.datumDirty << v.datumLat << v.datumLon;
    s << v.imuDirty << v.imuYaw << v.imuGyroZ << v.imuRoll << v.imuPitch;
    s << v.encDirty << v.encLeft << v.encRight;
    s << v.planningDirty << v.planLastMs << v.planSuccessRuns << v.planTotalRuns << v.planState;
    s << v.perceptionDirty << v.percDetectedRuns << v.percTotalRuns << v.percFalsePos << v.percTriggerAccPct;
    s << v.networkDirty << v.netWifiPingMs << v.netLossRatePct << v.netRssiDbm << v.netState;
    s << v.locDirty << v.locMode << v.locQuality << v.locLaneDevMm;
    s << v.behaviorDirty << v.behaviorMode;
    s << v.busStatsDirty << v.txLatencyMs << v.framesRx << v.framesTx << v.uptimeMs << v.canBitrate << v.canOnline;
    s << v.replaying;
    s << quint16(logs.size());
    for (const LogEntry &e : logs) s << e.code << e.msg << e.severity << e.src;
    return buf;
}

// Run-list (bridge → client): the recorded-run filenames the client can replay.
QByteArray serializeRunList(const QStringList &runs) {
    QByteArray buf;
    QDataStream s(&buf, QIODevice::WriteOnly); setVer(s);
    s << MAGIC_RUNLIST << WIRE_VERSION << runs;
    return buf;
}

bool deserializeSnapshot(const QByteArray &buf, LatestValues &v, QVector<LogEntry> &logs) {
    QDataStream s(buf); setVer(s);
    quint32 magic = 0; quint16 ver = 0;
    s >> magic >> ver;
    if (magic != MAGIC_SNAPSHOT || ver != WIRE_VERSION) return false;
    s >> v.kpiDirty >> v.inferenceMs >> v.gpuPct >> v.cpuPct >> v.gpuTempC >> v.pathErrorMm;
    s >> v.vehicleDirty >> v.speedKmh >> v.steeringDeg >> v.driveStateName >> v.optMode >> v.yoloModel >> v.controlEnable;
    s >> v.obstacleDirty >> v.obstacleDistM >> v.obstacleAngleDeg >> v.obstacleConf01 >> v.obstacleClassName >> v.failsafeLevel;
    s >> v.sysDirty >> v.ramPct >> v.swapPct >> v.busLoadPct >> v.canLossPct >> v.sessionRateHz >> v.pi5StatusCode >> v.cameraStatus;
    s >> v.routeDirty >> v.routeProgressM >> v.amclErrorM >> v.missionSuccessPct >> v.routeState;
    s >> v.hardwareDirty >> v.jetsonModelCode >> v.orinMemoryGb;
    s >> v.failsafeEventDirty >> v.fsEventCode >> v.fsReasonCode >> v.fsLevel;
    s >> v.mapInfoDirty >> v.mapOriginX >> v.mapOriginY >> v.mapResolution >> v.mapWidth >> v.mapHeight >> v.mapVersion;
    s >> v.egoDirty >> v.egoX >> v.egoY >> v.egoYaw;
    s >> v.datumDirty >> v.datumLat >> v.datumLon;
    s >> v.imuDirty >> v.imuYaw >> v.imuGyroZ >> v.imuRoll >> v.imuPitch;
    s >> v.encDirty >> v.encLeft >> v.encRight;
    s >> v.planningDirty >> v.planLastMs >> v.planSuccessRuns >> v.planTotalRuns >> v.planState;
    s >> v.perceptionDirty >> v.percDetectedRuns >> v.percTotalRuns >> v.percFalsePos >> v.percTriggerAccPct;
    s >> v.networkDirty >> v.netWifiPingMs >> v.netLossRatePct >> v.netRssiDbm >> v.netState;
    s >> v.locDirty >> v.locMode >> v.locQuality >> v.locLaneDevMm;
    s >> v.behaviorDirty >> v.behaviorMode;
    s >> v.busStatsDirty >> v.txLatencyMs >> v.framesRx >> v.framesTx >> v.uptimeMs >> v.canBitrate >> v.canOnline;
    s >> v.replaying;
    quint16 n = 0; s >> n;
    logs.clear();
    for (quint16 i = 0; i < n; ++i) { LogEntry e; s >> e.code >> e.msg >> e.severity >> e.src; logs.push_back(e); }
    return s.status() == QDataStream::Ok;
}
} // namespace

StateLink::StateLink(Role role, QObject *parent) : QObject(parent), m_role(role) {}

void StateLink::start(const QString &peerHost, quint16 snapshotPort,
                      quint16 commandPort, int snapshotHz) {
    m_snapshotPort = snapshotPort;
    m_commandPort  = commandPort;
    m_sock = new QUdpSocket(this);
    connect(m_sock, &QUdpSocket::readyRead, this, &StateLink::onReadyRead);

    if (m_role == Role::Bridge) {
        // Bind the command port to receive UI commands; learn the client on RX.
        if (!m_sock->bind(QHostAddress::AnyIPv4, m_commandPort))
            qWarning() << "[StateLink/bridge] bind command port" << m_commandPort << "failed:" << m_sock->errorString();
        m_tick = new QTimer(this);
        m_tick->setInterval(snapshotHz > 0 ? qMax(1, 1000 / snapshotHz) : 20);
        connect(m_tick, &QTimer::timeout, this, &StateLink::onSnapshotTick);
        m_tick->start();
#if !defined(Q_OS_IOS)
        // Forward raw CAN frames to the client so its Raw CAN Monitor works.
        // Queued: frameForRecord fires on the worker; we buffer on the tick thread.
        if (m_can)
            connect(m_can, &CanBridge::frameForRecord,
                    this, &StateLink::onBridgeRawFrame, Qt::QueuedConnection);
#endif
        qInfo().nospace() << "[StateLink] BRIDGE: forwarding snapshots @" << snapshotHz
                          << "Hz, listening for commands on udp/" << m_commandPort;
    } else {
        // Client: bind the snapshot port; send commands to the bridge host.
        m_peer = QHostAddress(peerHost);
        m_peerKnown = !m_peer.isNull();
        if (!m_sock->bind(QHostAddress::AnyIPv4, m_snapshotPort))
            qWarning() << "[StateLink/client] bind snapshot port" << m_snapshotPort << "failed:" << m_sock->errorString();
        // Keepalive HELLO so the bridge (re)learns our address, even after a
        // hotspot reconnect. Cheap (1 datagram / 2 s).
        m_hello = new QTimer(this);
        m_hello->setInterval(2000);
        connect(m_hello, &QTimer::timeout, this, [this]() { sendCommand(CMD_HELLO, {}); });
        m_hello->start();
        sendCommand(CMD_HELLO, {});
        qInfo().nospace() << "[StateLink] CLIENT: receiving snapshots on udp/" << m_snapshotPort
                          << ", commands → " << peerHost.toStdString().c_str() << ":" << m_commandPort;
    }
}

void StateLink::onSnapshotTick() {
#if !defined(Q_OS_IOS)
    if (m_role != Role::Bridge || !m_can || !m_peerKnown) return;
    const LatestValues v = m_can->peekLatest();
    const QVector<LogEntry> logs = m_can->takePendingLogs();
    m_sock->writeDatagram(serializeSnapshot(v, logs), m_peer, m_snapshotPort);
    // Advertise the recorded-run list ~once a second (it changes rarely) so the
    // iPad can list + replay runs that live on this bridge.
    if (m_rec && (++m_runListTick % 50 == 0))
        m_sock->writeDatagram(serializeRunList(m_rec->runList()), m_peer, m_snapshotPort);
    // Flush buffered raw frames → the client's Raw CAN Monitor (original ts kept
    // so the client computes correct per-ID rates).
    if (!m_rawBuf.isEmpty()) {
        QByteArray buf; QDataStream s(&buf, QIODevice::WriteOnly); setVer(s);
        s << MAGIC_RAWFRAMES << WIRE_VERSION << (quint32)m_rawBuf.size();
        for (const RawFrame &f : m_rawBuf) s << f.id << f.payload << f.ts;
        m_sock->writeDatagram(buf, m_peer, m_snapshotPort);
        m_rawBuf.clear();
    }
#endif
}

void StateLink::onBridgeRawFrame(quint32 id, const QByteArray &payload, qint64 ts) {
    // Bound the buffer so one datagram stays small on a busy bus (the monitor only
    // shows the most recent frames); drop oldest on overflow.
    if (m_rawBuf.size() >= 96) m_rawBuf.remove(0);
    m_rawBuf.push_back(RawFrame{id, payload, ts});
}

void StateLink::onReadyRead() {
    while (m_sock->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_sock->receiveDatagram();
        const QByteArray buf = dg.data();
        if (buf.size() < 6) continue;
        QDataStream s(buf); setVer(s);
        quint32 magic = 0; quint16 ver = 0;
        s >> magic >> ver;
        if (ver != WIRE_VERSION) continue;

        if (m_role == Role::Client && magic == MAGIC_SNAPSHOT) {
            LatestValues v; QVector<LogEntry> logs;
            if (deserializeSnapshot(buf, v, logs)) {
                QMutexLocker lock(&m_mutex);
                m_latest = v;
                m_logs += logs;
                m_haveSnapshot = true;
            }
        } else if (m_role == Role::Client && magic == MAGIC_RUNLIST) {
            QStringList runs; s >> runs;
            if (s.status() == QDataStream::Ok && runs != m_bridgeRunList) {
                m_bridgeRunList = runs;
                emit runListChanged();
            }
        } else if (m_role == Role::Client && magic == MAGIC_RAWFRAMES) {
            quint32 n = 0; s >> n;
            for (quint32 i = 0; i < n && s.status() == QDataStream::Ok; ++i) {
                quint32 id = 0; QByteArray payload; qint64 ts = 0;
                s >> id >> payload >> ts;
                if (s.status() == QDataStream::Ok)
                    emit rawFrameReceived(id, payload, ts);
            }
        } else if (m_role == Role::Bridge && magic == MAGIC_COMMAND) {
            // Learn / refresh the client address from whoever sends commands.
            m_peer = dg.senderAddress();
            m_peerKnown = !m_peer.isNull();
#if !defined(Q_OS_IOS)
            quint8 type = 0; s >> type;
            if (!m_can) continue;
            switch (type) {
                case CMD_HELLO: break;
                case CMD_PRECISION: { QString m; s >> m; m_can->sendSetPrecision(m); break; }
                case CMD_MODEL:     { QString m; s >> m; m_can->sendSetModel(m); break; }
                case CMD_AMCL:      break;   // AMCL removed (reserved type)
                case CMD_ESTOP:     m_can->sendEStop(); break;
                case CMD_CLEAR_ESTOP: m_can->clearEStop(); break;
                case CMD_DEST:      { qint32 id; QString n; s >> id >> n; m_can->sendSetDestination(id, n); break; }
                case CMD_GOAL:      { double x, y, yaw; s >> x >> y >> yaw; m_can->sendSetGoalPose(x, y, yaw); break; }
                case CMD_REPLAY:    { QString name; s >> name; if (m_rec) m_can->startReplay(m_rec->runPath(name)); break; }
                case CMD_STOP_REPLAY: m_can->stopReplay(); break;
                case CMD_CANCEL_GOAL: m_can->sendCancelGoal(); break;
                default: break;
            }
#endif
        }
    }
}

LatestValues StateLink::takeLatest() {
    QMutexLocker lock(&m_mutex);
    if (!m_haveSnapshot) return LatestValues{};   // nothing yet → all dirty=false
    LatestValues out = m_latest;
    // One-shot Fail_Safe_Event edge: apply once, then stop re-applying until the
    // next snapshot actually carries a fresh edge.
    m_latest.failsafeEventDirty = false;
    return out;
}

QVector<LogEntry> StateLink::takePendingLogs() {
    QMutexLocker lock(&m_mutex);
    QVector<LogEntry> out = m_logs;
    m_logs.clear();
    return out;
}

void StateLink::sendCommand(quint8 type, const QByteArray &payload) {
    if (m_role != Role::Client || !m_sock || !m_peerKnown) return;
    QByteArray buf;
    QDataStream s(&buf, QIODevice::WriteOnly); setVer(s);
    s << MAGIC_COMMAND << WIRE_VERSION << type;
    if (!payload.isEmpty()) buf.append(payload);
    m_sock->writeDatagram(buf, m_peer, m_commandPort);
}

// ── QML-facing TX → serialized commands ─────────────────────────────────────
void StateLink::sendSetPrecision(const QString &mode) {
    QByteArray p; QDataStream s(&p, QIODevice::WriteOnly); setVer(s); s << mode;
    sendCommand(CMD_PRECISION, p);
}
void StateLink::sendSetModel(const QString &model) {
    QByteArray p; QDataStream s(&p, QIODevice::WriteOnly); setVer(s); s << model;
    sendCommand(CMD_MODEL, p);
}
void StateLink::sendEStop()    { sendCommand(CMD_ESTOP, {}); }
void StateLink::clearEStop()   { sendCommand(CMD_CLEAR_ESTOP, {}); }
void StateLink::sendSetDestination(int destId, const QString &name) {
    QByteArray p; QDataStream s(&p, QIODevice::WriteOnly); setVer(s); s << qint32(destId) << name;
    sendCommand(CMD_DEST, p);
}
void StateLink::sendSetGoalPose(double distM, double latM, double yawDeg) {
    QByteArray p; QDataStream s(&p, QIODevice::WriteOnly); setVer(s); s << distM << latM << yawDeg;
    sendCommand(CMD_GOAL, p);
}
void StateLink::sendCancelGoal() { sendCommand(CMD_CANCEL_GOAL, {}); }
void StateLink::startReplay(const QString &nameOrPath) {
    // The CSV lives on the bridge — send just the run NAME (basename) and let the
    // bridge resolve it in its own runs dir, then play it back.
    const QString name = QFileInfo(nameOrPath).fileName();
    QByteArray p; QDataStream s(&p, QIODevice::WriteOnly); setVer(s); s << name;
    sendCommand(CMD_REPLAY, p);
}
void StateLink::stopReplay() { sendCommand(CMD_STOP_REPLAY, {}); }
