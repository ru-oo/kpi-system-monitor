#include "I2cImuBridge.h"

#include <QMutexLocker>
#include <QMetaObject>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_LINUX
#  include <linux/i2c-dev.h>
#  include <sys/ioctl.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <cerrno>
#  include <cstring>
#endif

// BNO055 register addresses (FUSION_MODE NDOF assumed configured externally;
// our reads are agnostic to the operating mode beyond expecting EUL_* to be
// valid). All Euler values are int16 LE, scaled 1/16 deg.
static constexpr quint8 BNO055_EUL_HEADING_LSB = 0x1A;
static constexpr quint8 BNO055_CALIB_STAT      = 0x35;

I2cImuBridge::I2cImuBridge(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setParent(this);
    connect(&m_pollTimer, &QTimer::timeout, this, &I2cImuBridge::sampleTick);
}

I2cImuBridge::~I2cImuBridge() {
#ifdef Q_OS_LINUX
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
#endif
}

QString I2cImuBridge::status() const {
    QMutexLocker lock(&m_mutex);
    return m_status;
}
bool I2cImuBridge::isOpen() const { return m_fd >= 0; }

void I2cImuBridge::setStatus(const QString &s) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_status == s) return;
        m_status = s;
    }
    emit statusChanged();
}

// ── Public API (thread-safe) ────────────────────────────────────────────
bool I2cImuBridge::openBus(const QString &devicePath, int slaveAddr, int interval) {
    if (QThread::currentThread() == thread())
        return openBusImpl(devicePath, slaveAddr, interval);
    bool ok = false;
    QMetaObject::invokeMethod(this, [this, devicePath, slaveAddr, interval, &ok]() {
        ok = openBusImpl(devicePath, slaveAddr, interval);
    }, Qt::BlockingQueuedConnection);
    return ok;
}
void I2cImuBridge::closeBus() {
    QMetaObject::invokeMethod(this, &I2cImuBridge::closeBusImpl, Qt::QueuedConnection);
}
void I2cImuBridge::shutdown() {
    if (QThread::currentThread() == thread()) { shutdownImpl(); return; }
    QMetaObject::invokeMethod(this, &I2cImuBridge::shutdownImpl, Qt::BlockingQueuedConnection);
}

// ── Worker-thread implementations ───────────────────────────────────────
bool I2cImuBridge::openBusImpl(const QString &devicePath, int slaveAddr, int interval) {
    closeBusImpl();
    m_slaveAddr = slaveAddr;

#ifdef Q_OS_LINUX
    m_fd = ::open(devicePath.toLocal8Bit().constData(), O_RDWR);
    if (m_fd < 0) {
        setStatus(QStringLiteral("open(%1) failed: %2").arg(devicePath).arg(::strerror(errno)));
        qWarning() << "[IMU] open" << devicePath << "failed:" << ::strerror(errno);
        return false;
    }
    if (::ioctl(m_fd, I2C_SLAVE, slaveAddr) < 0) {
        setStatus(QStringLiteral("ioctl(I2C_SLAVE 0x%1) failed: %2")
                      .arg(slaveAddr, 0, 16).arg(::strerror(errno)));
        qWarning() << "[IMU] ioctl I2C_SLAVE failed:" << ::strerror(errno);
        ::close(m_fd); m_fd = -1;
        return false;
    }
    m_pollTimer.setInterval(interval > 0 ? interval : 50);
    m_pollTimer.start();
    setStatus(QStringLiteral("online: %1 @ 0x%2 (%3 Hz)")
                  .arg(devicePath).arg(slaveAddr, 0, 16).arg(1000 / m_pollTimer.interval()));
    qInfo() << "[IMU] online" << devicePath << "addr=0x" + QString::number(slaveAddr, 16);
    return true;
#else
    Q_UNUSED(devicePath); Q_UNUSED(slaveAddr); Q_UNUSED(interval);
    setStatus("I²C unsupported on this platform — IMU disabled");
    qInfo() << "[IMU] I²C unavailable on non-Linux build — bridge inactive.";
    return false;
#endif
}

void I2cImuBridge::closeBusImpl() {
    m_pollTimer.stop();
#ifdef Q_OS_LINUX
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
#endif
    setStatus("disconnected");
}

void I2cImuBridge::shutdownImpl() {
    closeBusImpl();
    moveToThread(QCoreApplication::instance()->thread());
}

// Periodic register read. Errors are logged but do not stop the timer —
// transient bus glitches (e.g. shared I²C contention) are normal on Jetson.
void I2cImuBridge::sampleTick() {
    qint16 heading = 0, roll = 0, pitch = 0;
    quint8 calByte = 0;
    if (!readFusion(heading, roll, pitch, calByte)) return;

    QMutexLocker lock(&m_mutex);
    m_latest.hasReading  = true;
    m_latest.headingDeg  = heading * (1.0 / 16.0);
    m_latest.rollDeg     = roll    * (1.0 / 16.0);
    m_latest.pitchDeg    = pitch   * (1.0 / 16.0);
    m_latest.calSys      = (calByte >> 6) & 0x03;
    m_latest.calGyro     = (calByte >> 4) & 0x03;
    m_latest.calAccel    = (calByte >> 2) & 0x03;
    m_latest.calMag      = (calByte >> 0) & 0x03;
    m_latest.dirty       = true;
}

bool I2cImuBridge::readFusion(qint16 &heading, qint16 &roll, qint16 &pitch, quint8 &calByte) {
#ifdef Q_OS_LINUX
    if (m_fd < 0) return false;

    // Repeated start: write register addr, then read 6 bytes for Euler XYZ.
    quint8 reg = BNO055_EUL_HEADING_LSB;
    if (::write(m_fd, &reg, 1) != 1) return false;
    quint8 buf[6];
    if (::read(m_fd, buf, 6) != 6) return false;
    heading = static_cast<qint16>(buf[0] | (buf[1] << 8));
    roll    = static_cast<qint16>(buf[2] | (buf[3] << 8));
    pitch   = static_cast<qint16>(buf[4] | (buf[5] << 8));

    // Calibration status byte
    reg = BNO055_CALIB_STAT;
    if (::write(m_fd, &reg, 1) != 1) return false;
    if (::read(m_fd, &calByte, 1) != 1) return false;
    return true;
#else
    Q_UNUSED(heading); Q_UNUSED(roll); Q_UNUSED(pitch); Q_UNUSED(calByte);
    return false;
#endif
}

I2cImuBridge::LatestImu I2cImuBridge::takeLatest() {
    QMutexLocker lock(&m_mutex);
    LatestImu out = m_latest;
    m_latest.dirty = false;
    return out;
}
