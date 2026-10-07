#include "settings.hpp"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <utility>

#include "boat/track/track.hpp"

namespace {

double clamp(double v, double lo, double hi) { return std::clamp(v, lo, hi); }

}  // namespace

Settings::Settings(boat::core::DataBus& bus, QString path, const QJsonObject& config, QObject* parent)
    : QObject(parent), bus_(bus), path_(std::move(path)) {
    // Defaults from boat.json ("alarms" section), then the user's saved settings on top
    const QJsonObject alarms = config.value("alarms").toObject();
    shallow_ = alarms.value("shallow_m").toDouble(shallow_);
    cpa_nm_ = alarms.value("cpa_nm").toDouble(cpa_nm_);
    tcpa_min_ = alarms.value("tcpa_min").toDouble(tcpa_min_);
    depth_offset_ = config.value("depth_offset_m").toDouble(depth_offset_);
    QFile file(path_);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError e{};
        const auto doc = QJsonDocument::fromJson(file.readAll(), &e);
        if (doc.isObject()) load(doc.object());
        else qWarning() << "OpenBoat: ignoring broken" << path_ << ":" << e.errorString();
    }
}

void Settings::load(const QJsonObject& o) {
    const auto unit = [&](const char* key, const QString& current, std::initializer_list<const char*> allowed) {
        const QString v = o.value(key).toString(current);
        return std::any_of(allowed.begin(), allowed.end(), [&](const char* a) { return v == QLatin1String(a); }) ? v : current;
    };
    speed_unit_ = unit("speed_unit", speed_unit_, {"kn", "kmh"});
    depth_unit_ = unit("depth_unit", depth_unit_, {"m", "ft"});
    distance_unit_ = unit("distance_unit", distance_unit_, {"nm", "km"});
    // Range checks: a hand-edited file must not switch alarms off by accident
    shallow_ = clamp(o.value("shallow_m").toDouble(shallow_), 0.0, 50.0);
    cpa_nm_ = clamp(o.value("cpa_nm").toDouble(cpa_nm_), 0.05, 5.0);
    tcpa_min_ = clamp(o.value("tcpa_min").toDouble(tcpa_min_), 1.0, 60.0);
    anchor_radius_ = clamp(o.value("anchor_radius_m").toDouble(anchor_radius_), 5.0, 500.0);
    arrival_radius_ = clamp(o.value("arrival_radius_m").toDouble(arrival_radius_), 10.0, 1000.0);
    vector_minutes_ = clamp(o.value("vector_minutes").toDouble(vector_minutes_), 0.0, 60.0);
    track_recording_ = o.value("track_recording").toBool(track_recording_);
    track_spacing_ = clamp(o.value("track_spacing_m").toDouble(track_spacing_), 2.0, 500.0);
    show_track_ = o.value("show_track").toBool(show_track_);
    overzoom_ = o.value("overzoom").toBool(overzoom_);
    orientation_ = unit("orientation", orientation_, {"north", "course"});
    depth_offset_mode_ = unit("depth_offset_mode", depth_offset_mode_, {"transducer", "manual"});
    depth_offset_ = clamp(o.value("depth_offset_m").toDouble(depth_offset_), -10.0, 10.0);
}

boat::nav::NavSettings Settings::navSettings() const {
    boat::nav::NavSettings s;
    s.alarms.shallow_m = shallow_ > 0.0 ? std::optional(shallow_) : std::nullopt;
    s.ais.cpa_alarm_m = cpa_nm_ * 1852.0;
    s.ais.tcpa_alarm_s = tcpa_min_ * 60.0;
    s.navigator.arrival_radius_m = arrival_radius_;
    return s;
}

void Settings::commit(bool nav, bool track) {
    const QJsonObject o{{"speed_unit", speed_unit_},       {"depth_unit", depth_unit_},
                        {"distance_unit", distance_unit_}, {"shallow_m", shallow_},
                        {"cpa_nm", cpa_nm_},               {"tcpa_min", tcpa_min_},
                        {"anchor_radius_m", anchor_radius_}, {"arrival_radius_m", arrival_radius_},
                        {"vector_minutes", vector_minutes_}, {"track_recording", track_recording_},
                        {"track_spacing_m", track_spacing_}, {"show_track", show_track_},
                        {"overzoom", overzoom_},           {"orientation", orientation_},           {"depth_offset_mode", depth_offset_mode_},
                        {"depth_offset_m", depth_offset_}};
    QSaveFile file(path_);
    if (file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(o).toJson()) >= 0 && file.commit()) {
        error_.clear();
    } else {
        error_ = QStringLiteral("Einstellungen nicht gespeichert: %1").arg(file.errorString());
        qWarning() << "OpenBoat:" << error_;
    }
    if (nav) bus_.publish(navSettings());
    if (track) {
        boat::track::TrackCommand c;
        c.action = boat::track::TrackCommand::Action::SetRecording;
        c.recording = track_recording_;
        c.filter.min_distance_m = track_spacing_;
        bus_.publish(c);
    }
    emit changed();
}

void Settings::setSpeedUnit(const QString& v) {
    if (v == speed_unit_ || (v != QLatin1String("kn") && v != QLatin1String("kmh"))) return;
    speed_unit_ = v;
    commit(false, false);
}
void Settings::setDepthUnit(const QString& v) {
    if (v == depth_unit_ || (v != QLatin1String("m") && v != QLatin1String("ft"))) return;
    depth_unit_ = v;
    commit(false, false);
}
void Settings::setDistanceUnit(const QString& v) {
    if (v == distance_unit_ || (v != QLatin1String("nm") && v != QLatin1String("km"))) return;
    distance_unit_ = v;
    commit(false, false);
}
void Settings::setShallowAlarm(double v) {
    shallow_ = clamp(v, 0.0, 50.0);
    commit(true, false);
}
void Settings::setCpaNm(double v) {
    cpa_nm_ = clamp(v, 0.05, 5.0);
    commit(true, false);
}
void Settings::setTcpaMin(double v) {
    tcpa_min_ = clamp(v, 1.0, 60.0);
    commit(true, false);
}
void Settings::setAnchorRadius(double v) {
    anchor_radius_ = clamp(v, 5.0, 500.0);
    commit(false, false);
}
void Settings::setArrivalRadius(double v) {
    arrival_radius_ = clamp(v, 10.0, 1000.0);
    commit(true, false);
}
void Settings::setVectorMinutes(double v) {
    vector_minutes_ = clamp(v, 0.0, 60.0);
    commit(false, false);
}
void Settings::setTrackRecording(bool v) {
    track_recording_ = v;
    commit(false, true);
}
void Settings::setTrackSpacing(double v) {
    track_spacing_ = clamp(v, 2.0, 500.0);
    commit(false, true);
}
void Settings::setShowTrack(bool v) {
    show_track_ = v;
    commit(false, false);
}

void Settings::setOverzoom(bool v) {
    overzoom_ = v;
    commit(false, false);
}
void Settings::setOrientation(const QString& v) {
    if (v == orientation_ || (v != QLatin1String("north") && v != QLatin1String("course"))) return;
    orientation_ = v;
    commit(false, false);
}
void Settings::setDepthOffsetMode(const QString& v) {
    if (v != QLatin1String("transducer") && v != QLatin1String("manual")) return;
    depth_offset_mode_ = v;
    commit(false, false);
    publishDepthOffset();
}
void Settings::setDepthOffset(double v) {
    depth_offset_ = clamp(v, -10.0, 10.0);
    commit(false, false);
    publishDepthOffset();
}

void Settings::publishDepthOffset() {
    bus_.publish(boat::core::DepthOffset{depth_offset_mode_ == QLatin1String("manual") ? std::optional(depth_offset_)
                                                                                         : std::nullopt});
}

void Settings::exportTrack() {
    bus_.publish(boat::track::TrackCommand{boat::track::TrackCommand::Action::ExportGpx});
}

void Settings::clearTrackDisplay() {
    bus_.publish(boat::track::TrackCommand{boat::track::TrackCommand::Action::ClearDisplay});
}
