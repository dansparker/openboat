#pragma once

// NMEA 2000 data source: receives frames from a CAN bus (SocketCAN on the
// Raspberry Pi, or a candump log for replay) and feeds the Decoder.
// Listen-only - nothing is ever sent (see decoder.hpp).

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "boat/core/module.hpp"
#include "boat/hal/can_bus.hpp"
#include "boat/nmea2000/decoder.hpp"

namespace boat::nmea2000 {

class Nmea2000Source final : public core::Module {
public:
    Nmea2000Source(std::string name, std::unique_ptr<hal::CanBus> bus, DecoderOptions options = {});
    ~Nmea2000Source() override;

    [[nodiscard]] std::string_view name() const override { return name_; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    std::string name_;
    std::unique_ptr<hal::CanBus> can_;
    DecoderOptions options_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::nmea2000
