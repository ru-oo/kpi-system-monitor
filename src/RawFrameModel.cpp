#include "RawFrameModel.h"

void RawFrameModel::onFrame(quint32 id, const QByteArray &payload, qint64 tsMs) {
    double rate = 0.0;
    const auto it = m_lastTs.constFind(id);
    if (it != m_lastTs.constEnd()) {
        const qint64 dt = tsMs - it.value();
        if (dt > 0) rate = 1000.0 / dt;
    }
    m_lastTs[id] = tsMs;

    m_rows.prepend({ id, static_cast<int>(payload.size()), payload, tsMs, rate });
    while (m_rows.size() > m_capacity) m_rows.removeLast();
    ++m_total;
    m_dirty = true;   // UI picks it up on the refresh tick
}

QVariantList RawFrameModel::frames() const {
    QVariantList out;
    out.reserve(m_rows.size());
    for (const Row &r : m_rows) {
        QVariantMap m;
        m["idHex"]   = QStringLiteral("0x") + QString::number(r.id, 16).toUpper();
        m["dlc"]     = r.dlc;
        m["hex"]     = QString::fromLatin1(r.bytes.toHex(' ')).toUpper();
        m["rateHz"]  = r.rateHz;
        m["counter"] = r.dlc > 0 ? (int)(quint8)r.bytes[r.dlc - 1] : 0;  // DBC counter = last byte
        out.push_back(m);
    }
    return out;
}
