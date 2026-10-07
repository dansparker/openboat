#pragma once

// Drives a piezo buzzer (via a transistor) on a GPIO line with the alarm
// beep pattern. Independent of the UI: it keeps working when the display
// application hangs, and it beeps when the alarm monitoring itself stops
// publishing (no fresh AlarmList), because silence would look like "all ok".

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "boat/core/module.hpp"

namespace boat::buzzer {

// Abstract so the module can be tested without hardware.
class Output {
public:
    virtual ~Output() = default;
    virtual void set(bool on) = 0;
};

struct BuzzerConfig {
    std::string chip = "/dev/gpiochip0";
    unsigned line = 17;
    bool active_low = false;
    bool startup_beep = true;  // proves at power-on that the buzzer works
};

[[nodiscard]] std::unique_ptr<Output> make_gpio_output(const BuzzerConfig& config);

class BuzzerModule final : public core::Module {
public:
    explicit BuzzerModule(std::unique_ptr<Output> output, bool startup_beep = true)
        : output_(std::move(output)), startup_beep_(startup_beep) {}
    ~BuzzerModule() override;

    [[nodiscard]] std::string_view name() const override { return "buzzer"; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    std::unique_ptr<Output> output_;
    bool startup_beep_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::buzzer
