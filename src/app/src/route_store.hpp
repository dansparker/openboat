#pragma once

// Waypoints and routes for the UI, stored as GPX (exchangeable with
// OpenCPN, Garmin, Navionics, ...). Every change is saved immediately and
// atomically (QSaveFile): a power cut never leaves a half-written file.
// Navigation commands go to the nav module over the bus.

#include <QObject>
#include <QString>
#include <QVariantList>

#include "boat/core/data_bus.hpp"

class RouteStore : public QObject {
    Q_OBJECT
    // [{ name, lat, lon }]
    Q_PROPERTY(QVariantList waypoints READ waypoints NOTIFY changed)
    // [{ name, points: [{ name, lat, lon }], lengthNm }]
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    RouteStore(boat::core::DataBus& bus, QString path, QObject* parent = nullptr);

    QVariantList waypoints() const { return waypoints_; }
    QVariantList routes() const { return routes_; }
    QString lastError() const { return error_; }

    Q_INVOKABLE QString addWaypoint(double lat, double lon, const QString& name = {});
    Q_INVOKABLE void removeWaypoint(int index);
    Q_INVOKABLE void addRoute(const QVariantList& points, const QString& name = {});
    Q_INVOKABLE void removeRoute(int index);

    Q_INVOKABLE void goTo(double lat, double lon, const QString& name);
    Q_INVOKABLE void startRoute(int index, bool reverse);
    Q_INVOKABLE void nextWaypoint();
    Q_INVOKABLE void stopNavigation();
    Q_INVOKABLE void manOverboard();

    // GPX import/export (exposed for tests and later file dialogs)
    static bool readGpx(const QString& path, QVariantList& waypoints, QVariantList& routes, QString& error);
    static bool writeGpx(const QString& path, const QVariantList& waypoints, const QVariantList& routes, QString& error);

signals:
    void changed();

private:
    void save();
    QString uniqueName(const QString& prefix) const;

    boat::core::DataBus& bus_;
    QString path_;
    QVariantList waypoints_;
    QVariantList routes_;
    QString error_;
};
