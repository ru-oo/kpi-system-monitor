#pragma once

#include <QObject>
#include <QHostAddress>
#include <QElapsedTimer>

class QUdpSocket;
class QTimer;

// DebugLink — receives free-text debug lines from the Jetson over UDP and feeds
// the Debug page. The viewer REGISTERS with the Jetson by sending a small HELLO
// every few seconds; the Jetson learns each viewer's address (which can differ
// per team member / change with DHCP) and pushes /debug lines back to every
// registered viewer. So nobody hardcodes viewer IPs — only the one, stable
// Jetson host is configured on the dashboard side.
//
// Wire format (Jetson → dashboard), one datagram per message:
//   "LEVEL|text"   where LEVEL ∈ {DEBUG,INFO,WARN,ERROR,FATAL} (default INFO).
// Registration (dashboard → Jetson): the literal datagram "KPIDBG-HELLO".
class DebugLink : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool linkUp       READ linkUp       NOTIFY linkChanged)
    Q_PROPERTY(int  messageCount READ messageCount NOTIFY messageCountChanged)
public:
    explicit DebugLink(QObject *parent = nullptr);

    // jetsonHost: the robot's IP to register with (empty → channel disabled).
    // jetsonPort: the Jetson node's listen port. localPort: where we receive.
    void start(const QString &jetsonHost, quint16 jetsonPort, quint16 localPort);

    bool linkUp() const       { return m_linkUp; }
    int  messageCount() const { return m_count; }

signals:
    // time = "HH:mm:ss", level = DEBUG/INFO/WARN/ERROR/FATAL, text = the message.
    void debugMessage(const QString &time, const QString &level, const QString &text);
    void linkChanged();
    void messageCountChanged();

private slots:
    void onReadyRead();
    void sendHello();
    void checkLink();

private:
    QUdpSocket   *m_sock = nullptr;
    QTimer       *m_hello = nullptr;
    QTimer       *m_watch = nullptr;
    QHostAddress  m_jetson;
    quint16       m_jetsonPort = 0;
    bool          m_haveJetson = false;
    bool          m_linkUp = false;
    int           m_count = 0;
    QElapsedTimer m_clock;
    qint64        m_lastRxMs = -100000;
};
