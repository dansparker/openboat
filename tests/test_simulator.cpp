#include "boat/sim/simulator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "boat/core/data_bus.hpp"
#include "boat/core/nav_math.hpp"
#include "boat/nav/nav.hpp"

using namespace boat;

TEST(Simulator, MovesAtConfiguredSpeed) {
    const auto a = sim::simulate(0.0);
    const auto b = sim::simulate(10.0);
    const double d = core::distance_m(a.position.point, b.position.point);
    EXPECT_NEAR(d, 10.0 * 5.0 * core::kMpsPerKnot, 0.5);
}

TEST(Simulator, ApparentWindRoundTripsToTrueWind) {
    // The simulator builds apparent wind from 12 kn true wind from 300 deg;
    // nav_math must recover exactly that.
    for (double t : {0.0, 100.0, 400.0}) {
        const auto s = sim::simulate(t);
        const auto tw = core::true_wind(s.wind, s.heading.heading_deg, s.stw.stw_mps);
        EXPECT_NEAR(tw.direction_deg, 300.0, 1e-6);
        EXPECT_NEAR(tw.speed_mps, 12.0 * core::kMpsPerKnot, 1e-6);
    }
}

TEST(Simulator, ModulesWorkTogetherOnTheBus) {
    core::DataBus bus;
    sim::Simulator sim;
    nav::NavModule nav;
    nav.start(bus);  // first, so it sees the simulator's first AIS report
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sim.start(bus);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    nav.stop();
    sim.stop();
    EXPECT_TRUE(bus.latest<core::TrueWind>());
    const auto ais = bus.latest<nav::AisTargetList>();
    ASSERT_TRUE(ais);
    EXPECT_EQ(ais->value.targets.size(), 1U);
}
