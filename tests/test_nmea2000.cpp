#include "boat/nmea2000/decoder.hpp"

#include "boat/core/marine_data.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace boat;

namespace {

std::uint32_t can_id(std::uint32_t pgn, std::uint8_t source, std::uint8_t priority = 2) {
    return (static_cast<std::uint32_t>(priority) << 26) | (pgn << 8) | source;
}

hal::CanFrame frame(std::uint32_t pgn, std::vector<std::uint8_t> bytes, std::uint8_t source = 7) {
    hal::CanFrame f;
    f.id = can_id(pgn, source);
    f.extended = true;
    f.dlc = static_cast<std::uint8_t>(bytes.size());
    std::memcpy(f.data.data(), bytes.data(), bytes.size());
    return f;
}

void put16(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = static_cast<std::uint8_t>(x & 0xFF);
    v[at + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
}

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    put16(v, at, x & 0xFFFF);
    put16(v, at + 2, x >> 16);
}

}  // namespace

TEST(Nmea2000, ParseCanIdPdu2) {
    const auto id = nmea2000::parse_can_id(can_id(129025, 35, 2));
    EXPECT_EQ(id.pgn, 129025U);
    EXPECT_EQ(id.source, 35);
    EXPECT_EQ(id.priority, 2);
    EXPECT_EQ(id.destination, 0xFF);
}

TEST(Nmea2000, ParseCanIdPdu1HasDestination) {
    // ISO request (59904) addressed to node 0x23
    const auto id = nmea2000::parse_can_id((6U << 26) | (59904U << 8) | (0x23U << 8) | 0x10);
    EXPECT_EQ(id.pgn, 59904U);
    EXPECT_EQ(id.destination, 0x23);
    EXPECT_EQ(id.source, 0x10);
}

TEST(Nmea2000, PositionRapidUpdate) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    std::vector<std::uint8_t> b(8);
    put32(b, 0, static_cast<std::uint32_t>(478700000));  // 47.87
    put32(b, 4, static_cast<std::uint32_t>(-135450000));  // -13.545
    EXPECT_TRUE(d.feed(frame(129025, b)));
    const auto p = bus.latest<core::Position>();
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->value.point.lat_deg, 47.87, 1e-7);
    EXPECT_NEAR(p->value.point.lon_deg, -13.545, 1e-7);
}

TEST(Nmea2000, NotAvailablePositionIsIgnored) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    std::vector<std::uint8_t> b(8, 0xFF);
    b[3] = 0x7F;
    b[7] = 0x7F;
    EXPECT_FALSE(d.feed(frame(129025, b)));
    EXPECT_FALSE(bus.latest<core::Position>());
}

TEST(Nmea2000, DepthWithOffset) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    std::vector<std::uint8_t> b(8, 0xFF);
    b[0] = 1;
    put32(b, 1, 523);                                   // 5.23 m
    put16(b, 5, static_cast<std::uint16_t>(-400));      // -0.4 m (below keel)
    EXPECT_TRUE(d.feed(frame(128267, b)));
    EXPECT_NEAR(bus.latest<core::Depth>()->value.depth_m(), 4.83, 1e-9);
}

TEST(Nmea2000, ShortFrameDoesNotReadPastEnd) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    EXPECT_FALSE(d.feed(frame(128267, {1, 0x10})));
    EXPECT_FALSE(bus.latest<core::Depth>());
}

TEST(Nmea2000, ApparentWindOnly) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    std::vector<std::uint8_t> b(8, 0xFF);
    put16(b, 1, 500);                                                 // 5 m/s
    put16(b, 3, static_cast<std::uint32_t>(std::lround(std::numbers::pi / 2 * 1e4)));  // 90 deg
    b[5] = 0xFA;                                                      // reference 2 = apparent
    EXPECT_TRUE(d.feed(frame(130306, b)));
    EXPECT_NEAR(bus.latest<core::ApparentWind>()->value.angle_deg, 90.0, 0.01);
    b[5] = 0xF8;  // true (ground referenced) is recomputed by us, not taken over
    EXPECT_FALSE(d.feed(frame(130306, b)));
}

TEST(Nmea2000, FastPacketAisClassBReassembly) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    std::vector<std::uint8_t> msg(27, 0xFF);
    msg[0] = 18;
    put32(msg, 1, 211234560);
    put32(msg, 5, static_cast<std::uint32_t>(135450000));  // lon 13.545
    put32(msg, 9, static_cast<std::uint32_t>(478700000));  // lat 47.87
    put16(msg, 14, 15708);                                 // COG ~90 deg
    put16(msg, 16, 300);                                   // 3 m/s
    // Split into fast-packet frames: 6 bytes, then 7 per frame
    const std::uint8_t seq = 3 << 5;
    std::size_t pos = 0;
    std::uint8_t counter = 0;
    bool published = false;
    while (pos < msg.size()) {
        std::vector<std::uint8_t> b(8, 0xFF);
        b[0] = static_cast<std::uint8_t>(seq | counter);
        std::size_t start = 1;
        if (counter == 0) {
            b[1] = static_cast<std::uint8_t>(msg.size());
            start = 2;
        }
        for (std::size_t i = start; i < 8 && pos < msg.size(); ++i) b[i] = msg[pos++];
        published = d.feed(frame(129039, b));
        ++counter;
    }
    EXPECT_TRUE(published);
    const auto a = bus.latest<core::AisReport>();
    ASSERT_TRUE(a);
    EXPECT_EQ(a->value.mmsi, 211234560U);
    EXPECT_TRUE(a->value.class_b);
    EXPECT_NEAR(a->value.position->lat_deg, 47.87, 1e-7);
    EXPECT_NEAR(*a->value.cog_deg, 90.0, 0.01);
}

TEST(Nmea2000, FastPacketLostFrameDiscardsMessage) {
    nmea2000::FastPacketAssembler fp;
    std::vector<std::uint8_t> first{0x20, 20, 1, 2, 3, 4, 5, 6};
    std::vector<std::uint8_t> third{0x22, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_FALSE(fp.push(frame(129038, first)));
    EXPECT_FALSE(fp.push(frame(129038, third)));  // frame 1 missing
}
