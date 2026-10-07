#pragma once

// Exposes the DataBus to QML. Polls the latest samples at 5 Hz (the bus
// is written from worker threads; QML must only be touched from the GUI
// thread) and marks values invalid when their source goes stale.

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "boat/core/data_bus.hpp"

class BoatModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool positionValid READ positionValid NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(bool cogValid READ cogValid NOTIFY changed)
    Q_PROPERTY(double cog READ cog NOTIFY changed)
    Q_PROPERTY(double sogKn READ sogKn NOTIFY changed)
    Q_PROPERTY(bool headingValid READ headingValid NOTIFY changed)
    Q_PROPERTY(bool headingTrue READ headingTrue NOTIFY changed)
    Q_PROPERTY(double heading READ heading NOTIFY changed)
    Q_PROPERTY(bool stwValid READ stwValid NOTIFY changed)
    Q_PROPERTY(double stwKn READ stwKn NOTIFY changed)
    Q_PROPERTY(bool depthValid READ depthValid NOTIFY changed)
    Q_PROPERTY(double depth READ depth NOTIFY changed)
    Q_PROPERTY(bool windValid READ windValid NOTIFY changed)
    Q_PROPERTY(double awa READ awa NOTIFY changed)
    Q_PROPERTY(double awsKn READ awsKn NOTIFY changed)
    Q_PROPERTY(double twd READ twd NOTIFY changed)
    Q_PROPERTY(double twsKn READ twsKn NOTIFY changed)
    Q_PROPERTY(bool waterTempValid READ waterTempValid NOTIFY changed)
    Q_PROPERTY(double waterTemp READ waterTemp NOTIFY changed)
    // Each alarm: { id: int (AlarmId; 99 = alarm monitoring itself failed), text, acknowledged }.
    // Acknowledgement is kept by the nav module (core), so loudspeaker and GPIO
    // buzzer fall silent together.
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY changed)
    Q_PROPERTY(bool anchorActive READ anchorActive NOTIFY changed)
    Q_PROPERTY(double anchorLat READ anchorLat NOTIFY changed)
    Q_PROPERTY(double anchorLon READ anchorLon NOTIFY changed)
    Q_PROPERTY(double anchorRadius READ anchorRadius NOTIFY changed)
    Q_PROPERTY(double anchorDistance READ anchorDistance NOTIFY changed)
    Q_PROPERTY(QVariantList aisTargets READ aisTargets NOTIFY changed)

public:
    explicit BoatModel(boat::core::DataBus& bus, QObject* parent = nullptr);

    bool positionValid() const { return position_valid_; }
    double latitude() const { return lat_; }
    double longitude() const { return lon_; }
    bool cogValid() const { return cog_valid_; }
    double cog() const { return cog_; }
    double sogKn() const { return sog_kn_; }
    bool headingValid() const { return heading_valid_; }
    bool headingTrue() const { return heading_true_; }
    double heading() const { return heading_; }
    bool stwValid() const { return stw_valid_; }
    double stwKn() const { return stw_kn_; }
    bool depthValid() const { return depth_valid_; }
    double depth() const { return depth_; }
    bool windValid() const { return wind_valid_; }
    double awa() const { return awa_; }
    double awsKn() const { return aws_kn_; }
    double twd() const { return twd_; }
    double twsKn() const { return tws_kn_; }
    bool waterTempValid() const { return water_valid_; }
    double waterTemp() const { return water_; }
    QVariantList alarms() const { return alarms_; }
    bool anchorActive() const { return anchor_active_; }
    double anchorLat() const { return anchor_lat_; }
    double anchorLon() const { return anchor_lon_; }
    double anchorRadius() const { return anchor_radius_; }
    double anchorDistance() const { return anchor_distance_; }
    QVariantList aisTargets() const { return ais_; }

    Q_INVOKABLE void dropAnchor(double radius_m);
    Q_INVOKABLE void raiseAnchor();
    Q_INVOKABLE void acknowledgeAlarms();

signals:
    void changed();

private:
    void poll();

    boat::core::DataBus& bus_;
    QTimer timer_;
    bool position_valid_ = false;
    double lat_ = 0.0, lon_ = 0.0;
    bool cog_valid_ = false;
    double cog_ = 0.0, sog_kn_ = 0.0;
    bool heading_valid_ = false, heading_true_ = false;
    double heading_ = 0.0;
    bool stw_valid_ = false;
    double stw_kn_ = 0.0;
    bool depth_valid_ = false;
    double depth_ = 0.0;
    bool wind_valid_ = false;
    double awa_ = 0.0, aws_kn_ = 0.0, twd_ = 0.0, tws_kn_ = 0.0;
    bool water_valid_ = false;
    double water_ = 0.0;
    QVariantList alarms_;
    bool anchor_active_ = false;
    double anchor_lat_ = 0.0, anchor_lon_ = 0.0, anchor_radius_ = 0.0, anchor_distance_ = 0.0;
    QVariantList ais_;
};
