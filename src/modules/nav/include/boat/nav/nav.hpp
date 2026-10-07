#pragma once

// Navigation services working only on DataBus data:
//   - true wind from apparent wind
//   - AIS target table with CPA/TCPA
//   - alarms: anchor watch, shallow water, AIS collision, GNSS lost
//
// The logic classes are pure (time is passed in) so they can be unit
// tested; NavModule wires them to the bus and runs them at 1 Hz.

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "boat/core/data_bus.hpp"
#include "boat/core/marine_data.hpp"
#include "boat/core/module.hpp"
#include "boat/nav/magnetic_model.hpp"
#include "boat/nav/route.hpp"

namespace boat::nav {

using core::Clock;

// ---- AIS --------------------------------------------------------------------

// What sends: a ship, or an emergency beacon (by MMSI, ITU-R M.585):
// 970xxxxxx AIS-SART (life raft), 972xxxxxx MOB device, 974xxxxxx EPIRB-AIS
enum class AisKind : std::uint8_t { Vessel, Sart, Mob, Epirb };

[[nodiscard]] AisKind ais_kind(std::uint32_t mmsi);

struct AisTarget {
    core::AisReport data;  // merged position + static data
    AisKind kind = AisKind::Vessel;
    // Beacon in test mode (navigational status 15): listed, no alarm
    bool beacon_test = false;
    Clock::time_point last_position{};
    std::optional<double> range_m;
    std::optional<double> bearing_deg;
    std::optional<double> cpa_m;
    std::optional<double> tcpa_s;
    bool dangerous = false;
    bool lost = false;  // no position for a while: shown greyed out
};

struct AisTargetList {
    std::vector<AisTarget> targets;
};

struct AisSettings {
    double cpa_alarm_m = 0.5 * 1852.0;
    double tcpa_alarm_s = 10 * 60.0;
    double lost_after_s = 3 * 60.0;
    double remove_after_s = 10 * 60.0;
    double ignore_slower_than_mps = 0.25;  // anchored boats do not alarm
};

class AisTable {
public:
    explicit AisTable(AisSettings settings = {}) : settings_(settings) {}
    void set_settings(const AisSettings& s) { settings_ = s; }

    void update(const core::AisReport& report, Clock::time_point now);
    // Recomputes range/CPA against own ship and expires old targets.
    AisTargetList evaluate(const std::optional<core::Position>& own, const std::optional<core::CourseOverGround>& cog,
                           Clock::time_point now);

private:
    AisSettings settings_;
    std::map<std::uint32_t, AisTarget> targets_;
};

// ---- Alarms -----------------------------------------------------------------

// Values are shown by the UI: append only
enum class AlarmId : std::uint8_t { AnchorDrag, ShallowWater, AisCollision, GnssLost, DepthLost, Arrival, Mob, AisBeacon };

// Alarm: immediate danger, continuous fast beeping until acknowledged.
// Warning: degraded information, short double beep every few seconds.
enum class AlarmLevel : std::uint8_t { Warning, Alarm };

struct Alarm {
    AlarmId id;
    std::string text;
    std::uint32_t subject = 0;  // AIS: MMSI, so each new target alarms on its own
    AlarmLevel level = AlarmLevel::Alarm;
    bool acknowledged = false;
};

struct AlarmList {
    std::vector<Alarm> active;
    // Highest level among unacknowledged alarms; nullopt = silent
    std::optional<AlarmLevel> sound;
};

// UI -> AlarmEvaluator: acknowledges all currently active alarms. An alarm
// that clears and comes back (or a new AIS target) sounds again.
struct AlarmAcknowledge {};

// Beep pattern shared by all sound outputs (GPIO buzzer, loudspeaker).
// t_s: any monotonic time in seconds.
[[nodiscard]] bool buzzer_on(std::optional<AlarmLevel> level, double t_s);

// Commands from the UI to the anchor watch (published on the bus).
struct AnchorCommand {
    enum class Action : std::uint8_t { Drop, Raise } action = Action::Drop;
    double radius_m = 40.0;
};

struct AnchorState {
    bool active = false;
    core::GeoPoint anchor;
    double radius_m = 0.0;
    double distance_m = 0.0;
};

struct AlarmSettings {
    std::optional<double> shallow_m = 2.0;  // nullopt = off
    double gnss_timeout_s = 5.0;
    double depth_timeout_s = 10.0;  // only once a depth has been seen
};

class AlarmEvaluator {
public:
    explicit AlarmEvaluator(AlarmSettings settings = {}) : settings_(settings) {}
    void set_settings(const AlarmSettings& s) { settings_ = s; }

    void command(const AnchorCommand& cmd, const std::optional<core::Sample<core::Position>>& own);
    void acknowledge();

    // Missing / stale data is an alarm of its own: an anchor watch that
    // silently stops when the GNSS drops out is worse than none.
    AlarmList evaluate(const std::optional<core::Sample<core::Position>>& own,
                       const std::optional<core::Sample<core::Depth>>& depth, const AisTargetList& ais,
                       Clock::time_point now, const std::vector<Alarm>& extra = {});

    [[nodiscard]] const AnchorState& anchor() const { return anchor_; }

private:
    AlarmList finish(AlarmList list);

    using Key = std::pair<AlarmId, std::uint32_t>;
    AlarmSettings settings_;
    AnchorState anchor_;
    std::set<Key> current_;
    std::set<Key> acknowledged_;
};

// ---- Module -----------------------------------------------------------------

struct NavSettings {
    AisSettings ais;
    AlarmSettings alarms;
    NavigatorSettings navigator;
};

// Publishing NavSettings on the bus (from the settings page) replaces the
// running module's settings immediately.

// Variation at a position and time from the model; nullopt when the model
// is outside its validity (5 years) - an outdated model is not used.
[[nodiscard]] std::optional<core::MagneticVariation> variation_at(const MagneticModel& model,
                                                                  const core::GeoPoint& p, std::int64_t unix_ms);

// Alarms raised by waypoint navigation (arrival, MOB).
[[nodiscard]] std::vector<Alarm> guidance_alarms(const Guidance& g);

class NavModule final : public core::Module {
public:
    // magnetic_model: optional; with it the variation at the own position is
    // published (MagneticVariation) for compasses that send none.
    explicit NavModule(NavSettings settings = {}, std::shared_ptr<const MagneticModel> magnetic_model = nullptr)
        : settings_(settings), magnetic_model_(std::move(magnetic_model)) {}
    ~NavModule() override;

    [[nodiscard]] std::string_view name() const override { return "nav"; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    NavSettings settings_;
    std::shared_ptr<const MagneticModel> magnetic_model_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::optional<core::SubscriptionId> wind_subscription_;
    core::DataBus* bus_ = nullptr;
};

}  // namespace boat::nav
