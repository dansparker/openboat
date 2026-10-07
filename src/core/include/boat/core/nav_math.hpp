#pragma once

// Navigation math on the WGS84 sphere approximation (good to ~0.5 % for
// distances; sufficient for chartplotter display, not for surveying).

#include <optional>

#include "boat/core/marine_data.hpp"

namespace boat::core {

inline constexpr double kEarthRadiusM = 6371008.8;  // mean radius
inline constexpr double kMetresPerNm = 1852.0;
inline constexpr double kMpsPerKnot = 1852.0 / 3600.0;

[[nodiscard]] double normalize_deg(double deg);         // -> [0, 360)
[[nodiscard]] double signed_angle_deg(double deg);      // -> (-180, 180]

[[nodiscard]] double distance_m(const GeoPoint& a, const GeoPoint& b);
[[nodiscard]] double initial_bearing_deg(const GeoPoint& from, const GeoPoint& to);
[[nodiscard]] GeoPoint destination(const GeoPoint& from, double bearing_deg, double distance_m);

// Cross-track error of `p` relative to the leg start->end.
// Positive: boat is right of the course line (steer left).
[[nodiscard]] double cross_track_m(const GeoPoint& start, const GeoPoint& end, const GeoPoint& p);

// Closest point of approach between own ship and a target, both moving on
// straight lines at constant speed (local flat-earth approximation, valid
// for the few nautical miles AIS collision alerts care about).
struct Cpa {
    double cpa_m = 0.0;
    double tcpa_s = 0.0;  // < 0: closest approach is in the past (diverging)
};
[[nodiscard]] Cpa closest_point_of_approach(const GeoPoint& own, double own_cog_deg, double own_sog_mps,
                                            const GeoPoint& target, double target_cog_deg,
                                            double target_sog_mps);

// True wind from apparent wind and boat motion through the water.
// heading_deg: true heading; boat_speed_mps: speed through water (or SOG
// when no log is fitted - then the result is "ground wind").
[[nodiscard]] TrueWind true_wind(const ApparentWind& apparent, double heading_deg, double boat_speed_mps);

}  // namespace boat::core
