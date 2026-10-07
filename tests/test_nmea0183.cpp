#include "boat/nmea0183/parser.hpp"
#include "boat/nmea0183/source.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_util.hpp"

using namespace boat;

TEST(Nmea0183, Checksum) {
    EXPECT_TRUE(nmea0183::checksum_ok(with_checksum("$GPHDT,123.4,T"), false));
    EXPECT_FALSE(nmea0183::checksum_ok("$GPHDT,123.4,T*00", false));
    EXPECT_FALSE(nmea0183::checksum_ok("$GPHDT,123.4,T", false));
    EXPECT_TRUE(nmea0183::checksum_ok("$GPHDT,123.4,T", true));
}

TEST(Nmea0183, RmcPublishesPositionAndCourse) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    EXPECT_TRUE(p.feed(with_checksum("$GPRMC,123519,A,4752.200,N,01332.700,E,5.0,084.4,230394,4.5,E,A") + "\r\n"));
    const auto pos = bus.latest<core::Position>();
    ASSERT_TRUE(pos);
    EXPECT_NEAR(pos->value.point.lat_deg, 47.87, 1e-6);
    EXPECT_NEAR(pos->value.point.lon_deg, 13.545, 1e-6);
    const auto cog = bus.latest<core::CourseOverGround>();
    ASSERT_TRUE(cog);
    EXPECT_NEAR(cog->value.cog_deg, 84.4, 1e-9);
    EXPECT_NEAR(cog->value.sog_mps, 5.0 * 1852.0 / 3600.0, 1e-9);
}

TEST(Nmea0183, RmcWithoutFixPublishesNothing) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("$GPRMC,123519,V,,,,,,,230394,,,N"));
    EXPECT_FALSE(bus.latest<core::Position>());
}

TEST(Nmea0183, SouthWestHemisphere) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("$GNGGA,123519,3351.000,S,15112.000,W,1,08,0.9,5.0,M,,M,,"));
    const auto pos = bus.latest<core::Position>();
    ASSERT_TRUE(pos);
    EXPECT_NEAR(pos->value.point.lat_deg, -33.85, 1e-9);
    EXPECT_NEAR(pos->value.point.lon_deg, -151.2, 1e-9);
    EXPECT_EQ(pos->value.satellites, 8);
}

TEST(Nmea0183, HdgAppliesDeviationAndVariation) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("$HCHDG,100.0,2.0,W,4.0,E"));
    const auto h = bus.latest<core::Heading>();
    ASSERT_TRUE(h);
    EXPECT_TRUE(h->value.is_true);
    EXPECT_NEAR(h->value.heading_deg, 102.0, 1e-9);
}

TEST(Nmea0183, HdmWithoutVariationStaysMagnetic) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("$HCHDM,200.0,M"));
    const auto h = bus.latest<core::Heading>();
    ASSERT_TRUE(h);
    EXPECT_FALSE(h->value.is_true);
}

TEST(Nmea0183, DptUsesSentenceOffsetDbtUsesConfigured) {
    core::DataBus bus;
    nmea0183::Parser p(bus, {.depth_offset_m = -0.5});
    p.feed(with_checksum("$SDDPT,4.2,0.3"));
    EXPECT_NEAR(bus.latest<core::Depth>()->value.depth_m(), 4.5, 1e-9);
    p.feed(with_checksum("$SDDBT,13.1,f,4.0,M,2.2,F"));
    EXPECT_NEAR(bus.latest<core::Depth>()->value.depth_m(), 3.5, 1e-9);
}

TEST(Nmea0183, MwvRelativeIsApparentWind) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("$WIMWV,045.0,R,10.0,N,A"));
    const auto w = bus.latest<core::ApparentWind>();
    ASSERT_TRUE(w);
    EXPECT_NEAR(w->value.angle_deg, 45.0, 1e-9);
    EXPECT_NEAR(w->value.speed_mps, 10.0 * 1852.0 / 3600.0, 1e-9);
}

TEST(Nmea0183, GarbageIsCountedNotPublished) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    EXPECT_FALSE(p.feed("$GPRMC,12*ZZ"));
    EXPECT_FALSE(p.feed(with_checksum("$GPRMC,123519,A,99xx.0,N,01332.700,E,5.0,084.4,230394,,,A")));
    EXPECT_FALSE(p.feed(with_checksum("$GPXYZ,1,2,3")));
    EXPECT_EQ(p.stats().bad_checksum, 1U);
    EXPECT_EQ(p.stats().unsupported, 1U);
    EXPECT_FALSE(bus.latest<core::Position>());
}

TEST(Nmea0183, TagBlockIsSkipped) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    EXPECT_TRUE(p.feed("\\s:r003669,c:1241544035*4A\\" + with_checksum("$GPHDT,10.0,T")));
}

TEST(Nmea0183, LineAssemblerHandlesSplitChunks) {
    nmea0183::LineAssembler a;
    std::vector<std::string> lines;
    const auto collect = [&](std::string_view l) { lines.emplace_back(l); };
    a.push("$GPHDT,1", collect);
    a.push("0.0,T*00\r\n$GP", collect);
    a.push("HDT\n", collect);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "$GPHDT,10.0,T*00");
    EXPECT_EQ(lines[1], "$GPHDT");
}
