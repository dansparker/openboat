#pragma once

// Plays the alarm beep pattern (nav::buzzer_on) on the loudspeaker.
// Needs Qt Multimedia; without it this class is silent and the GPIO
// buzzer module is the only audible output.

#include <QObject>
#include <QTimer>

#include <chrono>
#include <memory>

#include "boat/core/data_bus.hpp"

class QSoundEffect;
class QTemporaryFile;

class AlarmSound : public QObject {
    Q_OBJECT
public:
    explicit AlarmSound(boat::core::DataBus& bus, QObject* parent = nullptr);
    ~AlarmSound() override;

    [[nodiscard]] static bool available();

private:
    void tick();

    boat::core::DataBus& bus_;
    QTimer timer_;
    std::chrono::steady_clock::time_point t0_;
    bool last_ = false;
    std::unique_ptr<QTemporaryFile> wav_;
    QSoundEffect* effect_ = nullptr;
};
