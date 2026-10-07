#pragma once

// User settings (settings page). Stored in settings.json next to the
// configuration file, written atomically. Changes take effect at once:
// alarm/navigation values are published as nav::NavSettings, track values
// as a track::TrackCommand. Defaults come from the configuration file.

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "boat/core/data_bus.hpp"
#include "boat/nav/nav.hpp"

class Settings : public QObject {
    Q_OBJECT
    // Units: the UI multiplies the base value (kn, m, sm) by the factor
    Q_PROPERTY(QString speedUnit READ speedUnit WRITE setSpeedUnit NOTIFY changed)        // "kn" | "kmh"
    Q_PROPERTY(QString depthUnit READ depthUnit WRITE setDepthUnit NOTIFY changed)        // "m" | "ft"
    Q_PROPERTY(QString distanceUnit READ distanceUnit WRITE setDistanceUnit NOTIFY changed)  // "nm" | "km"
    Q_PROPERTY(double speedFactor READ speedFactor NOTIFY changed)
    Q_PROPERTY(QString speedLabel READ speedLabel NOTIFY changed)
    Q_PROPERTY(double depthFactor READ depthFactor NOTIFY changed)
    Q_PROPERTY(QString depthLabel READ depthLabel NOTIFY changed)
    Q_PROPERTY(double distanceFactor READ distanceFactor NOTIFY changed)
    Q_PROPERTY(QString distanceLabel READ distanceLabel NOTIFY changed)

    // Alarms / navigation (metres, nautical miles, minutes)
    Q_PROPERTY(double shallowAlarm READ shallowAlarm WRITE setShallowAlarm NOTIFY changed)  // 0 = off
    Q_PROPERTY(double cpaNm READ cpaNm WRITE setCpaNm NOTIFY changed)
    Q_PROPERTY(double tcpaMin READ tcpaMin WRITE setTcpaMin NOTIFY changed)
    Q_PROPERTY(double anchorRadius READ anchorRadius WRITE setAnchorRadius NOTIFY changed)
    Q_PROPERTY(double arrivalRadius READ arrivalRadius WRITE setArrivalRadius NOTIFY changed)

    // Chart / track
    Q_PROPERTY(double vectorMinutes READ vectorMinutes WRITE setVectorMinutes NOTIFY changed)
    Q_PROPERTY(bool trackRecording READ trackRecording WRITE setTrackRecording NOTIFY changed)
    Q_PROPERTY(double trackSpacing READ trackSpacing WRITE setTrackSpacing NOTIFY changed)
    Q_PROPERTY(bool showTrack READ showTrack WRITE setShowTrack NOTIFY changed)

    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    Settings(boat::core::DataBus& bus, QString path, const QJsonObject& config, QObject* parent = nullptr);

    QString speedUnit() const { return speed_unit_; }
    QString depthUnit() const { return depth_unit_; }
    QString distanceUnit() const { return distance_unit_; }
    double speedFactor() const { return speed_unit_ == QLatin1String("kmh") ? 1.852 : 1.0; }
    QString speedLabel() const { return speed_unit_ == QLatin1String("kmh") ? QStringLiteral("km/h") : QStringLiteral("kn"); }
    double depthFactor() const { return depth_unit_ == QLatin1String("ft") ? 1.0 / 0.3048 : 1.0; }
    QString depthLabel() const { return depth_unit_; }
    double distanceFactor() const { return distance_unit_ == QLatin1String("km") ? 1.852 : 1.0; }
    QString distanceLabel() const { return distance_unit_ == QLatin1String("km") ? QStringLiteral("km") : QStringLiteral("sm"); }
    double shallowAlarm() const { return shallow_; }
    double cpaNm() const { return cpa_nm_; }
    double tcpaMin() const { return tcpa_min_; }
    double anchorRadius() const { return anchor_radius_; }
    double arrivalRadius() const { return arrival_radius_; }
    double vectorMinutes() const { return vector_minutes_; }
    bool trackRecording() const { return track_recording_; }
    double trackSpacing() const { return track_spacing_; }
    bool showTrack() const { return show_track_; }
    QString lastError() const { return error_; }

    void setSpeedUnit(const QString& v);
    void setDepthUnit(const QString& v);
    void setDistanceUnit(const QString& v);
    void setShallowAlarm(double v);
    void setCpaNm(double v);
    void setTcpaMin(double v);
    void setAnchorRadius(double v);
    void setArrivalRadius(double v);
    void setVectorMinutes(double v);
    void setTrackRecording(bool v);
    void setTrackSpacing(double v);
    void setShowTrack(bool v);

    // Values for the core modules at start-up
    [[nodiscard]] boat::nav::NavSettings navSettings() const;

    Q_INVOKABLE void exportTrack();
    Q_INVOKABLE void clearTrackDisplay();

signals:
    void changed();

private:
    void load(const QJsonObject& o);
    void commit(bool nav, bool track);  // save, publish, notify

    boat::core::DataBus& bus_;
    QString path_;
    QString speed_unit_ = QStringLiteral("kn");
    QString depth_unit_ = QStringLiteral("m");
    QString distance_unit_ = QStringLiteral("nm");
    double shallow_ = 2.0;
    double cpa_nm_ = 0.5;
    double tcpa_min_ = 10.0;
    double anchor_radius_ = 40.0;
    double arrival_radius_ = 0.05 * 1852.0;
    double vector_minutes_ = 6.0;
    bool track_recording_ = true;
    double track_spacing_ = 10.0;
    bool show_track_ = true;
    QString error_;
};
