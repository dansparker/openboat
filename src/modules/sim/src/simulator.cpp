#include "boat/sim/simulator.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "boat/core/data_bus.hpp"
#include "boat/core/heading.hpp"
#include "boat/core/nav_math.hpp"

namespace boat::sim {

namespace {

constexpr double kSpeedMps = 5.0 * core::kMpsPerKnot;
constexpr double kTrueWindFromDeg = 300.0;
constexpr double kVariationDeg = 5.0;  // about the WMM value at the Attersee
constexpr double kTrueWindMps = 12.0 * core::kMpsPerKnot;
constexpr double kDeg = std::numbers::pi / 180.0;

}  // namespace

SimState simulate(double t, const SimSettings& settings) {
    const core::GeoPoint centre = settings.centre;
    const double radius_m = std::max(50.0, settings.radius_m);
    SimState s;
    // Clockwise circle: bearing from the centre grows with time
    const double omega = kSpeedMps / radius_m;  // rad/s
    const double bearing = core::normalize_deg(t * omega / kDeg);
    s.position.point = core::destination(centre, bearing, radius_m);
    s.position.quality = core::FixQuality::Simulated;
    s.position.satellites = 12;
    s.position.hdop = 0.8;
    const double course = core::normalize_deg(bearing + 90.0);
    s.cog = {course, kSpeedMps};
    s.heading = {core::normalize_deg(course - 3.0), true, std::nullopt, false};  // 3 deg leeway
    s.stw = {kSpeedMps * 0.97};
    // Depth: deepest at the north of the circle, shallowest at the south
    const double mid = (settings.depth_min_m + settings.depth_max_m) / 2.0;
    const double half = (settings.depth_max_m - settings.depth_min_m) / 2.0;
    s.depth = {std::max(0.5, mid + half * std::cos(bearing * kDeg)), -0.4};
    s.water = {17.5 + 0.5 * std::sin(t / 600.0)};

    // Apparent wind = true wind + headwind from own motion (boat frame)
    const double twa = (kTrueWindFromDeg - s.heading.heading_deg) * kDeg;
    const double wx = -kTrueWindMps * std::sin(twa);
    const double wy = -kTrueWindMps * std::cos(twa) - s.stw.stw_mps;
    s.wind.speed_mps = std::hypot(wx, wy);
    s.wind.angle_deg = core::normalize_deg(std::atan2(-wx, -wy) / kDeg);

    // AIS target: motor vessel crossing the lake north-south, 8 kn, repeating
    const double leg = std::fmod(t, 900.0);
    s.ais.mmsi = 203999123;
    s.ais.name = "MS SIMULATION";
    s.ais.callsign = "OE1234";
    s.ais.length_m = 32.0;
    s.ais.beam_m = 7.0;
    s.ais.ship_type = 60;  // passenger
    s.ais.cog_deg = 180.0;
    s.ais.sog_mps = 8.0 * core::kMpsPerKnot;
    s.ais.heading_deg = 180.0;
    s.ais.position = core::destination(core::destination(centre, 0.0, 1800.0), 180.0, leg * *s.ais.sog_mps);

    s.anchored.mmsi = 203888456;
    s.anchored.class_b = true;
    s.anchored.name = "SY WINDROSE";
    s.anchored.ship_type = 36;  // sailing
    s.anchored.length_m = 9.0;
    s.anchored.beam_m = 3.0;
    s.anchored.sog_mps = 0.0;
    s.anchored.cog_deg = 0.0;
    // Far enough from the circle that no straight-line CPA comes near (a boat at
    // anchor dead ahead SHOULD alarm - just not in every demo screenshot)
    s.anchored.position = core::destination(centre, 340.0, 2600.0);

    s.beacon.mmsi = 972000123;
    s.beacon.nav_status = 15;  // test
    s.beacon.sog_mps = 0.0;
    s.beacon.position = core::destination(centre, 200.0, 1400.0);

    s.aton.mmsi = 992036001;
    s.aton.station = core::AisStation::AtoN;
    s.aton.name = "UNTIEFE SIMULATION";
    s.aton.aton_type = 28;  // isolated danger (floating)
    s.aton.virtual_aton = true;
    s.aton.position = core::destination(centre, 120.0, 1300.0);
    return s;
}

Simulator::~Simulator() { stop(); }

void Simulator::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] {
        const auto t0 = std::chrono::steady_clock::now();
        int tick = 0;
        while (running_) {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const SimState s = simulate(t, settings_);
            // A GNSS receiver also delivers UTC
            bus.publish(core::UtcTime{std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count()});
            bus.publish(s.position);
            bus.publish(s.cog);
            // The simulated compass is magnetic and sends no variation (like many
            // fluxgate compasses): the nav module adds it from the World Magnetic Model
            bus.publish(core::magnetic_heading(s.heading.heading_deg - kVariationDeg, std::nullopt, bus));
            bus.publish(s.stw);
            core::Depth depth = s.depth;  // the simulated transducer sends an offset; the user may override it
            const auto user = bus.latest<core::DepthOffset>();
            depth.offset_m = core::effective_depth_offset(user ? std::optional(user->value) : std::nullopt,
                                                          s.depth.offset_m, 0.0);
            bus.publish(depth);
            bus.publish(s.wind);
            bus.publish(s.water);
            if (settings_.ais_targets && tick % 10 == 0) bus.publish(s.ais);  // AIS: every 2 s like a class A at speed
            if (settings_.ais_targets && tick % 300 == 0) {  // the test beacon also announces its test
                bus.publish(core::AisSafetyMessage{s.beacon.mmsi, "MOB TEST", false});
            }
            if (settings_.ais_targets && tick % 50 == 0) {                    // slow / anchored: rarely
                bus.publish(s.anchored);
                bus.publish(s.beacon);
                bus.publish(s.aton);
            }
            ++tick;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
}

void Simulator::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

}  // namespace boat::sim
