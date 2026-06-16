#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QVector>
#include <QPointF>

// LaneMapModel — loads the offline HD lane map as WGS84 GeoJSON and converts it
// to a local ENU metre frame AT RUNTIME using the Map_Datum (0x108) origin, so
// lanes, ego (0x107) and goal (0x201) all share one "metres from datum" frame.
// Main-thread object (QML binds it directly), modelled on MapModel.
//
// File: maps/testarea_all_wgs84.geojson — a GeoJSON FeatureCollection. Each
// feature has properties.category and geometry (Point/LineString/Polygon) with
// coordinates in [lon, lat] order (GeoJSON convention). We do NOT pre-bake
// metres — the datum arrives at runtime.
//
// categories: centerline (drivable ref — goals snap here), lane_marking,
// stop_line, surface_mark, traffic_light, sign, node, pole, guardrail, lane_area.
// No lanelet2 routing — render polylines + snap goal to nearest centerline only.
class LaneMapModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    loaded    READ loaded    NOTIFY laneChanged)
    Q_PROPERTY(QString name      READ name      NOTIFY laneChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY laneChanged)
    Q_PROPERTY(int     featureCount READ featureCount NOTIFY laneChanged)
    Q_PROPERTY(bool    hasDatum  READ hasDatum  NOTIFY laneChanged)   // ENU computed?
    Q_PROPERTY(double  minX      READ minX      NOTIFY laneChanged)   // ENU extent (m)
    Q_PROPERTY(double  minY      READ minY      NOTIFY laneChanged)
    Q_PROPERTY(double  maxX      READ maxX      NOTIFY laneChanged)
    Q_PROPERTY(double  maxY      READ maxY      NOTIFY laneChanged)

public:
    explicit LaneMapModel(QObject *parent = nullptr) : QObject(parent) {}

    bool    loaded() const       { return m_loaded; }
    QString name() const         { return m_name; }
    QString loadError() const    { return m_loadError; }
    int     featureCount() const { return m_featureCount; }
    bool    hasDatum() const     { return m_hasDatum; }
    double  minX() const { return m_minX; }
    double  minY() const { return m_minY; }
    double  maxX() const { return m_maxX; }
    double  maxY() const { return m_maxY; }

    Q_INVOKABLE bool loadLanes(const QString &path);     // parse WGS84 geojson
    // Set the local-frame origin (from Map_Datum 0x108) and (re)compute ENU.
    Q_INVOKABLE void setDatum(double lat0, double lon0);

    // ENU geometry for QML painting: per feature { type, coords:[QPointF...] }.
    Q_INVOKABLE QVariantList featuresByCategory(const QString &category) const;
    // Nearest point on nearest centerline (ENU). { found, x, y, headingDeg, dist }.
    Q_INVOKABLE QVariantMap nearestCenterlinePoint(double mx, double my) const;

signals:
    void laneChanged();

private:
    struct Feature {
        QString type;                 // "line" | "point" | "polygon"
        QVector<QPointF> ll;          // raw WGS84 (x=lon, y=lat)
        QVector<QPointF> enu;         // converted ENU metres (x=East, y=North)
    };
    void recomputeEnu();

    bool    m_loaded = false;
    bool    m_hasDatum = false;
    bool    m_isLocalMetre = false;   // file already in local ENU metres (no datum needed)
    bool    m_hasLocalOrigin = false; // local file carries its own WGS84 origin
    double  m_localOriginLon = 0, m_localOriginLat = 0;  // file origin [lon,lat]
    QString m_name, m_loadError;
    int     m_featureCount = 0;
    double  m_lat0 = 0, m_lon0 = 0;
    double  m_minX = 0, m_minY = 0, m_maxX = 0, m_maxY = 0;   // ENU extent
    QHash<QString, QVector<Feature>> m_byCategory;
};
