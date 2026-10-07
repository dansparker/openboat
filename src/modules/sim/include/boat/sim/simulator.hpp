#pragma once

// Simulated boat for development without hardware: sails a large circle on
// the Attersee (Austria) and publishes position, heading, speed, depth,
// wind, water temperature and one AIS target on a crossing course.

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
};

// Deterministic state at time t (seconds since start) - used by the module
// and directly by the tests.
[[nodiscard]] SimState simulate(double t);

class Simulator final : public core::Module {
public:
    ~Simulator() override;
    [[nodiscard]] std::string_view name() const override { return "sim"; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::sim
