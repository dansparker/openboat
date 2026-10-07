#include "boat_model.hpp"
#include "mbtiles_provider.hpp"

#include "boat/core/data_bus.hpp"
#include "boat/hal/can_bus.hpp"
#include "boat/nav/nav.hpp"
#include "boat/nmea0183/source.hpp"
#include "boat/nmea2000/source.hpp"
#include "boat/sim/simulator.hpp"

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

#include <exception>
#include <memory>
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
QString resolve(const QString& base_dir, const QString& path) {
    return QFileInfo(path).isAbsolute() ? path : QDir(base_dir).filePath(path);
}

std::unique_ptr<Module> make_source(const QJsonObject& s, const QString& base_dir, double depth_offset) {
    const QString type = s.value("type").toString();
    if (type == "sim") return std::make_unique<boat::sim::Simulator>();
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
        return std::make_unique<boat::nmea2000::Nmea2000Source>("nmea2000", std::move(can),
                                                                boat::nmea2000::DecoderOptions{depth_offset});
    }
    throw std::runtime_error("unknown source type: " + type.toStdString());
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
    cli.addOptions({config_opt, fullscreen_opt, night_opt, screenshot_opt, delay_opt});
    cli.process(app);

    const QString config_path = cli.value(config_opt);
    const QJsonObject config = load_config(config_path);
    const QString base_dir = QFileInfo(config_path).absolutePath();

    boat::core::DataBus bus;
    std::vector<std::unique_ptr<Module>> modules;

    // Alarm settings
    boat::nav::NavSettings nav;
    const QJsonObject alarms = config.value("alarms").toObject();
    if (alarms.contains("shallow_m")) {
        const double v = alarms.value("shallow_m").toDouble(-1.0);
        nav.alarms.shallow_m = v > 0 ? std::optional(v) : std::nullopt;
    }
    nav.ais.cpa_alarm_m = alarms.value("cpa_nm").toDouble(0.5) * 1852.0;
    nav.ais.tcpa_alarm_s = alarms.value("tcpa_min").toDouble(10.0) * 60.0;
    modules.push_back(std::make_unique<boat::nav::NavModule>(nav));

    const double depth_offset = config.value("depth_offset_m").toDouble(0.0);
    for (const auto& value : config.value("sources").toArray()) {
        try {
            modules.push_back(make_source(value.toObject(), base_dir, depth_offset));
        } catch (const std::exception& e) {
            // One broken source (e.g. no CAN interface) must not stop the others
            qWarning() << "OpenBoat: source not started:" << e.what();
        }
    }
    for (auto& m : modules) m->start(bus);

    // Declared before the engine so it outlives the QML that binds to it
    BoatModel model(bus);

    // Charts: first entry is the base map, the rest are overlays (e.g. seamarks)
    QQmlApplicationEngine engine;
    QVariantList layers;
    int index = 0;
    for (const auto& value : config.value("charts").toArray()) {
        const QString path = resolve(base_dir, value.toString());
        const auto info = MbTilesProvider::inspect(path);
        if (!info.valid) {
            qWarning() << "OpenBoat: chart not loaded:" << info.error;
            continue;
        }
        const QString id = QStringLiteral("chart%1").arg(index++);
        engine.addImageProvider(id, new MbTilesProvider(path));
        layers.append(QVariantMap{{"id", id}, {"name", info.name}, {"minZoom", info.min_zoom},
                                  {"maxZoom", info.max_zoom}});
    }

    engine.rootContext()->setContextProperty(QStringLiteral("boat"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("chartLayers"), layers);
    engine.rootContext()->setContextProperty(QStringLiteral("startFullScreen"), cli.isSet(fullscreen_opt));
    engine.rootContext()->setContextProperty(QStringLiteral("startNight"), cli.isSet(night_opt));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OpenBoat", "Main");

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
