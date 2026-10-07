#include "boat/track/track.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

#include "boat/core/data_bus.hpp"
#include "boat/core/nav_math.hpp"

namespace boat::track {

namespace {

std::int64_t system_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string iso_time(std::int64_t unix_ms) {
    const std::int64_t s = unix_ms / 1000;
    const std::string date = utc_date(unix_ms);
    const auto sec_of_day = static_cast<int>(((s % 86400) + 86400) % 86400);
    char buf[32];
    std::snprintf(buf, sizeof buf, "T%02d:%02d:%02dZ", sec_of_day / 3600, sec_of_day / 60 % 60, sec_of_day % 60);
    return date + buf;
}

}  // namespace

bool accept(const std::optional<TrackPoint>& last, const TrackPoint& c, const FilterSettings& s) {
    if (!last) return true;
    const double d = core::distance_m(last->point, c.point);
    const double dt = static_cast<double>(c.unix_ms - last->unix_ms) / 1000.0;
    // Implausible jump. Only checked with a positive time step: when the clock is
    // corrected (system time -> GNSS time) the step may be negative or huge.
    if (dt > 0 && d / dt > s.max_speed_mps && d > s.min_distance_m) return false;
    if (d >= s.min_distance_m) return true;
    const double turn = std::abs(core::signed_angle_deg(c.cog_deg - last->cog_deg));
    return d >= s.min_turn_distance_m && turn >= s.turn_deg && c.sog_mps > 0.5;
}

std::string to_csv_line(const TrackPoint& p) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "%lld,%.7f,%.7f,%.2f,%.1f\n", static_cast<long long>(p.unix_ms), p.point.lat_deg,
                  p.point.lon_deg, p.sog_mps, p.cog_deg);
    return buf;
}

std::vector<TrackPoint> parse_csv(std::string_view text) {
    std::vector<TrackPoint> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        TrackPoint p;
        long long ms = 0;
        char tail = 0;
        // %c after the last field catches garbage; a cut-off line has too few fields
        const int n = std::sscanf(line.c_str(), "%lld,%lf,%lf,%lf,%lf%c", &ms, &p.point.lat_deg, &p.point.lon_deg,
                                  &p.sog_mps, &p.cog_deg, &tail);
        if (n != 5 && !(n == 6 && (tail == '\r' || tail == ' '))) continue;
        if (std::abs(p.point.lat_deg) > 90 || std::abs(p.point.lon_deg) > 180) continue;
        p.unix_ms = ms;
        out.push_back(p);
    }
    return out;
}

std::string utc_date(std::int64_t unix_ms) {
    // civil_from_days (H. Hinnant)
    std::int64_t z = (unix_ms >= 0 ? unix_ms : unix_ms - 86399999) / 86400000 + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const auto y = static_cast<long long>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02u", y, m, d);
    return buf;
}

double track_length_m(const std::vector<TrackPoint>& points) {
    double m = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) m += core::distance_m(points[i - 1].point, points[i].point);
    return m;
}

std::string to_gpx(const std::vector<TrackPoint>& points, const std::string& name) {
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" version=\"1.1\" creator=\"OpenBoat\">\n"
         "  <trk>\n    <name>"
      << name << "</name>\n    <trkseg>\n";
    char buf[96];
    for (const auto& p : points) {
        std::snprintf(buf, sizeof buf, "      <trkpt lat=\"%.7f\" lon=\"%.7f\">", p.point.lat_deg, p.point.lon_deg);
        x << buf << "<time>" << iso_time(p.unix_ms) << "</time></trkpt>\n";
    }
    x << "    </trkseg>\n  </trk>\n</gpx>\n";
    return x.str();
}

// ---- TrackModule -------------------------------------------------------------

TrackModule::~TrackModule() { stop(); }

void TrackModule::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] { run(bus); });
}

void TrackModule::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void TrackModule::run(core::DataBus& bus) {
    using namespace std::chrono_literals;
    std::mutex mutex;
    std::vector<TrackCommand> commands;
    const auto sub = bus.topic<TrackCommand>().subscribe([&](const auto& s) {
        std::scoped_lock lock(mutex);
        commands.push_back(s.value);
    });

    std::error_code ec;
    std::filesystem::create_directories(config_.dir, ec);

    TrackState state;
    state.recording = config_.recording;
    FilterSettings filter = config_.filter;
    std::string day;  // UTC date of the open file
    std::ofstream file;
    std::optional<TrackPoint> last;
    std::vector<TrackPoint> day_points;  // all points of `day` (for distance and export)
    bool dirty = true;
    const auto started = core::Clock::now();

    const auto open_day = [&](const std::string& d) {
        day = d;
        file.close();
        const auto path = config_.dir / (day + ".csv");
        std::ifstream in(path);
        std::stringstream text;
        text << in.rdbuf();
        day_points = parse_csv(text.str());
        last = day_points.empty() ? std::nullopt : std::optional(day_points.back());
        file.open(path, std::ios::app);
        if (!file) std::cerr << "[track] cannot write " << path << '\n';
        state.points.assign(day_points.end() - static_cast<std::ptrdiff_t>(std::min(day_points.size(), config_.max_display_points)),
                            day_points.end());
        state.today_m = track_length_m(day_points);
        dirty = true;
    };

    while (running_) {
        // Time: GNSS if seen recently, otherwise the system clock
        std::int64_t now_ms = system_ms();
        state.time_from_gnss = false;
        if (const auto t = bus.latest<core::UtcTime>(); t && core::is_fresh(*t, 5s)) {
            now_ms = t->value.unix_ms + std::chrono::duration_cast<std::chrono::milliseconds>(core::Clock::now() - t->timestamp).count();
            state.time_from_gnss = true;
        }
        if (utc_date(now_ms) != day) open_day(utc_date(now_ms));

        {
            std::scoped_lock lock(mutex);
            for (const auto& c : commands) {
                switch (c.action) {
                    case TrackCommand::Action::SetRecording:
                        state.recording = c.recording;
                        filter = c.filter;
                        break;
                    case TrackCommand::Action::ClearDisplay:
                        state.points.clear();
                        break;
                    case TrackCommand::Action::ExportGpx: {
                        const auto path = config_.dir / (day + ".gpx");
                        std::ofstream out(path, std::ios::trunc);
                        out << to_gpx(day_points, "OpenBoat " + day);
                        state.last_export = out ? path.string() : "Export fehlgeschlagen: " + path.string();
                        break;
                    }
                }
                dirty = true;
            }
            commands.clear();
        }

        const auto pos = bus.latest<core::Position>();
        // Without GNSS time, wait a little before trusting the (possibly wrong) system clock
        const bool time_ok = state.time_from_gnss || core::Clock::now() - started > 15s;
        if (state.recording && time_ok && pos && core::is_fresh(*pos, 3s) && pos->value.quality != core::FixQuality::None &&
            pos->value.hdop.value_or(1.0) <= 5.0) {
            TrackPoint p;
            p.unix_ms = now_ms;
            p.point = pos->value.point;
            if (const auto c = bus.latest<core::CourseOverGround>(); c && core::is_fresh(*c, 3s)) {
                p.sog_mps = c->value.sog_mps;
                p.cog_deg = c->value.cog_deg;
            }
            if (accept(last, p, filter)) {
                if (last) state.today_m += core::distance_m(last->point, p.point);
                last = p;
                day_points.push_back(p);
                state.points.push_back(p);
                if (state.points.size() > config_.max_display_points) state.points.erase(state.points.begin());
                file << to_csv_line(p);
                file.flush();
                dirty = true;
            }
        }

        if (dirty) {
            ++state.version;
            bus.publish(state);
            dirty = false;
        }
        for (int i = 0; running_ && i < 10; ++i) std::this_thread::sleep_for(100ms);
    }
    bus.topic<TrackCommand>().unsubscribe(sub);
}

}  // namespace boat::track
