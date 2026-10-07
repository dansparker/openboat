#include "boat/core/nav_math.hpp"

#include <gtest/gtest.h>

using namespace boat::core;

TEST(NavMath, NormalizeAngles) {
    EXPECT_DOUBLE_EQ(normalize_deg(-10.0), 350.0);
    EXPECT_DOUBLE_EQ(normalize_deg(720.0), 0.0);
    EXPECT_DOUBLE_EQ(signed_angle_deg(270.0), -90.0);
}

TEST(NavMath, OneMinuteOfLatitudeIsAboutOneNauticalMile) {
    EXPECT_NEAR(distance_m({47.0, 13.0}, {47.0 + 1.0 / 60.0, 13.0}), 1853.0, 3.0);
}

TEST(NavMath, BearingAndDestinationRoundTrip) {
    const GeoPoint a{47.87, 13.545};
    const GeoPoint b = destination(a, 123.0, 5000.0);
    EXPECT_NEAR(distance_m(a, b), 5000.0, 0.5);
    EXPECT_NEAR(initial_bearing_deg(a, b), 123.0, 0.05);
}

TEST(NavMath, CrossTrackSignRightOfCourseIsPositive) {
    const GeoPoint start{47.0, 13.0};
    const GeoPoint end = destination(start, 0.0, 10000.0);
    const GeoPoint right = destination(destination(start, 0.0, 5000.0), 90.0, 100.0);
    EXPECT_NEAR(cross_track_m(start, end, right), 100.0, 0.5);
}

TEST(NavMath, HeadOnCollisionHasZeroCpa) {
    const GeoPoint own{47.0, 13.0};
    const GeoPoint target = destination(own, 0.0, 2000.0);
    const auto cpa = closest_point_of_approach(own, 0.0, 5.0, target, 180.0, 5.0);
    EXPECT_NEAR(cpa.cpa_m, 0.0, 1.0);
    EXPECT_NEAR(cpa.tcpa_s, 200.0, 1.0);
}

TEST(NavMath, DivergingTargetHasNegativeTcpa) {
    const GeoPoint own{47.0, 13.0};
    const GeoPoint target = destination(own, 0.0, 2000.0);
    EXPECT_LT(closest_point_of_approach(own, 180.0, 5.0, target, 0.0, 5.0).tcpa_s, 0.0);
}

TEST(NavMath, TrueWindWhenMotoringIntoCalmIsZero) {
    const auto tw = true_wind({0.0, 5.0}, 90.0, 5.0);
    EXPECT_NEAR(tw.speed_mps, 0.0, 1e-9);
}

TEST(NavMath, TrueWindBeamReach) {
    // Apparent 10 m/s at 45 deg starboard, boat 7.07 m/s: true wind abeam (90 deg), 7.07 m/s
    const auto tw = true_wind({45.0, 10.0}, 0.0, 7.0710678);
    EXPECT_NEAR(tw.angle_deg, 90.0, 0.01);
    EXPECT_NEAR(tw.speed_mps, 7.0710678, 1e-4);
    EXPECT_NEAR(tw.direction_deg, 90.0, 0.01);
}
