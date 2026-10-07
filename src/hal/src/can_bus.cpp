#include "boat/hal/can_bus.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

namespace boat::hal {

// --- VirtualCanBus ----------------------------------------------------------

void VirtualCanBus::inject(const CanFrame& frame) {
    {
        std::scoped_lock lock(mutex_);
        incoming_.push_back(frame);
    }
    cv_.notify_one();
}

void VirtualCanBus::send(const CanFrame& frame) {
    std::scoped_lock lock(mutex_);
    sent_.push_back(frame);
}

std::optional<CanFrame> VirtualCanBus::receive(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !incoming_.empty(); })) {
        return std::nullopt;
    }
    const CanFrame frame = incoming_.front();
    incoming_.pop_front();
    return frame;
}

std::vector<CanFrame> VirtualCanBus::sent() const {
    std::scoped_lock lock(mutex_);
    return sent_;
}

// --- candump log parsing ----------------------------------------------------

namespace {

std::optional<std::uint32_t> parse_hex(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

std::optional<CandumpRecord> parse_candump_line(std::string_view line) {
    // "(1600000000.123456) can0 123#DEADBEEF"
    if (line.empty() || line.front() != '(') return std::nullopt;
    const auto close = line.find(')');
    if (close == std::string_view::npos) return std::nullopt;

    CandumpRecord record;
    const auto time_text = line.substr(1, close - 1);
    {
        // std::from_chars for double is not available everywhere; use strtod on a copy.
        const std::string copy(time_text);
        char* end = nullptr;
        record.time_s = std::strtod(copy.c_str(), &end);
        if (end == copy.c_str() || *end != '\0') return std::nullopt;
    }

    const auto rest = line.substr(close + 1);
    const auto id_start = rest.find_first_not_of(' ');
    if (id_start == std::string_view::npos) return std::nullopt;
    const auto interface_end = rest.find(' ', id_start);  // skip the interface name
    if (interface_end == std::string_view::npos) return std::nullopt;
    auto frame_text = rest.substr(interface_end + 1);
    while (!frame_text.empty() && (frame_text.back() == '\r' || frame_text.back() == ' ')) {
        frame_text.remove_suffix(1);
    }

    const auto hash = frame_text.find('#');
    if (hash == std::string_view::npos || hash == 0) return std::nullopt;
    const auto id_text = frame_text.substr(0, hash);
    const auto data_text = frame_text.substr(hash + 1);
    if (!data_text.empty() && (data_text.front() == '#' || data_text.front() == 'R')) {
        return std::nullopt;  // CAN FD or remote frame
    }
    if (id_text.size() != 3 && id_text.size() != 8) return std::nullopt;
    const auto id = parse_hex(id_text);
    if (!id) return std::nullopt;
    if (data_text.size() % 2 != 0 || data_text.size() > 16) return std::nullopt;

    record.frame.id = *id;
    record.frame.extended = id_text.size() == 8;
    record.frame.dlc = static_cast<std::uint8_t>(data_text.size() / 2);
    for (std::size_t i = 0; i < data_text.size() / 2; ++i) {
        const auto byte = parse_hex(data_text.substr(i * 2, 2));
        if (!byte) return std::nullopt;
        record.frame.data[i] = static_cast<std::uint8_t>(*byte);
    }
    return record;
}

}  // namespace

std::vector<CandumpRecord> parse_candump(const std::string& text) {
    std::vector<CandumpRecord> records;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (auto record = parse_candump_line(line)) {
            records.push_back(*record);
        }
    }
    return records;
}

// --- CandumpReplayBus ---------------------------------------------------------

CandumpReplayBus::CandumpReplayBus(const std::filesystem::path& file, bool loop, double speed)
    : loop_(loop), speed_(speed > 0.0 ? speed : 1.0) {
    std::ifstream in(file);
    if (!in) throw CanError("cannot open CAN log " + file.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    records_ = parse_candump(buffer.str());
    if (records_.empty()) throw CanError("no CAN frames in " + file.string() + " (expected candump -L format)");
}

CandumpReplayBus::CandumpReplayBus(std::vector<CandumpRecord> records, bool loop, double speed)
    : records_(std::move(records)), loop_(loop), speed_(speed > 0.0 ? speed : 1.0) {
    if (records_.empty()) throw CanError("no CAN frames to replay");
}

std::optional<CanFrame> CandumpReplayBus::receive(std::chrono::milliseconds timeout) {
    const auto now = Clock::now();
    if (!started_) {
        started_ = true;
        cycle_start_ = now;
    }
    if (next_ >= records_.size()) {
        if (!loop_) {
            std::this_thread::sleep_for(timeout);
            return std::nullopt;
        }
        next_ = 0;
        cycle_start_ = now;
    }

    const double offset_s = (records_[next_].time_s - records_.front().time_s) / speed_;
    const auto due = cycle_start_ + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(offset_s));
    if (due <= now) {
        return records_[next_++].frame;
    }
    const auto wait = due - now;
    if (wait > timeout) {
        std::this_thread::sleep_for(timeout);
        return std::nullopt;
    }
    std::this_thread::sleep_for(wait);
    return records_[next_++].frame;
}

}  // namespace boat::hal
