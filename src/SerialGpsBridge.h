#pragma once

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QString>

QT_BEGIN_NAMESPACE
class QSerialPort;
QT_END_NAMESPACE

// SerialGpsBridge — reads NMEA-0183 sentences from a u-blox M10 (or
// equivalent) GPS receiver connected over USB-serial / UART. Same threading
// pattern as CanBridge: this object is moveToThread'd to a dedicated worker;
// decoding writes to m_latest under m_mutex; the UI thread polls via
// takeLatest() once per render frame.
//
// Why an independent bridge: per the engineering review, route progress and
// path deviation must be independently cross-checkable. The Jetson reports
// Route_Progress on 0x104 from its SLAM stack; the dashboard's own GPS feed
// lets the operator see a second source-of-truth (fix quality, sat count,
// raw lat/lon). If they diverge significantly, something is wrong with
// SLAM or the GPS antenna.
//
// Sentences parsed:
//   $..RMC — recommended minimum: time, status, lat, lon, speed (kn), track
//   $..GGA — fix quality, sat count, altitude
//   $..GSV — satellites in view (sat count fallback if no GGA fix yet)
//
// Talker IDs (GP, GN, GA, GL, ...) are accepted — we only switch on the
// 3-letter sentence type.
class SerialGpsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool    open   READ isOpen NOTIFY statusChanged)

public:
    explicit SerialGpsBridge(QObject *parent = nullptr);
    ~SerialGpsBridge();

    QString status() const;
    bool isOpen() const;

    // Public API — safe from any thread (bounces to worker via QueuedConnection).
    Q_INVOKABLE bool openPort(const QString &portName, int baud = 9600);
    Q_INVOKABLE void closePort();
    Q_INVOKABLE void shutdown();

    // Latest decoded snapshot — populated by the worker, consumed by UI poll.
    // `dirty` indicates "fresh since last takeLatest()".
    struct LatestGps {
        bool   dirty = false;
        bool   hasFix = false;
        int    satCount = 0;
        double latDeg = 0;
        double lonDeg = 0;
        double altitudeM = 0;
        double speedKmh = 0;
        double headingDeg = 0;
    };

    LatestGps takeLatest();

signals:
    void statusChanged();

private slots:
    void onReadyRead();

private:
    bool openPortImpl(const QString &portName, int baud);
    void closePortImpl();
    void shutdownImpl();

    void parseLine(const QByteArray &line);
    void parseRMC(const QStringList &fields);
    void parseGGA(const QStringList &fields);
    static double dmToDeg(const QString &dm, const QString &hemi);
    static bool   verifyChecksum(const QByteArray &line);

    void setStatus(const QString &s);

    QSerialPort *m_port = nullptr;
    QByteArray   m_inBuf;

    mutable QMutex m_mutex;
    LatestGps      m_latest;
    QString        m_status = "disconnected";
};
