#pragma once

// Minimal cross-platform (POSIX / Winsock) IPv4 UDP socket.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace boat::hal {

struct Endpoint {
    std::string host;  // dotted IPv4 address or host name
    std::uint16_t port{};

    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

struct Datagram {
    std::size_t size{};
    Endpoint from;
};

// Resolves a host name to a dotted IPv4 address. Throws std::runtime_error.
[[nodiscard]] std::string resolve_ipv4(const std::string& host);

class UdpSocket {
public:
    UdpSocket();  // throws std::system_error
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Port 0 = ephemeral port chosen by the OS.
    void bind(std::uint16_t port = 0, const std::string& address = "0.0.0.0");
    [[nodiscard]] std::uint16_t local_port() const;

    void send_to(const Endpoint& to, std::span<const std::byte> data);

    // Waits up to `timeout`; nullopt on timeout. Datagrams larger than the
    // buffer are truncated.
    [[nodiscard]] std::optional<Datagram> receive_from(std::span<std::byte> buffer,
                                                       std::chrono::milliseconds timeout);

private:
    std::intptr_t handle_;
};

}  // namespace boat::hal
