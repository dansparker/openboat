#include "boat/sim/simulator.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "boat/core/data_bus.hpp"
#include "boat/core/nav_math.hpp"

namespace boat::sim {

namespace {

constexpr core::GeoPoint kCentre{47.8700, 13.5450};  // Attersee
constexpr double kRadiusM = 900.0;
constexpr double kSpeedMps = 5.0 * core::kMpsPerKnot;
constexpr double kTrueWindFromDeg = 300.0;
constexpr double kTrueWindMps = 12.0 * core::kMpsPerKnot;
constexpr double kDeg = std::numbers::pi / 180.0;

}  // namespace

SimState simulate(double t) {
    SimState s;
    // Clockwise circle: bearing from the centre grows with time
    const double omega = kSpeedMps / kRadiusM;  // rad/s
    const double bearing = core::normalize_deg(t * omega / kDeg);
    s.position.point = core::destination(kCentre, bearing, kRadiusM);
    s.position.quality = core::FixQuality::Simulated;
    s.position.satellites = 12;
    s.position.hdop = 0.8;
    const double course = core::normalize_deg(bearing + 90.0);
    s.cog = {course, kSpeedMps};
    s.heading = {core::normalize_deg(course - 3.0), true, 4.5};  // 3 deg leeway
    s.stw = {kSpeedMps * 0.97};
    // Depth: shelf near the shore (north), deep water elsewhere
    s.depth = {std::max(1.2, 25.0 + 22.0 * std::cos(bearing * kDeg)), -0.4};
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
    s.ais.position = core::destination(core::destination(kCentre, 0.0, 1800.0), 180.0, leg * *s.ais.sog_mps);
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
            const SimState s = simulate(t);
            bus.publish(s.position);
            bus.publish(s.cog);
            bus.publish(s.heading);
            bus.publish(s.stw);
            bus.publish(s.depth);
            bus.publish(s.wind);
            bus.publish(s.water);
            if (tick % 10 == 0) bus.publish(s.ais);  // AIS: every 2 s like a class A at speed
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
