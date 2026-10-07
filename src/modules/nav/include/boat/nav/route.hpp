#pragma once

// Waypoint navigation: Go-To, routes with automatic leg switching, MOB.
// The Navigator is pure (time and position are passed in); NavModule runs
// it at 1 Hz and publishes the Guidance on the bus.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "boat/core/data_bus.hpp"
#include "boat/core/marine_data.hpp"

namespace boat::nav {

struct Waypoint {
    std::string name;
    core::GeoPoint point;
};

struct Route {
    std::string name;
    std::vector<Waypoint> points;
};

// UI -> Navigator
struct NavCommand {
    enum class Action : std::uint8_t {
        GoTo,          // to `waypoint`, starting from the current position
        StartRoute,    // `route`, first leg towards points[start_index]
        NextWaypoint,  // skip to the next leg
        Stop,
        Mob,           // man overboard: mark the current position and Go-To it
    } action = Action::Stop;
    Waypoint waypoint;
    Route route;
    std::size_t start_index = 0;
};

enum class NavMode : std::uint8_t { None, GoTo, Route, Mob };

struct Guidance {
    NavMode mode = NavMode::None;
    std::string route_name;
    std::optional<Waypoint> from;  // start of the active leg
    Waypoint to;
    std::size_t leg = 0;           // index of `to` in the route
    std::size_t legs = 0;          // number of route points
    std::optional<double> btw_deg;  // bearing to waypoint
    std::optional<double> dtw_m;    // distance to waypoint
    std::optional<double> xte_m;    // cross-track error, > 0: right of course line (steer left)
    std::optional<double> vmg_mps;  // velocity made good towards the waypoint
    std::optional<double> ttg_s;    // time to go (to the waypoint), from VMG
    std::optional<double> route_remaining_m;
    bool arrived = false;  // final waypoint reached (arrival circle)
    std::vector<Waypoint> remaining;  // `to` and the following route points (for the chart)
};

struct NavigatorSettings {
    double arrival_radius_m = 0.05 * 1852.0;
};

class Navigator {
public:
    explicit Navigator(NavigatorSettings settings = {}) : settings_(settings) {}
    void set_settings(const NavigatorSettings& s) { settings_ = s; }

    // `own` may be missing: Go-To and MOB then wait for the next fix.
    void command(const NavCommand& cmd, const std::optional<core::GeoPoint>& own);

    Guidance update(const std::optional<core::GeoPoint>& own, const std::optional<core::CourseOverGround>& cog);

    [[nodiscard]] NavMode mode() const { return mode_; }

private:
    void start_leg(std::size_t index, const std::optional<core::GeoPoint>& own);

    NavigatorSettings settings_;
    NavMode mode_ = NavMode::None;
    Route route_;  // Go-To / MOB: a one-point route
    std::size_t leg_ = 0;
    std::optional<Waypoint> from_;
    bool pending_from_ = false;  // start position not known yet
    bool arrived_ = false;
};

}  // namespace boat::nav
