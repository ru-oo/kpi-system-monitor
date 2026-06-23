#include "CanBridge.h"
#include "KpiData.h"

#include <QtSerialBus/QCanBus>
#include <QtSerialBus/QCanBusDeviceInfo>
#include <QRandomGenerator>
#include <QDebug>
#include <QtMath>
#include <QVariant>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

// ─────────────────────────────────────────────────────────────────────────
//  ENDIANNESS — valeo_project_can.dbc audit
//
//  Every signal in the current DBC is declared as Intel byte order:
//    `start_bit|length@1+`  → little-endian unsigned
//    `start_bit|length@1-`  → little-endian signed
//  No `@0` (Motorola/big-endian) signals exist.
//
//  Verified signals (id / signal / declaration → our helper):
//    0x100 Obstacle_Distance  : 0|16@1+  → getU16(p, 0)
//    0x100 Obstacle_Angle     : 16|16@1- → getI16(p, 2)
//    0x101 Vehicle_Speed      : 0|16@1-  → getI16(p, 0)
//    0x101 Steering_Angle     : 16|16@1- → getI16(p, 2)
//    0x102 Inference_Latency  : 0|16@1+  → getU16(p, 0)
//    0x102 Path_Error         : 48|16@1- → getI16(p, 6)
//    0x104 Route_Progress     : 0|16@1+  → getU16(p, 0)
//    0x104 Amcl_Error         : 16|16@1- → getI16(p, 2)
//    0x104 Mission_Success    : 32|16@1+ → getU16(p, 4)
//    0x1FF Timestamp_ms       : 16|32@1+ → getU32(p, 2)
//
//  If the firmware team ever ships a Motorola-encoded signal, the helper
//  below must switch on a per-signal basis. Until then, all multi-byte
//  reads use the LE path unconditionally.
// ─────────────────────────────────────────────────────────────────────────
namespace {
constexpr quint32 ID_ENCODER      = 0x020;   // STM32_Encoder_Feedback (Left/Right count)
constexpr quint32 ID_IMU          = 0x021;   // STM32_IMU_Feedback (yaw/gyroZ/roll/pitch)
constexpr quint32 ID_OBSTACLE     = 0x100;
constexpr quint32 ID_VEHICLE      = 0x101;
constexpr quint32 ID_REALTIME_KPI = 0x102;
constexpr quint32 ID_SYS_RESOURCE = 0x103;
constexpr quint32 ID_ROUTE_STATUS = 0x104;
constexpr quint32 ID_HARDWARE     = 0x105;
// Teammate's v3 firmware frames occupy 0x106 Planning_Status / 0x107
// Perception_Validation / 0x108 Map_Datum / 0x109 Network_Status. We adopt their
// Map_Datum at 0x108; our UI-only frames live above (0x10A+) so the real bus
// never collides. (Merged DBC: valeo_project_can.dbc.)
constexpr quint32 ID_PLANNING     = 0x106;   // Planning_Status (v3)
constexpr quint32 ID_PERCEPTION    = 0x107;   // Perception_Validation (v3)
constexpr quint32 ID_MAP_DATUM    = 0x108;   // Map_Datum (v3: Origin_Lat/Lon 1e-7)
constexpr quint32 ID_NETWORK      = 0x109;   // Network_Status (v3)
constexpr quint32 ID_LOCALIZATION = 0x10A;   // Localization_Status (+Loc_Lane_Dev)
constexpr quint32 ID_BEHAVIOR     = 0x10B;   // Behavior_State
constexpr quint32 ID_MAP_INFO     = 0x10C;   // Map_Info (multiplexed)
constexpr quint32 ID_EGO_POSE     = 0x10D;   // Ego_Pose (map frame)
constexpr quint32 ID_FAILSAFE_EV  = 0x1FF;
constexpr quint32 ID_UI_COMMAND   = 0x200;
constexpr quint32 ID_UI_GOAL_POSE = 0x201;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double rnd(double a, double b) {
    return a + QRandomGenerator::global()->generateDouble() * (b - a);
}

QString drivingStateName(quint8 v) {
    switch (v) {
        case 0: return "IDLE";   case 1: return "MANUAL"; case 2: return "AUTO";
        case 3: return "AVOID";  case 4: return "STOP";   case 5: return "ERROR";
        default: return "UNKNOWN";
    }
}
QString classIdName(quint8 v) {
    switch (v) {
        case 0:  return "person"; case 1: return "bike"; case 2: return "car";
        case 3:  return "truck";  default: return "obj";
    }
}
struct YoloModeDecode { QString opt; QString model; };
YoloModeDecode decodeYoloMode(quint8 v) {
    switch (v) {
        case 1: return { "FP32", "YOLO26s" };
        case 2: return { "FP16", "YOLO26s" };
        case 3: return { "INT8", "YOLO26s" };
        case 4: return { "INT8", "YOLO26n" };
        case 5: return { "OFF",  "LiDAR" };
        default: return { "OFF",  "" };
    }
}
quint8 encodeYoloMode(const QString &opt, const QString &model) {
    if (model == "YOLO26n" && opt == "INT8") return 4;
    if (opt == "FP32")                        return 1;
    if (opt == "FP16")                        return 2;
    if (opt == "INT8")                        return 3;
    return 0;
}
quint8 encodePrecisionCmd(const QString &mode) {
    if (mode == "FP32") return 1;
    if (mode == "FP16") return 2;
    if (mode == "INT8") return 3;
    return 0;
}
quint8 encodeModelCmd(const QString &model) {
    if (model == "YOLO26s") return 1;
    if (model == "YOLO26n") return 2;
    return 0;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────
CanBridge::CanBridge(KpiData *kpi, QObject *parent)
    : QObject(parent), m_kpi(kpi)
{
    m_uptime.start();
    m_lastBusStatsPush.start();

    // CRITICAL: value-member QObjects have no parent by default, so
    // moveToThread() on CanBridge does NOT move them. Without setParent(this)
    // they'd stay on the main thread and start() would emit the classic
    // "Timers cannot be started from another thread" warning.
    m_virt20ms.setParent(this);
    m_virt100ms.setParent(this);
    m_virt1000ms.setParent(this);
    m_reconnectTimer.setParent(this);
    m_replayTimer.setParent(this);
    m_virt20ms.setInterval(20);
    m_virt100ms.setInterval(100);
    m_virt1000ms.setInterval(1000);
    m_reconnectTimer.setInterval(1000);     // 1 s retry cadence
    m_reconnectTimer.setSingleShot(true);   // one shot per attempt
    m_replayTimer.setInterval(5);           // replay scheduler tick
    connect(&m_virt20ms,      &QTimer::timeout, this, &CanBridge::virtualTick20);
    connect(&m_virt100ms,     &QTimer::timeout, this, &CanBridge::virtualTick100);
    connect(&m_virt1000ms,    &QTimer::timeout, this, &CanBridge::virtualTick1000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &CanBridge::tryReconnect);
    connect(&m_replayTimer,    &QTimer::timeout, this, &CanBridge::replayTick);
    // E-stop BURST timer: re-send the current Estop_Command a few times for
    // reliable delivery, then stop (NOT a continuous stream — continuous 10 Hz
    // made the actuators buzz). Quiet (empty summary → no log). The Jetson must
    // latch the actuator stop until an explicit clear.
    m_estopRepeat.setParent(this);
    m_estopRepeat.setInterval(80);
    connect(&m_estopRepeat, &QTimer::timeout, this, [this]() {
        sendUiCommandImpl(0, 0, 0, 0, /*Estop=*/ m_estopLatched ? 1 : 0, 0, QString());
        if (--m_estopBurst <= 0) m_estopRepeat.stop();
    });
}

CanBridge::~CanBridge() {
    // m_dev should have been disposed in shutdown(); guard for safety.
    if (m_dev) {
        m_dev->disconnectDevice();
        delete m_dev;
        m_dev = nullptr;
    }
}

// Clean shutdown — runs on worker thread (via BlockingQueuedConnection from
// main). Stops timers + closes device while we still have the right thread
// affinity, then re-parents ourselves back to the main thread so the
// destructor (running on main after canThread.wait()) doesn't try to kill
// timers across threads.
void CanBridge::shutdown() {
    m_replayTimer.stop();
    m_replaying = false;
    stopVirtualBusImpl();
    closeInterfaceImpl();
    moveToThread(QCoreApplication::instance()->thread());
}

QString CanBridge::status() const {
    QMutexLocker lock(&m_mutex);
    return m_status;
}

void CanBridge::setStatus(const QString &s) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_status == s) return;
        m_status = s;
    }
    emit statusChanged();
}

// ── Public API — thread-safe entry points (bounce to worker thread) ─────
bool CanBridge::openInterface(const QString &plugin, const QString &deviceName) {
    if (QThread::currentThread() == thread()) {
        return openInterfaceImpl(plugin, deviceName);
    }
    bool ok = false;
    // Block until worker thread reports back so callers (main.cpp startup)
    // can decide whether to fall back to the virtual bus.
    QMetaObject::invokeMethod(this, [this, plugin, deviceName, &ok]() {
        ok = openInterfaceImpl(plugin, deviceName);
    }, Qt::BlockingQueuedConnection);
    return ok;
}
bool CanBridge::openSlcan(const QString &portName, int bitrate) {
    if (QThread::currentThread() == thread())
        return openSlcanImpl(portName, bitrate);
    bool ok = false;
    QMetaObject::invokeMethod(this, [this, portName, bitrate, &ok]() {
        ok = openSlcanImpl(portName, bitrate);
    }, Qt::BlockingQueuedConnection);
    return ok;
}
void CanBridge::closeInterface() {
    QMetaObject::invokeMethod(this, &CanBridge::closeInterfaceImpl, Qt::QueuedConnection);
}
void CanBridge::startVirtualBus() {
    QMetaObject::invokeMethod(this, &CanBridge::startVirtualBusImpl, Qt::QueuedConnection);
}
void CanBridge::stopVirtualBus() {
    QMetaObject::invokeMethod(this, &CanBridge::stopVirtualBusImpl, Qt::QueuedConnection);
}
void CanBridge::startReplay(const QString &path) {
    QMetaObject::invokeMethod(this, [this, path]() { startReplayImpl(path); }, Qt::QueuedConnection);
}
void CanBridge::stopReplay() {
    QMetaObject::invokeMethod(this, &CanBridge::stopReplayImpl, Qt::QueuedConnection);
}

void CanBridge::sendSetPrecision(const QString &mode) {
    QMetaObject::invokeMethod(this, [this, mode]() {
        sendUiCommandImpl(/*Set_Precision*/ 1, encodePrecisionCmd(mode), 0, 0, 0, 0,
                          QStringLiteral("Set precision → %1").arg(mode));
    }, Qt::QueuedConnection);
}
void CanBridge::sendSetModel(const QString &model) {
    QMetaObject::invokeMethod(this, [this, model]() {
        sendUiCommandImpl(/*Set_Model*/ 2, 0, encodeModelCmd(model), 0, 0, 0,
                          QStringLiteral("Set model → %1").arg(model));
    }, Qt::QueuedConnection);
}
// (sendAmclInit removed — AMCL is no longer used; localization is EKF-based.
//  The DBC Amcl_Command signal stays for firmware compatibility.)
void CanBridge::sendEStop() {
    QMetaObject::invokeMethod(this, [this]() {
        // E-stop is carried ONLY by Estop_Command (DBC CM_ BO_ 512 / SG_ 512):
        // Command_Id 4 is now "Reserved", so we send No_Command (0) and set the
        // dedicated Estop_Command=1. The Jetson must translate that request to
        // STM32_Control_Command.Control_Flag=E_Stop for the actuator stop.
        //
        // Engage: latch the UI state and send Estop_Command=1 once + a short burst
        // for reliable delivery (then stop). The Jetson must hold the actuator
        // stop until clearEStop(). (Was a continuous 10 Hz stream → actuator buzz.)
        m_estopLatched = true;
        sendUiCommandImpl(/*No_Command*/ 0, 0, 0, 0, /*Estop=Request*/ 1, 0,
                          QStringLiteral("E-STOP engaged"));
        m_estopBurst = 3;                 // 1 sent now + 3 more @80ms ≈ 4 frames / 240 ms
        m_estopRepeat.start();
    }, Qt::QueuedConnection);
}
void CanBridge::clearEStop() {
    QMetaObject::invokeMethod(this, [this]() {
        // Release: Estop_Command=0 once + a short burst (so the clear isn't lost),
        // then stop. The burst timer sends 0 because m_estopLatched is now false.
        m_estopLatched = false;
        sendUiCommandImpl(/*No_Command*/ 0, 0, 0, 0, /*Estop=Clear*/ 0, 0,
                          QStringLiteral("E-STOP cleared"));
        m_estopBurst = 3;
        m_estopRepeat.start();
    }, Qt::QueuedConnection);
}
void CanBridge::sendSetDestination(int destId, const QString &name) {
    quint8 dest = (destId >= 0 && destId <= 255) ? static_cast<quint8>(destId) : 0;
    QMetaObject::invokeMethod(this, [this, dest, name]() {
        sendUiCommandImpl(/*Set_Destination*/ 5, 0, 0, 0, 0, dest,
                          QStringLiteral("Set destination → %1").arg(name));
    }, Qt::QueuedConnection);
}
void CanBridge::sendSetGoalPose(double distM, double latM, double yawDeg) {
    // TX only — the goal marker state lives on KpiData (main thread); QML sets
    // it via kpiData.setGoal() at the same call site. Bounce the CAN TX to the
    // worker event loop.
    QMetaObject::invokeMethod(this, [this, distM, latM, yawDeg]() {
        sendGoalPoseImpl(distM, latM, yawDeg);
    }, Qt::QueuedConnection);
}
void CanBridge::sendCancelGoal() {
    QMetaObject::invokeMethod(this, [this]() {
        sendCancelGoalImpl();
    }, Qt::QueuedConnection);
}

// ── Worker-thread implementations ───────────────────────────────────────
bool CanBridge::openInterfaceImpl(const QString &plugin, const QString &deviceName) {
    closeInterfaceImpl();

    QString errorString;
    QCanBusDevice *dev = QCanBus::instance()->createDevice(plugin.toLocal8Bit(),
                                                           deviceName, &errorString);
    if (!dev) {
        setStatus(QStringLiteral("error: %1").arg(errorString));
        qWarning() << "[CanBridge] createDevice failed:" << errorString;
        return false;
    }
    if (!dev->connectDevice()) {
        setStatus(QStringLiteral("connect failed: %1").arg(dev->errorString()));
        qWarning() << "[CanBridge] connectDevice failed:" << dev->errorString();
        delete dev;
        return false;
    }

    m_dev = dev;
    m_currentPlugin = plugin;
    m_currentDevice = deviceName;
    m_wantAutoReconnect = true;
    m_reconnectAttempts = 0;
    connect(m_dev, &QCanBusDevice::framesReceived, this, &CanBridge::onFramesReceived);
    connect(m_dev, &QCanBusDevice::framesWritten,  this, &CanBridge::onFramesWritten);
    connect(m_dev, &QCanBusDevice::errorOccurred,  this, &CanBridge::onErrorOccurred);
    connect(m_dev, &QCanBusDevice::stateChanged,   this, &CanBridge::onStateChanged);

    QVariant br = m_dev->configurationParameter(QCanBusDevice::BitRateKey);
    int bitrate = br.isValid() ? br.toInt() : m_defaultBitrate;
    {
        QMutexLocker lock(&m_mutex);
        m_latest.canBitrate = bitrate;
        m_latest.canOnline = true;
        m_latest.busStatsDirty = true;
    }

    setStatus(QStringLiteral("online: %1/%2").arg(plugin, deviceName));
    queueLog("CAN_UP",
             QString("CAN bus online · %1/%2").arg(plugin, deviceName),
             "good", "0x000");
    return true;
}

void CanBridge::closeInterfaceImpl() {
    // Caller-initiated close: don't try to reconnect afterwards.
    m_wantAutoReconnect = false;
    m_reconnectTimer.stop();
    if (m_dev) {
        m_dev->disconnectDevice();
        m_dev->deleteLater();
        m_dev = nullptr;
        setStatus("disconnected");
    }
    if (m_serial) {
        if (m_serial->isOpen()) { m_serial->write("C\r"); m_serial->flush(); m_serial->close(); } // slcan close
        m_serial->deleteLater();
        m_serial = nullptr;
        m_slcanRxBuf.clear();
        setStatus("disconnected");
    }
}

// ── slcan (LAWICEL) over serial ─────────────────────────────────────────────
// CANable in slcan mode enumerates as a COM port (USB-CDC). Qt has no slcan CAN
// plugin, so we drive the ASCII protocol on QSerialPort directly:
//   setup: "C\r" (close) → "S<n>\r" (bitrate) → "O\r" (open)
//   RX:    "tIIILDD…\r" (std) / "TIIIIIIIILDD…\r" (ext) → QCanBusFrame → processFrame()
//   TX:    same encoding written back to the port (see writeCanFrame/slcanEncode)
bool CanBridge::openSlcanImpl(const QString &portName, int bitrate) {
    closeInterfaceImpl();

    m_serial = new QSerialPort(this);
    m_serial->setPortName(portName);     // "COM3" (Windows) / "/dev/ttyACM0" (Linux)
    m_serial->setBaudRate(115200);       // USB-CDC: the device ignores it, but Qt needs a value
    if (!m_serial->open(QIODevice::ReadWrite)) {
        setStatus(QStringLiteral("slcan open failed: %1").arg(m_serial->errorString()));
        qWarning() << "[CanBridge] slcan open failed on" << portName << ":" << m_serial->errorString();
        m_serial->deleteLater();
        m_serial = nullptr;
        return false;
    }
    connect(m_serial, &QSerialPort::readyRead, this, &CanBridge::onSlcanReadyRead);

    m_slcanRxBuf.clear();
    m_serial->write("C\r");                                                        // ensure closed
    m_serial->write(QByteArray("S") + QByteArray::number(slcanBitrateCode(bitrate)) + "\r");
    m_serial->write("O\r");                                                        // open (normal mode)
    m_serial->flush();

    m_currentPlugin = "slcan";
    m_currentDevice = portName;
    {
        QMutexLocker lock(&m_mutex);
        m_latest.canBitrate = bitrate;
        m_latest.canOnline = true;
        m_latest.busStatsDirty = true;
    }
    setStatus(QStringLiteral("online: slcan/%1").arg(portName));
    queueLog("CAN_UP", QStringLiteral("slcan online · %1 @ %2 bps").arg(portName).arg(bitrate),
             "good", "0x000");
    return true;
}

void CanBridge::onSlcanReadyRead() {
    if (!m_serial) return;
    m_slcanRxBuf += m_serial->readAll();
    int nl;
    while ((nl = m_slcanRxBuf.indexOf('\r')) >= 0) {
        const QByteArray line = m_slcanRxBuf.left(nl);
        m_slcanRxBuf.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        const char tag = line.at(0);
        // Frame lines only (t/T data, r/R remote); ignore command acks
        // ("\r" alone, z/Z TX-ok, V/N version/serial, "\a" error).
        if (tag != 't' && tag != 'T' && tag != 'r' && tag != 'R') continue;
        const bool ext = (tag == 'T' || tag == 'R');
        const bool rtr = (tag == 'r' || tag == 'R');
        const int idLen = ext ? 8 : 3;
        if (line.size() < 1 + idLen + 1) continue;                  // need id + DLC nibble
        bool ok = false;
        const quint32 id = line.mid(1, idLen).toUInt(&ok, 16);
        if (!ok) continue;
        const int dlc = QByteArray(1, line.at(1 + idLen)).toInt(&ok, 16);
        if (!ok || dlc < 0 || dlc > 8) continue;
        QByteArray data;
        if (!rtr) {
            const QByteArray hex = line.mid(1 + idLen + 1, dlc * 2);
            if (hex.size() < dlc * 2) continue;                     // truncated
            data = QByteArray::fromHex(hex);
        }
        QCanBusFrame frame(id, data);
        frame.setExtendedFrameFormat(ext);
        if (rtr) frame.setFrameType(QCanBusFrame::RemoteRequestFrame);
        processFrame(frame);                                        // same decode path as a real bus
    }
}

// Encode a CAN frame as an slcan line (correct-case tag, uppercase hex, '\r').
QByteArray CanBridge::slcanEncode(quint32 id, const QByteArray &payload) {
    const int dlc = qMin(payload.size(), 8);
    QByteArray s;
    if (id <= 0x7FF) { s += 't'; s += QByteArray::number(id, 16).rightJustified(3, '0').toUpper(); }
    else             { s += 'T'; s += QByteArray::number(id, 16).rightJustified(8, '0').toUpper(); }
    s += QByteArray::number(dlc);                       // single nibble 0..8
    s += payload.left(dlc).toHex().toUpper();
    s += '\r';
    return s;
}

int CanBridge::slcanBitrateCode(int bitrate) {
    switch (bitrate) {
        case 10000:   return 0;  case 20000:  return 1;  case 50000:   return 2;
        case 100000:  return 3;  case 125000: return 4;  case 250000:  return 5;
        case 500000:  return 6;  case 800000: return 7;  case 1000000: return 8;
        default:      return 6;  // 500 kbit/s
    }
}

// Central CAN TX: slcan if in slcan mode, else QCanBus. Returns false only when
// there is NO real device (virtual-bus mode) so the caller can do demo accounting.
bool CanBridge::writeCanFrame(quint32 id, const QByteArray &payload) {
    if (m_serial && m_serial->isOpen()) {
        const QByteArray line = slcanEncode(id, payload);
        m_txTimer.restart();
        m_txPending = true;
        if (m_serial->write(line) == line.size()) { m_serial->flush(); ++m_txFrames; }
        else { m_txPending = false; qWarning() << "[CanBridge] slcan write failed:" << m_serial->errorString(); }
        return true;
    }
    if (m_dev) {
        QCanBusFrame frame(id, payload);
        m_txTimer.restart();
        m_txPending = true;
        if (m_dev->writeFrame(frame)) { ++m_txFrames; }
        else { m_txPending = false; qWarning() << "[CanBridge] writeFrame failed:" << m_dev->errorString(); }
        return true;
    }
    return false;
}

// QCanBusDevice::stateChanged — listens for the driver going to
// UnconnectedState (bus-off, cable yank, transceiver fault, etc.). If we
// were running on this interface and the user didn't intentionally close
// it, schedule a reconnect attempt 1 s later. Repeats with exponential
// back-off up to ~30 s, then resets and keeps trying.
void CanBridge::onStateChanged(QCanBusDevice::CanBusDeviceState state) {
    if (state == QCanBusDevice::ConnectedState) {
        m_reconnectAttempts = 0;
        queueLog("CAN_UP", "Bus link recovered", "good", "0x000");
        QMutexLocker lock(&m_mutex);
        m_latest.canOnline = true;
        m_latest.busStatsDirty = true;
        return;
    }
    if (state == QCanBusDevice::UnconnectedState && m_wantAutoReconnect) {
        queueLog("CAN_DOWN",
                 QString("Bus off (state=unconnected) · retry #%1 in 1 s")
                     .arg(m_reconnectAttempts + 1),
                 "warning", "0x000");
        {
            QMutexLocker lock(&m_mutex);
            m_latest.canOnline = false;
            m_latest.busStatsDirty = true;
        }
        if (m_dev) {
            // Drop the broken device; tryReconnect() will rebuild it.
            disconnect(m_dev, nullptr, this, nullptr);
            m_dev->deleteLater();
            m_dev = nullptr;
        }
        setStatus("recovering");
        if (!m_reconnectTimer.isActive()) m_reconnectTimer.start();
    }
}

// 1 s tick after a disconnect. Try to re-open the same plugin/device. On
// failure, back off and try again. Capped at ~30 s so we don't hammer the
// driver if the cable is genuinely unplugged.
void CanBridge::tryReconnect() {
    if (!m_wantAutoReconnect) return;
    if (m_currentPlugin.isEmpty() || m_currentDevice.isEmpty()) return;

    ++m_reconnectAttempts;
    if (openInterfaceImpl(m_currentPlugin, m_currentDevice)) {
        // success — onStateChanged(ConnectedState) will fire and clear counter
        return;
    }
    int backoffMs = 1000 * (m_reconnectAttempts < 30 ? m_reconnectAttempts : 30);
    queueLog("CAN_RETRY",
             QString("Reconnect failed · next attempt in %1 s").arg(backoffMs / 1000),
             "warning", "0x000");
    m_reconnectTimer.setInterval(backoffMs);
    m_reconnectTimer.start();
}

void CanBridge::startVirtualBusImpl() {
    if (!m_virt20ms.isActive())   m_virt20ms.start();
    if (!m_virt100ms.isActive())  m_virt100ms.start();
    if (!m_virt1000ms.isActive()) m_virt1000ms.start();
    setStatus("virtual");
    {
        QMutexLocker lock(&m_mutex);
        m_latest.canBitrate = m_defaultBitrate;
        m_latest.canOnline = true;
        m_latest.busStatsDirty = true;
    }
    queueLog("CAN_UP",
             QString("Virtual CAN bus (in-process) · %1 kbps")
                 .arg(m_defaultBitrate / 1000),
             "info", "0x000");
}

void CanBridge::stopVirtualBusImpl() {
    m_virt20ms.stop();
    m_virt100ms.stop();
    m_virt1000ms.stop();
}

// ── Replay (worker thread) ──────────────────────────────────────────────
void CanBridge::startReplayImpl(const QString &path) {
    if (m_replaying) stopReplayImpl();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        queueLog("REPLAY", QStringLiteral("Cannot open %1").arg(path), "warning", "replay");
        return;
    }
    m_replayRows.clear();
    double rgX = 0, rgY = 0, rgYaw = 0; bool hasReplayGoal = false;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith("# goal,")) {                           // the run's destination
            const QStringList g = line.split(',');
            if (g.size() >= 4) { rgX = g[1].toDouble(); rgY = g[2].toDouble();
                                 rgYaw = g[3].toDouble(); hasReplayGoal = true; }
            continue;
        }
        if (line.isEmpty() || line.startsWith('#')) continue;       // header/comment
        const QStringList c = line.split(',');
        if (c.size() < 4) continue;
        bool okTs = false, okId = false;
        const qint64 ts = c[0].toLongLong(&okTs);
        const quint32 id = c[1].toUInt(&okId, 16);
        if (!okTs || !okId) continue;                                // skip header row
        m_replayRows.push_back({ ts, id, QByteArray::fromHex(c[3].toLatin1()) });
    }
    f.close();
    if (m_replayRows.isEmpty()) {
        queueLog("REPLAY", QStringLiteral("No frames in %1").arg(path), "warning", "replay");
        return;
    }

    // Pause the live virtual bus during replay; remember to restore it.
    m_replayWasVirtual = m_virt20ms.isActive();
    stopVirtualBusImpl();

    m_replayIdx = 0;
    m_replaying = true;
    { QMutexLocker lock(&m_mutex); m_latest.replaying = true; }   // forwarded to client
    emit replayingChanged(true);
    if (hasReplayGoal) emit replayGoalLoaded(rgX, rgY, rgYaw);   // restore the run's destination marker
    queueLog("REPLAY", QStringLiteral("Replaying %1 frames").arg(m_replayRows.size()), "info", "replay");
    m_replayClock.restart();
    m_replayTimer.start();
}

void CanBridge::replayTick() {
    if (!m_replaying || m_replayRows.isEmpty()) { m_replayTimer.stop(); return; }
    const qint64 base = m_replayRows.first().ts;
    const qint64 elapsed = m_replayClock.elapsed();
    while (m_replayIdx < m_replayRows.size()
           && (m_replayRows[m_replayIdx].ts - base) <= elapsed) {
        const ReplayRow &r = m_replayRows[m_replayIdx];
        processFrame(QCanBusFrame(r.id, r.bytes));
        ++m_replayIdx;
    }
    if (m_replayIdx >= m_replayRows.size()) stopReplayImpl();
}

void CanBridge::stopReplayImpl() {
    if (!m_replaying) return;
    m_replayTimer.stop();
    m_replaying = false;
    { QMutexLocker lock(&m_mutex); m_latest.replaying = false; }   // forwarded to client
    m_replayRows.clear();
    m_replayIdx = 0;
    emit replayingChanged(false);
    queueLog("REPLAY", QStringLiteral("Replay finished"), "info", "replay");
    if (m_replayWasVirtual) startVirtualBusImpl();   // resume live sim
}

void CanBridge::onErrorOccurred(QCanBusDevice::CanBusError err) {
    Q_UNUSED(err)
    if (m_dev) {
        qWarning() << "[CanBridge] bus error:" << m_dev->errorString();
        setStatus(QStringLiteral("bus error: %1").arg(m_dev->errorString()));
    }
}

void CanBridge::onFramesReceived() {
    if (!m_dev) return;
    while (m_dev->framesAvailable())
        processFrame(m_dev->readFrame());
}

void CanBridge::onFramesWritten(qint64 framesCount) {
    Q_UNUSED(framesCount)
    if (m_txPending) {
        m_lastTxLatencyMs = m_txTimer.nsecsElapsed() / 1.0e6;
        m_txPending = false;
        publishBusStats();
    }
}

void CanBridge::processFrame(const QCanBusFrame &f) {
    if (!f.isValid()) return;
    ++m_rxFrames;
    const QByteArray p = f.payload();
    // Tap the raw frame for the recorder (skip while replaying so we don't
    // re-record what we're playing back). Queued to the RunRecorder on main.
    if (!m_replaying)
        emit frameForRecord(f.frameId(), p, QDateTime::currentMSecsSinceEpoch());
    switch (f.frameId()) {
        case ID_ENCODER:      decodeStm32Encoder(p);    break;
        case ID_IMU:          decodeStm32Imu(p);        break;
        case ID_OBSTACLE:     decodeObstacle(p);        break;
        case ID_VEHICLE:      decodeVehicleStatus(p);   break;
        case ID_REALTIME_KPI: decodeRealtimeKpi(p);     break;
        case ID_SYS_RESOURCE: decodeSystemResource(p);  break;
        case ID_ROUTE_STATUS: decodeRouteStatus(p);     break;
        case ID_HARDWARE:     decodeHardwareInfo(p);    break;
        case ID_PLANNING:     decodePlanningStatus(p);  break;
        case ID_PERCEPTION:   decodePerceptionValidation(p); break;
        case ID_NETWORK:      decodeNetworkStatus(p);   break;
        case ID_MAP_INFO:     decodeMapInfo(p);         break;
        case ID_EGO_POSE:     decodeEgoPose(p);         break;
        case ID_MAP_DATUM:    decodeMapDatum(p);        break;
        case ID_LOCALIZATION: decodeLocalizationStatus(p); break;
        case ID_BEHAVIOR:     decodeBehaviorState(p);   break;
        case ID_FAILSAFE_EV:  decodeFailsafeEvent(p);   break;
        default: break;
    }
    if (m_lastBusStatsPush.elapsed() >= 200) publishBusStats();
}

// ── DBC decoders ────────────────────────────────────────────────────────
void CanBridge::decodeObstacle(const QByteArray &p) {
    if (p.size() < 8) return;
    double dist = getU16(p, 0) * 0.01;
    double ang  = getI16(p, 2) * 0.1;
    quint8 cls  = getU8 (p, 4);
    double conf = getU8 (p, 5) * 0.5;
    quint8 lvl  = getU8 (p, 6);
    quint8 ctr  = getU8 (p, 7);

    {
        QMutexLocker lock(&m_mutex);
        m_latest.obstacleDistM     = dist;
        m_latest.obstacleAngleDeg  = ang;
        m_latest.obstacleConf01    = conf / 100.0;
        m_latest.obstacleClassName = classIdName(cls);
        if (lvl >= 1 && lvl <= 4) m_latest.failsafeLevel = lvl;
        m_latest.obstacleDirty = true;
    }
    noteRxFromAlive(ID_OBSTACLE, ctr);

    qint64 now = m_uptime.elapsed();
    if (now - m_lastPerceptLogMs > 1500) {
        m_lastPerceptLogMs = now;
        queueLog("PERCEPT",
                 QString("Obstacle: %1 · %2m · %3° · conf %4")
                     .arg(classIdName(cls)).arg(dist, 0, 'f', 1)
                     .arg(ang, 0, 'f', 0).arg(conf / 100.0, 0, 'f', 2),
                 "info", "0x100");
    }
}

void CanBridge::decodeVehicleStatus(const QByteArray &p) {
    if (p.size() < 8) return;
    double speedMs = getI16(p, 0) * 0.01;
    double steer   = getI16(p, 2) * 0.1;
    quint8 driv    = getU8 (p, 4);
    quint8 yolo    = getU8 (p, 5);
    quint8 enable  = getU8 (p, 6);
    quint8 ctr     = getU8 (p, 7);

    auto ym = decodeYoloMode(yolo);
    const QString driveName = drivingStateName(driv);

    {
        QMutexLocker lock(&m_mutex);
        m_latest.speedKmh        = speedMs * 3.6;
        m_latest.steeringDeg     = steer;
        m_latest.driveStateName  = driveName;
        m_latest.optMode         = ym.opt;
        m_latest.yoloModel       = ym.model;
        m_latest.controlEnable   = enable;
        m_latest.vehicleDirty    = true;

        // Keep our copy in sync so the virtual bus's 0x101 echo encodes
        // the right Yolo_Mode without crossing back through the UI thread.
        if (!ym.opt.isEmpty()   && ym.opt   != "OFF")   m_currentOptMode   = ym.opt;
        if (!ym.model.isEmpty() && ym.model != "LiDAR") m_currentYoloModel = ym.model;
    }
    noteRxFromAlive(ID_VEHICLE, ctr);

    if (!m_lastDriveState.isEmpty() && m_lastDriveState != driveName)
        queueLog("DRIVE", QString("Drive mode → %1").arg(driveName), "info", "0x101");
    m_lastDriveState = driveName;

    if (!ym.opt.isEmpty() && ym.opt != "OFF" && !m_lastOptModeLog.isEmpty() && m_lastOptModeLog != ym.opt)
        queueLog("OPT_MODE",
            QString("Optimization → %1 · re-loading engine").arg(ym.opt), "info", "0x101");
    if (!ym.opt.isEmpty() && ym.opt != "OFF") m_lastOptModeLog = ym.opt;

    if (!ym.model.isEmpty() && ym.model != "LiDAR" && !m_lastYoloModelLog.isEmpty() && m_lastYoloModelLog != ym.model)
        queueLog("MODEL",
            QString("Model → %1 (%2)").arg(ym.model, ym.model == "YOLO26n" ? "Light" : "Heavy"),
            "info", "0x101");
    if (!ym.model.isEmpty() && ym.model != "LiDAR") m_lastYoloModelLog = ym.model;
}

void CanBridge::decodeRealtimeKpi(const QByteArray &p) {
    if (p.size() < 8) return;
    double inf  = getU16(p, 0);
    double gpu  = getU8 (p, 2);
    double cpu  = getU8 (p, 3);
    double temp = getU8 (p, 4);
    quint8 ctr  = getU8 (p, 5);
    double path = getI16(p, 6);

    {
        QMutexLocker lock(&m_mutex);
        m_latest.inferenceMs  = inf;
        m_latest.gpuPct       = gpu;
        m_latest.cpuPct       = cpu;
        m_latest.gpuTempC     = temp;
        m_latest.pathErrorMm  = path;
        m_latest.kpiDirty     = true;
    }
    noteRxFromAlive(ID_REALTIME_KPI, ctr);
}

void CanBridge::decodeSystemResource(const QByteArray &p) {
    if (p.size() < 8) return;
    double ramPct  = getU8(p, 0);
    double swapPct = getU8(p, 1);
    int    sessHz  = getU8(p, 2);
    double busLoad = getU8(p, 3);
    double canLoss = getU8(p, 4) * 0.1;
    quint8 pi5     = getU8(p, 5);
    quint8 camera  = getU8(p, 6);
    quint8 ctr     = getU8(p, 7);

    {
        QMutexLocker lock(&m_mutex);
        m_latest.ramPct        = ramPct;
        m_latest.swapPct       = swapPct;
        m_latest.sessionRateHz = sessHz;
        m_latest.busLoadPct    = busLoad;
        m_latest.canLossPct    = canLoss;
        m_latest.pi5StatusCode = pi5;
        m_latest.cameraStatus  = camera;
        m_latest.sysDirty      = true;
    }
    noteRxFromAlive(ID_SYS_RESOURCE, ctr);
}

void CanBridge::decodeRouteStatus(const QByteArray &p) {
    if (p.size() < 8) return;
    double progress = getU16(p, 0) * 0.1;
    double amclErr  = getI16(p, 2) * 0.01;
    double mission  = getU16(p, 4) * 0.1;
    quint8 state    = getU8 (p, 6);
    quint8 ctr      = getU8 (p, 7);

    {
        QMutexLocker lock(&m_mutex);
        m_latest.routeProgressM    = progress;
        m_latest.amclErrorM        = amclErr;
        m_latest.missionSuccessPct = mission;
        m_latest.routeState        = state;
        m_latest.routeDirty        = true;
    }
    noteRxFromAlive(ID_ROUTE_STATUS, ctr);
}

void CanBridge::decodeHardwareInfo(const QByteArray &p) {
    if (p.size() < 8) return;
    quint8 model = getU8(p, 0);
    double memGb = getU8(p, 1);
    QMutexLocker lock(&m_mutex);
    m_latest.jetsonModelCode = model;
    m_latest.orinMemoryGb    = memGb;
    m_latest.hardwareDirty   = true;
}

// 0x10C Map_Info — multiplexed. mux byte0: 0 = geometry (origin+resolution),
// 1 = size (width+height). version (byte6) + counter (byte7) shared. Only
// metadata travels on CAN — never the image. We cache both halves and publish.
void CanBridge::decodeMapInfo(const QByteArray &p) {
    if (p.size() < 8) return;
    const quint8 mux = getU8(p, 0);
    m_miVersion = getU8(p, 6);
    if (mux == 0) {
        m_miOriginX    = getI16(p, 1) * 0.01;
        m_miOriginY    = getI16(p, 3) * 0.01;
        m_miResolution = getU8 (p, 5) * 0.001;
    } else if (mux == 1) {
        m_miWidth  = getU16(p, 1);
        m_miHeight = getU16(p, 3);
    }
    QMutexLocker lock(&m_mutex);
    m_latest.mapOriginX    = m_miOriginX;
    m_latest.mapOriginY    = m_miOriginY;
    m_latest.mapResolution = m_miResolution;
    m_latest.mapWidth      = m_miWidth;
    m_latest.mapHeight     = m_miHeight;
    m_latest.mapVersion    = m_miVersion;
    m_latest.mapInfoDirty  = true;
}

// 0x10D Ego_Pose (ID_EGO_POSE) — map-frame vehicle pose (x,y signed 0.01 m, yaw 0.1 deg).
void CanBridge::decodeEgoPose(const QByteArray &p) {
    if (p.size() < 8) return;
    const double x   = getI16(p, 0) * 0.01;
    const double y   = getI16(p, 2) * 0.01;
    const double yaw = getI16(p, 4) * 0.1;
    QMutexLocker lock(&m_mutex);
    m_latest.egoX = x; m_latest.egoY = y; m_latest.egoYaw = yaw;
    m_latest.egoDirty = true;
}

// 0x21 STM32_IMU_Feedback — yaw/gyroZ/roll/pitch, each int16 @ 0.01 LE.
void CanBridge::decodeStm32Imu(const QByteArray &p) {
    if (p.size() < 8) return;
    const double yaw   = getI16(p, 0) * 0.01;
    const double gyroZ = getI16(p, 2) * 0.01;
    const double roll  = getI16(p, 4) * 0.01;
    const double pitch = getI16(p, 6) * 0.01;
    QMutexLocker lock(&m_mutex);
    m_latest.imuYaw = yaw; m_latest.imuGyroZ = gyroZ;
    m_latest.imuRoll = roll; m_latest.imuPitch = pitch;
    m_latest.imuDirty = true;
}

// 0x20 STM32_Encoder_Feedback — Left/Right wheel tick counts, int32 LE.
void CanBridge::decodeStm32Encoder(const QByteArray &p) {
    if (p.size() < 8) return;
    const qint32 left  = (qint32)getU32(p, 0);
    const qint32 right = (qint32)getU32(p, 4);
    QMutexLocker lock(&m_mutex);
    m_latest.encLeft = left; m_latest.encRight = right;
    m_latest.encDirty = true;
}

// 0x106 Planning_Status: PathPlan_Last_ms u16@0, PathPlan_Success_Runs u8@2,
// PathPlan_Total_Runs u8@3, Planning_State u8@4 [0..3].
void CanBridge::decodePlanningStatus(const QByteArray &p) {
    if (p.size() < 8) return;
    const double lastMs  = getU16(p, 0);
    const int    success = (quint8)p[2];
    const int    total   = (quint8)p[3];
    const int    state   = (quint8)p[4];
    QMutexLocker lock(&m_mutex);
    m_latest.planLastMs = lastMs; m_latest.planSuccessRuns = success;
    m_latest.planTotalRuns = total; m_latest.planState = state;
    m_latest.planningDirty = true;
}

// 0x107 Perception_Validation: Perception_Detected_Runs u8@0,
// Perception_Total_Runs u8@1, False_Positive_Count u8@2,
// Trigger_Accuracy u8@3 (scale 0.5, %).
void CanBridge::decodePerceptionValidation(const QByteArray &p) {
    if (p.size() < 8) return;
    const int    detected = (quint8)p[0];
    const int    total    = (quint8)p[1];
    const int    falsePos = (quint8)p[2];
    const double trigAcc  = (quint8)p[3] * 0.5;
    QMutexLocker lock(&m_mutex);
    m_latest.percDetectedRuns = detected; m_latest.percTotalRuns = total;
    m_latest.percFalsePos = falsePos; m_latest.percTriggerAccPct = trigAcc;
    m_latest.perceptionDirty = true;
}

// 0x109 Network_Status: Wifi_Ping_ms u16@0, Network_Loss_Rate u8@2 (scale 0.1, %),
// Wifi_Rssi_dBm i8@3 (signed), Network_Status u8@4 [0..3].
void CanBridge::decodeNetworkStatus(const QByteArray &p) {
    if (p.size() < 8) return;
    const double ping  = getU16(p, 0);
    const double loss  = (quint8)p[2] * 0.1;
    const int    rssi  = (qint8)(quint8)p[3];   // signed dBm
    const int    state = (quint8)p[4];
    QMutexLocker lock(&m_mutex);
    m_latest.netWifiPingMs = ping; m_latest.netLossRatePct = loss;
    m_latest.netRssiDbm = rssi; m_latest.netState = state;
    m_latest.networkDirty = true;
}

// 0x108 Map_Datum — local-frame origin in WGS84 (int32 1e-7 deg). The UI uses
// this to convert the HD lane WGS84 geometry to local ENU metres at runtime.
void CanBridge::decodeMapDatum(const QByteArray &p) {
    if (p.size() < 8) return;
    const double lat = (qint32)getU32(p, 0) * 1e-7;
    const double lon = (qint32)getU32(p, 4) * 1e-7;
    QMutexLocker lock(&m_mutex);
    m_latest.datumLat = lat; m_latest.datumLon = lon; m_latest.datumDirty = true;
}

// 0x10A Localization_Status (B1) — TODO(CAN): firmware does not send this yet;
// decoder is wired so the pill switches to real data the moment 0x10A appears.
void CanBridge::decodeLocalizationStatus(const QByteArray &p) {
    if (p.size() < 8) return;
    QMutexLocker lock(&m_mutex);
    m_latest.locMode     = getU8(p, 0);
    m_latest.locQuality  = getU8(p, 1) / 100.0;
    m_latest.locLaneDevMm = getI16(p, 2);   // B3
    m_latest.locDirty    = true;
}

// 0x10B Behavior_State (B2) — TODO(CAN): firmware does not send this yet.
void CanBridge::decodeBehaviorState(const QByteArray &p) {
    if (p.size() < 8) return;
    QMutexLocker lock(&m_mutex);
    m_latest.behaviorMode  = getU8(p, 0);
    m_latest.behaviorDirty = true;
}

void CanBridge::decodeFailsafeEvent(const QByteArray &p) {
    if (p.size() < 8) return;
    quint8  evCode = getU8 (p, 0);
    quint8  reason = getU8 (p, 1);
    quint32 ts     = getU32(p, 2);
    quint8  lvl    = getU8 (p, 6);
    Q_UNUSED(ts)

    {
        QMutexLocker lock(&m_mutex);
        m_latest.fsEventCode  = evCode;
        m_latest.fsReasonCode = reason;
        m_latest.fsLevel      = lvl;
        m_latest.failsafeEventDirty = true;
        if (lvl >= 1 && lvl <= 4) m_latest.failsafeLevel = lvl;
    }

    if (lvl != m_lastFailsafeLevel) {
        QString code, msg, sev;
        if (lvl >= 4)      { code = "EMERGENCY"; msg = "Emergency stop · motor PWM = 0"; sev = "critical"; }
        else if (lvl == 3) { code = "AI_BYPASS"; msg = "GPU>95%, infer>300ms · LiDAR DBSCAN only"; sev = "warning"; }
        else if (lvl == 2) { code = "OVERLOAD";  msg = "GPU>85%, infer>150ms · Heavy → Light"; sev = "warning"; }
        else               { code = "RECOVER";   msg = "Recovered to normal · resume Heavy"; sev = "good"; }
        queueLog(code, msg, sev, "0x1FF");
        m_lastFailsafeLevel = lvl;
    }
}

void CanBridge::noteRxFromAlive(quint32 frameId, quint8 counter) {
    AliveTrack *t = nullptr;
    switch (frameId) {
        case ID_OBSTACLE:     t = &m_alive100; break;
        case ID_VEHICLE:      t = &m_alive101; break;
        case ID_REALTIME_KPI: t = &m_alive102; break;
        case ID_SYS_RESOURCE: t = &m_alive103; break;
        case ID_ROUTE_STATUS: t = &m_alive104; break;
        default: return;
    }
    t->last = counter;
    t->seen = true;
}

void CanBridge::publishBusStats() {
    m_lastBusStatsPush.restart();
    QMutexLocker lock(&m_mutex);
    m_latest.txLatencyMs   = m_lastTxLatencyMs;
    m_latest.framesRx      = m_rxFrames;
    m_latest.framesTx      = m_txFrames;
    m_latest.uptimeMs      = m_uptime.elapsed();
    m_latest.canOnline     = true;
    m_latest.busStatsDirty = true;
}

// ── 0x200 UI_Command TX ─────────────────────────────────────────────────
void CanBridge::sendUiCommandImpl(quint8 cmdId, quint8 precision, quint8 model,
                                  quint8 amcl, quint8 estop, quint8 destination,
                                  const QString &logSummary)
{
    // Update our local view of the current selection BEFORE building the
    // virtual-bus 0x101 echo. Otherwise the worker would keep producing the
    // old Yolo_Mode for one more cycle, which the UI poll would then apply
    // and overwrite the freshly-clicked button. (On real CAN, the Jetson is
    // expected to start broadcasting the new Yolo_Mode shortly after seeing
    // our 0x200; this local update gives the same effect during the
    // round-trip window so the button stays highlighted.)
    if (cmdId == 1 /*Set_Precision*/) {
        QString opt = (precision == 1) ? QStringLiteral("FP32")
                    : (precision == 2) ? QStringLiteral("FP16")
                    : (precision == 3) ? QStringLiteral("INT8")
                                       : QString();
        if (!opt.isEmpty()) {
            QMutexLocker lock(&m_mutex);
            m_currentOptMode = opt;
        }
    } else if (cmdId == 2 /*Set_Model*/) {
        QString m = (model == 1) ? QStringLiteral("YOLO26s")
                  : (model == 2) ? QStringLiteral("YOLO26n")
                                 : QString();
        if (!m.isEmpty()) {
            QMutexLocker lock(&m_mutex);
            m_currentYoloModel = m;
        }
    }

    QByteArray b(8, 0);
    putU8(b, 0, cmdId);
    putU8(b, 1, precision);
    putU8(b, 2, model);
    putU8(b, 3, amcl);
    putU8(b, 4, estop);
    putU8(b, 5, destination);   // Destination_Select
    putU8(b, 7, m_txCommandCounter++);

    if (!writeCanFrame(ID_UI_COMMAND, b)) {
        ++m_txFrames;
        m_lastTxLatencyMs = rnd(0.3, 1.4);
        publishBusStats();
    }
    // Empty summary = quiet re-assert (E-stop hold heartbeat) → don't flood the log.
    if (!logSummary.isEmpty())
        queueLog("UI_CMD", logSummary,
                 estop != 0 ? QStringLiteral("critical") : QStringLiteral("info"),
                 "0x200");
}

// ── 0x201 UI_Goal_Pose TX (NavigateToPose goal) ─────────────────────────
void CanBridge::sendGoalPoseImpl(double distM, double latM, double yawDeg)
{
    // Encoding (little-endian). Map-frame goal (report §3.2.3 NavigateToPose):
    //   Goal_X   bytes 0-1  int16  0.01 m  (map-frame X, signed)
    //   Goal_Y   bytes 2-3  int16  0.01 m  (map-frame Y, signed)
    //   Goal_Yaw bytes 4-5  int16  0.1 deg (final heading)
    //   Goal_Valid byte 6   uint8  1 = set
    //   Goal_Counter byte 7 uint8
    // (Goal_X was previously uint16 0.1 m 0–300 = along-route — a straight-road
    //  shortcut; map-frame signed is the general/correct form.)
    QByteArray b(8, 0);
    putI16(b, 0, (qint16) qBound(-32768, (int)qRound(distM  * 100.0), 32767));  // map X
    putI16(b, 2, (qint16) qBound(-32768, (int)qRound(latM   * 100.0), 32767));  // map Y
    putI16(b, 4, (qint16) qBound(-32768, (int)qRound(yawDeg * 10.0),  32767));
    putU8 (b, 6, 1);                       // Goal_Valid
    putU8 (b, 7, m_txGoalCounter++);

    if (!writeCanFrame(ID_UI_GOAL_POSE, b)) {
        ++m_txFrames;
        m_lastTxLatencyMs = rnd(0.3, 1.4);
        publishBusStats();
        // Virtual demo: accept the map-frame goal (x=distM, y=latM) and start an
        // AUTO mission from the ego's current pose toward it.
        m_vGoalX = distM;
        m_vGoalY = latM;
        m_vGoalValid = true;
        m_vRouteM = 0.0;
        m_vDriveState = 2;   // AUTO
    }
    queueLog("GOAL", QStringLiteral("NavigateToPose → %1 m, %2°")
                     .arg(distM, 0, 'f', 1).arg(yawDeg, 0, 'f', 0),
             QStringLiteral("info"), "0x201");
}

// ── 0x201 UI_Goal_Pose TX with Goal_Valid=0 — abort the current goal ────
void CanBridge::sendCancelGoalImpl()
{
    // Same frame layout as a goal, but Goal_Valid=0 and a zeroed pose → the
    // Jetson aborts the NavigateToPose and returns to IDLE. (Counter advances.)
    QByteArray b(8, 0);
    putU8(b, 6, 0);                        // Goal_Valid = 0 → cancel
    putU8(b, 7, m_txGoalCounter++);

    if (!writeCanFrame(ID_UI_GOAL_POSE, b)) {
        ++m_txFrames;
        m_lastTxLatencyMs = rnd(0.3, 1.4);
        publishBusStats();
        // Virtual demo: drop the goal and return to IDLE so goal-setting unlocks.
        m_vGoalValid  = false;
        m_vGoalX      = 0.0;
        m_vGoalY      = 0.0;
        m_vDriveState = 0;   // IDLE
    }
    queueLog("GOAL", QStringLiteral("Goal cancelled → IDLE"),
             QStringLiteral("info"), "0x201");
}

void CanBridge::queueLog(const QString &code, const QString &msg,
                         const QString &severity, const QString &src)
{
    QMutexLocker lock(&m_mutex);
    m_pendingLogs.append({ code, msg, severity, src });
    // Keep the queue bounded so a paused UI thread can't blow up memory.
    constexpr int kMaxQueue = 200;
    if (m_pendingLogs.size() > kMaxQueue)
        m_pendingLogs.remove(0, m_pendingLogs.size() - kMaxQueue);
}

// ── Public snapshot API (called from UI thread) ─────────────────────────
LatestValues CanBridge::takeLatest() {
    QMutexLocker lock(&m_mutex);
    LatestValues out = m_latest;
    m_latest.kpiDirty           = false;
    m_latest.vehicleDirty       = false;
    m_latest.obstacleDirty      = false;
    m_latest.sysDirty           = false;
    m_latest.routeDirty         = false;
    m_latest.hardwareDirty      = false;
    m_latest.failsafeEventDirty = false;
    m_latest.mapInfoDirty       = false;
    m_latest.egoDirty           = false;
    m_latest.datumDirty         = false;
    m_latest.locDirty           = false;
    m_latest.behaviorDirty      = false;
    m_latest.imuDirty           = false;
    m_latest.encDirty           = false;
    m_latest.planningDirty      = false;
    m_latest.perceptionDirty    = false;
    m_latest.networkDirty       = false;
    m_latest.busStatsDirty      = false;
    return out;
}

LatestValues CanBridge::peekLatest() {
    QMutexLocker lock(&m_mutex);
    LatestValues out = m_latest;
    // STATE domains: always forward the full current value (client re-applies
    // through setIf-guarded apply*, so unchanged values cost nothing).
    out.kpiDirty = out.vehicleDirty = out.obstacleDirty = out.sysDirty =
        out.routeDirty = out.hardwareDirty = out.mapInfoDirty = out.egoDirty =
        out.datumDirty = out.locDirty = out.behaviorDirty = out.imuDirty =
        out.encDirty = out.planningDirty = out.perceptionDirty =
        out.networkDirty = out.busStatsDirty = true;
    // EVENT domain: out carries the real edge; consume it so it forwards once.
    m_latest.failsafeEventDirty = false;
    return out;
}

void CanBridge::setMapInfoBroadcast(double ox, double oy, double res,
                                    int w, int h, int ver) {
    QMetaObject::invokeMethod(this, [=]() {
        m_bcOriginX = ox; m_bcOriginY = oy; m_bcResolution = res;
        m_bcWidth = w; m_bcHeight = h; m_bcVersion = ver; m_bcMapValid = true;
    }, Qt::QueuedConnection);
}

void CanBridge::setMapDatumBroadcast(double lat, double lon) {
    QMetaObject::invokeMethod(this, [=]() {
        m_bcDatumLat = lat; m_bcDatumLon = lon; m_bcDatumValid = true;
    }, Qt::QueuedConnection);
}

void CanBridge::setVirtualEgoStart(double x, double y, double yaw) {
    QMetaObject::invokeMethod(this, [=]() {
        m_vEgoX = x; m_vEgoY = y; m_vEgoYaw = yaw;
    }, Qt::QueuedConnection);
}

QVector<LogEntry> CanBridge::takePendingLogs() {
    QMutexLocker lock(&m_mutex);
    QVector<LogEntry> out;
    out.swap(m_pendingLogs);
    return out;
}

// ── Virtual bus generator ───────────────────────────────────────────────
void CanBridge::virtualTick20() {
    // Snapshot the dashboard's current selection under the mutex so the
    // virtual bus produces a coherent 0x101 echo.
    QString opt, model;
    {
        QMutexLocker lock(&m_mutex);
        opt = m_currentOptMode;
        model = m_currentYoloModel;
    }

    // Demo speedups vs the PyTorch FP32 baseline (TensorRT INT8≈3.4×, FP16≈2.5×, FP32≈1.22×).
    double optFloor = (opt == "INT8") ? m_ptBaselineMs / 3.4
                    : (opt == "FP16") ? m_ptBaselineMs / 2.5
                                      : m_ptBaselineMs / 1.22;
    double modelMul = (model == "YOLO26n") ? 0.55 : 1.0;
    double baseInfer = optFloor * modelMul;
    double baseGpu = (opt == "INT8") ? 50.0 : (opt == "FP16") ? 62.0 : 78.0;
    if (model == "YOLO26n") baseGpu *= 0.7;

    m_vInfer = clampd(m_vInfer + (baseInfer - m_vInfer) * 0.35 + rnd(-3, 3), 6, 600);
    m_vGpu   = clampd(m_vGpu   + (baseGpu   - m_vGpu)   * 0.30 + rnd(-2, 2), 5, 100);
    m_vCpu   = clampd(m_vCpu   + (36.0 - m_vCpu)        * 0.30 + rnd(-2, 2), 5, 100);
    m_vTemp  = clampd(m_vTemp  + (60.0 - m_vTemp)       * 0.15 + rnd(-0.4, 0.4), 40, 95);
    m_vPathErr = clampd(m_vPathErr + (110 - m_vPathErr) * 0.25 + rnd(-15, 15), 0, 800);

    // Speed tracks the mission state: cruise while AUTO, coast to 0 while IDLE.
    const double targetSpeed = (m_vDriveState == 2) ? 1.17 : 0.0;
    m_vSpeedMs = clampd(m_vSpeedMs + (targetSpeed - m_vSpeedMs) * 0.2 + rnd(-0.02, 0.02), 0, 2);
    m_vSteer   = clampd(m_vSteer * 0.85 + rnd(-3, 3), -25, 25);
    m_vLaneDev = clampd(m_vLaneDev * 0.9 + rnd(-25, 25), -250, 250);   // demo RPi lane dev (mm)
    m_vDistM   = clampd(m_vDistM - 0.04 + rnd(-0.08, 0.08), 1.5, 12);
    if (m_vDistM < 2.0) m_vDistM = 11.5;
    m_vAng     = clampd(m_vAng + rnd(-0.6, 0.6), -45, 45);
    m_vConf    = clampd(0.86 + rnd(-0.04, 0.06), 0.4, 0.99);

    // Drive the 2D ego pose straight toward the goal (any map shape). Stop &
    // return to IDLE on arrival — so a goal "behind" the car is reached and the
    // lock releases, instead of driving forward forever.
    if (m_vDriveState == 2 && m_vGoalValid) {
        const double dx = m_vGoalX - m_vEgoX;
        const double dy = m_vGoalY - m_vEgoY;
        const double d  = std::sqrt(dx * dx + dy * dy);
        const double step = m_vSpeedMs * 0.02;          // metres this 20 ms tick
        if (d <= qMax(step, 0.4)) {
            m_vEgoX = m_vGoalX; m_vEgoY = m_vGoalY;     // snap to goal
            m_vDriveState = 0;                          // arrived → IDLE
            m_vGoalValid = false;
            queueLog("GOAL", QStringLiteral("Goal reached → IDLE"),
                     QStringLiteral("good"), "sim");
        } else {
            m_vEgoX += dx / d * step;
            m_vEgoY += dy / d * step;
            m_vEgoYaw = std::atan2(dy, dx) * 180.0 / M_PI;
            m_vRouteM += step;                          // cumulative path length
        }
    }

    processFrame(QCanBusFrame(ID_OBSTACLE, buildObstacleFrame()));
    processFrame(QCanBusFrame(ID_VEHICLE,  buildVehicleStatusFrame()));
    processFrame(QCanBusFrame(ID_LOCALIZATION, buildLocalizationFrame()));
    // Ego_Pose (map frame) — the recommended signal, synthesised here for demo.
    {
        QByteArray e(8, 0);
        putI16(e, 0, (qint16)qBound(-32768, (int)qRound(m_vEgoX * 100.0), 32767));
        putI16(e, 2, (qint16)qBound(-32768, (int)qRound(m_vEgoY * 100.0), 32767));
        putI16(e, 4, (qint16)qBound(-32768, (int)qRound(m_vEgoYaw * 10.0), 32767));
        putU8 (e, 7, m_vEgoCounter++);
        processFrame(QCanBusFrame(ID_EGO_POSE, e));
    }
    // STM32 chassis feedback (0x20 encoder, 0x21 IMU) — demo heartbeat so the
    // monitoring page shows the drivetrain + IMU online on the virtual bus.
    {
        const qint32 ticks = (qint32)qRound(m_vRouteM * 4096.0);   // ~4096 ticks/m
        QByteArray en(8, 0);
        putU32(en, 0, (quint32)ticks);
        putU32(en, 4, (quint32)ticks);
        processFrame(QCanBusFrame(ID_ENCODER, en));

        QByteArray im(8, 0);
        putI16(im, 0, (qint16)qBound(-32768, (int)qRound(m_vEgoYaw * 100.0), 32767)); // yaw
        putI16(im, 2, (qint16)qBound(-32768, (int)qRound(m_vSteer  * 100.0), 32767)); // gyroZ proxy
        putI16(im, 4, (qint16)qRound(rnd(-1, 1) * 100.0));                            // roll
        putI16(im, 6, (qint16)qRound(rnd(-1, 1) * 100.0));                            // pitch
        processFrame(QCanBusFrame(ID_IMU, im));
    }
}

void CanBridge::virtualTick100() {
    m_vRamPct  = clampd(m_vRamPct  + (55.0 - m_vRamPct)  * 0.1 + rnd(-1.5, 1.5), 10, 95);
    m_vSwapPct = clampd(m_vSwapPct + (8.0  - m_vSwapPct) * 0.1 + rnd(-0.5, 0.5), 0, 100);
    // Bus load drifts ~8–40%; loss hovers near 0 with the occasional small spike.
    m_vBusLoad = clampd(m_vBusLoad + (22.0  - m_vBusLoad) * 0.15 + rnd(-2.5, 2.5), 8, 60);
    m_vCanLoss = clampd(m_vCanLoss + (0.04  - m_vCanLoss) * 0.10 + rnd(-0.02, 0.05), 0.0, 1.2);
    m_vAmclErr = clampd(m_vAmclErr + (0.22 - m_vAmclErr) * 0.15 + rnd(-0.02, 0.02), 0.05, 1.5);
    m_vMissionPct = clampd(m_vMissionPct + rnd(-0.3, 0.3), 0, 100);
    m_vSessionHz  = (int)clampd(m_vSessionHz + rnd(-1, 1), 5, 30);

    processFrame(QCanBusFrame(ID_REALTIME_KPI, buildRealtimeKpiFrame()));
    processFrame(QCanBusFrame(ID_SYS_RESOURCE, buildSystemResourceFrame()));
    processFrame(QCanBusFrame(ID_ROUTE_STATUS, buildRouteStatusFrame()));

    // Fail-safe level with HYSTERESIS: rise at the trigger, fall only below a
    // release margin wider than the per-tick jitter (rnd ±3 infer / ±2 gpu), so
    // a single jittered sample can't flip the level (was the banner flicker).
    // Single source of truth: 0x100 byte6 (buildObstacleFrame) AND the 0x1FF
    // event both use m_vFailsafeLevel.
    const quint8 prev = m_vFailsafeLevel;
    quint8 lvl = prev;
    const bool upL3 = (m_vInfer > 300 && m_vGpu > 95);
    const bool upL2 = (m_vInfer > 150 && m_vGpu > 85);
    if (upL3)               lvl = 3;
    else if (prev == 3)     lvl = (m_vInfer < 285 || m_vGpu < 92) ? 2 : 3;   // L3 release
    else if (upL2)          lvl = 2;
    else if (prev == 2)     lvl = (m_vInfer < 135 || m_vGpu < 80) ? 1 : 2;   // L2 release
    else                    lvl = 1;
    m_vFailsafeLevel = lvl;
    if (lvl != prev) {
        quint8 reason = (m_vInfer > 150) ? 2 : 1;
        processFrame(QCanBusFrame(ID_FAILSAFE_EV, buildFailsafeEventFrame(1, reason, lvl)));
    }
}

void CanBridge::virtualTick1000() {
    processFrame(QCanBusFrame(ID_HARDWARE, buildHardwareInfoFrame()));

    // Broadcast Map_Info (both mux halves) so the UI can verify its loaded map.
    if (m_bcMapValid) {
        QByteArray a(8, 0);
        putU8 (a, 0, 0);                                                  // mux 0
        putI16(a, 1, (qint16)qBound(-32768, (int)qRound(m_bcOriginX * 100.0), 32767));
        putI16(a, 3, (qint16)qBound(-32768, (int)qRound(m_bcOriginY * 100.0), 32767));
        putU8 (a, 5, (quint8)qBound(0, (int)qRound(m_bcResolution * 1000.0), 255));
        putU8 (a, 6, (quint8)m_bcVersion);
        putU8 (a, 7, m_vMapCounter++);
        processFrame(QCanBusFrame(ID_MAP_INFO, a));

        QByteArray b(8, 0);
        putU8 (b, 0, 1);                                                  // mux 1
        putU16(b, 1, (quint16)qBound(0, m_bcWidth,  0xFFFF));
        putU16(b, 3, (quint16)qBound(0, m_bcHeight, 0xFFFF));
        putU8 (b, 6, (quint8)m_bcVersion);
        putU8 (b, 7, m_vMapCounter++);
        processFrame(QCanBusFrame(ID_MAP_INFO, b));
    }

    // Broadcast Map_Datum (WGS84 local origin) so the UI can ENU-convert lanes.
    if (m_bcDatumValid) {
        QByteArray d(8, 0);
        const qint32 latRaw = (qint32)qRound(m_bcDatumLat * 1e7);
        const qint32 lonRaw = (qint32)qRound(m_bcDatumLon * 1e7);
        for (int i = 0; i < 4; ++i) d[i]     = (char)((latRaw >> (8 * i)) & 0xFF);
        for (int i = 0; i < 4; ++i) d[4 + i] = (char)((lonRaw >> (8 * i)) & 0xFF);
        processFrame(QCanBusFrame(ID_MAP_DATUM, d));
    }

    // 0x106 Planning_Status — plausible demo: plan time well under target,
    // 9/10 successful, state tracks AUTO. (Counters static across ticks.)
    {
        QByteArray pl(8, 0);
        const int lastMs = (int)qBound(0, (int)qRound(1150.0 + rnd(-150, 150)), 0xFFFF);
        putU16(pl, 0, (quint16)lastMs);
        putU8 (pl, 2, 9);                                    // PathPlan_Success_Runs
        putU8 (pl, 3, 10);                                   // PathPlan_Total_Runs
        putU8 (pl, 4, (quint8)(m_vDriveState == 2 ? 2 : 1)); // Planning_State
        putU8 (pl, 7, m_vPlanCounter++);
        processFrame(QCanBusFrame(ID_PLANNING, pl));
    }
    // 0x107 Perception_Validation — 9/10 detected, no false positives, 92% trigger.
    {
        QByteArray pc(8, 0);
        putU8(pc, 0, 9);                                     // Perception_Detected_Runs
        putU8(pc, 1, 10);                                    // Perception_Total_Runs
        putU8(pc, 2, 0);                                     // False_Positive_Count
        putU8(pc, 3, (quint8)qRound(92.0 / 0.5));            // Trigger_Accuracy (0.5 %/bit)
        putU8(pc, 7, m_vPercCounter++);
        processFrame(QCanBusFrame(ID_PERCEPTION, pc));
    }
    // 0x109 Network_Status — healthy wifi: low ping/loss, good RSSI.
    {
        QByteArray n(8, 0);
        const int ping = (int)qBound(0, (int)qRound(24.0 + rnd(-6, 6)), 0xFFFF);
        putU16(n, 0, (quint16)ping);
        putU8 (n, 2, (quint8)qRound(0.3 / 0.1));             // Network_Loss_Rate (0.1 %/bit)
        n[3] = (char)(qint8)(-55);                           // Wifi_Rssi_dBm (signed)
        putU8 (n, 4, 1);                                     // Network_Status
        putU8 (n, 7, m_vNetCounter++);
        processFrame(QCanBusFrame(ID_NETWORK, n));
    }
}

// ── Frame encoders (virtual bus) ────────────────────────────────────────
QByteArray CanBridge::buildObstacleFrame() {
    QByteArray b(8, 0);
    putU16(b, 0, (quint16)qBound(0, (int)qRound(m_vDistM * 100.0), 0xFFFF));
    putI16(b, 2, (qint16)qBound(-32768, (int)qRound(m_vAng * 10.0), 32767));
    putU8 (b, 4, 2);
    putU8 (b, 5, (quint8)qBound(0, (int)qRound(m_vConf * 100.0 / 0.5), 255));
    putU8 (b, 6, m_vFailsafeLevel);   // Fail_Safe_Level — unified with the 0x1FF event
    putU8 (b, 7, m_vCounter100++);
    return b;
}
QByteArray CanBridge::buildVehicleStatusFrame() {
    QString opt, model;
    {
        QMutexLocker lock(&m_mutex);
        opt = m_currentOptMode;
        model = m_currentYoloModel;
    }
    QByteArray b(8, 0);
    putI16(b, 0, (qint16)qBound(-32768, (int)qRound(m_vSpeedMs * 100.0), 32767));
    putI16(b, 2, (qint16)qBound(-32768, (int)qRound(m_vSteer * 10.0), 32767));
    putU8 (b, 4, (quint8)m_vDriveState);
    putU8 (b, 5, encodeYoloMode(opt, model));
    putU8 (b, 6, 1);
    putU8 (b, 7, m_vCounter101++);
    return b;
}
// 0x10A Localization_Status — demo RPi lane deviation (Loc_Lane_Dev, mm) + EKF/quality
// placeholders + an advancing Loc_Counter so the UI sees a live RPi lane stream.
QByteArray CanBridge::buildLocalizationFrame() {
    QByteArray b(8, 0);
    putU8 (b, 0, 1);                                                        // Loc_Mode = EKF
    putU8 (b, 1, 80);                                                       // Loc_Quality 80%
    putI16(b, 2, (qint16)qBound(-32768, (int)qRound(m_vLaneDev), 32767));   // Loc_Lane_Dev (mm)
    putU8 (b, 7, m_vCounter10A++);                                          // Loc_Counter (alive)
    return b;
}
QByteArray CanBridge::buildRealtimeKpiFrame() {
    QByteArray b(8, 0);
    putU16(b, 0, (quint16)qBound(0, (int)qRound(m_vInfer), 0xFFFF));
    putU8 (b, 2, (quint8)qBound(0, (int)qRound(m_vGpu),  100));
    putU8 (b, 3, (quint8)qBound(0, (int)qRound(m_vCpu),  100));
    putU8 (b, 4, (quint8)qBound(0, (int)qRound(m_vTemp), 125));
    putU8 (b, 5, m_vCounter102++);
    putI16(b, 6, (qint16)qBound(-32768, (int)qRound(m_vPathErr), 32767));
    return b;
}
QByteArray CanBridge::buildSystemResourceFrame() {
    QByteArray b(8, 0);
    putU8(b, 0, (quint8)qBound(0, (int)qRound(m_vRamPct),  100));
    putU8(b, 1, (quint8)qBound(0, (int)qRound(m_vSwapPct), 100));
    putU8(b, 2, (quint8)qBound(0, m_vSessionHz, 255));
    putU8(b, 3, (quint8)qBound(0, (int)qRound(m_vBusLoad), 100));        // Bus_Load
    putU8(b, 4, (quint8)qBound(0, (int)qRound(m_vCanLoss / 0.1), 255));  // Can_Loss_Rate (0.1%/bit)
    putU8(b, 5, 1);
    putU8(b, 6, 0x07);
    putU8(b, 7, m_vCounter103++);
    return b;
}
QByteArray CanBridge::buildRouteStatusFrame() {
    QByteArray b(8, 0);
    putU16(b, 0, (quint16)qBound(0, (int)qRound(m_vRouteM / 0.1), 0xFFFF));
    putI16(b, 2, (qint16)qBound(-32768, (int)qRound(m_vAmclErr / 0.01), 32767));
    putU16(b, 4, (quint16)qBound(0, (int)qRound(m_vMissionPct / 0.1), 0xFFFF));
    putU8 (b, 6, 2);
    putU8 (b, 7, m_vCounter104++);
    return b;
}
QByteArray CanBridge::buildHardwareInfoFrame() {
    QByteArray b(8, 0);
    putU8(b, 0, 1);
    putU8(b, 1, 8);
    putU8(b, 7, m_vCounter105++);
    return b;
}
QByteArray CanBridge::buildFailsafeEventFrame(quint8 evCode, quint8 reasonCode, quint8 lvl) {
    QByteArray b(8, 0);
    putU8 (b, 0, evCode);
    putU8 (b, 1, reasonCode);
    putU32(b, 2, (quint32)m_uptime.elapsed());
    putU8 (b, 6, lvl);
    putU8 (b, 7, m_vCounter1FF++);
    return b;
}

// ── Little-endian helpers ───────────────────────────────────────────────
void CanBridge::putU8 (QByteArray &b, int off, quint8 v)  { b[off] = (char)v; }
void CanBridge::putU16(QByteArray &b, int off, quint16 v) { b[off] = (char)(v & 0xFF); b[off+1] = (char)((v >> 8) & 0xFF); }
void CanBridge::putI16(QByteArray &b, int off, qint16  v) { putU16(b, off, (quint16)v); }
void CanBridge::putU32(QByteArray &b, int off, quint32 v) {
    b[off]   = (char)( v        & 0xFF);
    b[off+1] = (char)((v >>  8) & 0xFF);
    b[off+2] = (char)((v >> 16) & 0xFF);
    b[off+3] = (char)((v >> 24) & 0xFF);
}
quint8  CanBridge::getU8 (const QByteArray &b, int off) { return (quint8)b[off]; }
quint16 CanBridge::getU16(const QByteArray &b, int off) {
    return (quint16)((quint8)b[off] | ((quint16)(quint8)b[off+1] << 8));
}
qint16  CanBridge::getI16(const QByteArray &b, int off) { return (qint16)getU16(b, off); }
quint32 CanBridge::getU32(const QByteArray &b, int off) {
    return  (quint32)(quint8)b[off]
        | ((quint32)(quint8)b[off+1] <<  8)
        | ((quint32)(quint8)b[off+2] << 16)
        | ((quint32)(quint8)b[off+3] << 24);
}
