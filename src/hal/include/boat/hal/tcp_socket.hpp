#pragma once

// Minimal cross-platform (POSIX / Winsock) IPv4 TCP client socket.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace boat::hal {

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Throws std::runtime_error / std::system_error when the connection fails or times out.
    void connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout);
    [[nodiscard]] bool connected() const;
    void close();

    void send_all(std::span<const std::byte> data);  // throws std::system_error

    // Waits up to `timeout`: nullopt on timeout, 0 when the peer closed the connection.
    [[nodiscard]] std::optional<std::size_t> receive(std::span<std::byte> buffer, std::chrono::milliseconds timeout);

private:
    std::intptr_t handle_{-1};
};

}  // namespace boat::hal
