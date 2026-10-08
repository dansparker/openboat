#include "alarm_sound.hpp"
#include "boat_model.hpp"
#include "logbook.hpp"
#include "mbtiles_provider.hpp"
#include "route_store.hpp"
#include "settings.hpp"

#include "boat/buzzer/buzzer.hpp"
#include "boat/core/data_bus.hpp"
#include "boat/hal/can_bus.hpp"
#include "boat/nav/nav.hpp"
#include "boat/nmea0183/output.hpp"
#include "boat/nmea0183/source.hpp"
#include "boat/nmea2000/source.hpp"
#include "boat/sim/simulator.hpp"
#include "boat/track/track.hpp"

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantList>
#include <QtQml/QQmlExtensionPlugin>

#include <algorithm>
#include <exception>
#include <memory>
#include <sstream>
#include <vector>

Q_IMPORT_QML_PLUGIN(OpenBoatPlugin)

namespace {

using boat::core::Module;

QJsonObject load_config(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "OpenBoat: cannot read" << path << "- using the simulator";
        return QJsonObject{{"sources", QJsonArray{QJsonObject{{"type", "sim"}}}}};
    }
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        // A broken config must not leave the skipper with a black screen
        qWarning() << "OpenBoat: invalid JSON in" << path << ":" << error.errorString() << "- using the simulator";
        return QJsonObject{{"sources", QJsonArray{QJsonObject{{"type", "sim"}}}}};
    }
    return doc.object();
}

// Relative paths in the config are relative to the config file
// World Magnetic Model for compasses that send no variation. Missing or
// broken: headings from such compasses stay magnetic (shown as "HDG (mag)").
std::shared_ptr<const boat::nav::MagneticModel> load_magnetic_model(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "OpenBoat: no magnetic model at" << path << "- magnetic headings without variation stay magnetic";
        return nullptr;
    }
    std::istringstream in(file.readAll().toStdString());
    try {
        return std::make_shared<const boat::nav::MagneticModel>(boat::nav::MagneticModel::from_cof(in));
    } catch (const std::exception& e) {
        qWarning() << "OpenBoat: magnetic model" << path << "unusable:" << e.what();
        return nullptr;
    }
}

QString resolve(const QString& base_dir, const QString& path) {
    return QFileInfo(path).isAbsolute() ? path : QDir(base_dir).filePath(path);
}

std::unique_ptr<Module> make_source(const QJsonObject& s, const QString& base_dir, double depth_offset,
                                    const std::string& record_dir) {
    const QString type = s.value("type").toString();
    if (type == "sim") {
        boat::sim::SimSettings sim;
        sim.centre.lat_deg = s.value("lat").toDouble(sim.centre.lat_deg);
        sim.centre.lon_deg = s.value("lon").toDouble(sim.centre.lon_deg);
        sim.radius_m = s.value("radius_m").toDouble(sim.radius_m);
        // [shallowest, deepest] along the circle - set it to the real water there
        if (const auto d = s.value("depth_m").toArray(); d.size() == 2) {
            sim.depth_min_m = d[0].toDouble(sim.depth_min_m);
            sim.depth_max_m = std::max(sim.depth_min_m, d[1].toDouble(sim.depth_max_m));
        }
        // elsewhere than the Attersee the demo AIS targets would sail over land
        sim.ais_targets = s.value("ais").toBool(!s.contains("lat"));
        return std::make_unique<boat::sim::Simulator>(sim);
    }
    if (type == "nmea0183") {
        boat::nmea0183::SourceConfig c;
        const QString kind = s.value("kind").toString("udp");
        c.kind = kind == "tcp"      ? boat::nmea0183::SourceConfig::Kind::Tcp
                 : kind == "serial" ? boat::nmea0183::SourceConfig::Kind::Serial
                 : kind == "file"   ? boat::nmea0183::SourceConfig::Kind::File
                                    : boat::nmea0183::SourceConfig::Kind::Udp;
        c.host = s.value("host").toString().toStdString();
        c.port = static_cast<std::uint16_t>(s.value("port").toInt(10110));
        c.device = s.value("device").toString().toStdString();
        c.baud = s.value("baud").toInt(4800);
        c.path = resolve(base_dir, s.value("path").toString()).toStdString();
        c.lines_per_second = s.value("lines_per_second").toDouble(20.0);
        c.parser.depth_offset_m = depth_offset;
        c.parser.accept_missing_checksum = s.value("accept_missing_checksum").toBool(false);
        c.record_dir = record_dir;
        return std::make_unique<boat::nmea0183::Nmea0183Source>("nmea0183-" + kind.toStdString(), c);
    }
    if (type == "nmea2000") {
        std::unique_ptr<boat::hal::CanBus> can;
        if (s.contains("candump")) {
            can = std::make_unique<boat::hal::CandumpReplayBus>(
                resolve(base_dir, s.value("candump").toString()).toStdString());
        } else {
            can = std::make_unique<boat::hal::SocketCanBus>(s.value("interface").toString("can0").toStdString());
        }
        // a replayed candump log is not recorded again
        return std::make_unique<boat::nmea2000::Nmea2000Source>("nmea2000", std::move(can),
                                                                boat::nmea2000::DecoderOptions{depth_offset},
                                                                s.contains("candump") ? std::string() : record_dir);
    }
    throw std::runtime_error("unknown source type: " + type.toStdString());
}

// Sidecar with chart labels (tools/make_basemap.py, make_depth.py): drawn by the app,
// upright also in course-up. [{lon, lat, text, kind, z: [zoom levels]}]
QVariantList load_labels(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object().value("labels").toArray().toVariantList();
}

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("OpenBoat"));
    QGuiApplication::setApplicationVersion(QStringLiteral(OPENBOAT_VERSION));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Open-source chartplotter"));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption config_opt({"c", "config"}, QStringLiteral("Configuration file"), QStringLiteral("file"),
                                  QStringLiteral("boat.json"));
    QCommandLineOption fullscreen_opt(QStringLiteral("fullscreen"), QStringLiteral("Start in full screen"));
    QCommandLineOption night_opt(QStringLiteral("night"), QStringLiteral("Start in night mode"));
    QCommandLineOption screenshot_opt(QStringLiteral("screenshot"),
                                      QStringLiteral("Save a screenshot after <ms> and quit (docs, CI)"),
                                      QStringLiteral("file"));
    QCommandLineOption delay_opt(QStringLiteral("screenshot-delay"), QStringLiteral("Delay in ms"),
                                 QStringLiteral("ms"), QStringLiteral("8000"));
    QCommandLineOption demo_opt(QStringLiteral("demo"),
                                QStringLiteral("Start a demo route near the simulator (screenshots, trying out)"));
    QCommandLineOption page_opt(QStringLiteral("page"), QStringLiteral("Open a page at start: routes | settings | ais"),
                                QStringLiteral("page"));
    QCommandLineOption zoom_opt(QStringLiteral("zoom"), QStringLiteral("Initial chart zoom level (3..18)"),
                                QStringLiteral("level"), QStringLiteral("14"));
    cli.addOptions({config_opt, fullscreen_opt, night_opt, screenshot_opt, delay_opt, demo_opt, page_opt, zoom_opt});
    cli.process(app);

    const QString config_path = cli.value(config_opt);
    const QJsonObject config = load_config(config_path);
    const QString base_dir = QFileInfo(config_path).absolutePath();

    boat::core::DataBus bus;
    std::vector<std::unique_ptr<Module>> modules;

    // User settings (settings page) on top of the configuration defaults
    Settings settings(bus, resolve(base_dir, config.value("settings_file").toString("settings.json")), config);
    modules.push_back(std::make_unique<boat::nav::NavModule>(
        settings.navSettings(), load_magnetic_model(resolve(base_dir, config.value("magnetic_model").toString("../data/WMM.COF")))));

    boat::track::TrackConfig track;
    track.dir = resolve(base_dir, config.value("track_dir").toString("tracks")).toStdString();
    track.recording = settings.trackRecording();
    track.filter.min_distance_m = settings.trackSpacing();
    modules.push_back(std::make_unique<boat::track::TrackModule>(track));

    // Default when the transducer sends no offset and none is set manually (settings page)
    const double depth_offset = config.value("depth_offset_m").toDouble(0.0);
    for (const auto& value : config.value("sources").toArray()) {
        try {
            modules.push_back(make_source(value.toObject(), base_dir, depth_offset,
                                          resolve(base_dir, config.value("record_dir").toString("logs")).toStdString()));
        } catch (const std::exception& e) {
            // One broken source (e.g. no CAN interface) must not stop the others
            qWarning() << "OpenBoat: source not started:" << e.what();
        }
    }
    // Audible alarm via a GPIO buzzer (Raspberry Pi), independent of the UI
    if (const QJsonObject b = config.value("buzzer").toObject(); !b.isEmpty() && b.value("enabled").toBool(true)) {
        boat::buzzer::BuzzerConfig c;
        c.chip = b.value("chip").toString("/dev/gpiochip0").toStdString();
        c.line = static_cast<unsigned>(b.value("line").toInt(17));
        c.active_low = b.value("active_low").toBool(false);
        c.startup_beep = b.value("startup_beep").toBool(true);
        try {
            modules.push_back(std::make_unique<boat::buzzer::BuzzerModule>(boat::buzzer::make_gpio_output(c),
                                                                           c.startup_beep));
        } catch (const std::exception& e) {
            qWarning() << "OpenBoat: GPIO buzzer not available:" << e.what();
        }
    }
    // NMEA 0183 output for an autopilot (RMB/APB/XTE)
    if (const QJsonObject o = config.value("autopilot_output").toObject(); !o.isEmpty()) {
        boat::nmea0183::OutputConfig c;
        c.kind = o.value("kind").toString("udp") == "serial" ? boat::nmea0183::OutputConfig::Kind::Serial
                                                             : boat::nmea0183::OutputConfig::Kind::Udp;
        c.host = o.value("host").toString("255.255.255.255").toStdString();
        c.port = static_cast<std::uint16_t>(o.value("port").toInt(10110));
        c.device = o.value("device").toString().toStdString();
        c.baud = o.value("baud").toInt(4800);
        modules.push_back(std::make_unique<boat::nmea0183::Nmea0183Output>(c));
    }
    settings.publishDepthOffset();
    settings.publishRecording();  // before the sources start: the first depth already uses it
    for (auto& m : modules) m->start(bus);

    // Declared before the engine so it outlives the QML that binds to it
    BoatModel model(bus);
    AlarmSound sound(bus);
    RouteStore routes(bus, resolve(base_dir, config.value("navigation_file").toString("navigation.gpx")));
    {
        QStringList roots{QStringLiteral("/media"), QStringLiteral("/run/media"), QStringLiteral("/mnt")};
        if (config.contains("usb_roots")) roots = config.value("usb_roots").toVariant().toStringList();
        QStringList fixed;
        for (const auto& d : config.value("usb_drives").toArray()) fixed.append(resolve(base_dir, d.toString()));
        routes.setUsbRoots(roots, fixed);
    }
    if (!AlarmSound::available() && !config.contains("buzzer")) {
        qWarning() << "OpenBoat: NO AUDIBLE ALARM - built without Qt Multimedia and no GPIO buzzer configured";
    }

    // Charts: first entry is the base map, the rest are overlays (e.g. seamarks)
    // Before the engine: QML objects must outlive it (destroyed in reverse order)
    Logbook logbook(bus, resolve(base_dir, config.value("logbook_file").toString("logbook.json")));
    QQmlApplicationEngine engine;
    QVariantList layers;
    int index = 0;
    bool depth_chart_set = false;
    QVariantList clearances;
    for (const auto& value : config.value("charts").toArray()) {
        const QString path = resolve(base_dir, value.toString());
        const auto info = MbTilesProvider::inspect(path);
        if (!info.valid) {
            qWarning() << "OpenBoat: chart not loaded:" << info.error;
            continue;
        }
        if (info.depth_encoded && !depth_chart_set) {
            routes.setDepthChart(path);  // route check against the safety depth
            depth_chart_set = true;
        }
        // Bridges / overhead cables of ENC charts (tools/make_enc.py): route check against the air draught
        if (QFile file(path + QStringLiteral(".clearances.json")); file.open(QIODevice::ReadOnly)) {
            clearances.append(QJsonDocument::fromJson(file.readAll()).object().value("clearances").toArray().toVariantList());
        }
        const QString id = QStringLiteral("chart%1").arg(index++);
        engine.addImageProvider(id, new MbTilesProvider(path, info.depth_encoded));
        layers.append(QVariantMap{{"provider", id}, {"name", info.name}, {"attribution", info.attribution},
                                  {"depth", info.depth_encoded}, {"labels", load_labels(path + QStringLiteral(".labels.json"))}, {"minZoom", info.min_zoom},
                                  {"maxZoom", info.max_zoom}});
    }

    routes.setClearances(clearances);
    engine.rootContext()->setContextProperty(QStringLiteral("boat"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("chartLayers"), layers);
    engine.rootContext()->setContextProperty(QStringLiteral("routes"), &routes);
    engine.rootContext()->setContextProperty(QStringLiteral("logbook"), &logbook);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("startFullScreen"), cli.isSet(fullscreen_opt));
    engine.rootContext()->setContextProperty(QStringLiteral("startNight"), cli.isSet(night_opt));
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), cli.value(page_opt));
    engine.rootContext()->setContextProperty(QStringLiteral("startZoom"), std::clamp(cli.value(zoom_opt).toInt(), 3, 18));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OpenBoat", "Main");

    if (cli.isSet(demo_opt)) {
        QTimer::singleShot(1500, &app, [&bus] {
            boat::nav::NavCommand c;
            c.action = boat::nav::NavCommand::Action::StartRoute;
            c.route = {"Demo", {{"Litzlberg", {47.9255, 13.5560}}, {"Kammer", {47.9310, 13.5900}}, {"Weyregg", {47.9050, 13.5730}}}};
            bus.publish(c);
        });
    }
    if (cli.isSet(screenshot_opt)) {
        QTimer::singleShot(cli.value(delay_opt).toInt(), &app, [&engine, file = cli.value(screenshot_opt)] {
            auto* window = engine.rootObjects().isEmpty() ? nullptr
                                                          : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            const bool saved = window != nullptr && window->grabWindow().save(file);
            if (!saved) qWarning() << "OpenBoat: screenshot failed:" << file;
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    const int rc = QGuiApplication::exec();
    for (auto it = modules.rbegin(); it != modules.rend(); ++it) (*it)->stop();
    return rc;
}
