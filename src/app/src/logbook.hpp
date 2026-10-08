#pragma once

// Logbook: entries made with one touch ("Abgelegt", "Anker", "Reff", ...). Time,
// position, speed, course, depth and wind are taken from the bus at that moment,
// so the skipper never types numbers at sea. Stored as JSON (atomic save);
// exportable as CSV (e.g. to a USB stick).

#include <QObject>
#include <QString>
#include <QVariantList>

#include "boat/core/data_bus.hpp"

class Logbook : public QObject {
    Q_OBJECT
    // newest first: [{ timeMs, text, lat, lon, hasPosition, sogKn, cog, depth, twd, twsKn }]
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    Logbook(boat::core::DataBus& bus, QString path, QObject* parent = nullptr);

    QVariantList entries() const { return entries_; }
    QString lastError() const { return error_; }

    Q_INVOKABLE void add(const QString& text);
    Q_INVOKABLE void remove(int index);
    // Writes all entries as CSV into `dir`; returns the file path or "" on error
    Q_INVOKABLE QString exportCsv(const QString& dir);

signals:
    void changed();

private:
    void save();

    boat::core::DataBus& bus_;
    QString path_;
    QVariantList entries_;  // oldest first in the file, newest first for the UI
    QString error_;
};
