#include "boat/buzzer/buzzer.hpp"

#include <chrono>
#include <iostream>

#include "boat/core/data_bus.hpp"
#include "boat/hal/gpio.hpp"
#include "boat/nav/nav.hpp"

namespace boat::buzzer {

using namespace std::chrono_literals;

namespace {

class GpioBuzzer final : public Output {
public:
    explicit GpioBuzzer(const BuzzerConfig& c) : gpio_(c.chip, c.line, c.active_low, "openboat-buzzer") {}
    void set(bool on) override { gpio_.set(on); }

private:
    hal::GpioOutput gpio_;
};

}  // namespace

std::unique_ptr<Output> make_gpio_output(const BuzzerConfig& config) { return std::make_unique<GpioBuzzer>(config); }

BuzzerModule::~BuzzerModule() { stop(); }

void BuzzerModule::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] {
        const auto t0 = core::Clock::now();
        bool last = false;
        while (running_) {
            const auto now = core::Clock::now();
            const double t = std::chrono::duration<double>(now - t0).count();
            bool on = false;
            if (startup_beep_ && t < 0.2) {
                on = true;
            } else if (t > 5.0) {  // give the nav module time to publish its first list
                const auto list = bus.latest<nav::AlarmList>();
                const auto level = list && core::is_fresh(*list, 5s) ? list->value.sound
                                                                       : std::optional(nav::AlarmLevel::Alarm);
                on = nav::buzzer_on(level, t);
            }
            if (on != last) {
                try {
                    output_->set(on);
                    last = on;
                } catch (const std::exception& e) {
                    std::cerr << "[buzzer] " << e.what() << '\n';
                }
            }
            std::this_thread::sleep_for(25ms);
        }
        try {
            output_->set(false);
        } catch (...) {
        }
    });
}

void BuzzerModule::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

}  // namespace boat::buzzer
