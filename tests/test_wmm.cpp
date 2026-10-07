// Validation of the WMM evaluation against the official WMM2025 test values
// (NOAA NCEI, WMM2025_TEST_VALUES.txt) using the bundled WMM.COF.

#include "boat/nav/magnetic_model.hpp"
#include "boat/nav/nav.hpp"
#include "boat/core/nav_math.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <thread>

#include "boat/sim/simulator.hpp"

using namespace boat::nav;

namespace {

struct TestValue {
    double year, height_km, lat, lon;
    double x, y, z;          // nT
    double inclination, declination;  // deg
};

// Date, height, lat, lon, X, Y, Z, I, D from the official table
constexpr TestValue kValues[] = {
    {2025.0, 0.0, 80.0, 0.0, 6521.6, 145.9, 54791.5, 83.21, 1.28},
    {2025.0, 0.0, 0.0, 120.0, 39677.8, -109.6, -10580.2, -14.93, -0.16},
    {2025.0, 0.0, -80.0, 240.0, 6117.5, 15751.9, -52022.5, -72.00, 68.78},
    {2025.0, 100.0, 80.0, 0.0, 6216.0, 92.4, 52598.8, 83.26, 0.85},
    {2025.0, 100.0, 0.0, 120.0, 37688.6, -96.2, -10152.1, -15.08, -0.15},
    {2025.0, 100.0, -80.0, 240.0, 5907.6, 14780.3, -49540.7, -72.19, 68.21},
    {2027.5, 0.0, 80.0, 0.0, 6500.8, 294.5, 54869.4, 83.24, 2.59},
    {2027.5, 0.0, 0.0, 120.0, 39701.6, -167.4, -10381.8, -14.65, -0.24},
    {2027.5, 0.0, -80.0, 240.0, 6200.7, 15730.3, -51783.7, -71.92, 68.49},
    {2027.5, 100.0, 80.0, 0.0, 6196.7, 233.8, 52670.5, 83.29, 2.16},
    {2027.5, 100.0, 0.0, 120.0, 37711.5, -148.7, -9969.8, -14.81, -0.23},
    {2027.5, 100.0, -80.0, 240.0, 5984.0, 14760.1, -49317.7, -72.10, 67.93},
};

MagneticModel load_wmm2025() {
    std::ifstream in(BOAT_SOURCE_DIR "/data/WMM.COF");
    return MagneticModel::from_cof(in);
}

}  // namespace

TEST(Wmm2025, LoadsTheBundledCoefficients) {
    const auto model = load_wmm2025();
    EXPECT_EQ(model.name(), "WMM-2025");
    EXPECT_DOUBLE_EQ(model.epoch(), 2025.0);
    EXPECT_EQ(model.degree(), 12);
    EXPECT_TRUE(model.valid_for(2026.75));
}

TEST(Wmm2025, MatchesOfficialTestValues) {
    const auto model = load_wmm2025();
    for (const auto& t : kValues) {
        SCOPED_TRACE(testing::Message() << t.year << " h=" << t.height_km << " lat=" << t.lat << " lon=" << t.lon);
        const auto f = model.field({t.lat, t.lon}, t.height_km, t.year);
        // Table values are rounded to 0.1 nT / 0.01 deg
        EXPECT_NEAR(f.north_nt, t.x, 0.3);
        EXPECT_NEAR(f.east_nt, t.y, 0.3);
        EXPECT_NEAR(f.down_nt, t.z, 0.3);
        EXPECT_NEAR(f.declination_deg, t.declination, 0.01);
        EXPECT_NEAR(f.inclination_deg, t.inclination, 0.01);
    }
}

TEST(Wmm2025, VariationAtTheAttersee) {
    const auto model = load_wmm2025();
    // Attersee, October 2026: about 5° east (sanity check; exact values are tested above)
    const auto v = variation_at(model, {47.87, 13.545}, boat::core::unix_ms(2026, 10, 1, 12, 0, 0));
    ASSERT_TRUE(v);
    EXPECT_NEAR(v->variation_deg, 5.0, 1.0);
    EXPECT_EQ(v->model, "WMM-2025");
}

TEST(Wmm2025, OutdatedModelIsNotUsed) {
    const auto model = load_wmm2025();
    EXPECT_FALSE(variation_at(model, {47.87, 13.545}, boat::core::unix_ms(2031, 1, 1, 0, 0, 0)));
    EXPECT_FALSE(variation_at(model, {47.87, 13.545}, boat::core::unix_ms(2020, 1, 1, 0, 0, 0)));
}

TEST(Wmm2025, SimulatedMagneticCompassBecomesTrue) {
    boat::core::DataBus bus;
    boat::sim::Simulator sim;
    NavModule nav({}, std::make_shared<const MagneticModel>(load_wmm2025()));
    nav.start(bus);
    sim.start(bus);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    sim.stop();
    nav.stop();
    const auto v = bus.latest<boat::core::MagneticVariation>();
    ASSERT_TRUE(v);
    const auto h = bus.latest<boat::core::Heading>();
    ASSERT_TRUE(h);
    EXPECT_TRUE(h->value.is_true);
    EXPECT_TRUE(h->value.variation_from_model);
}
