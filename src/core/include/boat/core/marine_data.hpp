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
// Variation: from the device (RMC, HDG, PGN 127250/127258) if it sends one,
// otherwise from the World Magnetic Model (MagneticVariation below).
struct Heading {
    double heading_deg = 0.0;
    bool is_true = false;
    std::optional<double> variation_deg;  // east positive
    bool variation_from_model = false;
};

// Magnetic variation at the own position from the World Magnetic Model
// (published by the nav module about once a minute; east positive).
struct MagneticVariation {
    double variation_deg = 0.0;
    std::string model;  // e.g. "WMM-2025"
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

// Depth offset chosen on the settings page (published by the UI).
// nullopt: use the offset sent by the transducer (DPT / PGN 128267), or the
// configured default when it sends none. A value replaces the transducer's
// offset - never added to it, so it cannot be counted twice.
struct DepthOffset {
    std::optional<double> offset_m;
};

// Offset to use for a depth reading: user setting > transducer > configured default.
[[nodiscard]] inline double effective_depth_offset(const std::optional<DepthOffset>& user,
                                                   std::optional<double> from_transducer, double configured) {
    if (user && user->offset_m) return *user->offset_m;
    return from_transducer.value_or(configured);
}

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
// What sends an AIS report: a ship, a base station (message 4, PGN 129793)
// or an aid to navigation (message 21, PGN 129041).
enum class AisStation : std::uint8_t { Vessel, BaseStation, AtoN };

struct AisReport {
    std::uint32_t mmsi = 0;
    AisStation station = AisStation::Vessel;
    std::optional<GeoPoint> position;
    std::optional<double> cog_deg;
    std::optional<double> sog_mps;
    std::optional<double> heading_deg;
    std::optional<std::string> name;
    std::optional<std::string> callsign;
    std::optional<double> length_m;
    std::optional<double> beam_m;
    std::uint8_t ship_type = 0;
    // Navigational status (class A position reports): 14 = AIS-SART active,
    // 15 = undefined (also used by SART/MOB devices in test mode)
    std::optional<std::uint8_t> nav_status;
    bool class_b = false;
    // Aids to navigation: type 1..31 (ITU-R M.1371 table 74), virtual = only a
    // radio signal, no physical mark; off_position = the buoy has drifted
    std::uint8_t aton_type = 0;
    bool virtual_aton = false;
    bool off_position = false;
};

// AIS safety related message (message 12 addressed / 14 broadcast,
// PGN 129801/129802), e.g. "SART ACTIVE" or a navigational warning.
struct AisSafetyMessage {
    std::uint32_t mmsi = 0;
    std::string text;
    bool addressed = false;  // message 12: addressed to one station (possibly us)
};

}  // namespace boat::core
