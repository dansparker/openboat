#pragma once

// Serves raster tiles from an MBTiles file (SQLite) to QML as
//   image://mbtiles/<z>/<x>/<y>      (XYZ / "slippy map" numbering)
// Tiles are stored in TMS row order inside MBTiles; the provider flips y.
// Use it from QML with Image.asynchronous: false (SQLite connections are
// bound to the thread that opened them).

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

class MbTilesProvider : public QQuickImageProvider {
public:
    struct Info {
        bool valid = false;
        QString name;
        QString attribution;  // source / licence, shown on the chart
        bool depth_encoded = false;  // tools/make_depth.py: tiles carry depths, coloured on load
        int min_zoom = 0;
        int max_zoom = 0;
        QString error;
    };

    // depth_encoded: tiles are coloured by styleDepthTile(); the request id then ends in
    // "?s=<safety depth m>" (part of the id, so a new safety depth bypasses the cache)
    explicit MbTilesProvider(QString path, bool depth_encoded = false);
    ~MbTilesProvider() override;

    // Opens the file once to read metadata (and validates it).
    [[nodiscard]] static Info inspect(const QString& path);

    QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;

private:
    QString path_;
    QString connection_;
    bool depth_encoded_ = false;
    QMutex mutex_;
};
