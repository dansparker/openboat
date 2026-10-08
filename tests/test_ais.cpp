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

TEST(Ais, Type4BaseStation) {
    const auto r = nmea0183::AisDecoder::decode_payload("403OviQuMGCqWrRO9>E6fE700@GO", 0);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->mmsi, 3669702U);
    EXPECT_EQ(r->station, core::AisStation::BaseStation);
    EXPECT_NEAR(r->position->lat_deg, 36.883767, 1e-5);
    EXPECT_NEAR(r->position->lon_deg, -76.352362, 1e-5);
}

TEST(Ais, Type21AidToNavigation) {
    // virtual, off position, type 1 (reference point)
    const auto r = nmea0183::AisDecoder::decode_payload("E>j9bPPQ7R2W9RRh:2ab00000000Dk6`=a04P10888v@10", 4);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->mmsi, 992111234U);
    EXPECT_EQ(r->station, core::AisStation::AtoN);
    EXPECT_EQ(r->aton_type, 1);
    EXPECT_EQ(r->name, "BODENSEE TEST");
    EXPECT_NEAR(r->position->lat_deg, 47.6775, 1e-5);
    EXPECT_NEAR(r->position->lon_deg, 9.087, 1e-5);
    EXPECT_TRUE(r->virtual_aton);
    EXPECT_TRUE(r->off_position);
}

TEST(Ais, Type21NameExtension) {
    const auto r = nmea0183::AisDecoder::decode_payload("E>j9bPhRa6Pb4W3RW@40S2W2TW30Dj50=`oB010888v0024U0", 4);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->name, "ERMATINGEN HAFENEINFAHRT");
    EXPECT_FALSE(r->virtual_aton);
    EXPECT_FALSE(r->off_position);
}

TEST(Ais, SafetyMessagesViaParser) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("!AIVDM,1,1,,A,>>M;`h1<59B04=@UHD,2"));
    auto m = bus.latest<core::AisSafetyMessage>();
    ASSERT_TRUE(m);
    EXPECT_EQ(m->value.mmsi, 970123456U);
    EXPECT_EQ(m->value.text, "SART ACTIVE");
    EXPECT_FALSE(m->value.addressed);
    EXPECT_FALSE(bus.latest<core::AisReport>());  // no station report
    p.feed(with_checksum("!AIVDM,1,1,,A,<39>Jh@jRo?tCDEB=G1B>E>7,0"));
    m = bus.latest<core::AisSafetyMessage>();
    EXPECT_EQ(m->value.text, "STURMWARNUNG");
    EXPECT_TRUE(m->value.addressed);
}
