#pragma once

#include <QObject>
#include <QVariantList>
#include <QByteArray>
#include <QList>
#include <QHash>
#include <QTimer>

// RawFrameModel — a small main-thread ring buffer of the most recent raw CAN
// frames actually received on the bus, for bus-level observation evidence
// ("observe on the bus, not endpoint logs"). Fed via the same queued
// CanBridge::frameForRecord(id, payload, tsMs) connection RunRecorder uses.
//
// The UI view (QVariantList) is refreshed on a timer (not per-frame) so a busy
// bus can't thrash QML bindings.
class RawFrameModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList frames READ frames NOTIFY framesChanged)  // newest first
    Q_PROPERTY(int total READ total NOTIFY framesChanged)

public:
    explicit RawFrameModel(int capacity = 64, QObject *parent = nullptr)
        : QObject(parent), m_capacity(capacity) {
        m_refresh.setInterval(200);          // 5 Hz UI refresh
        connect(&m_refresh, &QTimer::timeout, this, [this]() {
            if (m_dirty) { m_dirty = false; emit framesChanged(); }
        });
        m_refresh.start();
    }

    QVariantList frames() const;
    int total() const { return m_total; }

public slots:
    void onFrame(quint32 id, const QByteArray &payload, qint64 tsMs); // queued from CanBridge

signals:
    void framesChanged();

private:
    struct Row { quint32 id; int dlc; QByteArray bytes; qint64 ts; double rateHz; };
    int               m_capacity;
    QList<Row>        m_rows;                 // newest at front
    QHash<quint32, qint64> m_lastTs;          // per-ID last timestamp → rate
    int               m_total = 0;
    bool              m_dirty = false;
    QTimer            m_refresh;
};
