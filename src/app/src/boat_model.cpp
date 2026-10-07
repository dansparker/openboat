#include "boat_model.hpp"

#include <QVariantMap>

#include <chrono>

#include "boat/core/marine_data.hpp"
#include "boat/core/nav_math.hpp"
#include "boat/nav/nav.hpp"

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
    const auto stw = fresh<core::SpeedThroughWater>(bus_, 3s);
    stw_valid_ = stw.has_value();
    if (stw) stw_kn_ = stw->stw_mps * kKnPerMps;
    const auto depth = fresh<core::Depth>(bus_, 5s);
    depth_valid_ = depth.has_value();
    if (depth) depth_ = depth->depth_m();
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
                                       {QStringLiteral("text"), QString::fromStdString(a.text)}});
        }
    } else {
        alarms_.append(QVariantMap{{QStringLiteral("id"), 99},
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
            m[QStringLiteral("lost")] = t.lost;
            m[QStringLiteral("classB")] = t.data.class_b;
            ais_.append(m);
        }
    }
    emit changed();
}
