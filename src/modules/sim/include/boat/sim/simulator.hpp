#pragma once

// Simulated boat for development without hardware: sails a circle (default:
// on the Attersee, Austria; any water via SimSettings) and publishes position,
// heading, speed, depth, wind, water temperature and AIS targets.

#include <atomic>
#include <thread>

#include "boat/core/marine_data.hpp"
#include "boat/core/module.hpp"

namespace boat::sim {

struct SimState {
    core::Position position;
    core::CourseOverGround cog;
    core::Heading heading;
    core::SpeedThroughWater stw;
    core::Depth depth;
    core::ApparentWind wind;
    core::WaterTemperature water;
    core::AisReport ais;
    core::AisReport anchored;  // sailing boat at anchor (class B)
    core::AisReport beacon;    // MOB device in TEST mode: listed, no alarm
    core::AisReport aton;      // virtual aid to navigation (isolated danger)
};

struct SimSettings {
    core::GeoPoint centre{47.8700, 13.5450};  // Attersee
    double radius_m = 900.0;                  // must stay on the water
    // Depth along the circle varies between these (deepest towards north)
    double depth_min_m = 3.0;
    double depth_max_m = 47.0;
    // The demo AIS targets are placed for the Attersee: off elsewhere
    bool ais_targets = true;
};

// Deterministic state at time t (seconds since start) - used by the module
// and directly by the tests.
[[nodiscard]] SimState simulate(double t, const SimSettings& settings = {});

class Simulator final : public core::Module {
public:
    explicit Simulator(SimSettings settings = {}) : settings_(settings) {}
    ~Simulator() override;
    [[nodiscard]] std::string_view name() const override { return "sim"; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    SimSettings settings_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::sim
