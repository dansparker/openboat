#include "boat/nav/route.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "boat/core/nav_math.hpp"

namespace boat::nav {

void Navigator::command(const NavCommand& cmd, const std::optional<core::GeoPoint>& own) {
    switch (cmd.action) {
        case NavCommand::Action::GoTo:
            mode_ = NavMode::GoTo;
            route_ = Route{"", {cmd.waypoint}};
            start_leg(0, own);
            break;
        case NavCommand::Action::StartRoute:
            if (cmd.route.points.empty()) return;
            mode_ = NavMode::Route;
            route_ = cmd.route;
            start_leg(std::min(cmd.start_index, route_.points.size() - 1), own);
            break;
        case NavCommand::Action::NextWaypoint:
            if (mode_ == NavMode::Route && leg_ + 1 < route_.points.size()) {
                start_leg(leg_ + 1, std::nullopt);
            }
            break;
        case NavCommand::Action::Stop:
            mode_ = NavMode::None;
            route_ = {};
            from_.reset();
            arrived_ = false;
            break;
        case NavCommand::Action::Mob: {
            // Position captured at the key press if available, else the current one
            const auto mark = !cmd.waypoint.name.empty() ? std::optional(cmd.waypoint.point) : own;
            if (!mark) return;  // no position: nothing to mark (GNSS alarm is active anyway)
            mode_ = NavMode::Mob;
            route_ = Route{"", {Waypoint{"MOB", *mark}}};
            leg_ = 0;
            from_.reset();  // steer directly back, no course line
            pending_from_ = false;
            arrived_ = false;
            break;
        }
    }
}

void Navigator::start_leg(std::size_t index, const std::optional<core::GeoPoint>& own) {
    leg_ = index;
    arrived_ = false;
    if (index > 0) {
        from_ = route_.points[index - 1];
        pending_from_ = false;
    } else if (own) {
        from_ = Waypoint{"Start", *own};
        pending_from_ = false;
    } else {
        from_.reset();
        pending_from_ = true;
    }
}

Guidance Navigator::update(const std::optional<core::GeoPoint>& own,
                           const std::optional<core::CourseOverGround>& cog) {
    Guidance g;
    g.mode = mode_;
    if (mode_ == NavMode::None || route_.points.empty()) return g;
    if (pending_from_ && own) start_leg(leg_, own);

    // Leg switching: inside the arrival circle, or past the perpendicular at
    // the leg end (a boat that misses the circle must not circle back).
    if (own && mode_ == NavMode::Route) {
        while (leg_ + 1 < route_.points.size()) {
            const auto& to = route_.points[leg_].point;
            bool passed = core::distance_m(*own, to) <= settings_.arrival_radius_m;
            if (!passed && from_) {
                const double leg_brg = core::initial_bearing_deg(from_->point, to);
                const double brg_to = core::initial_bearing_deg(*own, to);
                passed = std::abs(core::signed_angle_deg(brg_to - leg_brg)) > 90.0;
            }
            if (!passed) break;
            start_leg(leg_ + 1, std::nullopt);
        }
    }

    g.route_name = route_.name;
    g.to = route_.points[leg_];
    g.from = from_;
    g.leg = leg_;
    g.legs = route_.points.size();
    g.remaining.assign(route_.points.begin() + static_cast<std::ptrdiff_t>(leg_), route_.points.end());
    if (!own) return g;

    g.btw_deg = core::initial_bearing_deg(*own, g.to.point);
    g.dtw_m = core::distance_m(*own, g.to.point);
    if (from_) g.xte_m = core::cross_track_m(from_->point, g.to.point, *own);
    double remaining = *g.dtw_m;
    for (std::size_t i = leg_ + 1; i < route_.points.size(); ++i) {
        remaining += core::distance_m(route_.points[i - 1].point, route_.points[i].point);
    }
    g.route_remaining_m = remaining;
    if (cog) {
        g.vmg_mps = cog->sog_mps * std::cos((cog->cog_deg - *g.btw_deg) * std::numbers::pi / 180.0);
        if (*g.vmg_mps > 0.1) g.ttg_s = *g.dtw_m / *g.vmg_mps;
    }
    const bool last = leg_ + 1 == route_.points.size();
    if (last && *g.dtw_m <= settings_.arrival_radius_m) arrived_ = true;
    g.arrived = arrived_ && mode_ != NavMode::Mob;  // MOB has its own alarm
    return g;
}

}  // namespace boat::nav
