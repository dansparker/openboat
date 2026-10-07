#pragma once

// Serial port (UART), raw 8N1 without flow control. Linux only (termios);
// on other systems the constructor throws.

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace boat::hal {

class SerialPort {
public:
    // Opens e.g. "/dev/serial0" at one of the standard rates (9600 ... 921600).
    // Throws std::system_error / std::runtime_error.
    SerialPort(const std::string& device, int baud);
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Waits up to `timeout`: nullopt on timeout. Throws when the device is gone.
    [[nodiscard]] std::optional<std::size_t> read(std::span<std::byte> buffer, std::chrono::milliseconds timeout);

private:
    int fd_{-1};
};

}  // namespace boat::hal
