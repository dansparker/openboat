#include "boat/nmea0183/output.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>

#include "boat/core/data_bus.hpp"
#include "boat/core/nav_math.hpp"
#include "boat/hal/serial_port.hpp"
#include "boat/hal/udp_socket.hpp"

namespace boat::nmea0183 {

namespace {

std::string fmt(const char* f, double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

// ddmm.mmm,N / dddmm.mmm,E
std::string lat_lon(const core::GeoPoint& p) {
    const auto part = [](double v, int deg_digits, char pos, char neg) {
        const double a = std::abs(v);
        double deg = std::floor(a);
        double min = (a - deg) * 60.0;
        if (min >= 59.9995) {  // avoid "60.000" after rounding
            deg += 1.0;
            min = 0.0;
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%0*d%06.3f,%c", deg_digits, static_cast<int>(deg), min, v < 0 ? neg : pos);
        return std::string(buf);
    };
    return part(p.lat_deg, 2, 'N', 'S') + "," + part(p.lon_deg, 3, 'E', 'W');
}

// Waypoint IDs: printable ASCII without NMEA delimiters, max 10 chars
std::string ident(const std::string& name) {
    std::string out;
    for (const char c : name) {
        if (out.size() == 10) break;
        if (c > 32 && c < 127 && c != ',' && c != '*' && c != '$' && c != '!') out.push_back(c);
    }
    return out.empty() ? "WPT" : out;
}

}  // namespace

std::string finish_sentence(const std::string& body) {
    unsigned sum = 0;
    for (const char c : body) sum ^= static_cast<unsigned char>(c);
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", sum);
    return "$" + body + tail;
}

std::vector<std::string> autopilot_sentences(const nav::Guidance& g, const std::string& talker) {
    std::vector<std::string> out;
    if (g.mode == nav::NavMode::None || !g.dtw_m || !g.btw_deg) return out;

    const double xte_nm = std::min(9.99, std::abs(g.xte_m.value_or(0.0)) / core::kMetresPerNm);
    const std::string steer = g.xte_m.value_or(0.0) > 0.0 ? "L" : "R";  // right of track: steer left
    const std::string from_id = g.from ? ident(g.from->name) : "";
    const std::string to_id = ident(g.to.name);
    const std::string arrived = g.arrived ? "A" : "V";
    const double course = g.from ? core::initial_bearing_deg(g.from->point, g.to.point) : *g.btw_deg;

    out.push_back(finish_sentence(talker + "RMB,A," + fmt("%.2f", xte_nm) + "," + steer + "," + from_id + "," + to_id +
                                  "," + lat_lon(g.to.point) + "," + fmt("%.2f", std::min(999.9, *g.dtw_m / core::kMetresPerNm)) +
                                  "," + fmt("%.1f", *g.btw_deg) + "," +
                                  fmt("%.1f", g.vmg_mps.value_or(0.0) / core::kMpsPerKnot) + "," + arrived + ",A"));
    out.push_back(finish_sentence(talker + "APB,A,A," + fmt("%.2f", xte_nm) + "," + steer + ",N," + arrived + ",V," +
                                  fmt("%.1f", course) + ",T," + to_id + "," + fmt("%.1f", *g.btw_deg) + ",T," +
                                  fmt("%.1f", *g.btw_deg) + ",T,A"));
    out.push_back(finish_sentence(talker + "XTE,A,A," + fmt("%.2f", xte_nm) + "," + steer + ",N,A"));
    return out;
}

Nmea0183Output::~Nmea0183Output() { stop(); }

void Nmea0183Output::start(core::DataBus& bus) {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this, &bus] {
        using namespace std::chrono_literals;
        std::unique_ptr<hal::UdpSocket> udp;
        std::unique_ptr<hal::SerialPort> serial;
        while (running_) {
            try {
                if (config_.kind == OutputConfig::Kind::Udp && !udp) {
                    udp = std::make_unique<hal::UdpSocket>();
                    udp->bind(0);
                }
                if (config_.kind == OutputConfig::Kind::Serial && !serial) {
                    serial = std::make_unique<hal::SerialPort>(config_.device, config_.baud);
                }
                const auto g = bus.latest<nav::Guidance>();
                if (g && core::is_fresh(*g, 3s)) {
                    for (const auto& s : autopilot_sentences(g->value, config_.talker)) {
                        const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(s.data()), s.size());
                        if (udp) udp->send_to({config_.host, config_.port}, bytes);
                        if (serial) serial->write_all(bytes);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[nmea0183-out] " << e.what() << '\n';
                udp.reset();
                serial.reset();
            }
            for (int i = 0; running_ && i < 10; ++i) std::this_thread::sleep_for(100ms);
        }
    });
}

void Nmea0183Output::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

}  // namespace boat::nmea0183
