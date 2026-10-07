#include "route_store.hpp"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <utility>

#include "boat/core/nav_math.hpp"
#include "boat/nav/route.hpp"

namespace {

QVariantMap point(const QString& name, double lat, double lon) {
    return QVariantMap{{"name", name}, {"lat", lat}, {"lon", lon}};
}

boat::nav::Waypoint to_waypoint(const QVariant& v) {
    const QVariantMap m = v.toMap();
    return {m.value("name").toString().toStdString(), {m.value("lat").toDouble(), m.value("lon").toDouble()}};
}

double route_length_nm(const QVariantList& points) {
    double m = 0.0;
    for (qsizetype i = 1; i < points.size(); ++i) {
        m += boat::core::distance_m(to_waypoint(points[i - 1]).point, to_waypoint(points[i]).point);
    }
    return m / boat::core::kMetresPerNm;
}

QVariantMap make_route(const QString& name, const QVariantList& points) {
    return QVariantMap{{"name", name}, {"points", points}, {"lengthNm", route_length_nm(points)}};
}

bool valid(double lat, double lon) { return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180; }

}  // namespace

RouteStore::RouteStore(boat::core::DataBus& bus, QString path, QObject* parent)
    : QObject(parent), bus_(bus), path_(std::move(path)) {
    if (QFileInfo::exists(path_)) readGpx(path_, waypoints_, routes_, error_);
}

QString RouteStore::uniqueName(const QString& prefix) const {
    for (int i = 1;; ++i) {
        const QString name = QStringLiteral("%1%2").arg(prefix).arg(i, 3, 10, QLatin1Char('0'));
        const auto taken = std::any_of(waypoints_.begin(), waypoints_.end(),
                                       [&](const QVariant& w) { return w.toMap().value("name").toString() == name; }) ||
                           std::any_of(routes_.begin(), routes_.end(),
                                       [&](const QVariant& r) { return r.toMap().value("name").toString() == name; });
        if (!taken) return name;
    }
}

QString RouteStore::addWaypoint(double lat, double lon, const QString& name) {
    const QString n = name.isEmpty() ? uniqueName(QStringLiteral("WP")) : name;
    waypoints_.append(point(n, lat, lon));
    save();
    return n;
}

void RouteStore::removeWaypoint(int index) {
    if (index < 0 || index >= waypoints_.size()) return;
    waypoints_.removeAt(index);
    save();
}

void RouteStore::addRoute(const QVariantList& points, const QString& name) {
    if (points.size() < 2) return;
    QVariantList named;
    for (qsizetype i = 0; i < points.size(); ++i) {
        QVariantMap p = points[i].toMap();
        if (p.value("name").toString().isEmpty()) p["name"] = QStringLiteral("RP%1").arg(i + 1);
        named.append(p);
    }
    routes_.append(make_route(name.isEmpty() ? uniqueName(QStringLiteral("Route ")) : name, named));
    save();
}

void RouteStore::removeRoute(int index) {
    if (index < 0 || index >= routes_.size()) return;
    routes_.removeAt(index);
    save();
}

void RouteStore::goTo(double lat, double lon, const QString& name) {
    boat::nav::NavCommand c;
    c.action = boat::nav::NavCommand::Action::GoTo;
    c.waypoint = {name.isEmpty() ? std::string("Ziel") : name.toStdString(), {lat, lon}};
    bus_.publish(c);
}

void RouteStore::startRoute(int index, bool reverse) {
    if (index < 0 || index >= routes_.size()) return;
    const QVariantMap r = routes_[index].toMap();
    boat::nav::NavCommand c;
    c.action = boat::nav::NavCommand::Action::StartRoute;
    c.route.name = r.value("name").toString().toStdString();
    for (const auto& p : r.value("points").toList()) c.route.points.push_back(to_waypoint(p));
    if (reverse) std::reverse(c.route.points.begin(), c.route.points.end());
    bus_.publish(c);
}

void RouteStore::nextWaypoint() { bus_.publish(boat::nav::NavCommand{boat::nav::NavCommand::Action::NextWaypoint}); }

void RouteStore::stopNavigation() { bus_.publish(boat::nav::NavCommand{boat::nav::NavCommand::Action::Stop}); }

void RouteStore::manOverboard() { bus_.publish(boat::nav::NavCommand{boat::nav::NavCommand::Action::Mob}); }

void RouteStore::save() {
    if (!writeGpx(path_, waypoints_, routes_, error_)) qWarning() << "OpenBoat:" << error_;
    else error_.clear();
    emit changed();
}

bool RouteStore::writeGpx(const QString& path, const QVariantList& waypoints, const QVariantList& routes,
                          QString& error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    QXmlStreamWriter x(&file);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeStartElement("gpx");
    x.writeDefaultNamespace("http://www.topografix.com/GPX/1/1");
    x.writeAttribute("version", "1.1");
    x.writeAttribute("creator", "OpenBoat");
    const auto write_point = [&x](const char* tag, const QVariant& v) {
        const QVariantMap m = v.toMap();
        x.writeStartElement(tag);
        x.writeAttribute("lat", QString::number(m.value("lat").toDouble(), 'f', 7));
        x.writeAttribute("lon", QString::number(m.value("lon").toDouble(), 'f', 7));
        x.writeTextElement("name", m.value("name").toString());
        x.writeEndElement();
    };
    for (const auto& w : waypoints) write_point("wpt", w);
    for (const auto& r : routes) {
        const QVariantMap m = r.toMap();
        x.writeStartElement("rte");
        x.writeTextElement("name", m.value("name").toString());
        for (const auto& p : m.value("points").toList()) write_point("rtept", p);
        x.writeEndElement();
    }
    x.writeEndElement();
    x.writeEndDocument();
    if (!file.commit()) {
        error = QStringLiteral("cannot save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool RouteStore::readGpx(const QString& path, QVariantList& waypoints, QVariantList& routes, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    QXmlStreamReader x(&file);
    QVariantList route_points;
    QString route_name;
    bool in_route = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement() && (x.name() == u"wpt" || x.name() == u"rtept")) {
            const bool is_route_point = x.name() == u"rtept";
            const double lat = x.attributes().value("lat").toDouble();
            const double lon = x.attributes().value("lon").toDouble();
            QString name;
            while (!(x.isEndElement() && (x.name() == u"wpt" || x.name() == u"rtept")) && !x.atEnd()) {
                x.readNext();
                if (x.isStartElement() && x.name() == u"name") name = x.readElementText();
            }
            if (!valid(lat, lon)) continue;
            if (is_route_point) route_points.append(point(name, lat, lon));
            else waypoints.append(point(name, lat, lon));
        } else if (x.isStartElement() && x.name() == u"rte") {
            in_route = true;
            route_points.clear();
            route_name.clear();
        } else if (in_route && x.isStartElement() && x.name() == u"name") {
            route_name = x.readElementText();
        } else if (x.isEndElement() && x.name() == u"rte") {
            in_route = false;
            if (route_points.size() >= 2) routes.append(make_route(route_name, route_points));
        }
    }
    if (x.hasError()) {
        error = QStringLiteral("%1: %2").arg(path, x.errorString());
        return false;
    }
    return true;
}
