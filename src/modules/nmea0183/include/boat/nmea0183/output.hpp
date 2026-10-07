#pragma once

// NMEA 0183 output for autopilots and other displays: RMB, APB and XTE from
// the active waypoint navigation (nav::Guidance), once per second.
// Nothing is sent while no navigation is active, so the autopilot drops
// out of track mode instead of steering towards a stale waypoint.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "boat/core/module.hpp"
#include "boat/nav/route.hpp"

namespace boat::nmea0183 {

// Adds "$", "*hh" and CRLF to a sentence body like "GPRMB,A,...".
[[nodiscard]] std::string finish_sentence(const std::string& body);

// Sentences for one guidance sample (empty if there is nothing to steer to).
[[nodiscard]] std::vector<std::string> autopilot_sentences(const nav::Guidance& g, const std::string& talker = "GP");

struct OutputConfig {
    enum class Kind { Udp, Serial } kind = Kind::Udp;
    std::string host = "255.255.255.255";  // udp; broadcast by default
    std::uint16_t port = 10110;
    std::string device;  // serial
    int baud = 4800;
    std::string talker = "GP";
};

class Nmea0183Output final : public core::Module {
public:
    explicit Nmea0183Output(OutputConfig config) : config_(std::move(config)) {}
    ~Nmea0183Output() override;

    [[nodiscard]] std::string_view name() const override { return "nmea0183-out"; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    OutputConfig config_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::nmea0183
