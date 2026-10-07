#include "depth_style.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr std::array<double, 12> kLevels{2, 3, 5, 10, 15, 20, 30, 50, 100, 200, 500, 1000};

const QRgb kShallow = qRgba(110, 165, 220, 170);
const QRgb kDeep = qRgba(255, 255, 255, 110);
const QRgb kContour = qRgba(60, 90, 130, 255);
const QRgb kSafety = qRgba(20, 40, 90, 255);

}  // namespace

QImage styleDepthTile(const QImage& encoded, double safety_m, double deep_m) {
    const QImage in = encoded.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width();
    const int h = in.height();
    // depth per pixel, NaN = no data; kind 1 = surface, 2 = vector contour
    std::vector<float> depth(static_cast<std::size_t>(w * h), NAN);
    std::vector<unsigned char> kind(static_cast<std::size_t>(w * h), 0);
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            if (qAlpha(p) == 0) continue;
            const auto i = static_cast<std::size_t>(y * w + x);
            depth[i] = static_cast<float>((qRed(p) * 256 + qGreen(p)) / 10.0);
            kind[i] = static_cast<unsigned char>(qBlue(p));
        }
    }

    QImage out(w, h, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    const auto at = [&](int x, int y) { return static_cast<std::size_t>(y * w + x); };
    const auto put = [&](int x, int y, QRgb c) {
        if (x >= 0 && y >= 0 && x < w && y < h) reinterpret_cast<QRgb*>(out.scanLine(y))[x] = c;
    };

    // 1. shading
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = at(x, y);
            if (kind[i] != 1) continue;
            if (depth[i] < safety_m) put(x, y, kShallow);
            else if (depth[i] >= deep_m) put(x, y, kDeep);
        }
    }

    // 2. contours from the depth surface: a level is crossed between neighbours
    //    (land/no-data neighbours are not a contour - the shore is on the base chart)
    const auto crossed = [&](std::size_t a, std::size_t b, double level) {
        return kind[a] == 1 && kind[b] == 1 && ((depth[a] < level) != (depth[b] < level));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = at(x, y);
            if (kind[i] != 1) continue;
            const bool right = x + 1 < w;
            const bool down = y + 1 < h;
            if (right && crossed(i, at(x + 1, y), safety_m)) {
                put(x, y, kSafety);  // heavy: two pixels wide
                put(x + 1, y, kSafety);
                continue;
            }
            if (down && crossed(i, at(x, y + 1), safety_m)) {
                put(x, y, kSafety);
                put(x, y + 1, kSafety);
                continue;
            }
            for (const double level : kLevels) {
                if (std::abs(level - safety_m) < 0.05) continue;
                if ((right && crossed(i, at(x + 1, y), level)) || (down && crossed(i, at(x, y + 1), level))) {
                    put(x, y, kContour);
                    break;
                }
            }
        }
    }

    // 3. contour lines from vector data (S-57): the one at the safety depth heavy
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = at(x, y);
            if (kind[i] != 2) continue;
            const bool safety = std::abs(depth[i] - safety_m) < 0.05;
            put(x, y, safety ? kSafety : kContour);
            if (safety) put(x + 1, y, kSafety);
        }
    }
    return out;
}
