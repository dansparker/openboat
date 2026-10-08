#include "boat/nav/nav.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utility>
#include <sstream>

#include "boat/core/nav_math.hpp"

namespace boat::nav {

namespace {

double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

}  // namespace

// ---- AisTable ---------------------------------------------------------------

AisKind ais_kind(std::uint32_t mmsi) {
    switch (mmsi / 1000000) {
        case 970: return AisKind::Sart;
        case 972: return AisKind::Mob;
        case 974: return AisKind::Epirb;
        default: return AisKind::Vessel;
    }
}

void AisTable::update(const core::AisReport& r, Clock::time_point now) {
    AisTarget& t = targets_[r.mmsi];
    core::AisReport& d = t.data;
    d.mmsi = r.mmsi;
    t.kind = ais_kind(r.mmsi);
    if (r.station != core::AisStation::Vessel) {
        d.station = r.station;
        if (r.station == core::AisStation::AtoN) {
            d.aton_type = r.aton_type;
            d.virtual_aton = r.virtual_aton;
            d.off_position = r.off_position;
        }
    }
    if (r.nav_status) {
        d.nav_status = r.nav_status;
        t.beacon_test = t.kind != AisKind::Vessel && *r.nav_status == 15;
    }
    d.class_b = d.class_b || r.class_b;
    // Position reports and static reports arrive separately: merge them
    if (r.position) {
        if (r.station == core::AisStation::Vessel &&
            (t.trail.empty() || now - t.trail.back().second >= std::chrono::seconds(30))) {
            t.trail.emplace_back(*r.position, now);
        }
        std::erase_if(t.trail, [&](const auto& p) { return now - p.second > std::chrono::minutes(10); });
        d.position = r.position;
        d.cog_deg = r.cog_deg;
        d.sog_mps = r.sog_mps;
        d.heading_deg = r.heading_deg;
        t.last_position = now;
    }
    if (r.name) d.name = r.name;
    if (r.callsign) d.callsign = r.callsign;
    if (r.length_m) d.length_m = r.length_m;
    if (r.beam_m) d.beam_m = r.beam_m;
    if (r.ship_type != 0) d.ship_type = r.ship_type;
}

void AisTable::message(const core::AisSafetyMessage& m, Clock::time_point now) {
    // The same text is repeated by the sender: keep one entry, refreshed
    std::erase_if(messages_, [&](const auto& e) { return e.first.mmsi == m.mmsi && e.first.text == m.text; });
    messages_.emplace_back(m, now);
    if (messages_.size() > 50) messages_.erase(messages_.begin());
}

AisTargetList AisTable::evaluate(const std::optional<core::Position>& own,
                                 const std::optional<core::CourseOverGround>& cog, Clock::time_point now) {
    AisTargetList list;
    std::erase_if(messages_, [&](const auto& e) { return seconds(now - e.second) > 3600.0; });
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        AisSafetyEntry e;
        e.message = it->first;
        e.kind = ais_kind(it->first.mmsi);
        e.age_s = seconds(now - it->second);
        const auto t = targets_.find(it->first.mmsi);
        e.sender = t != targets_.end() && t->second.data.name ? *t->second.data.name : std::to_string(it->first.mmsi);
        list.messages.push_back(std::move(e));
    }
    for (auto it = targets_.begin(); it != targets_.end();) {
        AisTarget& t = it->second;
        const double age = seconds(now - t.last_position);
        const bool station = t.data.station != core::AisStation::Vessel;
        if (!t.data.position || age > (station ? settings_.station_remove_after_s : settings_.remove_after_s)) {
            // Static data alone (no position yet) is kept, but not listed
            it = t.data.position ? targets_.erase(it) : std::next(it);
            continue;
        }
        t.lost = age > (station ? settings_.station_lost_after_s : settings_.lost_after_s);
        t.range_m.reset();
        t.bearing_deg.reset();
        t.cpa_m.reset();
        t.tcpa_s.reset();
        t.dangerous = false;
        if (own) {
            t.range_m = core::distance_m(own->point, *t.data.position);
            t.bearing_deg = core::initial_bearing_deg(own->point, *t.data.position);
            if (!station && !t.lost && t.data.cog_deg && t.data.sog_mps) {
                const double own_cog = cog ? cog->cog_deg : 0.0;
                const double own_sog = cog ? cog->sog_mps : 0.0;
                // Dead-reckon the target to "now": its report may be minutes old
                const core::GeoPoint target_now = core::destination(*t.data.position, *t.data.cog_deg,
                                                                    *t.data.sog_mps * age);
                const auto cpa = core::closest_point_of_approach(own->point, own_cog, own_sog, target_now,
                                                                 *t.data.cog_deg, *t.data.sog_mps);
                t.cpa_m = cpa.cpa_m;
                t.tcpa_s = cpa.tcpa_s;
                const bool moving = *t.data.sog_mps >= settings_.ignore_slower_than_mps ||
                                    own_sog >= settings_.ignore_slower_than_mps;
                t.dangerous = moving && cpa.tcpa_s >= 0.0 && cpa.tcpa_s <= settings_.tcpa_alarm_s &&
                              cpa.cpa_m <= settings_.cpa_alarm_m;
            }
        }
        list.targets.push_back(t);
        ++it;
    }
    // Emergency beacons first, then collision danger, then by range
    const auto rank = [](const AisTarget& t) {
        if (t.kind != AisKind::Vessel && !t.beacon_test) return 0;
        if (t.dangerous) return 1;
        return t.data.station == core::AisStation::Vessel ? 2 : 3;  // fixed stations last
    };
    std::sort(list.targets.begin(), list.targets.end(), [&](const AisTarget& a, const AisTarget& b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a.range_m.value_or(1e12) < b.range_m.value_or(1e12);
    });
    return list;
}

// ---- AlarmEvaluator ---------------------------------------------------------

void AlarmEvaluator::command(const AnchorCommand& cmd, const std::optional<core::Sample<core::Position>>& own) {
    if (cmd.action == AnchorCommand::Action::Raise) {
        anchor_ = {};
        return;
    }
    if (!own) return;  // cannot drop without a position
    anchor_.active = true;
    anchor_.anchor = own->value.point;
    anchor_.radius_m = std::max(5.0, cmd.radius_m);
    anchor_.distance_m = 0.0;
}

bool buzzer_on(std::optional<AlarmLevel> level, double t_s) {
    if (!level) return false;
    if (*level == AlarmLevel::Alarm) return std::fmod(t_s, 0.5) < 0.25;  // 2 Hz
    const double p = std::fmod(t_s, 4.0);  // double beep every 4 s
    return p < 0.15 || (p >= 0.3 && p < 0.45);
}

void AlarmEvaluator::acknowledge() { acknowledged_.insert(current_.begin(), current_.end()); }

AlarmList AlarmEvaluator::evaluate(const std::optional<core::Sample<core::Position>>& own,
                                   const std::optional<core::Sample<core::Depth>>& depth, const AisTargetList& ais,
                                   Clock::time_point now, const std::vector<Alarm>& extra) {
    AlarmList out;
    const bool gnss_ok = own && seconds(now - own->timestamp) <= settings_.gnss_timeout_s &&
                         own->value.quality != core::FixQuality::None;
    if (!gnss_ok) {
        // Without position the anchor watch is blind: that is an alarm, not a warning
        out.active.push_back({AlarmId::GnssLost, "GNSS-Position verloren", 0,
                              anchor_.active ? AlarmLevel::Alarm : AlarmLevel::Warning});
    }

    if (anchor_.active && gnss_ok) {
        anchor_.distance_m = core::distance_m(anchor_.anchor, own->value.point);
        if (anchor_.distance_m > anchor_.radius_m) {
            std::ostringstream s;
            s << "Anker slippt: " << static_cast<int>(std::lround(anchor_.distance_m)) << " m vom Ankerplatz";
            out.active.push_back({AlarmId::AnchorDrag, s.str()});
        }
    }

    if (depth) {
        if (seconds(now - depth->timestamp) > settings_.depth_timeout_s) {
            out.active.push_back({AlarmId::DepthLost, "Keine Tiefendaten", 0, AlarmLevel::Warning});
        } else if (settings_.shallow_m && depth->value.depth_m() < *settings_.shallow_m) {
            std::ostringstream s;
            s.precision(1);
            s << std::fixed << "Flachwasser: " << depth->value.depth_m() << " m";
            out.active.push_back({AlarmId::ShallowWater, s.str()});
        }
    }

    for (const auto& t : ais.targets) {
        // Emergency beacon (life raft, person in the water): always an alarm,
        // whatever the CPA - the skipper decides whether to assist
        if (t.kind != AisKind::Vessel && !t.beacon_test && !t.lost) {
            std::ostringstream s;
            s << (t.kind == AisKind::Sart ? "AIS-SART aktiv" : t.kind == AisKind::Mob ? "AIS-MOB-Sender aktiv" : "AIS-EPIRB aktiv");
            if (t.range_m && t.bearing_deg) {
                s.precision(2);
                s << std::fixed << ": " << *t.range_m / core::kMetresPerNm << " sm, "
                  << static_cast<int>(std::lround(*t.bearing_deg)) << "°";
            }
            out.active.push_back({AlarmId::AisBeacon, s.str(), t.data.mmsi});
            continue;
        }
        // A drifted buoy nearby: its charted position is wrong
        if (t.data.station == core::AisStation::AtoN && t.data.off_position && !t.lost && t.range_m &&
            *t.range_m <= settings_.off_position_warning_m) {
            out.active.push_back({AlarmId::AtonOffPosition,
                                  "Seezeichen nicht auf Position: " + (t.data.name ? *t.data.name : std::to_string(t.data.mmsi)),
                                  t.data.mmsi, AlarmLevel::Warning});
        }
        if (!t.dangerous) continue;
        std::ostringstream s;
        s << "AIS-Kollisionsgefahr: " << (t.data.name ? *t.data.name : std::to_string(t.data.mmsi));
        if (t.tcpa_s) s << " in " << static_cast<int>(*t.tcpa_s / 60.0) << " min";
        out.active.push_back({AlarmId::AisCollision, s.str(), t.data.mmsi});
    }
    // Safety messages: from an emergency beacon an alarm, otherwise a warning;
    // test transmissions ("SART TEST", "MOB TEST") are only listed
    for (const auto& m : ais.messages) {
        if (m.age_s > settings_.message_alarm_s || m.message.text.find("TEST") != std::string::npos) continue;
        const bool beacon = m.kind != AisKind::Vessel;
        out.active.push_back({AlarmId::AisMessage, "AIS-Meldung von " + m.sender + ": " + m.message.text, m.message.mmsi,
                              beacon ? AlarmLevel::Alarm : AlarmLevel::Warning});
    }
    out.active.insert(out.active.end(), extra.begin(), extra.end());
    return finish(std::move(out));
}

AlarmList AlarmEvaluator::finish(AlarmList list) {
    current_.clear();
    for (auto& a : list.active) {
        const Key key{a.id, a.subject};
        current_.insert(key);
        a.acknowledged = acknowledged_.count(key) > 0;
        if (!a.acknowledged && (!list.sound || a.level > *list.sound)) list.sound = a.level;
    }
    // Forget acknowledgements of alarms that cleared, so they sound again next time
    for (auto it = acknowledged_.begin(); it != acknowledged_.end();) {
        it = current_.count(*it) > 0 ? std::next(it) : acknowledged_.erase(it);
    }
    return list;
}

std::vector<Alarm> guidance_alarms(const Guidance& g) {
    std::vector<Alarm> out;
    if (g.mode == NavMode::Mob) {
        std::ostringstream s;
        s << "MANN ÜBER BORD";
        if (g.dtw_m && g.btw_deg) {
            s << ": " << static_cast<int>(std::lround(*g.dtw_m)) << " m, " << static_cast<int>(std::lround(*g.btw_deg))
              << "°";
        }
        out.push_back({AlarmId::Mob, s.str(), 0, AlarmLevel::Alarm});
    } else if (g.arrived) {
        out.push_back({AlarmId::Arrival, "Ziel erreicht: " + g.to.name, 0, AlarmLevel::Warning});
    }
    return out;
}

// ---- Magnetic variation -------------------------------------------------------

std::optional<core::MagneticVariation> variation_at(const MagneticModel& model, const core::GeoPoint& p,
                                                    std::int64_t unix_ms) {
    using namespace std::chrono;
    const sys_days day = floor<days>(sys_time<milliseconds>(milliseconds(unix_ms)));
    const year_month_day ymd{day};
    const double year = decimal_year(static_cast<int>(ymd.year()), static_cast<int>(static_cast<unsigned>(ymd.month())),
                                     static_cast<int>(static_cast<unsigned>(ymd.day())));
    if (!model.valid_for(year)) return std::nullopt;
    return core::MagneticVariation{model.declination_deg(p, 0.0, year), model.name()};
}

// ---- NavModule --------------------------------------------------------------

NavModule::~NavModule() { stop(); }

void NavModule::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    bus_ = &bus;

    // True wind immediately on every apparent wind sample (no extra latency)
    wind_subscription_ = bus.topic<core::ApparentWind>().subscribe([&bus](const auto& s) {
        double heading = 0.0;
        if (const auto h = bus.latest<core::Heading>(); h && core::is_fresh(*h, std::chrono::seconds(3))) {
            heading = h->value.heading_deg;
        } else if (const auto c = bus.latest<core::CourseOverGround>()) {
            heading = c->value.cog_deg;  // no compass: COG is the best estimate
        }
        double speed = 0.0;
        if (const auto w = bus.latest<core::SpeedThroughWater>(); w && core::is_fresh(*w, std::chrono::seconds(3))) {
            speed = w->value.stw_mps;
        } else if (const auto c = bus.latest<core::CourseOverGround>()) {
            speed = c->value.sog_mps;
        }
        bus.publish(core::true_wind(s.value, heading, speed));
    });

    worker_ = std::thread([this, &bus] {
        std::mutex mutex;
        AisTable ais(settings_.ais);
        AlarmEvaluator alarms(settings_.alarms);
        std::vector<core::AisReport> reports;
        std::vector<AnchorCommand> commands;
        std::vector<NavCommand> nav_commands;
        Navigator navigator(settings_.navigator);
        bool acknowledge = false;
        std::optional<NavSettings> new_settings;
        std::optional<core::GeoPoint> variation_point;
        Clock::time_point variation_time{};
        std::vector<core::AisSafetyMessage> safety;
        const auto ais_sub = bus.topic<core::AisReport>().subscribe([&](const auto& s) {
            std::scoped_lock lock(mutex);
            reports.push_back(s.value);
        });
        const auto safety_sub = bus.topic<core::AisSafetyMessage>().subscribe([&](const auto& s) {
            std::scoped_lock lock(mutex);
            safety.push_back(s.value);
        });
        const auto anchor_sub = bus.topic<AnchorCommand>().subscribe([&](const auto& s) {
            std::scoped_lock lock(mutex);
            commands.push_back(s.value);
        });
        const auto nav_sub = bus.topic<NavCommand>().subscribe([&](const auto& s) {
            NavCommand c = s.value;
            // MOB: take the position at the moment of the key press, not at the next 1 Hz tick
            if (c.action == NavCommand::Action::Mob) {
                if (const auto p = bus.latest<core::Position>(); p && core::is_fresh(*p, std::chrono::seconds(5))) {
                    c.waypoint = Waypoint{"MOB", p->value.point};
                }
            }
            std::scoped_lock lock(mutex);
            nav_commands.push_back(std::move(c));
        });
        const auto settings_sub = bus.topic<NavSettings>().subscribe([&](const auto& s) {
            std::scoped_lock lock(mutex);
            new_settings = s.value;
        });
        const auto ack_sub = bus.topic<AlarmAcknowledge>().subscribe([&](const auto&) {
            std::scoped_lock lock(mutex);
            acknowledge = true;
        });
        while (running_) {
            const auto now = Clock::now();
            const auto own = bus.latest<core::Position>();
            {
                std::scoped_lock lock(mutex);
                for (const auto& r : reports) ais.update(r, now);
                for (const auto& m : safety) ais.message(m, now);
                safety.clear();
                for (const auto& c : commands) alarms.command(c, own);
                if (new_settings) {
                    ais.set_settings(new_settings->ais);
                    alarms.set_settings(new_settings->alarms);
                    navigator.set_settings(new_settings->navigator);
                    new_settings.reset();
                }
                for (const auto& c : nav_commands) {
                    navigator.command(c, own ? std::optional(own->value.point) : std::nullopt);
                }
                if (acknowledge) alarms.acknowledge();
                reports.clear();
                commands.clear();
                nav_commands.clear();
                acknowledge = false;
            }
            const auto cog = bus.latest<core::CourseOverGround>();
            const auto list = ais.evaluate(own ? std::optional(own->value) : std::nullopt,
                                           cog ? std::optional(cog->value) : std::nullopt, now);
            bus.publish(list);
            const bool own_fresh = own && core::is_fresh(*own, std::chrono::seconds(5), now);
            const Guidance guidance = navigator.update(own_fresh ? std::optional(own->value.point) : std::nullopt,
                                                       cog ? std::optional(cog->value) : std::nullopt);
            bus.publish(guidance);
            bus.publish(alarms.evaluate(own, bus.latest<core::Depth>(), list, now, guidance_alarms(guidance)));
            bus.publish(alarms.anchor());
            // Variation changes slowly: once a minute or after 10 km is plenty
            if (magnetic_model_ && own_fresh &&
                (!variation_point || now - variation_time > std::chrono::minutes(1) ||
                 core::distance_m(*variation_point, own->value.point) > 10000.0)) {
                // GNSS time (the Pi has no clock); system time only as a fallback
                std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch()).count();
                if (const auto t = bus.latest<core::UtcTime>(); t && core::is_fresh(*t, std::chrono::seconds(10), now)) {
                    ms = t->value.unix_ms;
                }
                if (const auto v = variation_at(*magnetic_model_, own->value.point, ms)) bus.publish(*v);
                variation_point = own->value.point;
                variation_time = now;
            }
            for (int i = 0; running_ && i < 10; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bus.topic<core::AisReport>().unsubscribe(ais_sub);
        bus.topic<core::AisSafetyMessage>().unsubscribe(safety_sub);
        bus.topic<AnchorCommand>().unsubscribe(anchor_sub);
        bus.topic<AlarmAcknowledge>().unsubscribe(ack_sub);
        bus.topic<NavCommand>().unsubscribe(nav_sub);
        bus.topic<NavSettings>().unsubscribe(settings_sub);
    });
}

void NavModule::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    if (bus_ != nullptr && wind_subscription_) {
        bus_->topic<core::ApparentWind>().unsubscribe(*wind_subscription_);
        wind_subscription_.reset();
    }
}

}  // namespace boat::nav
