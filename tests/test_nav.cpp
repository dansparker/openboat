#include "boat/nav/nav.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include "boat/core/nav_math.hpp"

using namespace boat;
using namespace std::chrono_literals;

namespace {

const core::GeoPoint kHome{47.87, 13.545};

core::Sample<core::Position> fix(core::GeoPoint p, core::Clock::time_point t) {
    core::Position pos;
    pos.point = p;
    pos.quality = core::FixQuality::Gnss;
    return {pos, t};
}

bool has(const nav::AlarmList& l, nav::AlarmId id) {
    return std::any_of(l.active.begin(), l.active.end(), [id](const nav::Alarm& a) { return a.id == id; });
}

}  // namespace

TEST(AlarmEvaluator, NoPositionMeansGnssLost) {
    nav::AlarmEvaluator e;
    EXPECT_TRUE(has(e.evaluate(std::nullopt, std::nullopt, {}, core::Clock::now()), nav::AlarmId::GnssLost));
}

TEST(AlarmEvaluator, StalePositionMeansGnssLost) {
    nav::AlarmEvaluator e;
    const auto now = core::Clock::now();
    EXPECT_TRUE(has(e.evaluate(fix(kHome, now - 10s), std::nullopt, {}, now), nav::AlarmId::GnssLost));
    EXPECT_FALSE(has(e.evaluate(fix(kHome, now), std::nullopt, {}, now), nav::AlarmId::GnssLost));
}

TEST(AlarmEvaluator, AnchorDragAlarmsOutsideRadius) {
    nav::AlarmEvaluator e;
    const auto now = core::Clock::now();
    e.command({nav::AnchorCommand::Action::Drop, 30.0}, fix(kHome, now));
    EXPECT_FALSE(has(e.evaluate(fix(core::destination(kHome, 90, 20), now), std::nullopt, {}, now),
                     nav::AlarmId::AnchorDrag));
    EXPECT_TRUE(has(e.evaluate(fix(core::destination(kHome, 90, 45), now), std::nullopt, {}, now),
                    nav::AlarmId::AnchorDrag));
    e.command({nav::AnchorCommand::Action::Raise, 0.0}, std::nullopt);
    EXPECT_FALSE(e.anchor().active);
}

TEST(AlarmEvaluator, ShallowWaterAndLostDepth) {
    nav::AlarmEvaluator e({.shallow_m = 2.0});
    const auto now = core::Clock::now();
    const core::Sample<core::Depth> shallow{{1.5, 0.0}, now};
    EXPECT_TRUE(has(e.evaluate(fix(kHome, now), shallow, {}, now), nav::AlarmId::ShallowWater));
    const core::Sample<core::Depth> old{{10.0, 0.0}, now - 30s};
    EXPECT_TRUE(has(e.evaluate(fix(kHome, now), old, {}, now), nav::AlarmId::DepthLost));
}

TEST(AisTable, CollisionCourseIsDangerous) {
    nav::AisTable table;
    const auto now = core::Clock::now();
    core::AisReport r;
    r.mmsi = 1;
    r.position = core::destination(kHome, 0.0, 1500.0);
    r.cog_deg = 180.0;
    r.sog_mps = 4.0;
    table.update(r, now);
    const auto list = table.evaluate(fix(kHome, now).value, core::CourseOverGround{0.0, 3.0}, now);
    ASSERT_EQ(list.targets.size(), 1U);
    EXPECT_TRUE(list.targets[0].dangerous);
    EXPECT_NEAR(*list.targets[0].tcpa_s, 1500.0 / 7.0, 1.0);
}

TEST(AisTable, StaticDataIsMergedAndOldTargetsExpire) {
    nav::AisTable table;
    const auto now = core::Clock::now();
    core::AisReport pos;
    pos.mmsi = 2;
    pos.position = core::destination(kHome, 45.0, 3000.0);
    core::AisReport stat;
    stat.mmsi = 2;
    stat.name = "WOLFGANG";
    table.update(pos, now);
    table.update(stat, now);
    auto list = table.evaluate(std::nullopt, std::nullopt, now);
    ASSERT_EQ(list.targets.size(), 1U);
    EXPECT_EQ(list.targets[0].data.name, "WOLFGANG");
    EXPECT_TRUE(list.targets[0].data.position.has_value());

    list = table.evaluate(std::nullopt, std::nullopt, now + 5min);
    ASSERT_EQ(list.targets.size(), 1U);
    EXPECT_TRUE(list.targets[0].lost);
    EXPECT_TRUE(table.evaluate(std::nullopt, std::nullopt, now + 11min).targets.empty());
}

TEST(AisTable, AnchoredBoatsDoNotAlarm) {
    nav::AisTable table;
    const auto now = core::Clock::now();
    core::AisReport r;
    r.mmsi = 3;
    r.position = core::destination(kHome, 0.0, 50.0);
    r.cog_deg = 0.0;
    r.sog_mps = 0.0;
    table.update(r, now);
    const auto list = table.evaluate(fix(kHome, now).value, core::CourseOverGround{0.0, 0.0}, now);
    EXPECT_FALSE(list.targets[0].dangerous);
}
