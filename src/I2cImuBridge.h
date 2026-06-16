#pragma once

#include <QObject>
#include <QMutex>
#include <QTimer>
#include <QString>

// I2cImuBridge — reads orientation (Euler) + calibration status from a
// Bosch BNO055 over I²C. Same threading pattern as CanBridge and
// SerialGpsBridge.
//
// Platform support:
//   • Linux (Jetson) — talks to /dev/i2c-N via ioctl(I2C_SLAVE) + read().
//                       Real hardware reads.
//   • Windows / macOS — no native I²C bus; openBus() returns false, the
//                       worker stays idle, the dashboard shows "—". The
//                       class still compiles and links so the rest of the
//                       app works during desktop development.
//
// BNO055 registers we use (FUSION_MODE assumed):
//   0x1A..0x1F  Euler heading / roll / pitch  (int16 LE, scale 1/16 deg)
//   0x35        Calibration status byte: bits [7:6]=sys [5:4]=gyro
//                                             [3:2]=accel [1:0]=mag (0-3)
//
// Why this bridge: the engineering review notes that route/heading
// monitoring shouldn't rely solely on CAN-reported state. An independent
// IMU gives the operator a cross-check on the Jetson's perceived heading
// and reveals if the SLAM stack is drifting.
class I2cImuBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool    open   READ isOpen NOTIFY statusChanged)

public:
    explicit I2cImuBridge(QObject *parent = nullptr);
    ~I2cImuBridge();

    QString status() const;
    bool isOpen() const;

    // Sample period default 50 ms (20 Hz) — BNO055 fusion output ≤100 Hz.
    Q_INVOKABLE bool openBus(const QString &devicePath, int slaveAddr = 0x28,
                             int sampleIntervalMs = 50);
    Q_INVOKABLE void closeBus();
    Q_INVOKABLE void shutdown();

    struct LatestImu {
        bool   dirty = false;
        bool   hasReading = false;
        int    calSys = 0, calGyro = 0, calAccel = 0, calMag = 0;
        double headingDeg = 0;
        double rollDeg = 0;
        double pitchDeg = 0;
    };
    LatestImu takeLatest();

signals:
    void statusChanged();

private slots:
    void sampleTick();

private:
    bool openBusImpl(const QString &devicePath, int slaveAddr, int sampleIntervalMs);
    void closeBusImpl();
    void shutdownImpl();

    // Returns false if the I²C read failed (transient error). When called
    // on a non-Linux build it always returns false.
    bool readFusion(qint16 &heading, qint16 &roll, qint16 &pitch, quint8 &calByte);

    void setStatus(const QString &s);

    QTimer m_pollTimer;
    int    m_fd = -1;            // POSIX file descriptor on Linux, -1 elsewhere
    int    m_slaveAddr = 0x28;

    mutable QMutex m_mutex;
    LatestImu      m_latest;
    QString        m_status = "disconnected";
};
