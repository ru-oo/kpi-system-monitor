#include "SerialGpsBridge.h"

#include <QSerialPort>
#include <QMutexLocker>
#include <QMetaObject>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>

SerialGpsBridge::SerialGpsBridge(QObject *parent)
    : QObject(parent)
{
}

SerialGpsBridge::~SerialGpsBridge() {
    if (m_port) {
        if (m_port->isOpen()) m_port->close();
        delete m_port;
        m_port = nullptr;
    }
}

QString SerialGpsBridge::status() const {
    QMutexLocker lock(&m_mutex);
    return m_status;
}

bool SerialGpsBridge::isOpen() const {
    return m_port && m_port->isOpen();
}

void SerialGpsBridge::setStatus(const QString &s) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_status == s) return;
        m_status = s;
    }
    emit statusChanged();
}

// ── Public API (thread-safe bounce to worker) ───────────────────────────
bool SerialGpsBridge::openPort(const QString &portName, int baud) {
    if (QThread::currentThread() == thread())
        return openPortImpl(portName, baud);
    bool ok = false;
    QMetaObject::invokeMethod(this, [this, portName, baud, &ok]() {
        ok = openPortImpl(portName, baud);
    }, Qt::BlockingQueuedConnection);
    return ok;
}
void SerialGpsBridge::closePort() {
    QMetaObject::invokeMethod(this, &SerialGpsBridge::closePortImpl, Qt::QueuedConnection);
}
void SerialGpsBridge::shutdown() {
    if (QThread::currentThread() == thread()) { shutdownImpl(); return; }
    QMetaObject::invokeMethod(this, &SerialGpsBridge::shutdownImpl, Qt::BlockingQueuedConnection);
}

// ── Worker-thread implementations ───────────────────────────────────────
bool SerialGpsBridge::openPortImpl(const QString &portName, int baud) {
    closePortImpl();
    m_port = new QSerialPort(this);
    m_port->setPortName(portName);
    m_port->setBaudRate(baud);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_port->open(QIODevice::ReadOnly)) {
        const QString err = m_port->errorString();
        setStatus(QStringLiteral("error: %1").arg(err));
        qWarning() << "[GPS] open" << portName << "failed:" << err;
        delete m_port; m_port = nullptr;
        return false;
    }
    connect(m_port, &QSerialPort::readyRead, this, &SerialGpsBridge::onReadyRead);
    setStatus(QStringLiteral("online: %1 @ %2 baud").arg(portName).arg(baud));
    qInfo() << "[GPS] open" << portName << "@" << baud;
    return true;
}

void SerialGpsBridge::closePortImpl() {
    if (m_port) {
        if (m_port->isOpen()) m_port->close();
        m_port->deleteLater();
        m_port = nullptr;
        setStatus("disconnected");
    }
}

void SerialGpsBridge::shutdownImpl() {
    closePortImpl();
    moveToThread(QCoreApplication::instance()->thread());
}

// ── NMEA framing + dispatch ─────────────────────────────────────────────
void SerialGpsBridge::onReadyRead() {
    if (!m_port) return;
    m_inBuf.append(m_port->readAll());
    int nl;
    while ((nl = m_inBuf.indexOf('\n')) >= 0) {
        QByteArray line = m_inBuf.left(nl);
        m_inBuf.remove(0, nl + 1);
        // strip trailing \r if present
        if (!line.isEmpty() && line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty()) parseLine(line);
    }
    // Cap buffer in case of garbage so we don't grow without bound.
    if (m_inBuf.size() > 4096) m_inBuf.clear();
}

void SerialGpsBridge::parseLine(const QByteArray &line) {
    // Minimum sentence: "$XXYYY,...*hh"
    if (line.size() < 7 || line[0] != '$') return;
    if (!verifyChecksum(line)) return;

    // Drop checksum suffix "*hh" for field split
    int star = line.indexOf('*');
    QByteArray body = (star > 0) ? line.left(star) : line;
    const QStringList fields = QString::fromLatin1(body).split(',');
    if (fields.isEmpty()) return;

    const QString tag = fields.first();   // e.g. "$GPRMC", "$GNGGA"
    if (tag.size() < 6) return;
    const QString type = tag.right(3);    // last 3 chars: RMC / GGA / ...

    if      (type == "RMC") parseRMC(fields);
    else if (type == "GGA") parseGGA(fields);
}

// $..RMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a,m*hh
//          1        2   3     4    5    6  7   8     9    10  11
//   1: UTC time
//   2: status A=active V=void
//   3,4: latitude ddmm.mmmm, hemisphere
//   5,6: longitude dddmm.mmmm, hemisphere
//   7: speed over ground (knots)
//   8: track angle (deg)
void SerialGpsBridge::parseRMC(const QStringList &f) {
    if (f.size() < 9) return;
    const bool active = (f.value(2) == "A");
    if (!active) {
        QMutexLocker lock(&m_mutex);
        m_latest.hasFix = false;
        m_latest.dirty = true;
        return;
    }
    const double lat   = dmToDeg(f.value(3), f.value(4));
    const double lon   = dmToDeg(f.value(5), f.value(6));
    const double knots = f.value(7).toDouble();
    const double track = f.value(8).toDouble();
    QMutexLocker lock(&m_mutex);
    m_latest.hasFix     = true;
    m_latest.latDeg     = lat;
    m_latest.lonDeg     = lon;
    m_latest.speedKmh   = knots * 1.852;   // knots → km/h
    m_latest.headingDeg = track;
    m_latest.dirty      = true;
}

// $..GGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,,*hh
//         1         2     3   4      5  6  7  8   9  10
//   6: fix quality (0=invalid, 1=GPS fix, 2=DGPS, ...)
//   7: satellites used
//   9: altitude (m)
void SerialGpsBridge::parseGGA(const QStringList &f) {
    if (f.size() < 10) return;
    const int    fix   = f.value(6).toInt();
    const int    sats  = f.value(7).toInt();
    const double alt   = f.value(9).toDouble();
    const double lat   = dmToDeg(f.value(2), f.value(3));
    const double lon   = dmToDeg(f.value(4), f.value(5));
    QMutexLocker lock(&m_mutex);
    m_latest.hasFix    = (fix >= 1);
    m_latest.satCount  = sats;
    m_latest.altitudeM = alt;
    if (m_latest.hasFix) {
        m_latest.latDeg = lat;
        m_latest.lonDeg = lon;
    }
    m_latest.dirty = true;
}

// NMEA "ddmm.mmmm" or "dddmm.mmmm" + hemisphere → signed decimal degrees.
double SerialGpsBridge::dmToDeg(const QString &dm, const QString &hemi) {
    if (dm.isEmpty()) return 0.0;
    bool ok = false;
    const double raw = dm.toDouble(&ok);
    if (!ok) return 0.0;
    const double deg = std::floor(raw / 100.0);
    const double min = raw - deg * 100.0;
    double v = deg + min / 60.0;
    if (hemi == "S" || hemi == "W") v = -v;
    return v;
}

// NMEA checksum: XOR of bytes between '$' and '*'.
bool SerialGpsBridge::verifyChecksum(const QByteArray &line) {
    const int star = line.indexOf('*');
    if (star < 1 || star + 2 >= line.size()) return false;
    quint8 sum = 0;
    for (int i = 1; i < star; ++i) sum ^= static_cast<quint8>(line[i]);
    bool ok = false;
    const QString hex = QString::fromLatin1(line.mid(star + 1, 2));
    const quint8 expected = static_cast<quint8>(hex.toUInt(&ok, 16));
    return ok && (sum == expected);
}

// ── UI snapshot ─────────────────────────────────────────────────────────
SerialGpsBridge::LatestGps SerialGpsBridge::takeLatest() {
    QMutexLocker lock(&m_mutex);
    LatestGps out = m_latest;
    m_latest.dirty = false;
    return out;
}
