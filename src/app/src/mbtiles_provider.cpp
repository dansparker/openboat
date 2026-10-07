#include "mbtiles_provider.hpp"

#include "depth_style.hpp"

#include <QFileInfo>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

#include <utility>

namespace {

constexpr int kTileSize = 256;

QImage empty_tile() {
    QImage image(kTileSize, kTileSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

QSqlDatabase open_readonly(const QString& path, const QString& connection) {
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    db.open();
    return db;
}

}  // namespace

MbTilesProvider::MbTilesProvider(QString path, bool depth_encoded)
    : QQuickImageProvider(QQuickImageProvider::Image),
      path_(std::move(path)),
      connection_(QStringLiteral("mbtiles-") + QUuid::createUuid().toString(QUuid::WithoutBraces)),
      depth_encoded_(depth_encoded) {
    open_readonly(path_, connection_);
}

MbTilesProvider::~MbTilesProvider() {
    {
        auto db = QSqlDatabase::database(connection_, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(connection_);
}

MbTilesProvider::Info MbTilesProvider::inspect(const QString& path) {
    Info info;
    if (!QFileInfo::exists(path)) {
        info.error = QStringLiteral("file not found: %1").arg(path);
        return info;
    }
    const QString connection = QStringLiteral("mbtiles-inspect-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        auto db = open_readonly(path, connection);
        if (!db.isOpen()) {
            info.error = db.lastError().text();
        } else {
            QSqlQuery meta(db);
            if (meta.exec(QStringLiteral("SELECT name, value FROM metadata"))) {
                while (meta.next()) {
                    const QString key = meta.value(0).toString();
                    if (key == QLatin1String("name")) info.name = meta.value(1).toString();
                    if (key == QLatin1String("attribution")) info.attribution = meta.value(1).toString();
                    if (key == QLatin1String("encoding")) {
                        info.depth_encoded = meta.value(1).toString() == QLatin1String("openboat-depth-dm-v1");
                    }
                    if (key == QLatin1String("minzoom")) info.min_zoom = meta.value(1).toInt();
                    if (key == QLatin1String("maxzoom")) info.max_zoom = meta.value(1).toInt();
                }
            }
            // Fall back to the actual tile range when metadata lacks zoom levels
            QSqlQuery range(db);
            if (range.exec(QStringLiteral("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles")) && range.next() &&
                !range.value(0).isNull()) {
                if (info.max_zoom == 0) {
                    info.min_zoom = range.value(0).toInt();
                    info.max_zoom = range.value(1).toInt();
                }
                info.valid = true;
            } else {
                info.error = QStringLiteral("no 'tiles' table in %1").arg(path);
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return info;
}

QImage MbTilesProvider::requestImage(const QString& id, QSize* size, const QSize& /*requested_size*/) {
    QImage image = empty_tile();
    // optional query: "z/x/y?s=3.0"
    const qsizetype q = id.indexOf(QLatin1Char('?'));
    double safety = 3.0;
    if (q >= 0) {
        for (const auto& kv : id.mid(q + 1).split(QLatin1Char('&'))) {
            if (kv.startsWith(QLatin1String("s="))) safety = kv.mid(2).toDouble();
        }
    }
    const QStringList parts = (q >= 0 ? id.left(q) : id).split(QLatin1Char('/'));
    if (parts.size() == 3) {
        const int z = parts[0].toInt();
        const int x = parts[1].toInt();
        const int y = parts[2].toInt();
        const int tms_row = (1 << z) - 1 - y;

        QMutexLocker lock(&mutex_);
        auto db = QSqlDatabase::database(connection_, false);
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?"));
        query.addBindValue(z);
        query.addBindValue(x);
        query.addBindValue(tms_row);
        if (query.exec() && query.next()) {
            QImage decoded;
            if (decoded.loadFromData(query.value(0).toByteArray())) {
                image = depth_encoded_ ? styleDepthTile(decoded, safety, deepDepthFor(safety)) : decoded;
            }
        }
    }
    if (size != nullptr) *size = image.size();
    return image;
}
