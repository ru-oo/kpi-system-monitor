#pragma once

#include <QObject>
#include <QImage>
#include <QPointF>
#include <QVariantList>
#include <QMutex>
#include <QQuickImageProvider>

// Serves the loaded occupancy-grid QImage to QML via image://tacticalmap/<ver>.
// requestImage() is invoked on QML's async image-loader thread, while setImage()
// is called from the main thread on map (re)load — guard the shared QImage.
class MapImageProvider : public QQuickImageProvider {
public:
    MapImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        Q_UNUSED(id)
        QMutexLocker lock(&m_mutex);
        if (size) *size = m_image.size();
        return m_image;
    }
    void setImage(const QImage &img) { QMutexLocker lock(&m_mutex); m_image = img; }
private:
    QMutex m_mutex;
    QImage m_image;
};

// MapModel — loads a ROS occupancy grid (<name>.pgm + <name>.yaml) from disk
// and is the single source of spatial truth for the Tactical page. Exposes
// exact pixel↔map conversions, an occupancy test, and per-map presets.
//
// ROS conventions handled explicitly:
//   • origin = real-world coord of the LOWER-LEFT pixel
//   • image row 0 is the TOP → y axis is flipped vs the map frame
class MapModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    loaded     READ loaded     NOTIFY mapChanged)
    Q_PROPERTY(QString name       READ name       NOTIFY mapChanged)
    Q_PROPERTY(QString source     READ source     NOTIFY mapChanged)   // image:// url
    Q_PROPERTY(int     widthPx    READ widthPx    NOTIFY mapChanged)
    Q_PROPERTY(int     heightPx   READ heightPx   NOTIFY mapChanged)
    Q_PROPERTY(double  resolution READ resolution NOTIFY mapChanged)
    Q_PROPERTY(double  originX    READ originX    NOTIFY mapChanged)
    Q_PROPERTY(double  originY    READ originY    NOTIFY mapChanged)
    Q_PROPERTY(double  originTheta READ originTheta NOTIFY mapChanged)
    Q_PROPERTY(QVariantList presets READ presets  NOTIFY mapChanged)
    Q_PROPERTY(QString loadError  READ loadError  NOTIFY mapChanged)   // "" when ok

public:
    explicit MapModel(MapImageProvider *provider, QObject *parent = nullptr);

    bool    loaded() const     { return m_loaded; }
    QString loadError() const  { return m_loadError; }
    QString name() const       { return m_name; }
    QString source() const     { return m_source; }
    int     widthPx() const    { return m_image.width(); }
    int     heightPx() const   { return m_image.height(); }
    double  resolution() const { return m_resolution; }
    double  originX() const    { return m_originX; }
    double  originY() const    { return m_originY; }
    double  originTheta() const { return m_originTheta; }
    QVariantList presets() const { return m_presets; }

    // Load <yamlPath> (+ sibling pgm + optional presets). Returns success.
    Q_INVOKABLE bool loadMap(const QString &yamlPath);

    // ── exact conversions (kept in one place so overlays & clicks agree) ──
    // pixel (px,py with py measured from the TOP) ↔ map frame (mx,my).
    Q_INVOKABLE QPointF pixelToMap(double px, double py) const {
        return QPointF(m_originX + px * m_resolution,
                       m_originY + (heightPx() - py) * m_resolution);
    }
    Q_INVOKABLE QPointF mapToPixel(double mx, double my) const {
        return QPointF((mx - m_originX) / m_resolution,
                       heightPx() - (my - m_originY) / m_resolution);
    }

    // Is the map cell at map coord (mx,my) free/drivable? (rejects occupied/unknown)
    Q_INVOKABLE bool isFree(double mx, double my) const;

signals:
    void mapChanged();

private:
    bool parseYaml(const QString &path, QString *imageFile,
                   double *res, double *ox, double *oy, double *oth,
                   int *negate, double *occ, double *freeT);
    bool loadPgm(const QString &path, QImage *out);
    void loadPresets(const QString &baseNoExt);

    MapImageProvider *m_provider;
    QImage  m_image;
    bool    m_loaded = false;
    QString m_loadError;
    QString m_name, m_source;
    double  m_resolution = 0.05, m_originX = 0, m_originY = 0, m_originTheta = 0;
    double  m_occupiedThresh = 0.65, m_freeThresh = 0.196;
    int     m_negate = 0;
    int     m_version = 0;          // bumps the image:// url to bust QML cache
    QVariantList m_presets;
};
