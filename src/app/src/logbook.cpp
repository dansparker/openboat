#include "logbook.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>
#include <QVariantMap>

#include <chrono>
#include <utility>

#include "boat/core/marine_data.hpp"
#include "boat/core/nav_math.hpp"

namespace core = boat::core;

namespace {

constexpr double kKnPerMps = 1.0 / core::kMpsPerKnot;

template <typename T>
std::optional<T> fresh(core::DataBus& bus, std::chrono::seconds max_age) {
    const auto s = bus.latest<T>();
    if (!s || !core::is_fresh(*s, max_age)) return std::nullopt;
    return s->value;
}

}  // namespace

Logbook::Logbook(core::DataBus& bus, QString path, QObject* parent)
    : QObject(parent), bus_(bus), path_(std::move(path)) {
    QFile file(path_);
    if (file.open(QIODevice::ReadOnly)) {
        const auto array = QJsonDocument::fromJson(file.readAll()).array();
        for (const auto& v : array) entries_.prepend(v.toObject().toVariantMap());
    }
}

void Logbook::add(const QString& text) {
    QVariantMap e;
    // GNSS time if there is one (the Pi has no clock), otherwise the system clock
    qint64 ms = QDateTime::currentMSecsSinceEpoch();
    if (const auto t = fresh<core::UtcTime>(bus_, std::chrono::seconds(10))) ms = t->unix_ms;
    e["timeMs"] = static_cast<double>(ms);
    e["text"] = text;
    const auto pos = fresh<core::Position>(bus_, std::chrono::seconds(5));
    e["hasPosition"] = pos.has_value();
    if (pos) {
        e["lat"] = pos->point.lat_deg;
        e["lon"] = pos->point.lon_deg;
    }
    if (const auto c = fresh<core::CourseOverGround>(bus_, std::chrono::seconds(5))) {
        e["sogKn"] = c->sog_mps * kKnPerMps;
        e["cog"] = c->cog_deg;
    }
    if (const auto d = fresh<core::Depth>(bus_, std::chrono::seconds(5))) e["depth"] = d->depth_m();
    if (const auto w = fresh<core::TrueWind>(bus_, std::chrono::seconds(5))) {
        e["twd"] = w->direction_deg;
        e["twsKn"] = w->speed_mps * kKnPerMps;
    }
    entries_.prepend(e);
    save();
}

void Logbook::remove(int index) {
    if (index < 0 || index >= entries_.size()) return;
    entries_.removeAt(index);
    save();
}

void Logbook::save() {
    QJsonArray array;
    for (auto it = entries_.crbegin(); it != entries_.crend(); ++it) array.append(QJsonObject::fromVariantMap(it->toMap()));
    QSaveFile file(path_);
    if (file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(array).toJson()) >= 0 && file.commit()) {
        error_.clear();
    } else {
        error_ = QStringLiteral("Logbuch nicht gespeichert: %1").arg(file.errorString());
    }
    emit changed();
}

QString Logbook::exportCsv(const QString& dir) {
    const QString path = QDir(dir).filePath(
        QStringLiteral("Logbuch-%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"))));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    QTextStream out(&file);
    out << "UTC;Eintrag;Breite;Laenge;SOG kn;COG;Tiefe m;Wind wahr Richtung;Wind wahr kn\n";
    const auto num = [](const QVariantMap& e, const char* key, int decimals) {
        return e.contains(key) ? QString::number(e.value(key).toDouble(), 'f', decimals) : QString();
    };
    for (auto it = entries_.crbegin(); it != entries_.crend(); ++it) {
        const QVariantMap e = it->toMap();
        QString text = e.value("text").toString();
        text.replace(QLatin1Char(';'), QLatin1Char(','));
        out << QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(e.value("timeMs").toDouble()), QTimeZone::UTC)
                   .toString(Qt::ISODate)
            << ';' << text << ';' << num(e, "lat", 5) << ';' << num(e, "lon", 5) << ';' << num(e, "sogKn", 1) << ';'
            << num(e, "cog", 0) << ';' << num(e, "depth", 1) << ';' << num(e, "twd", 0) << ';' << num(e, "twsKn", 0)
            << '\n';
    }
    out.flush();
    return file.commit() ? path : QString();
}
