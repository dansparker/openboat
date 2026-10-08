#pragma once

// NMEA 0183 data source module: reads sentences from UDP, TCP, a serial
// port or a log file (replay) and feeds them to the Parser.
//
// Typical setups:
//   udp    WLAN multiplexer / Signal K server broadcasting on port 10110
//   tcp    multiplexer serving NMEA on a TCP port (10110 or 2000)
//   serial USB GNSS / AIS receiver, RS-422 via USB adapter (4800 or 38400 Bd)
//   file   recorded log, replayed with its original timing when the lines carry
//          a time tag block (OpenBoat recordings), otherwise at `lines_per_second`
//
// With `record_dir` set, every received line is recorded (prefixed with a
// "\c:<time>*hh\" tag block) while RecordCommand{on = true} is active.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "boat/core/module.hpp"
#include "boat/nmea0183/parser.hpp"

namespace boat::nmea0183 {

struct SourceConfig {
    enum class Kind { Udp, Tcp, Serial, File } kind = Kind::Udp;
    std::string host;           // tcp
    std::uint16_t port = 10110; // udp, tcp
    std::string device;         // serial
    int baud = 4800;            // serial
    std::string path;           // file
    double lines_per_second = 20.0;  // file
    bool loop = true;                // file
    std::string record_dir;          // empty: no recording
    ParserOptions parser;
};

// Splits a byte stream into lines (CR, LF or CRLF); overlong garbage is
// dropped instead of growing without bound.
class LineAssembler {
public:
    template <typename OnLine>
    void push(std::string_view chunk, OnLine&& on_line) {
        for (const char c : chunk) {
            if (c == '\r' || c == '\n') {
                if (!line_.empty()) on_line(std::string_view(line_));
                line_.clear();
            } else if (line_.size() < kMaxLine) {
                line_.push_back(c);
            }
        }
    }

private:
    static constexpr std::size_t kMaxLine = 1024;
    std::string line_;
};

class Nmea0183Source final : public core::Module {
public:
    Nmea0183Source(std::string name, SourceConfig config);
    ~Nmea0183Source() override;

    [[nodiscard]] std::string_view name() const override { return name_; }
    void start(core::DataBus& bus) override;
    void stop() override;

private:
    void run(core::DataBus& bus);

    std::string name_;
    SourceConfig config_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace boat::nmea0183
