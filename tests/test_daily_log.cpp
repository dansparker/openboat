#include "boat/core/daily_log.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "boat/core/nav_math.hpp"
#include "boat/nmea0183/parser.hpp"
#include "test_util.hpp"

using namespace boat;

TEST(DailyLog, TimeTagRoundTrip) {
    const auto ms = core::unix_ms(2026, 10, 8, 12, 30, 15);
    const std::string tag = core::nmea_time_tag(ms);
    EXPECT_EQ(tag.front(), '\\');
    EXPECT_EQ(core::nmea_tag_time_ms(tag + "$GPRMC,..."), ms);
    EXPECT_EQ(core::nmea_tag_time_ms("\\s:r003669,c:1696789012*5A\\!AIVDM"), 1696789012000);
    EXPECT_EQ(core::nmea_tag_time_ms("\\c:1696789012345*00\\$GP"), 1696789012345);  // milliseconds
    EXPECT_EQ(core::nmea_tag_time_ms("$GPRMC,no tag"), -1);
}

TEST(DailyLog, TaggedLinesAreParsed) {
    // The parser must accept the recorded lines (tag block in front)
    core::DataBus bus;
    nmea0183::Parser p(bus);
    EXPECT_TRUE(p.feed(core::nmea_time_tag(core::unix_ms(2026, 10, 8, 12, 0, 0)) + with_checksum("$HCHDT,123.0,T")));
    EXPECT_TRUE(bus.latest<core::Heading>());
}

TEST(DailyLog, OneFilePerDayWithCap) {
    const auto dir = std::filesystem::temp_directory_path() / "openboat-dailylog-test";
    std::filesystem::remove_all(dir);
    {
        core::DailyLog log(dir, "nmea0183-test", "nmea", 60);
        EXPECT_TRUE(log.write("line one", core::unix_ms(2026, 10, 8, 23, 59, 0)));
        EXPECT_TRUE(log.write("line two", core::unix_ms(2026, 10, 9, 0, 1, 0)));  // next UTC day
        EXPECT_EQ(log.current_file().filename().string(), "nmea0183-test-2026-10-09.nmea");
        EXPECT_TRUE(log.write(std::string(40, 'x'), core::unix_ms(2026, 10, 9, 0, 2, 0)));
        EXPECT_FALSE(log.write(std::string(40, 'y'), core::unix_ms(2026, 10, 9, 0, 3, 0)));  // over 60 bytes
    }
    std::ifstream in(dir / "nmea0183-test-2026-10-08.nmea");
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_EQ(text.str(), "line one\n");
    std::filesystem::remove_all(dir);
}
