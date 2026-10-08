// UI tests: the real Main.qml with real models (no hardware, no charts),
// driven by mouse clicks from tst_ui.qml.

#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickTest>

#include <memory>

#include "boat/core/data_bus.hpp"
#include "boat_model.hpp"
#include "logbook.hpp"
#include "route_store.hpp"
#include "settings.hpp"

Q_IMPORT_QML_PLUGIN(OpenBoatPlugin)

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        settings_ = std::make_unique<Settings>(bus_, dir_.filePath("settings.json"), QJsonObject{});
        routes_ = std::make_unique<RouteStore>(bus_, dir_.filePath("navigation.gpx"));
        model_ = std::make_unique<BoatModel>(bus_);
        logbook_ = std::make_unique<Logbook>(bus_, dir_.filePath("logbook.json"));
        auto* ctx = engine->rootContext();
        ctx->setContextProperty("boat", model_.get());
        ctx->setContextProperty("routes", routes_.get());
        ctx->setContextProperty("settings", settings_.get());
        ctx->setContextProperty("logbook", logbook_.get());
        ctx->setContextProperty("chartLayers", QVariantList{});
        ctx->setContextProperty("startFullScreen", false);
        ctx->setContextProperty("startNight", false);
        ctx->setContextProperty("startPage", QString());
        ctx->setContextProperty("startZoom", 14);
    }

private:
    QTemporaryDir dir_;
    boat::core::DataBus bus_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<RouteStore> routes_;
    std::unique_ptr<BoatModel> model_;
    std::unique_ptr<Logbook> logbook_;
};

QUICK_TEST_MAIN_WITH_SETUP(openboat_ui, Setup)
#include "tst_ui.moc"
