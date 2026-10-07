#include "boat/nmea0183/ais.hpp"
#include "boat/nmea0183/parser.hpp"

#include <gtest/gtest.h>

#include "test_util.hpp"

using namespace boat;

// Test vectors from the gpsd AIVDM/AIVDO protocol description.
TEST(Ais, Type1PositionReport) {
    const auto r = nmea0183::AisDecoder::decode_payload("177KQJ5000G?tO`K>RA1wUbN0TKH", 0);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->mmsi, 477553000U);
    ASSERT_TRUE(r->position);
    EXPECT_NEAR(r->position->lat_deg, 47.582833, 1e-5);
    EXPECT_NEAR(r->position->lon_deg, -122.345832, 1e-5);
    EXPECT_NEAR(*r->cog_deg, 51.0, 1e-9);
    EXPECT_NEAR(*r->sog_mps, 0.0, 1e-9);
    EXPECT_NEAR(*r->heading_deg, 181.0, 1e-9);
    EXPECT_FALSE(r->class_b);
    EXPECT_EQ(r->nav_status, 5);  // moored
}

TEST(Ais, Type5TwoFragmentsViaParser) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0"));
    EXPECT_FALSE(bus.latest<core::AisReport>());
    p.feed(with_checksum("!AIVDM,2,2,1,A,88888888880,2"));
    const auto r = bus.latest<core::AisReport>();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->value.mmsi, 351759000U);
    EXPECT_EQ(r->value.name, "EVER DIADEM");
    EXPECT_EQ(r->value.callsign, "3FOF8");
    EXPECT_EQ(r->value.ship_type, 70);
    EXPECT_NEAR(*r->value.length_m, 295.0, 1e-9);
    EXPECT_NEAR(*r->value.beam_m, 32.0, 1e-9);
}

TEST(Ais, OutOfOrderFragmentIsDropped) {
    nmea0183::AisDecoder d;
    EXPECT_FALSE(d.feed(2, 2, "3", 'A', "88888888880", 2));
}

TEST(Ais, OwnShipVdoIsNotATarget) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    EXPECT_TRUE(p.feed(with_checksum("!AIVDO,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0")));
    EXPECT_FALSE(bus.latest<core::AisReport>());
}

TEST(Ais, TooShortPayloadIsRejected) {
    EXPECT_FALSE(nmea0183::AisDecoder::decode_payload("177KQJ", 0));
}
