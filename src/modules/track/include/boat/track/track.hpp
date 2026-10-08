#pragma once

// Track recording ("breadcrumbs").
//
// Storage: one CSV file per UTC day (<dir>/YYYY-MM-DD.csv), append-only:
// a power cut can at worst lose the last, partly written line, which the
// reader skips. GPX export on demand.
//
// Timestamps come from the GNSS (core::UtcTime) when available - the
// Raspberry Pi has no real-time clock and boots with a wrong date when it
// has no internet.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "boat/core/marine_data.hpp"
#include "boat/core/module.hpp"

namespace boat::track {

struct TrackPoint {
    std::int64_t unix_ms = 0;
    core::GeoPoint point;
    double sog_mps = 0.0;
    double cog_deg = 0.0;
};

struct FilterSettings {
    double min_distance_m = 10.0;      // record after moving this far ...
    double turn_deg = 15.0;            // ... or when the course changed this much
    double min_turn_distance_m = 3.0;  //     (and the boat moved at least this far)
    double max_speed_mps = 30.0;       // faster than ~58 kn between two fixes: GNSS glitch
};

// Decides whether `candidate` becomes a new track point after `last`.
// At anchor the position jitters by a few metres: nothing is recorded.
[[nodiscard]] bool accept(const std::optional<TrackPoint>& last, const TrackPoint& candidate,
                          const FilterSettings& settings);

// File format helpers (exposed for tests)
[[nodiscard]] std::string to_csv_line(const TrackPoint& p);
[[nodiscard]] std::vector<TrackPoint> parse_csv(std::string_view text);  // skips broken lines
[[nodiscard]] std::string to_gpx(const std::vector<TrackPoint>& points, const std::string& name);
[[nodiscard]] std::string utc_date(std::int64_t unix_ms);  // "YYYY-MM-DD"
[[nodiscard]] double track_length_m(const std::vector<TrackPoint>& points);
// Keeps every n-th point (and always the last) so that at most `max_points` remain.
[[nodiscard]] std::vector<TrackPoint> thin_out(const std::vector<TrackPoint>& points, std::size_t max_points);

// UI -> TrackModule
struct TrackCommand {
    enum class Action : std::uint8_t {
        SetRecording,
        ClearDisplay,
        ExportGpx,   // `day` ("YYYY-MM-DD"), empty = today
        ShowDays,    // load `days` for display (TrackHistory); empty = hide all
    } action = Action::SetRecording;
    bool recording = true;
    FilterSettings filter;  // applied with SetRecording
    std::string day;
    std::vector<std::string> days;
};

struct DayInfo {
    std::string date;  // "YYYY-MM-DD" (UTC)
    double length_m = 0.0;
    std::size_t points = 0;
    bool today = false;  // the day currently being recorded
    // Logbook figures
    std::int64_t start_ms = 0;  // first / last recorded point
    std::int64_t end_ms = 0;
    double max_sog_mps = 0.0;
    double underway_s = 0.0;  // time moving (gaps over 10 min = stopped, not counted)
};

// Logbook figures of one day's points
[[nodiscard]] DayInfo day_summary(const std::string& date, const std::vector<TrackPoint>& points);

// Tracks of earlier days selected for display (thinned out for drawing)
struct TrackHistory {
    struct Day {
        std::string date;
        std::vector<TrackPoint> points;
    };
    std::vector<Day> days;
};

// TrackModule -> UI (published when something changed)
struct TrackState {
    bool recording = false;
    std::uint64_t version = 0;           // increments with every change of `points`
    std::vector<TrackPoint> points;      // displayed track (today, newest last, capped)
    double today_m = 0.0;                // distance logged today
    std::string last_export;             // path of the last GPX export, or error text
    bool time_from_gnss = false;
    std::vector<DayInfo> days;  // all recorded days, newest first (today included)
};

struct TrackConfig {
    std::filesystem::path dir = "tracks";
    bool recording = true;
    FilterSettings filter;
    std::size_t max_display_points = 5000;
};

class TrackModule final : public core::Module {
public:
    explicit TrackModule(TrackConfig config) : config_(std::move(config)) {}
    ~TrackModule() override;

    [[nodiscard]] std::string_view name() const override { return "track"; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    void run(core::DataBus& bus);

    TrackConfig config_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::track
