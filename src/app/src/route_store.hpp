#pragma once

// Waypoints and routes for the UI, stored as GPX (exchangeable with
// OpenCPN, Garmin, Navionics, ...). Every change is saved immediately and
// atomically (QSaveFile): a power cut never leaves a half-written file.
// Navigation commands go to the nav module over the bus.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>

class DepthChart;

#include "boat/core/data_bus.hpp"

class RouteStore : public QObject {
    Q_OBJECT
    // [{ name, lat, lon }]
    Q_PROPERTY(QVariantList waypoints READ waypoints NOTIFY changed)
    // [{ name, points: [{ name, lat, lon }], lengthNm }]
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    // USB stick: mounted drives and the GPX files on them [{ path, name, drive }]
    Q_PROPERTY(QStringList usbDrives READ usbDrives NOTIFY usbChanged)
    Q_PROPERTY(QVariantList usbFiles READ usbFiles NOTIFY usbChanged)
    Q_PROPERTY(QString usbMessage READ usbMessage NOTIFY usbChanged)
    // Last route check against the depth chart (see checkRouteDepth) plus
    // { available, name }; empty map = none shown
    Q_PROPERTY(QVariantMap routeCheck READ routeCheck NOTIFY checkChanged)

public:
    RouteStore(boat::core::DataBus& bus, QString path, QObject* parent = nullptr);
    ~RouteStore() override;

    QVariantList waypoints() const { return waypoints_; }
    QVariantList routes() const { return routes_; }
    QString lastError() const { return error_; }
    QStringList usbDrives() const { return usb_drives_; }
    QVariantList usbFiles() const { return usb_files_; }
    QString usbMessage() const { return usb_message_; }
    QVariantMap routeCheck() const { return check_; }

    // Depth-encoded chart used by checkRoute (empty: none)
    void setDepthChart(const QString& path);
    Q_INVOKABLE void checkRoute(int index, double safety_m);
    Q_INVOKABLE void checkPoints(const QVariantList& points, double safety_m, const QString& name);
    Q_INVOKABLE void clearCheck();

    // Where removable drives are mounted (Linux: /media/<user>/<label>, /run/media/..., /mnt/...).
    // fixedDrives: directories used as drives as they are (config "usb_drives"; tests).
    void setUsbRoots(QStringList roots, QStringList fixedDrives = {});
    Q_INVOKABLE void refreshUsb();
    // Adds the waypoints and routes of a GPX file (same names get a suffix)
    Q_INVOKABLE bool importGpx(const QString& path);
    // Writes all waypoints and routes to the first USB drive: OpenBoat-YYYYMMDD-HHMM.gpx
    Q_INVOKABLE bool exportToUsb();

    Q_INVOKABLE QString addWaypoint(double lat, double lon, const QString& name = {});
    Q_INVOKABLE void removeWaypoint(int index);
    // Returns the index of the new route (-1: fewer than 2 points)
    Q_INVOKABLE int addRoute(const QVariantList& points, const QString& name = {});
    // Replaces the points of a route (name kept); an active navigation keeps its copy
    Q_INVOKABLE bool updateRoute(int index, const QVariantList& points);
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
    void usbChanged();
    void checkChanged();

private:
    void save();
    QString uniqueName(const QString& prefix) const;

    boat::core::DataBus& bus_;
    QString path_;
    QVariantList waypoints_;
    QVariantList routes_;
    QString error_;
    QStringList usb_roots_{QStringLiteral("/media"), QStringLiteral("/run/media"), QStringLiteral("/mnt")};
    QStringList usb_fixed_;
    QStringList usb_drives_;
    QVariantList usb_files_;
    QString usb_message_;
    std::unique_ptr<DepthChart> depth_;
    QVariantMap check_;
};
