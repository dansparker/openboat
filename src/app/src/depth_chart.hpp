#pragma once

// Reads depths from a depth-encoded MBTiles chart (tools/make_depth.py) at its
// highest zoom level - for checks such as "does this route cross water
// shallower than the safety depth?". Not thread-safe; one instance per thread.

#include <QHash>
#include <QImage>
#include <QString>
#include <QVariantList>

#include <optional>

class DepthChart {
public:
    explicit DepthChart(QString path);
    ~DepthChart();
    DepthChart(const DepthChart&) = delete;
    DepthChart& operator=(const DepthChart&) = delete;

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] int zoom() const { return zoom_; }

    // Depth in metres at a position; nullopt = no depth data (land, outside the chart)
    [[nodiscard]] std::optional<double> depthAt(double lat, double lon);

private:
    QImage tile(int x, int y);

    QString path_;
    QString connection_;
    bool valid_ = false;
    int zoom_ = 0;
    QHash<quint64, QImage> cache_;
};

// Checks the legs of a route [{lat, lon}] against the safety depth: samples every
// few metres (about one chart pixel). Result:
//   { ok, shallowLegs, minDepth (-1 = none), noDataPercent,
//     marks: [{ lat, lon, depth, leg }]  (shallowest point of each shallow leg) }
[[nodiscard]] QVariantMap checkRouteDepth(DepthChart& chart, const QVariantList& points, double safety_m);

// Bridges, overhead cables and pipes (tools/make_enc.py *.clearances.json) that the
// route passes under: { crossings: [{ lat, lon, clearance, name, kind, leg, low }], lowCount }.
// low: clearance below the boat's air draught (air_draft_m <= 0: not set, nothing is low).
// clearances: [{ kind, name, clearance, lines: [ [ [lon, lat], ... ], ... ] }]
[[nodiscard]] QVariantMap checkRouteClearance(const QVariantList& points, const QVariantList& clearances,
                                              double air_draft_m);
