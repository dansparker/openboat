#include "boat_model.hpp"

#include <QVariantMap>

#include <chrono>
#include <cmath>

#include "boat/core/marine_data.hpp"
#include "boat/core/nav_math.hpp"
#include "boat/nav/nav.hpp"
#include "boat/track/track.hpp"

using namespace std::chrono_literals;
namespace core = boat::core;

namespace {

constexpr double kKnPerMps = 1.0 / core::kMpsPerKnot;

template <typename T>
std::optional<T> fresh(core::DataBus& bus, std::chrono::milliseconds max_age) {
    const auto s = bus.latest<T>();
    if (!s || !core::is_fresh(*s, max_age)) return std::nullopt;
    return s->value;
}

}  // namespace

BoatModel::BoatModel(core::DataBus& bus, QObject* parent) : QObject(parent), bus_(bus) {
    connect(&timer_, &QTimer::timeout, this, &BoatModel::poll);
    timer_.start(200);
}

void BoatModel::dropAnchor(double radius_m) {
    bus_.publish(boat::nav::AnchorCommand{boat::nav::AnchorCommand::Action::Drop, radius_m});
}

void BoatModel::raiseAnchor() { bus_.publish(boat::nav::AnchorCommand{boat::nav::AnchorCommand::Action::Raise, 0.0}); }

void BoatModel::setTrackDayShown(const QString& date, bool shown) {
    shown_days_.removeAll(date);
    if (shown) shown_days_.append(date);
    boat::track::TrackCommand c;
    c.action = boat::track::TrackCommand::Action::ShowDays;
    for (const auto& d : shown_days_) c.days.push_back(d.toStdString());
    bus_.publish(c);
}

void BoatModel::exportTrackDay(const QString& date) {
    boat::track::TrackCommand c;
    c.action = boat::track::TrackCommand::Action::ExportGpx;
    c.day = date.toStdString();
    bus_.publish(c);
}

void BoatModel::acknowledgeAlarms() { bus_.publish(boat::nav::AlarmAcknowledge{}); }

void BoatModel::poll() {
    const auto pos = fresh<core::Position>(bus_, 3s);
    position_valid_ = pos && pos->quality != core::FixQuality::None;
    if (position_valid_) {
        lat_ = pos->point.lat_deg;
        lon_ = pos->point.lon_deg;
    }
    const auto cog = fresh<core::CourseOverGround>(bus_, 3s);
    cog_valid_ = cog.has_value();
    if (cog) {
        cog_ = cog->cog_deg;
        sog_kn_ = cog->sog_mps * kKnPerMps;
    }
    const auto hdg = fresh<core::Heading>(bus_, 2s);
    heading_valid_ = hdg.has_value();
    if (hdg) {
        heading_ = hdg->heading_deg;
        heading_true_ = hdg->is_true;
    }
    {
        const auto model = fresh<core::MagneticVariation>(bus_, 5min);
        std::optional<double> var;
        QString source;
        if (hdg && hdg->variation_deg && !hdg->variation_from_model) {
            var = hdg->variation_deg;
            source = QStringLiteral("Gerät");
        } else if (model) {
            var = model->variation_deg;
            source = QString::fromStdString(model->model);
        }
        variation_text_ = var ? QStringLiteral("%1° %2 (%3)")
                                    .arg(std::abs(*var), 0, 'f', 1)
                                    .arg(*var >= 0 ? QStringLiteral("O") : QStringLiteral("W"), source)
                              : QString();
    }
    const auto stw = fresh<core::SpeedThroughWater>(bus_, 3s);
    stw_valid_ = stw.has_value();
    if (stw) stw_kn_ = stw->stw_mps * kKnPerMps;
    const auto depth = fresh<core::Depth>(bus_, 5s);
    depth_valid_ = depth.has_value();
    if (depth) {
        depth_ = depth->depth_m();
        depth_raw_ = depth->below_transducer_m;
        depth_offset_ = depth->offset_m;
    }
    const auto aw = fresh<core::ApparentWind>(bus_, 3s);
    const auto tw = fresh<core::TrueWind>(bus_, 3s);
    wind_valid_ = aw && tw;
    if (wind_valid_) {
        awa_ = aw->angle_deg;
        aws_kn_ = aw->speed_mps * kKnPerMps;
        twd_ = tw->direction_deg;
        tws_kn_ = tw->speed_mps * kKnPerMps;
    }
    const auto water = fresh<core::WaterTemperature>(bus_, 30s);
    water_valid_ = water.has_value();
    if (water) water_ = water->celsius;

    // Alarms and AIS come from the nav module; if it stopped, say so loudly
    alarms_.clear();
    if (const auto list = fresh<boat::nav::AlarmList>(bus_, 5s)) {
        for (const auto& a : list->active) {
            alarms_.append(QVariantMap{{QStringLiteral("id"), static_cast<int>(a.id)},
                                       {QStringLiteral("text"), QString::fromStdString(a.text)},
                                       {QStringLiteral("acknowledged"), a.acknowledged}});
        }
    } else {
        alarms_.append(QVariantMap{{QStringLiteral("id"), 99},
                                   {QStringLiteral("acknowledged"), false},
                                   {QStringLiteral("text"), QStringLiteral("Alarmüberwachung ausgefallen")}});
    }
    if (const auto anchor = bus_.latest<boat::nav::AnchorState>()) {
        anchor_active_ = anchor->value.active;
        anchor_lat_ = anchor->value.anchor.lat_deg;
        anchor_lon_ = anchor->value.anchor.lon_deg;
        anchor_radius_ = anchor->value.radius_m;
        anchor_distance_ = anchor->value.distance_m;
    }
    ais_.clear();
    ais_messages_.clear();
    const auto kind_name = [](boat::nav::AisKind k) {
        return k == boat::nav::AisKind::Sart  ? QStringLiteral("sart")
               : k == boat::nav::AisKind::Mob  ? QStringLiteral("mob")
               : k == boat::nav::AisKind::Epirb ? QStringLiteral("epirb")
                                                 : QStringLiteral("vessel");
    };
    if (const auto list = fresh<boat::nav::AisTargetList>(bus_, 5s)) {
        for (const auto& t : list->targets) {
            QVariantMap m;
            m[QStringLiteral("mmsi")] = static_cast<qulonglong>(t.data.mmsi);
            m[QStringLiteral("name")] = t.data.name ? QString::fromStdString(*t.data.name) : QString();
            m[QStringLiteral("lat")] = t.data.position->lat_deg;
            m[QStringLiteral("lon")] = t.data.position->lon_deg;
            m[QStringLiteral("cog")] = t.data.cog_deg.value_or(t.data.heading_deg.value_or(0.0));
            m[QStringLiteral("sogKn")] = t.data.sog_mps.value_or(0.0) * kKnPerMps;
            m[QStringLiteral("rangeNm")] = t.range_m ? *t.range_m / core::kMetresPerNm : -1.0;
            m[QStringLiteral("cpaNm")] = t.cpa_m ? *t.cpa_m / core::kMetresPerNm : -1.0;
            m[QStringLiteral("tcpaMin")] = t.tcpa_s ? *t.tcpa_s / 60.0 : -1.0;
            m[QStringLiteral("dangerous")] = t.dangerous;
            m[QStringLiteral("bearing")] = t.bearing_deg.value_or(-1.0);
            m[QStringLiteral("heading")] = t.data.heading_deg.value_or(-1.0);
            m[QStringLiteral("hasCog")] = t.data.cog_deg.has_value();
            m[QStringLiteral("callsign")] = t.data.callsign ? QString::fromStdString(*t.data.callsign) : QString();
            m[QStringLiteral("shipType")] = static_cast<int>(t.data.ship_type);
            m[QStringLiteral("lengthM")] = t.data.length_m.value_or(0.0);
            m[QStringLiteral("beamM")] = t.data.beam_m.value_or(0.0);
            m[QStringLiteral("navStatus")] = t.data.nav_status ? static_cast<int>(*t.data.nav_status) : -1;
            m[QStringLiteral("ageS")] = std::chrono::duration<double>(core::Clock::now() - t.last_position).count();
            m[QStringLiteral("kind")] = kind_name(t.kind);
            m[QStringLiteral("station")] = t.data.station == core::AisStation::BaseStation ? QStringLiteral("base")
                                           : t.data.station == core::AisStation::AtoN     ? QStringLiteral("aton")
                                                                                         : QStringLiteral("vessel");
            m[QStringLiteral("atonType")] = static_cast<int>(t.data.aton_type);
            m[QStringLiteral("virtualAton")] = t.data.virtual_aton;
            m[QStringLiteral("offPosition")] = t.data.off_position;
            m[QStringLiteral("beaconTest")] = t.beacon_test;
            QVariantList trail;
            for (const auto& [p, when] : t.trail) trail.append(QVariantMap{{QStringLiteral("lat"), p.lat_deg}, {QStringLiteral("lon"), p.lon_deg}});
            m[QStringLiteral("trail")] = trail;
            m[QStringLiteral("lost")] = t.lost;
            m[QStringLiteral("classB")] = t.data.class_b;
            ais_.append(m);
        }
        for (const auto& e : list->messages) {
            QVariantMap m;
            m[QStringLiteral("mmsi")] = static_cast<qulonglong>(e.message.mmsi);
            m[QStringLiteral("sender")] = QString::fromStdString(e.sender);
            m[QStringLiteral("text")] = QString::fromStdString(e.message.text);
            m[QStringLiteral("kind")] = kind_name(e.kind);
            m[QStringLiteral("ageMin")] = e.age_s / 60.0;
            m[QStringLiteral("addressed")] = e.message.addressed;
            ais_messages_.append(m);
        }
    }
    guidance_.clear();
    const auto g = fresh<boat::nav::Guidance>(bus_, 5s);
    guidance_[QStringLiteral("active")] = g && g->mode != boat::nav::NavMode::None;
    if (g && g->mode != boat::nav::NavMode::None) {
        const auto nm = [](const std::optional<double>& m) { return m ? *m / core::kMetresPerNm : -1.0; };
        guidance_[QStringLiteral("mode")] = g->mode == boat::nav::NavMode::Mob     ? QStringLiteral("mob")
                                            : g->mode == boat::nav::NavMode::Route ? QStringLiteral("route")
                                                                                   : QStringLiteral("goto");
        guidance_[QStringLiteral("target")] = QString::fromStdString(g->to.name);
        guidance_[QStringLiteral("routeName")] = QString::fromStdString(g->route_name);
        guidance_[QStringLiteral("leg")] = static_cast<int>(g->leg);
        guidance_[QStringLiteral("legs")] = static_cast<int>(g->legs);
        guidance_[QStringLiteral("dtwNm")] = nm(g->dtw_m);
        guidance_[QStringLiteral("btw")] = g->btw_deg.value_or(-1.0);
        guidance_[QStringLiteral("xteNm")] = g->xte_m ? *g->xte_m / core::kMetresPerNm : 0.0;
        guidance_[QStringLiteral("hasXte")] = g->xte_m.has_value();
        guidance_[QStringLiteral("ttgMin")] = g->ttg_s ? *g->ttg_s / 60.0 : -1.0;
        guidance_[QStringLiteral("remainingNm")] = nm(g->route_remaining_m);
        guidance_[QStringLiteral("arrived")] = g->arrived;
        guidance_[QStringLiteral("from")] =
            g->from ? QVariant(QVariantMap{{"lat", g->from->point.lat_deg}, {"lon", g->from->point.lon_deg}}) : QVariant();
        QVariantList points;
        for (const auto& p : g->remaining) {
            points.append(QVariantMap{{"name", QString::fromStdString(p.name)},
                                      {"lat", p.point.lat_deg},
                                      {"lon", p.point.lon_deg}});
        }
        guidance_[QStringLiteral("points")] = points;
    }
    if (const auto t = bus_.latest<boat::track::TrackState>()) {
        const auto& s = t->value;
        track_recording_ = s.recording;
        track_today_nm_ = s.today_m / core::kMetresPerNm;
        time_from_gnss_ = s.time_from_gnss;
        track_export_ = QString::fromStdString(s.last_export);
        if (s.version != track_version_) {
            track_version_ = s.version;
            track_days_.clear();
            for (const auto& d : s.days) {
                const QString date = QString::fromStdString(d.date);
                track_days_.append(QVariantMap{{QStringLiteral("date"), date},
                                               {QStringLiteral("lengthNm"), d.length_m / core::kMetresPerNm},
                                               {QStringLiteral("shown"), shown_days_.contains(date)},
                                               {QStringLiteral("today"), d.today},
                                               {QStringLiteral("startMs"), static_cast<double>(d.start_ms)},
                                               {QStringLiteral("endMs"), static_cast<double>(d.end_ms)},
                                               {QStringLiteral("maxKn"), d.max_sog_mps * kKnPerMps},
                                               {QStringLiteral("underwayH"), d.underway_s / 3600.0}});
            }
            track_.clear();
            track_.reserve(static_cast<qsizetype>(s.points.size()));
            for (const auto& p : s.points) {
                track_.append(QVariantMap{{QStringLiteral("lat"), p.point.lat_deg}, {QStringLiteral("lon"), p.point.lon_deg}});
            }
            emit trackChanged();
        }
    }
    // Earlier days: converted only when a new history was published
    if (const auto h = bus_.latest<boat::track::TrackHistory>()) {
        const auto stamp = h->timestamp.time_since_epoch().count();
        if (stamp != history_stamp_) {
            history_stamp_ = stamp;
            track_history_.clear();
            for (const auto& day : h->value.days) {
                QVariantList pts;
                pts.reserve(static_cast<qsizetype>(day.points.size()));
                for (const auto& p : day.points) {
                    pts.append(QVariantMap{{QStringLiteral("lat"), p.point.lat_deg}, {QStringLiteral("lon"), p.point.lon_deg}});
                }
                track_history_.append(QVariantMap{{QStringLiteral("date"), QString::fromStdString(day.date)},
                                                  {QStringLiteral("points"), pts}});
            }
            emit trackHistoryChanged();
        }
    }
    emit changed();
}
