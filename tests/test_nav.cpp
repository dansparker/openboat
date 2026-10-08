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

TEST(AlarmEvaluator, AcknowledgeSilencesUntilAlarmReturns) {
    nav::AlarmEvaluator e({.shallow_m = 2.0});
    const auto now = core::Clock::now();
    const core::Sample<core::Depth> shallow{{1.5, 0.0}, now};
    const core::Sample<core::Depth> deep{{10.0, 0.0}, now};
    auto l = e.evaluate(fix(kHome, now), shallow, {}, now);
    EXPECT_EQ(l.sound, nav::AlarmLevel::Alarm);
    e.acknowledge();
    l = e.evaluate(fix(kHome, now), shallow, {}, now);
    EXPECT_FALSE(l.sound.has_value());
    ASSERT_EQ(l.active.size(), 1U);
    EXPECT_TRUE(l.active[0].acknowledged);
    e.evaluate(fix(kHome, now), deep, {}, now);  // condition clears ...
    l = e.evaluate(fix(kHome, now), shallow, {}, now);  // ... and returns: sounds again
    EXPECT_EQ(l.sound, nav::AlarmLevel::Alarm);
}

TEST(AlarmEvaluator, NewAisTargetSoundsAfterAcknowledge) {
    nav::AlarmEvaluator e;
    const auto now = core::Clock::now();
    nav::AisTargetList ais;
    nav::AisTarget t;
    t.dangerous = true;
    t.data.mmsi = 1;
    ais.targets.push_back(t);
    e.evaluate(fix(kHome, now), std::nullopt, ais, now);
    e.acknowledge();
    EXPECT_FALSE(e.evaluate(fix(kHome, now), std::nullopt, ais, now).sound);
    t.data.mmsi = 2;
    ais.targets.push_back(t);
    EXPECT_EQ(e.evaluate(fix(kHome, now), std::nullopt, ais, now).sound, nav::AlarmLevel::Alarm);
}

TEST(AlarmEvaluator, GnssLostIsWarningUnlessAnchorWatchActive) {
    nav::AlarmEvaluator e;
    const auto now = core::Clock::now();
    EXPECT_EQ(e.evaluate(std::nullopt, std::nullopt, {}, now).sound, nav::AlarmLevel::Warning);
    e.command({nav::AnchorCommand::Action::Drop, 30.0}, fix(kHome, now));
    EXPECT_EQ(e.evaluate(std::nullopt, std::nullopt, {}, now).sound, nav::AlarmLevel::Alarm);
}

TEST(Buzzer, Patterns) {
    EXPECT_FALSE(nav::buzzer_on(std::nullopt, 0.1));
    EXPECT_TRUE(nav::buzzer_on(nav::AlarmLevel::Alarm, 0.1));
    EXPECT_FALSE(nav::buzzer_on(nav::AlarmLevel::Alarm, 0.3));
    EXPECT_TRUE(nav::buzzer_on(nav::AlarmLevel::Warning, 0.1));
    EXPECT_TRUE(nav::buzzer_on(nav::AlarmLevel::Warning, 0.35));
    EXPECT_FALSE(nav::buzzer_on(nav::AlarmLevel::Warning, 2.0));
}

TEST(AisTable, BeaconsAreRecognisedAndListedFirst) {
    EXPECT_EQ(nav::ais_kind(970123456), nav::AisKind::Sart);
    EXPECT_EQ(nav::ais_kind(972123456), nav::AisKind::Mob);
    EXPECT_EQ(nav::ais_kind(974123456), nav::AisKind::Epirb);
    EXPECT_EQ(nav::ais_kind(211234560), nav::AisKind::Vessel);

    nav::AisTable table;
    const auto now = core::Clock::now();
    core::AisReport ship;
    ship.mmsi = 211234560;
    ship.position = core::destination(kHome, 90.0, 300.0);
    table.update(ship, now);
    core::AisReport sart;
    sart.mmsi = 970123456;
    sart.position = core::destination(kHome, 0.0, 3000.0);
    sart.nav_status = 14;
    table.update(sart, now);
    const auto list = table.evaluate(fix(kHome, now).value, std::nullopt, now);
    ASSERT_EQ(list.targets.size(), 2U);
    EXPECT_EQ(list.targets[0].kind, nav::AisKind::Sart);  // farther away, but first

    nav::AlarmEvaluator e;
    const auto alarms = e.evaluate(fix(kHome, now), std::nullopt, list, now);
    ASSERT_TRUE(has(alarms, nav::AlarmId::AisBeacon));
    EXPECT_EQ(alarms.sound, nav::AlarmLevel::Alarm);
}

TEST(AisTable, BeaconInTestModeDoesNotAlarm) {
    nav::AisTable table;
    const auto now = core::Clock::now();
    core::AisReport sart;
    sart.mmsi = 972000001;
    sart.position = kHome;
    sart.nav_status = 15;  // test
    table.update(sart, now);
    const auto list = table.evaluate(fix(kHome, now).value, std::nullopt, now);
    ASSERT_EQ(list.targets.size(), 1U);
    EXPECT_TRUE(list.targets[0].beacon_test);
    nav::AlarmEvaluator e;
    EXPECT_FALSE(has(e.evaluate(fix(kHome, now), std::nullopt, list, now), nav::AlarmId::AisBeacon));
}

TEST(AisTable, FixedStationsHaveNoCpaAndComeLast) {
    nav::AisTable table;
    const auto now = core::Clock::now();
    core::AisReport aton;
    aton.mmsi = 992111234;
    aton.station = core::AisStation::AtoN;
    aton.off_position = true;
    aton.name = "TONNE 3";
    aton.position = core::destination(kHome, 0.0, 200.0);  // right ahead
    table.update(aton, now);
    core::AisReport ship;
    ship.mmsi = 211234560;
    ship.position = core::destination(kHome, 90.0, 3000.0);
    table.update(ship, now);
    const auto list = table.evaluate(fix(kHome, now).value, core::CourseOverGround{0.0, 3.0}, now);
    ASSERT_EQ(list.targets.size(), 2U);
    EXPECT_EQ(list.targets[1].data.station, core::AisStation::AtoN);  // nearer, but after the ship
    EXPECT_FALSE(list.targets[1].cpa_m);
    EXPECT_FALSE(list.targets[1].dangerous);
    // still listed after 5 min without a report (AtoN report every 3 min)
    EXPECT_FALSE(table.evaluate(fix(kHome, now).value, std::nullopt, now + 5min).targets.back().lost);

    nav::AlarmEvaluator e;
    const auto alarms = e.evaluate(fix(kHome, now), std::nullopt, list, now);
    ASSERT_TRUE(has(alarms, nav::AlarmId::AtonOffPosition));
    EXPECT_EQ(alarms.sound, nav::AlarmLevel::Warning);
}

TEST(AisTable, SafetyMessagesAlarmUnlessTest) {
    nav::AisTable table;
    const auto now = core::Clock::now();
    table.message({211000001, "STURMWARNUNG", false}, now);
    table.message({972000123, "MOB TEST", false}, now);
    table.message({211000001, "STURMWARNUNG", false}, now + 1s);  // repeated: one entry
    auto list = table.evaluate(fix(kHome, now).value, std::nullopt, now + 2s);
    ASSERT_EQ(list.messages.size(), 2U);
    EXPECT_EQ(list.messages[0].message.text, "STURMWARNUNG");  // newest first

    nav::AlarmEvaluator e;
    auto alarms = e.evaluate(fix(kHome, now), std::nullopt, list, now);
    EXPECT_EQ(std::count_if(alarms.active.begin(), alarms.active.end(),
                            [](const nav::Alarm& a) { return a.id == nav::AlarmId::AisMessage; }),
              1);
    EXPECT_EQ(alarms.sound, nav::AlarmLevel::Warning);

    table.message({970123456, "SART ACTIVE", false}, now + 3s);  // from a beacon: alarm
    list = table.evaluate(fix(kHome, now).value, std::nullopt, now + 4s);
    EXPECT_EQ(e.evaluate(fix(kHome, now), std::nullopt, list, now).sound, nav::AlarmLevel::Alarm);
    // after an hour the messages are gone
    EXPECT_TRUE(table.evaluate(fix(kHome, now).value, std::nullopt, now + 2h).messages.empty());
}
