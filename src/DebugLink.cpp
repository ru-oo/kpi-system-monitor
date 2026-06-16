#include "DebugLink.h"

#include <QUdpSocket>
#include <QTimer>
#include <QNetworkDatagram>
#include <QTime>
#include <QDebug>

DebugLink::DebugLink(QObject *parent) : QObject(parent) {
    m_clock.start();
}

void DebugLink::start(const QString &jetsonHost, quint16 jetsonPort, quint16 localPort) {
    if (jetsonHost.isEmpty()) {
        qInfo() << "[DebugLink] no debug host configured — debug channel disabled.";
        return;
    }
    m_jetson = QHostAddress(jetsonHost);
    m_haveJetson = !m_jetson.isNull();
    if (!m_haveJetson) {
        qWarning() << "[DebugLink] debug host is not a valid IPv4 address:" << jetsonHost
                   << "— debug disabled (use the Jetson's IP, not a hostname).";
        return;
    }
    m_jetsonPort = jetsonPort;

    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress::AnyIPv4, localPort))
        qWarning() << "[DebugLink] bind udp/" << localPort << "failed:" << m_sock->errorString();
    connect(m_sock, &QUdpSocket::readyRead, this, &DebugLink::onReadyRead);

    // Register with the Jetson (HELLO) every 3 s so it (re)learns our address.
    m_hello = new QTimer(this);
    m_hello->setInterval(3000);
    connect(m_hello, &QTimer::timeout, this, &DebugLink::sendHello);
    m_hello->start();
    sendHello();

    // Flip linkUp off when no debug line has arrived for a while.
    m_watch = new QTimer(this);
    m_watch->setInterval(1000);
    connect(m_watch, &QTimer::timeout, this, &DebugLink::checkLink);
    m_watch->start();

    qInfo().nospace() << "[DebugLink] receiving on udp/" << localPort
                      << ", registering with Jetson " << jetsonHost.toStdString().c_str()
                      << ":" << m_jetsonPort;
}

void DebugLink::sendHello() {
    if (!m_sock || !m_haveJetson) return;
    m_sock->writeDatagram(QByteArrayLiteral("KPIDBG-HELLO"), m_jetson, m_jetsonPort);
}

void DebugLink::checkLink() {
    const bool up = (m_clock.elapsed() - m_lastRxMs) < 8000;   // 8 s freshness window
    if (up != m_linkUp) { m_linkUp = up; emit linkChanged(); }
}

void DebugLink::onReadyRead() {
    while (m_sock->hasPendingDatagrams()) {
        const QByteArray data = m_sock->receiveDatagram().data();
        if (data.isEmpty()) continue;
        m_lastRxMs = m_clock.elapsed();
        if (!m_linkUp) { m_linkUp = true; emit linkChanged(); }

        const QString raw = QString::fromUtf8(data);
        // "LEVEL|text" → split on the first '|'; tolerate a bare message (→ INFO).
        QString level = QStringLiteral("INFO");
        QString text  = raw;
        const int bar = raw.indexOf('|');
        if (bar > 0) {
            const QString lv = raw.left(bar).trimmed().toUpper();
            if (lv == "DEBUG" || lv == "INFO" || lv == "WARN" || lv == "ERROR" || lv == "FATAL") {
                level = lv;
                text  = raw.mid(bar + 1);
            }
        }
        emit debugMessage(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                          level, text.trimmed());
        ++m_count;
        emit messageCountChanged();
    }
}
