#pragma once

#include <QObject>
#include <QHostAddress>
#include <QMutex>
#include <QVector>
#include <QStringList>
#include <QByteArray>
#include "WireState.h"

class QUdpSocket;
class QTimer;
class CanBridge;
class RunRecorder;

// StateLink — UDP transport for the split bridge/client deployment.
//
//   Bridge role (laptop, has CAN): at a fixed cadence it pulls the decoded
//     snapshot (CanBridge::peekLatest + takePendingLogs) and ships it to the
//     iPad; it receives UI-command datagrams and replays them onto CanBridge's
//     TX. It LEARNS the client's address from the first datagram it receives —
//     no hardcoded IP.
//
//   Client role (iPad, no CAN): receives snapshots → exposes takeLatest() /
//     takePendingLogs() to the UI poll (same shape as CanBridge); UI commands
//     are serialized and sent to the bridge. It exposes the SAME QML-invokable
//     method names as CanBridge, so it can be the "canBridge" context property
//     with the QML unchanged.
//
// Design choice (lowest latency on a phone-hotspot WiFi): the bridge forwards a
// COALESCED full-state snapshot at a fixed rate, not raw CAN frames. A dropped
// datagram costs one tick (~20 ms) because the next snapshot carries the full
// latest state — there is no per-signal staleness and no packet storm.
class StateLink : public QObject {
    Q_OBJECT
    // Client: the recorded-run names the bridge advertises (so the iPad's Runs
    // page can list + replay runs that live on the laptop).
    Q_PROPERTY(QStringList bridgeRunList READ bridgeRunList NOTIFY runListChanged)
public:
    enum class Role { Bridge, Client };

    explicit StateLink(Role role, QObject *parent = nullptr);

    void attachBridge(CanBridge *can) { m_can = can; }     // bridge role only
    void attachRecorder(RunRecorder *rec) { m_rec = rec; } // bridge role only

    QStringList bridgeRunList() const { return m_bridgeRunList; }
    // Bind sockets + start the cadence. peerHost is the bridge host (client
    // role); ignored by the bridge (it learns the client address on RX).
    void start(const QString &peerHost, quint16 snapshotPort,
               quint16 commandPort, int snapshotHz);

    // Client-side drain — same contract as CanBridge for the UI poll.
    LatestValues takeLatest();
    QVector<LogEntry> takePendingLogs();

    // ── QML-facing TX (mirror CanBridge) ────────────────────────────────
    // Client role → serialized to the bridge over UDP.
    Q_INVOKABLE void sendSetPrecision(const QString &mode);
    Q_INVOKABLE void sendSetModel(const QString &model);
    Q_INVOKABLE void sendEStop();
    Q_INVOKABLE void clearEStop();
    Q_INVOKABLE void sendSetDestination(int destId, const QString &name);
    Q_INVOKABLE void sendSetGoalPose(double distM, double latM, double yawDeg);
    Q_INVOKABLE void sendCancelGoal();
    // Replay is a bridge-side concern: the client sends a "replay <run>" command
    // to the laptop bridge, which plays the CSV back onto its CAN decode → the
    // replayed state streams to the client via the normal snapshot path.
    Q_INVOKABLE void startReplay(const QString &nameOrPath);
    Q_INVOKABLE void stopReplay();
    // Map/datum broadcasts are demo/virtual-bus-only (the real Jetson carries
    // them on CAN) → no-ops in client mode.
    Q_INVOKABLE void setMapInfoBroadcast(double, double, double, int, int, int) {}
    Q_INVOKABLE void setMapDatumBroadcast(double, double) {}

signals:
    void runListChanged();
    // Client: a raw CAN frame forwarded by the bridge (feeds the Raw CAN Monitor).
    void rawFrameReceived(quint32 id, const QByteArray &payload, qint64 ts);

private slots:
    void onSnapshotTick();   // bridge: serialize + send
    void onReadyRead();      // both: receive datagrams
    // Bridge: buffer a raw frame from CanBridge::frameForRecord for forwarding.
    void onBridgeRawFrame(quint32 id, const QByteArray &payload, qint64 ts);

private:
    void sendCommand(quint8 type, const QByteArray &payload);

    Role         m_role;
    CanBridge   *m_can = nullptr;
    RunRecorder *m_rec = nullptr;          // bridge role: resolves run name → path
    QUdpSocket  *m_sock = nullptr;
    QTimer      *m_tick = nullptr;
    QTimer      *m_hello = nullptr;        // client keepalive
    QHostAddress m_peer;                   // bridge: learned client; client: bridge host
    quint16      m_snapshotPort = 0, m_commandPort = 0;
    bool         m_peerKnown = false;
    int          m_runListTick = 0;        // bridge: throttles run-list advertise

    // Client receive buffer
    QMutex            m_mutex;
    LatestValues      m_latest;
    QVector<LogEntry> m_logs;
    bool              m_haveSnapshot = false;
    QStringList       m_bridgeRunList;     // run names advertised by the bridge

    // Bridge: raw frames buffered from CanBridge::frameForRecord (queued from the
    // worker → runs on the tick thread, no lock needed), flushed to the client
    // each tick as a MAGIC_RAWFRAMES datagram so its Raw CAN Monitor works.
    struct RawFrame { quint32 id; QByteArray payload; qint64 ts; };
    QVector<RawFrame> m_rawBuf;
};
