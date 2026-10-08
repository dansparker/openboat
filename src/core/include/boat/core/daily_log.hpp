#pragma once

// Raw data recorder (NMEA 0183 sentences, NMEA 2000 frames) for later replay
// and for finding out what an instrument really sends. One file per UTC day
// "<dir>/<prefix>-YYYY-MM-DD.<ext>", appended line by line; a daily size cap
// protects the SD card from a chatty bus filling it up.
//
// Switched on and off at runtime with RecordCommand on the bus.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace boat::core {

// UI -> data sources: start / stop recording raw data
struct RecordCommand {
    bool on = false;
};

class DailyLog {
public:
    DailyLog(std::filesystem::path dir, std::string prefix, std::string extension,
             std::uintmax_t max_bytes_per_day = 200ULL * 1024 * 1024);

    // Appends one line (newline added). Returns false when not written
    // (directory not writable, daily cap reached).
    bool write(std::string_view line, std::int64_t unix_ms);
    void close();

    [[nodiscard]] std::filesystem::path current_file() const { return path_; }

private:
    std::filesystem::path dir_;
    std::string prefix_;
    std::string extension_;
    std::uintmax_t max_bytes_;
    std::string day_;
    std::filesystem::path path_;
    std::ofstream out_;
    std::uintmax_t written_ = 0;
    std::int64_t last_flush_ms_ = 0;
};

// "YYYY-MM-DD" (UTC) of a Unix time in ms
[[nodiscard]] std::string utc_day(std::int64_t unix_ms);

// NMEA 0183 tag block with the receive time: "\c:1696789012*hh\" (seconds, IEC 61162-450)
[[nodiscard]] std::string nmea_time_tag(std::int64_t unix_ms);

// Receive time from a tag block at the start of a line, in ms (nullopt-like: -1)
[[nodiscard]] std::int64_t nmea_tag_time_ms(std::string_view line);

}  // namespace boat::core
