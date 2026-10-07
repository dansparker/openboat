#include "boat/core/nav_math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace boat::core {

namespace {

constexpr double kDeg = std::numbers::pi / 180.0;

// Local east/north offset of b relative to a in metres.
void local_en(const GeoPoint& a, const GeoPoint& b, double& east, double& north) {
    const double lat0 = a.lat_deg * kDeg;
    double dlon = b.lon_deg - a.lon_deg;
    if (dlon > 180.0) dlon -= 360.0;
    if (dlon < -180.0) dlon += 360.0;
    east = dlon * kDeg * std::cos(lat0) * kEarthRadiusM;
    north = (b.lat_deg - a.lat_deg) * kDeg * kEarthRadiusM;
}

}  // namespace

std::int64_t unix_ms(int year, int month, int day, int hour, int minute, double second) {
    // days_from_civil (H. Hinnant)
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int yoe = year - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    return ((days * 24 + hour) * 60 + minute) * 60000 + static_cast<std::int64_t>(std::llround(second * 1000.0));
}

double normalize_deg(double deg) {
    double r = std::fmod(deg, 360.0);
    if (r < 0.0) r += 360.0;
    return r >= 360.0 ? 0.0 : r;
}

double signed_angle_deg(double deg) {
    const double r = normalize_deg(deg);
    return r > 180.0 ? r - 360.0 : r;
}

double distance_m(const GeoPoint& a, const GeoPoint& b) {
    const double p1 = a.lat_deg * kDeg;
    const double p2 = b.lat_deg * kDeg;
    const double dp = p2 - p1;
    const double dl = (b.lon_deg - a.lon_deg) * kDeg;
    const double h = std::sin(dp / 2) * std::sin(dp / 2) +
                     std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

double initial_bearing_deg(const GeoPoint& from, const GeoPoint& to) {
    const double p1 = from.lat_deg * kDeg;
    const double p2 = to.lat_deg * kDeg;
    const double dl = (to.lon_deg - from.lon_deg) * kDeg;
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return normalize_deg(std::atan2(y, x) / kDeg);
}

GeoPoint destination(const GeoPoint& from, double bearing_deg, double dist_m) {
    const double d = dist_m / kEarthRadiusM;
    const double b = bearing_deg * kDeg;
    const double p1 = from.lat_deg * kDeg;
    const double l1 = from.lon_deg * kDeg;
    const double p2 = std::asin(std::sin(p1) * std::cos(d) + std::cos(p1) * std::sin(d) * std::cos(b));
    const double l2 =
        l1 + std::atan2(std::sin(b) * std::sin(d) * std::cos(p1), std::cos(d) - std::sin(p1) * std::sin(p2));
    return {p2 / kDeg, signed_angle_deg(l2 / kDeg)};
}

double cross_track_m(const GeoPoint& start, const GeoPoint& end, const GeoPoint& p) {
    const double d13 = distance_m(start, p) / kEarthRadiusM;
    const double t13 = initial_bearing_deg(start, p) * kDeg;
    const double t12 = initial_bearing_deg(start, end) * kDeg;
    return std::asin(std::sin(d13) * std::sin(t13 - t12)) * kEarthRadiusM;
}

Cpa closest_point_of_approach(const GeoPoint& own, double own_cog_deg, double own_sog_mps,
                              const GeoPoint& target, double target_cog_deg, double target_sog_mps) {
    double px = 0.0;
    double py = 0.0;
    local_en(own, target, px, py);
    // Relative velocity of the target as seen from own ship
    const double vx = target_sog_mps * std::sin(target_cog_deg * kDeg) - own_sog_mps * std::sin(own_cog_deg * kDeg);
    const double vy = target_sog_mps * std::cos(target_cog_deg * kDeg) - own_sog_mps * std::cos(own_cog_deg * kDeg);
    const double v2 = vx * vx + vy * vy;
    if (v2 < 1e-9) {
        // Same velocity: the distance never changes
        return {std::hypot(px, py), 0.0};
    }
    const double t = -(px * vx + py * vy) / v2;
    return {std::hypot(px + vx * t, py + vy * t), t};
}

TrueWind true_wind(const ApparentWind& apparent, double heading_deg, double boat_speed_mps) {
    // Boat frame: x to starboard, y forward. Apparent wind vector points to
    // where the wind blows (opposite to where it comes from).
    const double awa = apparent.angle_deg * kDeg;
    const double ax = -apparent.speed_mps * std::sin(awa);
    const double ay = -apparent.speed_mps * std::cos(awa);
    // Remove the headwind caused by the boat's own motion
    const double tx = ax;
    const double ty = ay + boat_speed_mps;
    TrueWind tw;
    tw.speed_mps = std::hypot(tx, ty);
    tw.angle_deg = tw.speed_mps < 1e-6 ? 0.0 : normalize_deg(std::atan2(-tx, -ty) / kDeg);
    tw.direction_deg = normalize_deg(heading_deg + tw.angle_deg);
    return tw;
}

}  // namespace boat::core
