#include "boat/nav/route.hpp"

#include <gtest/gtest.h>

#include "boat/core/nav_math.hpp"
#include "boat/nav/nav.hpp"
#include "boat/nmea0183/output.hpp"
#include "boat/nmea0183/parser.hpp"

using namespace boat;

namespace {

const core::GeoPoint kStart{47.87, 13.545};

nav::Route three_leg_route() {
    nav::Route r{"Rundkurs", {}};
    r.points.push_back({"A", core::destination(kStart, 0.0, 1000.0)});
    r.points.push_back({"B", core::destination(r.points[0].point, 90.0, 1000.0)});
    r.points.push_back({"C", core::destination(r.points[1].point, 180.0, 1000.0)});
    return r;
}

}  // namespace

TEST(Navigator, GoToComputesBearingDistanceAndXte) {
    nav::Navigator n;
    const nav::Waypoint wp{"Boje", core::destination(kStart, 0.0, 2000.0)};
    n.command({nav::NavCommand::Action::GoTo, wp}, kStart);
    // 50 m east of the course line, halfway
    const auto pos = core::destination(core::destination(kStart, 0.0, 1000.0), 90.0, 50.0);
    const auto g = n.update(pos, core::CourseOverGround{0.0, 2.0});
    EXPECT_EQ(g.mode, nav::NavMode::GoTo);
    EXPECT_NEAR(*g.dtw_m, 1001.2, 1.0);
    EXPECT_NEAR(*g.xte_m, 50.0, 0.5);  // right of track
    EXPECT_GT(*g.btw_deg, 350.0);
    EXPECT_NEAR(*g.ttg_s, *g.dtw_m / *g.vmg_mps, 1e-9);
    EXPECT_FALSE(g.arrived);
}

TEST(Navigator, GoToWithoutFixWaitsForPosition) {
    nav::Navigator n;
    n.command({nav::NavCommand::Action::GoTo, {"X", core::destination(kStart, 45.0, 500.0)}}, std::nullopt);
    EXPECT_FALSE(n.update(std::nullopt, std::nullopt).dtw_m);
    const auto g = n.update(kStart, std::nullopt);
    ASSERT_TRUE(g.from);
    EXPECT_NEAR(core::distance_m(g.from->point, kStart), 0.0, 1e-6);
}

TEST(Navigator, RouteAdvancesInArrivalCircle) {
    nav::Navigator n;
    nav::NavCommand c;
    c.action = nav::NavCommand::Action::StartRoute;
    c.route = three_leg_route();
    n.command(c, kStart);
    EXPECT_EQ(n.update(kStart, std::nullopt).to.name, "A");
    const auto near_a = core::destination(c.route.points[0].point, 180.0, 30.0);
    const auto g = n.update(near_a, std::nullopt);
    EXPECT_EQ(g.to.name, "B");
    EXPECT_EQ(g.from->name, "A");
    EXPECT_EQ(g.remaining.size(), 2U);
}

TEST(Navigator, RouteAdvancesWhenWaypointIsPassedOutsideCircle) {
    nav::Navigator n;
    nav::NavCommand c;
    c.action = nav::NavCommand::Action::StartRoute;
    c.route = three_leg_route();
    n.command(c, kStart);
    // 300 m west of A and already 50 m beyond it: missed the circle, but passed
    const auto beyond = core::destination(core::destination(c.route.points[0].point, 0.0, 50.0), 270.0, 300.0);
    EXPECT_EQ(n.update(beyond, std::nullopt).to.name, "B");
}

TEST(Navigator, FinalWaypointRaisesArrivalUntilStopped) {
    nav::Navigator n;
    const nav::Waypoint wp{"Hafen", core::destination(kStart, 0.0, 500.0)};
    n.command({nav::NavCommand::Action::GoTo, wp}, kStart);
    const auto g = n.update(core::destination(wp.point, 180.0, 20.0), std::nullopt);
    EXPECT_TRUE(g.arrived);
    ASSERT_EQ(nav::guidance_alarms(g).size(), 1U);
    EXPECT_EQ(nav::guidance_alarms(g)[0].id, nav::AlarmId::Arrival);
    n.command({nav::NavCommand::Action::Stop}, std::nullopt);
    EXPECT_EQ(n.update(kStart, std::nullopt).mode, nav::NavMode::None);
}

TEST(Navigator, MobMarksPressPositionAndAlarms) {
    nav::Navigator n;
    nav::NavCommand c;
    c.action = nav::NavCommand::Action::Mob;
    c.waypoint = {"MOB", kStart};
    n.command(c, core::destination(kStart, 90.0, 30.0));  // 1 s later the boat is elsewhere
    const auto g = n.update(core::destination(kStart, 90.0, 100.0), std::nullopt);
    EXPECT_EQ(g.mode, nav::NavMode::Mob);
    EXPECT_NEAR(*g.dtw_m, 100.0, 0.5);
    EXPECT_NEAR(*g.btw_deg, 270.0, 0.1);
    const auto alarms = nav::guidance_alarms(g);
    ASSERT_EQ(alarms.size(), 1U);
    EXPECT_EQ(alarms[0].id, nav::AlarmId::Mob);
    EXPECT_EQ(alarms[0].level, nav::AlarmLevel::Alarm);
}

TEST(AutopilotOutput, SentencesAreValidAndSteerTowardsTrack) {
    nav::Navigator n;
    const nav::Waypoint wp{"Boje,1*", core::destination(kStart, 0.0, 2000.0)};
    n.command({nav::NavCommand::Action::GoTo, wp}, kStart);
    const auto pos = core::destination(core::destination(kStart, 0.0, 1000.0), 90.0, 92.6);  // 0.05 nm right
    const auto sentences = nmea0183::autopilot_sentences(n.update(pos, core::CourseOverGround{0.0, 2.0}));
    ASSERT_EQ(sentences.size(), 3U);
    for (const auto& s : sentences) {
        EXPECT_TRUE(nmea0183::checksum_ok(s, false)) << s;
        EXPECT_EQ(s.substr(s.size() - 2), "\r\n");
    }
    EXPECT_NE(sentences[0].find("GPRMB,A,0.05,L,Start,Boje1,"), std::string::npos) << sentences[0];
    EXPECT_NE(sentences[2].find("GPXTE,A,A,0.05,L,N,A"), std::string::npos) << sentences[2];
}

TEST(AutopilotOutput, NothingWithoutActiveNavigation) {
    EXPECT_TRUE(nmea0183::autopilot_sentences(nav::Guidance{}).empty());
}
