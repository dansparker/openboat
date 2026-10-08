#include "boat/core/daily_log.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <utility>

namespace boat::core {

std::string utc_day(std::int64_t unix_ms) {
    using namespace std::chrono;
    const year_month_day d{floor<days>(sys_time<milliseconds>(milliseconds(unix_ms)))};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(d.year()), static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

std::string nmea_time_tag(std::int64_t unix_ms) {
    const std::string body = "c:" + std::to_string(unix_ms / 1000);
    unsigned char sum = 0;
    for (const char c : body) sum ^= static_cast<unsigned char>(c);
    char hex[4];
    std::snprintf(hex, sizeof hex, "%02X", sum);
    return "\\" + body + "*" + hex + "\\";
}

std::int64_t nmea_tag_time_ms(std::string_view line) {
    if (line.size() < 4 || line[0] != '\\') return -1;
    const auto end = line.find('\\', 1);
    if (end == std::string_view::npos) return -1;
    std::string_view tag = line.substr(1, end - 1);
    if (const auto star = tag.find('*'); star != std::string_view::npos) tag = tag.substr(0, star);
    // fields "s:...,c:...,n:..."
    while (!tag.empty()) {
        const auto comma = tag.find(',');
        const std::string_view field = tag.substr(0, comma);
        if (field.size() > 2 && field.substr(0, 2) == "c:") {
            std::int64_t v = 0;
            const auto r = std::from_chars(field.data() + 2, field.data() + field.size(), v);
            if (r.ec != std::errc{} || v <= 0) return -1;
            return v > 100'000'000'000 ? v : v * 1000;  // some devices send milliseconds
        }
        if (comma == std::string_view::npos) break;
        tag.remove_prefix(comma + 1);
    }
    return -1;
}

DailyLog::DailyLog(std::filesystem::path dir, std::string prefix, std::string extension, std::uintmax_t max_bytes)
    : dir_(std::move(dir)), prefix_(std::move(prefix)), extension_(std::move(extension)), max_bytes_(max_bytes) {}

bool DailyLog::write(std::string_view line, std::int64_t unix_ms) {
    const std::string day = utc_day(unix_ms);
    if (day != day_ || !out_.is_open()) {
        close();
        day_ = day;
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        path_ = dir_ / (prefix_ + "-" + day + "." + extension_);
        written_ = std::filesystem::exists(path_, ec) ? std::filesystem::file_size(path_, ec) : 0;
        out_.open(path_, std::ios::app | std::ios::binary);
        if (!out_) return false;
    }
    if (written_ + line.size() + 1 > max_bytes_) return false;
    out_ << line << '\n';
    written_ += line.size() + 1;
    // Flush every few seconds: a power cut loses little, the SD card is not hammered
    if (unix_ms - last_flush_ms_ > 5000) {
        out_.flush();
        last_flush_ms_ = unix_ms;
    }
    return static_cast<bool>(out_);
}

void DailyLog::close() {
    if (out_.is_open()) out_.close();
}

}  // namespace boat::core
