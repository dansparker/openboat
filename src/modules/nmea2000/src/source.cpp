#include "boat/nmea2000/source.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <optional>

#include "boat/core/daily_log.hpp"
#include "boat/core/marine_data.hpp"

namespace boat::nmea2000 {

using namespace std::chrono_literals;

Nmea2000Source::Nmea2000Source(std::string name, std::unique_ptr<hal::CanBus> bus, DecoderOptions options,
                               std::string record_dir)
    : name_(std::move(name)), can_(std::move(bus)), options_(options), record_dir_(std::move(record_dir)) {}

namespace {

// "(1696789012.123456) can0 09F80103#0102030405060708"
std::string candump_line(const hal::CanFrame& f, std::int64_t unix_us) {
    char head[64];
    std::snprintf(head, sizeof head, "(%lld.%06lld) can0 %0*X#", static_cast<long long>(unix_us / 1000000),
                  static_cast<long long>(unix_us % 1000000), f.extended ? 8 : 3, static_cast<unsigned>(f.id));
    std::string s = head;
    char b[3];
    for (int i = 0; i < f.dlc; ++i) {
        std::snprintf(b, sizeof b, "%02X", f.data[static_cast<std::size_t>(i)]);
        s += b;
    }
    return s;
}

}  // namespace

Nmea2000Source::~Nmea2000Source() { stop(); }

void Nmea2000Source::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] {
        Decoder decoder(bus, options_);
        std::atomic<bool> record{false};
        std::optional<core::DailyLog> log;
        if (!record_dir_.empty()) {
            log.emplace(record_dir_, "nmea2000-" + name_, "log");
            if (const auto c = bus.latest<core::RecordCommand>()) record = c->value.on;
        }
        const auto record_sub = bus.topic<core::RecordCommand>().subscribe([&](const auto& s) { record = s.value.on; });
        while (running_) {
            try {
                if (const auto frame = can_->receive(200ms)) {
                    if (log && record) {
                        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::system_clock::now().time_since_epoch()).count();
                        // GNSS time when known: the Pi has no clock (file names would say 1970)
                        if (const auto t = bus.latest<core::UtcTime>(); t && core::is_fresh(*t, 10s)) {
                            us = t->value.unix_ms * 1000 +
                                 std::chrono::duration_cast<std::chrono::microseconds>(core::Clock::now() - t->timestamp).count();
                        }
                        log->write(candump_line(*frame, us), us / 1000);
                    }
                    decoder.feed(*frame);
                }
            } catch (const hal::CanError& e) {
                // Bus-off or interface down (e.g. N2K power switched off): keep trying
                std::cerr << "[" << name_ << "] " << e.what() << '\n';
                for (int i = 0; running_ && i < 20; ++i) std::this_thread::sleep_for(100ms);
            }
        }
        bus.topic<core::RecordCommand>().unsubscribe(record_sub);
    });
}

void Nmea2000Source::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

}  // namespace boat::nmea2000
