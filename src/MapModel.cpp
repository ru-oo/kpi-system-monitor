#include "MapModel.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <QUrl>
#include <QDebug>

MapModel::MapModel(MapImageProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {}

// Minimal flat-YAML parser (map.yaml is a fixed, shallow schema).
bool MapModel::parseYaml(const QString &path, QString *imageFile,
                         double *res, double *ox, double *oy, double *oth,
                         int *negate, double *occ, double *freeT) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        const int hash = line.indexOf('#');
        if (hash >= 0) line = line.left(hash);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed();
        QString val = line.mid(colon + 1).trimmed();
        if (key == "image") {
            *imageFile = val.remove('"').remove('\'');
        } else if (key == "resolution") {
            *res = val.toDouble();
        } else if (key == "origin") {
            val.remove('[').remove(']');
            const QStringList parts = val.split(',');
            if (parts.size() >= 2) { *ox = parts[0].trimmed().toDouble();
                                     *oy = parts[1].trimmed().toDouble(); }
            if (parts.size() >= 3) *oth = parts[2].trimmed().toDouble();
        } else if (key == "negate") {
            *negate = val.toInt();
        } else if (key == "occupied_thresh") {
            *occ = val.toDouble();
        } else if (key == "free_thresh") {
            *freeT = val.toDouble();
        }
    }
    return !imageFile->isEmpty() && *res > 0;
}

// P5 (binary) / P2 (ASCII) grayscale PGM loader.
bool MapModel::loadPgm(const QString &path, QImage *out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray data = f.readAll();
    int pos = 0;
    auto skipWs = [&]() {
        while (pos < data.size()) {
            char c = data[pos];
            if (c == '#') { while (pos < data.size() && data[pos] != '\n') ++pos; }
            else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    };
    auto token = [&]() -> QByteArray {
        skipWs();
        int start = pos;
        while (pos < data.size()) {
            char c = data[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
            ++pos;
        }
        return data.mid(start, pos - start);
    };
    const QByteArray magic = token();
    if (magic != "P5" && magic != "P2") return false;
    const int w = token().toInt();
    const int h = token().toInt();
    const int maxv = token().toInt();
    if (w <= 0 || h <= 0 || maxv <= 0) return false;

    QImage img(w, h, QImage::Format_Grayscale8);
    if (magic == "P5") {
        ++pos;  // single whitespace after maxval, then binary
        if (data.size() - pos < w * h) return false;
        for (int y = 0; y < h; ++y) {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uchar>(data[pos++]);
        }
    } else { // P2 ASCII
        for (int y = 0; y < h; ++y) {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uchar>(token().toInt());
        }
    }
    *out = img;
    return true;
}

void MapModel::loadPresets(const QString &baseNoExt) {
    m_presets.clear();
    QFile f(baseNoExt + ".presets.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QJsonObject p = it.value().toObject();
        QVariantMap m;
        m["name"] = it.key();
        m["x"]    = p.value("x").toDouble();
        m["y"]    = p.value("y").toDouble();
        m["yaw"]  = p.value("yaw").toDouble();
        m_presets.push_back(m);
    }
}

bool MapModel::loadMap(const QString &yamlPath) {
    QString resolved = yamlPath;
    if (resolved.startsWith("file://")) resolved = QUrl(resolved).toLocalFile();

    auto fail = [&](const QString &why) {
        qWarning().noquote() << "[MapModel]" << why;
        m_loadError = why;
        // Keep any previously-loaded map intact; just report the error.
        emit mapChanged();
        return false;
    };

    if (resolved.isEmpty())
        return fail(QStringLiteral("map path is empty (check tactical.map_path)"));
    if (!QFileInfo::exists(resolved))
        return fail(QStringLiteral("map file not found: %1").arg(resolved));

    QString imageFile;
    double res = 0.05, ox = 0, oy = 0, oth = 0, occ = 0.65, freeT = 0.196;
    int negate = 0;
    if (!parseYaml(resolved, &imageFile, &res, &ox, &oy, &oth, &negate, &occ, &freeT))
        return fail(QStringLiteral("invalid map YAML: %1").arg(resolved));

    const QFileInfo yi(resolved);
    const QString pgmPath = QDir(yi.absolutePath()).filePath(imageFile);
    if (!QFileInfo::exists(pgmPath))
        return fail(QStringLiteral("map image not found: %1").arg(pgmPath));
    QImage img;
    if (!loadPgm(pgmPath, &img))
        return fail(QStringLiteral("cannot decode PGM: %1").arg(pgmPath));

    m_image = img;
    m_loadError.clear();
    m_resolution = res; m_originX = ox; m_originY = oy; m_originTheta = oth;
    m_negate = negate; m_occupiedThresh = occ; m_freeThresh = freeT;
    m_name = yi.completeBaseName();
    m_loaded = true;
    ++m_version;
    m_source = QStringLiteral("image://tacticalmap/%1").arg(m_version);
    if (m_provider) m_provider->setImage(m_image);
    loadPresets(QDir(yi.absolutePath()).filePath(m_name));

    qInfo().nospace() << "[MapModel] loaded " << m_name << " "
                      << m_image.width() << "x" << m_image.height()
                      << " res=" << m_resolution << " origin=(" << m_originX
                      << "," << m_originY << ")";
    emit mapChanged();
    return true;
}

bool MapModel::isFree(double mx, double my) const {
    if (!m_loaded) return false;
    const QPointF px = mapToPixel(mx, my);
    const int x = qRound(px.x()), y = qRound(px.y());
    if (x < 0 || y < 0 || x >= m_image.width() || y >= m_image.height()) return false;
    const int v = qGray(m_image.pixel(x, y));   // grayscale 0..255
    // ROS: occupancy p = (255 - v)/255 (negate=0). free if p <= free_thresh.
    double p = (m_negate ? v : (255 - v)) / 255.0;
    return p <= m_freeThresh;                    // free/drivable only
}
