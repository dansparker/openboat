#include "depth_chart.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "boat/core/nav_math.hpp"

DepthChart::DepthChart(QString path)
    : path_(std::move(path)), connection_(QStringLiteral("depth-") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_);
    db.setDatabaseName(path_);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) return;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT MAX(zoom_level) FROM tiles")) && q.next() && !q.value(0).isNull()) {
        zoom_ = q.value(0).toInt();
        valid_ = true;
    }
}

DepthChart::~DepthChart() {
    {
        auto db = QSqlDatabase::database(connection_, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(connection_);
}

QImage DepthChart::tile(int x, int y) {
    const quint64 key = (static_cast<quint64>(static_cast<quint32>(x)) << 32) | static_cast<quint32>(y);
    const auto it = cache_.constFind(key);
    if (it != cache_.constEnd()) return *it;
    if (cache_.size() > 256) cache_.clear();
    QImage image;
    QSqlQuery q(QSqlDatabase::database(connection_, false));
    q.prepare(QStringLiteral("SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?"));
    q.addBindValue(zoom_);
    q.addBindValue(x);
    q.addBindValue((1 << zoom_) - 1 - y);  // TMS rows
    if (q.exec() && q.next()) {
        QImage decoded;
        if (decoded.loadFromData(q.value(0).toByteArray())) image = decoded.convertToFormat(QImage::Format_ARGB32);
    }
    cache_.insert(key, image);
    return image;
}

std::optional<double> DepthChart::depthAt(double lat, double lon) {
    if (!valid_) return std::nullopt;
    const double n = 256.0 * (1 << zoom_);
    const double s = std::sin(std::clamp(lat, -85.0511, 85.0511) * std::numbers::pi / 180.0);
    const double px = (lon + 180.0) / 360.0 * n;
    const double py = (0.5 - std::log((1 + s) / (1 - s)) / (4 * std::numbers::pi)) * n;
    const int tx = static_cast<int>(std::floor(px / 256.0));
    const int ty = static_cast<int>(std::floor(py / 256.0));
    const QImage img = tile(tx, ty);
    if (img.isNull()) return std::nullopt;
    const int ix = std::clamp(static_cast<int>(px - tx * 256.0), 0, img.width() - 1);
    const int iy = std::clamp(static_cast<int>(py - ty * 256.0), 0, img.height() - 1);
    const QRgb p = img.pixel(ix, iy);
    if (qAlpha(p) == 0) return std::nullopt;
    return (qRed(p) * 256 + qGreen(p)) / 10.0;
}

QVariantMap checkRouteDepth(DepthChart& chart, const QVariantList& points, double safety_m) {
    namespace core = boat::core;
    QVariantList marks;
    double min_depth = -1.0;
    long samples = 0, no_data = 0;
    int shallow_legs = 0;
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const QVariantMap a = points[i].toMap(), b = points[i + 1].toMap();
        const core::GeoPoint from{a.value("lat").toDouble(), a.value("lon").toDouble()};
        const core::GeoPoint to{b.value("lat").toDouble(), b.value("lon").toDouble()};
        const double length = core::distance_m(from, to);
        const double bearing = core::initial_bearing_deg(from, to);
        // about one chart pixel per sample
        const double pixel = 40075016.686 * std::cos(from.lat_deg * std::numbers::pi / 180.0) / (256.0 * (1 << chart.zoom()));
        const int n = std::clamp(static_cast<int>(std::ceil(length / std::max(2.0, pixel))), 1, 20000);
        std::optional<double> leg_min;
        core::GeoPoint leg_min_at = from;
        for (int k = 0; k <= n; ++k) {
            const core::GeoPoint p = core::destination(from, bearing, length * k / n);
            ++samples;
            const auto d = chart.depthAt(p.lat_deg, p.lon_deg);
            if (!d) {
                ++no_data;
                continue;
            }
            if (!leg_min || *d < *leg_min) {
                leg_min = d;
                leg_min_at = p;
            }
        }
        if (leg_min && (min_depth < 0 || *leg_min < min_depth)) min_depth = *leg_min;
        if (leg_min && *leg_min < safety_m) {
            ++shallow_legs;
            marks.append(QVariantMap{{"lat", leg_min_at.lat_deg}, {"lon", leg_min_at.lon_deg}, {"depth", *leg_min},
                                     {"leg", static_cast<int>(i + 1)}});
        }
    }
    const double no_data_percent = samples > 0 ? 100.0 * static_cast<double>(no_data) / static_cast<double>(samples) : 100.0;
    return QVariantMap{{"ok", shallow_legs == 0}, {"shallowLegs", shallow_legs}, {"minDepth", min_depth},
                       {"noDataPercent", no_data_percent}, {"marks", marks}};
}
