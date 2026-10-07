#pragma once

// Data types published on the DataBus. Each type is one topic.
//
// Units (always, everywhere in the core):
//   angles   degrees, true north unless the name says otherwise, 0..360
//   speeds   metres per second
//   lengths  metres
//   temps    degrees Celsius
// Conversion to knots, feet, ... happens only in the UI (see units.hpp).

#include <cstdint>
#include <optional>
#include <string>

namespace boat::core {

// UTC from the GNSS. The Raspberry Pi has no real-time clock: without
// internet its system time is wrong after boot, so logs use this instead.
struct UtcTime {
    std::int64_t unix_ms = 0;
};

struct GeoPoint {
    double lat_deg = 0.0;  // WGS84, north positive
    double lon_deg = 0.0;  // WGS84, east positive
};

enum class FixQuality : std::uint8_t { None, Gnss, Dgnss, Rtk, Estimated, Simulated };

struct Position {
    GeoPoint point;
    FixQuality quality = FixQuality::None;
    std::optional<double> hdop;
    std::optional<int> satellites;
};

// Course and speed over ground (from the GNSS).
struct CourseOverGround {
    double cog_deg = 0.0;
    double sog_mps = 0.0;
};

// Heading from a compass. Magnetic heading is converted to true by the
// producer when the variation is known; otherwise `is_true` stays false.
struct Heading {
    double heading_deg = 0.0;
    bool is_true = false;
    std::optional<double> variation_deg;  // east positive
};

// Speed through water (paddle wheel / log).
struct SpeedThroughWater {
    double stw_mps = 0.0;
};

// Depth below the transducer plus the configured offset:
//   offset > 0: below waterline (transducer depth)
//   offset < 0: below keel
// depth_m is what the skipper sees, i.e. transducer depth + offset.
struct Depth {
    double below_transducer_m = 0.0;
    double offset_m = 0.0;
    [[nodiscard]] double depth_m() const { return below_transducer_m + offset_m; }
};

struct WaterTemperature {
    double celsius = 0.0;
};

// Apparent wind as measured at the masthead, angle relative to the bow
// (0..360, clockwise).
struct ApparentWind {
    double angle_deg = 0.0;
    double speed_mps = 0.0;
};

// True wind (computed by nav_math from apparent wind + boat motion).
struct TrueWind {
    double direction_deg = 0.0;  // where it comes from, true north
    double angle_deg = 0.0;      // relative to the bow
    double speed_mps = 0.0;
};

// Engine data (NMEA 2000 PGN 127488 / 127489), single engine for now.
struct EngineData {
    std::uint8_t instance = 0;
    std::optional<double> rpm;
    std::optional<double> coolant_celsius;
    std::optional<double> oil_pressure_pa;
    std::optional<double> fuel_rate_lph;
};

// One AIS target (decoded from AIVDM or NMEA 2000 PGN 129038/129039/129794).
// Published per received message; the AisTargets module keeps the list.
struct AisReport {
    std::uint32_t mmsi = 0;
    std::optional<GeoPoint> position;
    std::optional<double> cog_deg;
    std::optional<double> sog_mps;
    std::optional<double> heading_deg;
    std::optional<std::string> name;
    std::optional<std::string> callsign;
    std::optional<double> length_m;
    std::optional<double> beam_m;
    std::uint8_t ship_type = 0;
    bool class_b = false;
};

}  // namespace boat::core
