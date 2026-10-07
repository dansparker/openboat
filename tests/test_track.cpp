#include "boat/track/track.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "boat/core/data_bus.hpp"
#include "boat/core/nav_math.hpp"
#include "boat/nmea0183/parser.hpp"
#include "boat/nmea2000/decoder.hpp"
#include "test_util.hpp"

using namespace boat;

namespace {

const core::GeoPoint kHome{47.87, 13.545};

track::TrackPoint at(std::int64_t ms, core::GeoPoint p, double sog = 2.5, double cog = 0.0) {
    return {ms, p, sog, cog};
}

}  // namespace

TEST(UtcTime, CivilToUnixAndBack) {
    EXPECT_EQ(core::unix_ms(2026, 10, 8, 12, 34, 56.5), 1791462896500);
    EXPECT_EQ(track::utc_date(1791462896500), "2026-10-08");
    EXPECT_EQ(track::utc_date(0), "1970-01-01");
}

TEST(UtcTime, FromRmc) {
    core::DataBus bus;
    nmea0183::Parser p(bus);
    p.feed(with_checksum("$GPRMC,123519,A,4752.200,N,01332.700,E,5.0,084.4,230394,4.5,E,A"));
    ASSERT_TRUE(bus.latest<core::UtcTime>());
    EXPECT_EQ(bus.latest<core::UtcTime>()->value.unix_ms, 764426119000);
}

TEST(UtcTime, FromNmea2000SystemTimeOnlyIfGnss) {
    core::DataBus bus;
    nmea2000::Decoder d(bus);
    hal::CanFrame f;
    f.id = (3U << 26) | (126992U << 8) | 5U;
    f.extended = true;
    f.dlc = 8;
    const std::uint16_t days = 20734;               // 2026-10-08
    const std::uint32_t time = 45296U * 10000U + 5000U;  // 12:34:56.5
    const std::uint8_t bytes[8] = {0,    0xF0, static_cast<std::uint8_t>(days & 0xFF), static_cast<std::uint8_t>(days >> 8),
                                   static_cast<std::uint8_t>(time & 0xFF), static_cast<std::uint8_t>(time >> 8),
                                   static_cast<std::uint8_t>(time >> 16), static_cast<std::uint8_t>(time >> 24)};
    std::memcpy(f.data.data(), bytes, 8);
    EXPECT_TRUE(d.feed(f));  // source 0 = GPS
    EXPECT_EQ(bus.latest<core::UtcTime>()->value.unix_ms, 1791462896500);
    f.data[1] = 0xF5;  // source 5 = local crystal clock: not trusted
    EXPECT_FALSE(d.feed(f));
}

TEST(TrackFilter, IgnoresAnchorJitterRecordsMovementAndTurns) {
    const track::FilterSettings s;
    const auto first = at(0, kHome);
    EXPECT_TRUE(track::accept(std::nullopt, first, s));
    EXPECT_FALSE(track::accept(first, at(5000, core::destination(kHome, 45, 4)), s));  // swinging at anchor
    EXPECT_TRUE(track::accept(first, at(5000, core::destination(kHome, 0, 12)), s));
    EXPECT_TRUE(track::accept(first, at(2000, core::destination(kHome, 90, 5), 2.5, 90.0), s));  // turn
}

TEST(TrackFilter, RejectsGnssJumps) {
    const track::FilterSettings s;
    const auto first = at(0, kHome);
    EXPECT_FALSE(track::accept(first, at(1000, core::destination(kHome, 0, 500)), s));  // 500 m in 1 s
    // Clock corrected backwards: no speed check, distance decides
    EXPECT_TRUE(track::accept(first, at(-3600000, core::destination(kHome, 0, 500)), s));
}

TEST(TrackFile, CsvRoundTripSkipsCutOffLine) {
    std::string text = "# OpenBoat track v1\n";
    text += track::to_csv_line(at(1000, kHome, 2.0, 10.0));
    text += track::to_csv_line(at(2000, core::destination(kHome, 0, 20), 2.0, 10.0));
    text += "3000,47.87";  // power cut while writing
    const auto pts = track::parse_csv(text);
    ASSERT_EQ(pts.size(), 2U);
    EXPECT_EQ(pts[1].unix_ms, 2000);
    EXPECT_NEAR(track::track_length_m(pts), 20.0, 0.01);
}

TEST(TrackFile, Gpx) {
    const auto gpx = track::to_gpx({at(1791462896500, kHome)}, "Test");
    EXPECT_NE(gpx.find("<trkpt lat=\"47.8700000\" lon=\"13.5450000\"><time>2026-10-08T12:34:56Z</time>"),
              std::string::npos)
        << gpx;
}

TEST(TrackModule, RecordsAppendsAndExports) {
    const auto dir = std::filesystem::temp_directory_path() / "openboat-track-test";
    std::filesystem::remove_all(dir);
    core::DataBus bus;
    track::TrackModule m({dir, true, {}, 100});
    m.start(bus);
    const std::int64_t t0 = core::unix_ms(2026, 10, 8, 10, 0, 0);
    for (int i = 0; i < 4; ++i) {
        bus.publish(core::UtcTime{t0 + i * 1000});
        core::Position p;
        p.point = core::destination(kHome, 0.0, 15.0 * i);
        p.quality = core::FixQuality::Gnss;
        bus.publish(p);
        bus.publish(core::CourseOverGround{0.0, 3.0});
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    }
    bus.publish(track::TrackCommand{track::TrackCommand::Action::ExportGpx});
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    m.stop();

    const auto state = bus.latest<track::TrackState>();
    ASSERT_TRUE(state);
    EXPECT_TRUE(state->value.time_from_gnss);
    EXPECT_GE(state->value.points.size(), 2U);
    EXPECT_NEAR(state->value.today_m, track::track_length_m(state->value.points), 0.5);
    EXPECT_TRUE(std::filesystem::exists(dir / "2026-10-08.csv"));
    EXPECT_TRUE(std::filesystem::exists(dir / "2026-10-08.gpx"));
    std::filesystem::remove_all(dir);
}

TEST(TrackFile, ThinOutKeepsEndsAndLimit) {
    std::vector<track::TrackPoint> pts;
    for (int i = 0; i < 1000; ++i) pts.push_back(at(i * 1000, core::destination(kHome, 0, 10.0 * i)));
    const auto thin = track::thin_out(pts, 100);
    EXPECT_LE(thin.size(), 101U);
    EXPECT_EQ(thin.front().unix_ms, 0);
    EXPECT_EQ(thin.back().unix_ms, 999000);
    EXPECT_EQ(track::thin_out(pts, 2000).size(), 1000U);
}

TEST(TrackModule, ListsAndShowsEarlierDays) {
    const auto dir = std::filesystem::temp_directory_path() / "openboat-track-history";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "2026-10-01.csv");
        f << track::to_csv_line(at(core::unix_ms(2026, 10, 1, 9, 0, 0), kHome));
        f << track::to_csv_line(at(core::unix_ms(2026, 10, 1, 9, 1, 0), core::destination(kHome, 0, 100)));
        std::ofstream junk(dir / "notes.csv");  // not a track file: ignored
        junk << "hello\n";
    }
    core::DataBus bus;
    track::TrackModule m({dir, false, {}, 100});
    m.start(bus);
    bus.publish(core::UtcTime{core::unix_ms(2026, 10, 8, 10, 0, 0)});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    track::TrackCommand c;
    c.action = track::TrackCommand::Action::ShowDays;
    c.days = {"2026-10-01", "../etc/passwd"};
    bus.publish(c);
    bus.publish(core::UtcTime{core::unix_ms(2026, 10, 8, 10, 0, 1)});
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    m.stop();

    const auto state = bus.latest<track::TrackState>();
    ASSERT_TRUE(state);
    ASSERT_EQ(state->value.days.size(), 1U);
    EXPECT_EQ(state->value.days[0].date, "2026-10-01");
    EXPECT_NEAR(state->value.days[0].length_m, 100.0, 0.5);
    const auto history = bus.latest<track::TrackHistory>();
    ASSERT_TRUE(history);
    ASSERT_EQ(history->value.days.size(), 1U);  // the path traversal attempt is rejected
    EXPECT_EQ(history->value.days[0].points.size(), 2U);
    std::filesystem::remove_all(dir);
}
