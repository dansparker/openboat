#include "boat/nmea0183/source.hpp"

#include "boat/core/daily_log.hpp"
#include "boat/core/marine_data.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>

#include "boat/hal/serial_port.hpp"
#include "boat/hal/tcp_socket.hpp"
#include "boat/hal/udp_socket.hpp"

namespace boat::nmea0183 {

using namespace std::chrono_literals;

Nmea0183Source::Nmea0183Source(std::string name, SourceConfig config)
    : name_(std::move(name)), config_(std::move(config)) {}

Nmea0183Source::~Nmea0183Source() { stop(); }

void Nmea0183Source::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] { run(bus); });
}

void Nmea0183Source::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void Nmea0183Source::run(core::DataBus& bus) {
    Parser parser(bus, config_.parser);
    LineAssembler lines;
    std::array<std::byte, 2048> buffer{};

    // Recording: switched by RecordCommand on the bus
    std::atomic<bool> record{false};
    std::optional<core::DailyLog> log;
    if (!config_.record_dir.empty() && config_.kind != SourceConfig::Kind::File) {
        log.emplace(config_.record_dir, "nmea0183-" + name_, "nmea");
        if (const auto c = bus.latest<core::RecordCommand>()) record = c->value.on;
    }
    const auto record_sub = bus.topic<core::RecordCommand>().subscribe([&](const auto& s) { record = s.value.on; });
    const auto on_line = [&](std::string_view l) {
        if (log && record) {
            std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch()).count();
            if (const auto t = bus.latest<core::UtcTime>(); t && core::is_fresh(*t, 10s)) ms = t->value.unix_ms;
            // lines that already carry a tag block keep it (the multiplexer's own)
            log->write(l.starts_with('\\') ? std::string(l) : core::nmea_time_tag(ms) + std::string(l), ms);
        }
        parser.feed(l);
    };
    const auto feed = [&](std::size_t n) {
        lines.push(std::string_view(reinterpret_cast<const char*>(buffer.data()), n), on_line);
    };
    const auto sleep_while_running = [this](std::chrono::milliseconds d) {
        for (auto t = 0ms; running_ && t < d; t += 100ms) std::this_thread::sleep_for(100ms);
    };

    // Each connection attempt is retried: a multiplexer that boots after
    // the display, or a USB receiver that is re-plugged, must recover alone.
    while (running_) {
        try {
            switch (config_.kind) {
                case SourceConfig::Kind::Udp: {
                    hal::UdpSocket socket;
                    socket.bind(config_.port);
                    while (running_) {
                        if (const auto d = socket.receive_from(buffer, 200ms)) {
                            feed(d->size);
                            lines.push("\n", on_line);  // datagram ends a line
                        }
                    }
                    break;
                }
                case SourceConfig::Kind::Tcp: {
                    hal::TcpSocket socket;
                    socket.connect(config_.host, config_.port, 3s);
                    while (running_) {
                        const auto n = socket.receive(buffer, 200ms);
                        if (n && *n == 0) break;  // peer closed: reconnect
                        if (n) feed(*n);
                    }
                    break;
                }
                case SourceConfig::Kind::Serial: {
                    hal::SerialPort port(config_.device, config_.baud);
                    while (running_) {
                        if (const auto n = port.read(buffer, 200ms)) feed(*n);
                    }
                    break;
                }
                case SourceConfig::Kind::File: {
                    std::ifstream in(config_.path);
                    if (!in) throw std::runtime_error("cannot open " + config_.path);
                    const auto period = std::chrono::duration<double>(1.0 / std::max(0.1, config_.lines_per_second));
                    std::string line;
                    std::int64_t last_ms = -1;
                    while (running_ && std::getline(in, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        // Recorded with time tags: original timing (gaps capped at 5 s)
                        const std::int64_t ms = core::nmea_tag_time_ms(line);
                        if (ms > 0 && last_ms > 0 && ms > last_ms) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<std::int64_t>(ms - last_ms, 5000)));
                        } else if (ms <= 0) {
                            std::this_thread::sleep_for(period);
                        }
                        if (ms > 0) last_ms = ms;
                        parser.feed(line);
                    }
                    if (!config_.loop) {
                        running_ = false;
                    }
                    break;
                }
            }
            sleep_while_running(500ms);  // connection closed or end of file
        } catch (const std::exception& e) {
            std::cerr << "[" << name_ << "] " << e.what() << " - retrying\n";
            sleep_while_running(2s);
        }
    }
    bus.topic<core::RecordCommand>().unsubscribe(record_sub);
}

}  // namespace boat::nmea0183
