// Tests of the app classes behind the UI: routes (edit, GPX, USB), the route
// check against the depth chart, the logbook and settings helpers.

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QBuffer>
#include <QTest>

#include <cmath>
#include <numbers>

#include "boat/core/data_bus.hpp"
#include "boat/core/marine_data.hpp"
#include "depth_chart.hpp"
#include "logbook.hpp"
#include "route_store.hpp"
#include "settings.hpp"

namespace core = boat::core;

namespace {

QVariantMap pt(double lat, double lon) { return {{"lat", lat}, {"lon", lon}}; }

// Depth chart with one zoom-14 tile around (47.87, 13.545): 2.0 m in the west
// half, 10.0 m in the east half (encoding of tools/make_depth.py)
QString makeDepthChart(const QString& dir) {
    const QString path = QDir(dir).filePath("depth.mbtiles");
    const int z = 14;
    const double n = 256.0 * (1 << z);
    const double s = std::sin(47.87 * std::numbers::pi / 180.0);
    const int tx = static_cast<int>((13.545 + 180.0) / 360.0 * n / 256.0);
    const int ty = static_cast<int>((0.5 - std::log((1 + s) / (1 - s)) / (4 * std::numbers::pi)) * n / 256.0);
    QImage img(256, 256, QImage::Format_ARGB32);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const int dm = x < 128 ? 20 : 100;
            img.setPixel(x, y, qRgba(dm >> 8, dm & 0xFF, 1, 255));
        }
    }
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "make-depth");
        db.setDatabaseName(path);
        db.open();
        QSqlQuery q(db);
        q.exec("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)");
        q.prepare("INSERT INTO tiles VALUES (?, ?, ?, ?)");
        q.addBindValue(z);
        q.addBindValue(tx);
        q.addBindValue((1 << z) - 1 - ty);
        q.addBindValue(png);
        q.exec();
        db.close();
    }
    QSqlDatabase::removeDatabase("make-depth");
    return path;
}

// West / east edge of that tile in degrees (for routes inside it)
double tileLon(double fraction) {
    const double n = 1 << 14;
    const int tx = static_cast<int>((13.545 + 180.0) / 360.0 * n);
    return (tx + fraction) / n * 360.0 - 180.0;
}

}  // namespace

class AppTest : public QObject {
    Q_OBJECT

private slots:
    void routeEditAndPersist() {
        QTemporaryDir dir;
        core::DataBus bus;
        const QString file = dir.filePath("nav.gpx");
        {
            RouteStore r(bus, file);
            QCOMPARE(r.addRoute({pt(47.87, 13.54), pt(47.88, 13.55), pt(47.89, 13.56)}), 0);
            QCOMPARE(r.addRoute({pt(47.87, 13.54)}), -1);  // one point is no route
            QVERIFY(r.updateRoute(0, {pt(47.87, 13.54), pt(47.90, 13.57)}));
            QVERIFY(!r.updateRoute(5, {pt(1, 1), pt(2, 2)}));
        }
        RouteStore again(bus, file);
        QCOMPARE(again.routes().size(), 1);
        const auto route = again.routes()[0].toMap();
        QCOMPARE(route.value("points").toList().size(), 2);
        QVERIFY(route.value("name").toString().startsWith("Route"));
    }

    void usbExportAndImport() {
        QTemporaryDir dir;
        const QString usb = dir.filePath("usb");
        QDir().mkpath(usb);
        core::DataBus bus;
        RouteStore a(bus, dir.filePath("a.gpx"));
        a.setUsbRoots({}, {usb});
        QVERIFY(!a.usbDrives().isEmpty());
        a.addWaypoint(47.87, 13.54, "Hafen");
        a.addRoute({pt(47.87, 13.54), pt(47.88, 13.55)}, "Runde");
        QVERIFY(a.exportToUsb());
        QCOMPARE(a.usbFiles().size(), 1);

        RouteStore b(bus, dir.filePath("b.gpx"));
        b.addWaypoint(47.0, 13.0, "Hafen");  // same name: the imported one gets a suffix
        QVERIFY(b.importGpx(a.usbFiles()[0].toMap().value("path").toString()));
        QCOMPARE(b.waypoints().size(), 2);
        QCOMPARE(b.waypoints()[1].toMap().value("name").toString(), QString("Hafen (2)"));
        QCOMPARE(b.routes().size(), 1);
        QVERIFY(!b.importGpx(dir.filePath("missing.gpx")));
        QVERIFY(b.usbMessage().contains("fehlgeschlagen"));
    }

    void routeCheckAgainstDepthChart() {
        QTemporaryDir dir;
        DepthChart chart(makeDepthChart(dir.path()));
        QVERIFY(chart.valid());
        QCOMPARE(chart.zoom(), 14);
        const double lat = 47.87;
        QCOMPARE(chart.depthAt(lat, tileLon(0.25)).value_or(-1), 2.0);
        QCOMPARE(chart.depthAt(lat, tileLon(0.75)).value_or(-1), 10.0);
        QVERIFY(!chart.depthAt(lat, tileLon(1.5)));  // outside the chart

        // east half only: deeper than 3 m
        auto r = checkRouteDepth(chart, {pt(lat, tileLon(0.6)), pt(lat, tileLon(0.9))}, 3.0);
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("minDepth").toDouble(), 10.0);
        // crossing into the west half: one shallow leg, marked
        r = checkRouteDepth(chart, {pt(lat, tileLon(0.9)), pt(lat, tileLon(0.1))}, 3.0);
        QVERIFY(!r.value("ok").toBool());
        QCOMPARE(r.value("shallowLegs").toInt(), 1);
        QCOMPARE(r.value("marks").toList().size(), 1);
        // leaving the chart: reported as "no data", never as fine
        r = checkRouteDepth(chart, {pt(lat, tileLon(0.9)), pt(lat, tileLon(1.9))}, 3.0);
        QVERIFY(r.value("noDataPercent").toDouble() > 40.0);
    }

    void routeStoreCheckWithoutChart() {
        QTemporaryDir dir;
        core::DataBus bus;
        RouteStore r(bus, dir.filePath("nav.gpx"));
        r.addRoute({pt(47.87, 13.54), pt(47.88, 13.55)});
        r.checkRoute(0, 3.0);
        QCOMPARE(r.routeCheck().value("available").toBool(), false);
        r.clearCheck();
        QVERIFY(r.routeCheck().isEmpty());
    }

    void logbookTakesDataFromTheBus() {
        QTemporaryDir dir;
        core::DataBus bus;
        core::Position pos;
        pos.point = {47.87, 13.545};
        pos.quality = core::FixQuality::Gnss;
        bus.publish(pos);
        bus.publish(core::CourseOverGround{90.0, 2.5});
        bus.publish(core::Depth{12.0, -0.5});
        bus.publish(core::TrueWind{300.0, 0.0, 6.0});
        const QString file = dir.filePath("logbook.json");
        {
            Logbook log(bus, file);
            log.add("Abgelegt");
            QCOMPARE(log.entries().size(), 1);
            const auto e = log.entries()[0].toMap();
            QCOMPARE(e.value("text").toString(), QString("Abgelegt"));
            QVERIFY(e.value("hasPosition").toBool());
            QCOMPARE(e.value("depth").toDouble(), 11.5);
            QVERIFY(qAbs(e.value("sogKn").toDouble() - 2.5 / 0.514444) < 0.01);
            const QString csv = log.exportCsv(dir.path());
            QVERIFY(!csv.isEmpty());
            QFile f(csv);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll().count('\n'), 2);  // header + entry
        }
        Logbook again(bus, file);  // persisted
        QCOMPARE(again.entries().size(), 1);
    }

    void settingsDepthText() {
        QTemporaryDir dir;
        core::DataBus bus;
        Settings s(bus, dir.filePath("settings.json"), QJsonObject{});
        QCOMPARE(s.depthText(3.0), QString("3.0 m"));
        s.setDepthUnit("ft");
        QCOMPARE(s.depthText(3.048), QString("10 ft"));
    }
};

QTEST_GUILESS_MAIN(AppTest)
#include "tst_app.moc"
