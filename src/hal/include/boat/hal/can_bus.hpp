#pragma once

// CAN bus abstraction: classic CAN frames (up to 8 data bytes).
//
//   SocketCanBus      Linux SocketCAN interface (can0, vcan0, ...)
//   VirtualCanBus     in-memory bus for unit tests
//   CandumpReplayBus  replays a `candump -L` log with its original timing

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace boat::hal {

struct CanFrame {
    std::uint32_t id{};  // 11 bit (standard) or 29 bit (extended) identifier
    bool extended{false};
    std::uint8_t dlc{};  // number of valid bytes in `data`, 0..8
    std::array<std::uint8_t, 8> data{};

    friend bool operator==(const CanFrame&, const CanFrame&) = default;
};

class CanError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CanBus {
public:
    virtual ~CanBus() = default;

    // Throws CanError when the frame cannot be sent.
    virtual void send(const CanFrame& frame) = 0;

    // Waits up to `timeout`; nullopt on timeout. Throws CanError on bus failure.
    [[nodiscard]] virtual std::optional<CanFrame> receive(std::chrono::milliseconds timeout) = 0;
};

// Linux SocketCAN (raw socket on a named interface). Throws CanError on other platforms.
class SocketCanBus final : public CanBus {
public:
    explicit SocketCanBus(const std::string& interface_name);
    ~SocketCanBus() override;

    SocketCanBus(const SocketCanBus&) = delete;
    SocketCanBus& operator=(const SocketCanBus&) = delete;

    void send(const CanFrame& frame) override;
    [[nodiscard]] std::optional<CanFrame> receive(std::chrono::milliseconds timeout) override;

private:
    int fd_{-1};
};

// In-memory bus: frames passed to inject() are returned by receive();
// frames passed to send() are recorded and available through sent().
class VirtualCanBus final : public CanBus {
public:
    void inject(const CanFrame& frame);

    void send(const CanFrame& frame) override;
    [[nodiscard]] std::optional<CanFrame> receive(std::chrono::milliseconds timeout) override;

    [[nodiscard]] std::vector<CanFrame> sent() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<CanFrame> incoming_;
    std::vector<CanFrame> sent_;
};

struct CandumpRecord {
    double time_s{};  // timestamp from the log
    CanFrame frame;
};

// Parses the `candump -L` format: "(1600000000.123456) can0 123#DEADBEEF".
// Lines that do not match (comments, error frames, CAN FD) are skipped.
[[nodiscard]] std::vector<CandumpRecord> parse_candump(const std::string& text);

// Plays a recorded log back in real time (scaled by `speed`). send() is ignored.
class CandumpReplayBus final : public CanBus {
public:
    // Throws CanError if the file cannot be read or contains no frames.
    CandumpReplayBus(const std::filesystem::path& file, bool loop = true, double speed = 1.0);
    // For tests: records directly.
    CandumpReplayBus(std::vector<CandumpRecord> records, bool loop, double speed);

    void send(const CanFrame&) override {}
    [[nodiscard]] std::optional<CanFrame> receive(std::chrono::milliseconds timeout) override;

private:
    using Clock = std::chrono::steady_clock;

    std::vector<CandumpRecord> records_;
    bool loop_;
    double speed_;
    std::size_t next_{0};
    bool started_{false};
    Clock::time_point cycle_start_{};
};

}  // namespace boat::hal
