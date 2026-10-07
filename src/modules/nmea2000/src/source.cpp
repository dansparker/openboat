#include "boat/nmea2000/source.hpp"

#include <chrono>
#include <iostream>

namespace boat::nmea2000 {

using namespace std::chrono_literals;

Nmea2000Source::Nmea2000Source(std::string name, std::unique_ptr<hal::CanBus> bus, DecoderOptions options)
    : name_(std::move(name)), can_(std::move(bus)), options_(options) {}

Nmea2000Source::~Nmea2000Source() { stop(); }

void Nmea2000Source::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] {
        Decoder decoder(bus, options_);
        while (running_) {
            try {
                if (const auto frame = can_->receive(200ms)) decoder.feed(*frame);
            } catch (const hal::CanError& e) {
                // Bus-off or interface down (e.g. N2K power switched off): keep trying
                std::cerr << "[" << name_ << "] " << e.what() << '\n';
                for (int i = 0; running_ && i < 20; ++i) std::this_thread::sleep_for(100ms);
            }
        }
    });
}

void Nmea2000Source::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

}  // namespace boat::nmea2000
