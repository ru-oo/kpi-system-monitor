#include "LaneMapModel.h"

#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <cmath>
#include <limits>

// WGS84 geodetic -> local ENU via ECEF (정밀 측지 변환). x=East m, y=North m,
// relative to datum. 시스템(gps_enu_bridge·map_offset)이 ecef_enu라 UI도 동일
// 모델로 통일 — equirectangular 근사와는 이 구역에서 ~0.95 m 어긋나 ego/지도가
// 틀어졌었다(2026-06-15 mapcal로 시스템이 ecef로 바뀐 뒤 UI만 옛 모델로 남음).
static QPointF toEnu(double lat, double lon, double lat0, double lon0) {
    const double D = M_PI / 180.0, A = 6378137.0;
    const double F = 1.0 / 298.257223563, E2 = F * (2.0 - F);
    auto ecef = [&](double la, double lo, double &X, double &Y, double &Z) {
        const double s = std::sin(la * D), c = std::cos(la * D);
        const double N = A / std::sqrt(1.0 - E2 * s * s);
        X = N * c * std::cos(lo * D);
        Y = N * c * std::sin(lo * D);
        Z = (N * (1.0 - E2)) * s;
    };
    double x0, y0, z0, x, y, z;
    ecef(lat0, lon0, x0, y0, z0);
    ecef(lat, lon, x, y, z);
    const double dx = x - x0, dy = y - y0, dz = z - z0;
    const double sl = std::sin(lat0 * D), cl = std::cos(lat0 * D);
    const double so = std::sin(lon0 * D), co = std::cos(lon0 * D);
    const double east  = -so * dx + co * dy;
    const double north = -sl * co * dx - sl * so * dy + cl * dz;
    return QPointF(east, north);
}

bool LaneMapModel::loadLanes(const QString &path) {
    QString resolved = path;
    if (resolved.startsWith("file://")) resolved = QUrl(resolved).toLocalFile();

    auto fail = [&](const QString &why) {
        qWarning().noquote() << "[LaneMapModel]" << why;
        m_loadError = why; emit laneChanged(); return false;
    };
    if (resolved.isEmpty())           return fail(QStringLiteral("HD lane path is empty"));
    if (!QFileInfo::exists(resolved)) return fail(QStringLiteral("HD lane file not found: %1").arg(resolved));

    QFile f(resolved);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("cannot open HD lane file: %1").arg(resolved));
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("invalid HD lane JSON: %1").arg(resolved));

    // Two accepted formats:
    //  (a) GeoJSON FeatureCollection — coords in [lon,lat] (WGS84) → ENU-converted
    //      at runtime against the Map_Datum (0x108 / config).
    //  (b) {features:[{category,type,coords:[{x,y}]}]} with a top-level
    //      "frame":"local_metre..." — coords ALREADY in local ENU metres → used
    //      directly, NO datum needed (renders immediately).
    QHash<QString, QVector<Feature>> byCat;
    int count = 0;
    const QJsonObject root = doc.object();
    const QJsonArray feats = root.value("features").toArray();
    const QString frame = root.value("frame").toString();
    // Local-metre if the frame says so, or if no feature carries GeoJSON geometry.
    bool isLocal = frame.contains("local", Qt::CaseInsensitive)
                   || frame.contains("metre", Qt::CaseInsensitive)
                   || frame.contains("meter", Qt::CaseInsensitive);
    if (!isLocal && !feats.isEmpty() && !feats.first().toObject().contains("geometry"))
        isLocal = true;

    auto pushLL = [](Feature &ft, double a, double b) { ft.ll.push_back(QPointF(a, b)); };

    for (const QJsonValue &fv : feats) {
        const QJsonObject fo = fv.toObject();
        QString cat, gtype;
        Feature ft;

        if (fo.contains("geometry")) {                  // GeoJSON
            cat = fo.value("properties").toObject().value("category").toString();
            const QJsonObject geom = fo.value("geometry").toObject();
            const QString gt = geom.value("type").toString();
            const QJsonArray co = geom.value("coordinates").toArray();
            if (gt == "Point") {
                ft.type = "point";
                if (co.size() >= 2) pushLL(ft, co[0].toDouble(), co[1].toDouble());
            } else if (gt == "LineString") {
                ft.type = "line";
                for (const QJsonValue &pv : co) { const QJsonArray pr = pv.toArray();
                    if (pr.size() >= 2) pushLL(ft, pr[0].toDouble(), pr[1].toDouble()); }
            } else if (gt == "Polygon") {
                ft.type = "polygon";
                const QJsonArray ring = co.isEmpty() ? QJsonArray() : co[0].toArray();
                for (const QJsonValue &pv : ring) { const QJsonArray pr = pv.toArray();
                    if (pr.size() >= 2) pushLL(ft, pr[0].toDouble(), pr[1].toDouble()); }
            }
        } else {                                        // {category,type,coords}
            cat = fo.value("category").toString();
            ft.type = fo.value("type").toString();
            const QJsonValue cv = fo.value("coords");
            if (ft.type == "point") { const QJsonObject po = cv.toObject();
                pushLL(ft, po.value("x").toDouble(), po.value("y").toDouble()); }
            else for (const QJsonValue &pv : cv.toArray()) { const QJsonObject po = pv.toObject();
                pushLL(ft, po.value("x").toDouble(), po.value("y").toDouble()); }
        }
        if (cat.isEmpty() || ft.type.isEmpty() || ft.ll.isEmpty()) continue;
        byCat[cat].push_back(ft);
        ++count;
    }
    if (count == 0) return fail(QStringLiteral("no usable features in: %1").arg(resolved));

    m_byCategory   = byCat;
    m_featureCount = count;
    m_isLocalMetre = isLocal;
    // Local-metre coords are offsets from the FILE's own WGS84 origin, not from
    // the current Map_Datum. Read it so recomputeEnu can re-base the lanes onto
    // the live datum (origin_wgs84 / origin = [lon, lat]).
    m_hasLocalOrigin = false;
    if (m_isLocalMetre) {
        QJsonValue ov = root.value("origin_wgs84");
        if (!ov.isArray()) ov = root.value("enu_origin_wgs84");   // testarea_all_enu.json key
        if (!ov.isArray()) ov = root.value("origin");
        const QJsonArray oa = ov.toArray();
        if (oa.size() >= 2) {
            m_localOriginLon = oa[0].toDouble();
            m_localOriginLat = oa[1].toDouble();
            m_hasLocalOrigin = true;
        } else {
            qWarning().noquote()
                << "[LaneMapModel] local file has no origin_wgs84/origin —"
                << "offset 0, lanes may be mis-aligned vs datum [확인필요]:" << resolved;
        }
    }
    m_name = QFileInfo(resolved).completeBaseName();
    m_loadError.clear();
    m_loaded = true;
    // Local-metre files need no datum — mark ready and compute the extent now so
    // they render immediately (even with no CAN / no 0x108).
    if (m_isLocalMetre) m_hasDatum = true;
    qInfo().nospace() << "[LaneMapModel] loaded " << m_featureCount << " features ("
                      << (m_isLocalMetre ? "local metre" : "WGS84") << ") from "
                      << QFileInfo(resolved).fileName();
    if (m_hasDatum) recomputeEnu();   // datum present (or local) → build ENU now
    emit laneChanged();
    return true;
}

void LaneMapModel::setDatum(double lat0, double lon0) {
    m_lat0 = lat0; m_lon0 = lon0; m_hasDatum = true;
    recomputeEnu();
    qInfo().nospace() << "[LaneMapModel] datum set (" << lat0 << "," << lon0
                      << ") → ENU extent x[" << m_minX << "," << m_maxX
                      << "] y[" << m_minY << "," << m_maxY << "]";
    emit laneChanged();
}

void LaneMapModel::recomputeEnu() {
    if (!m_loaded || !m_hasDatum) return;
    // Local-metre re-base: the file's coords are offsets from ITS OWN WGS84
    // origin, so shift them by that origin's ENU position in the current datum.
    // Recomputed here, so a new datum (0x108) re-aligns the lanes dynamically.
    // (Offset 0 when the origin is unknown — falls back to raw local coords.)
    const QPointF localOff = (m_isLocalMetre && m_hasLocalOrigin)
        ? toEnu(m_localOriginLat, m_localOriginLon, m_lat0, m_lon0)
        : QPointF(0, 0);
    double mnX = std::numeric_limits<double>::max(), mnY = mnX;
    double mxX = std::numeric_limits<double>::lowest(), mxY = mxX;
    for (auto it = m_byCategory.begin(); it != m_byCategory.end(); ++it) {
        for (Feature &ft : it.value()) {
            ft.enu.resize(ft.ll.size());
            for (int i = 0; i < ft.ll.size(); ++i) {
                // Local-metre files: coords are file-origin-relative ENU → add
                // the origin offset. WGS84: convert (ll = lon,lat) vs the datum.
                const QPointF e = m_isLocalMetre
                    ? QPointF(ft.ll[i].x() + localOff.x(), ft.ll[i].y() + localOff.y())
                    : toEnu(ft.ll[i].y(), ft.ll[i].x(), m_lat0, m_lon0);
                ft.enu[i] = e;
                mnX = std::min(mnX, e.x()); mnY = std::min(mnY, e.y());
                mxX = std::max(mxX, e.x()); mxY = std::max(mxY, e.y());
            }
        }
    }
    m_minX = mnX; m_minY = mnY; m_maxX = mxX; m_maxY = mxY;
}

QVariantList LaneMapModel::featuresByCategory(const QString &category) const {
    QVariantList out;
    if (!m_hasDatum) return out;        // no ENU yet → nothing to draw
    const auto it = m_byCategory.constFind(category);
    if (it == m_byCategory.constEnd()) return out;
    for (const Feature &ft : it.value()) {
        QVariantMap m;
        m["type"] = ft.type;
        QVariantList pts;
        pts.reserve(ft.enu.size());
        for (const QPointF &p : ft.enu) pts.push_back(p);
        m["coords"] = pts;
        out.push_back(m);
    }
    return out;
}

QVariantMap LaneMapModel::nearestCenterlinePoint(double mx, double my) const {
    QVariantMap out; out["found"] = false;
    if (!m_hasDatum) return out;
    const auto it = m_byCategory.constFind(QStringLiteral("centerline"));
    if (it == m_byCategory.constEnd()) return out;

    double bestD2 = std::numeric_limits<double>::max();
    QPointF best; double bestHeading = 0.0;
    for (const Feature &ft : it.value()) {
        const QVector<QPointF> &poly = ft.enu;
        for (int i = 0; i + 1 < poly.size(); ++i) {
            const QPointF a = poly[i], b = poly[i + 1];
            const double vx = b.x() - a.x(), vy = b.y() - a.y();
            const double len2 = vx * vx + vy * vy;
            double t = len2 > 0 ? ((mx - a.x()) * vx + (my - a.y()) * vy) / len2 : 0.0;
            t = qBound(0.0, t, 1.0);
            const double px = a.x() + t * vx, py = a.y() + t * vy;
            const double d2 = (mx - px) * (mx - px) + (my - py) * (my - py);
            if (d2 < bestD2) { bestD2 = d2; best = QPointF(px, py);
                bestHeading = std::atan2(vy, vx) * 180.0 / M_PI; }
        }
    }
    if (bestD2 == std::numeric_limits<double>::max()) return out;
    out["found"] = true; out["x"] = best.x(); out["y"] = best.y();
    out["headingDeg"] = bestHeading; out["dist"] = std::sqrt(bestD2);
    return out;
}
