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

namespace {

struct XY {
    double x, y;
};

// Intersection of segments ab and cd (planar), parameter along ab; nullopt if none
std::optional<double> intersect(XY a, XY b, XY c, XY d) {
    const double rx = b.x - a.x, ry = b.y - a.y, sx = d.x - c.x, sy = d.y - c.y;
    const double den = rx * sy - ry * sx;
    if (std::abs(den) < 1e-15) return std::nullopt;  // parallel
    const double t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / den;
    const double u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return std::nullopt;
    return t;
}

}  // namespace

QVariantMap checkRouteClearance(const QVariantList& points, const QVariantList& clearances, double air_draft_m) {
    QVariantList crossings;
    int low = 0;
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const QVariantMap pa = points[i].toMap(), pb = points[i + 1].toMap();
        // local plane: longitude scaled by cos(latitude) - fine for the few km of a leg
        const double k = std::cos(pa.value("lat").toDouble() * std::numbers::pi / 180.0);
        const XY a{pa.value("lon").toDouble() * k, pa.value("lat").toDouble()};
        const XY b{pb.value("lon").toDouble() * k, pb.value("lat").toDouble()};
        for (const QVariant& cv : clearances) {
            const QVariantMap c = cv.toMap();
            std::optional<double> best;
            for (const QVariant& lv : c.value("lines").toList()) {
                const QVariantList line = lv.toList();
                for (qsizetype j = 0; j + 1 < line.size(); ++j) {
                    const QVariantList p = line[j].toList(), q = line[j + 1].toList();
                    if (p.size() < 2 || q.size() < 2) continue;
                    const auto t = intersect(a, b, {p[0].toDouble() * k, p[1].toDouble()}, {q[0].toDouble() * k, q[1].toDouble()});
                    if (t && (!best || *t < *best)) best = t;
                }
            }
            if (!best) continue;
            const double clearance = c.value("clearance").toDouble();
            const bool is_low = air_draft_m > 0.0 && clearance < air_draft_m;
            if (is_low) ++low;
            crossings.append(QVariantMap{{"lat", a.y + (b.y - a.y) * *best}, {"lon", (a.x + (b.x - a.x) * *best) / k},
                                         {"clearance", clearance}, {"name", c.value("name")}, {"kind", c.value("kind")},
                                         {"leg", static_cast<int>(i + 1)}, {"low", is_low}});
        }
    }
    return QVariantMap{{"crossings", crossings}, {"lowCount", low}};
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
