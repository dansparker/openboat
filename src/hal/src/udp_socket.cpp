#include "boat/hal/udp_socket.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <cstring>
#include <stdexcept>
#include <system_error>

namespace boat::hal {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
int last_error() { return WSAGetLastError(); }
void close_native(NativeSocket s) { closesocket(s); }

struct WinsockSession {
    WinsockSession() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockSession() { WSACleanup(); }
};
void ensure_network() { static const WinsockSession session; }
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
int last_error() { return errno; }
void close_native(NativeSocket s) { ::close(s); }
void ensure_network() {}
#endif

NativeSocket native(std::intptr_t handle) { return static_cast<NativeSocket>(handle); }

[[noreturn]] void fail(const char* what) {
    throw std::system_error(last_error(), std::system_category(), what);
}

sockaddr_in make_address(const std::string& host, std::uint16_t port) {
    ensure_network();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        throw std::runtime_error("cannot resolve host '" + host + "'");
    }
    sockaddr_in address{};
    std::memcpy(&address, result->ai_addr, sizeof(address));
    freeaddrinfo(result);
    address.sin_port = htons(port);
    return address;
}

std::string to_string(const in_addr& address) {
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &address, text, sizeof(text));
    return text;
}

}  // namespace

std::string resolve_ipv4(const std::string& host) { return to_string(make_address(host, 0).sin_addr); }

UdpSocket::UdpSocket() {
    ensure_network();
    const NativeSocket s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidSocket) {
        fail("socket");
    }
    // Broadcast: NMEA over WLAN is usually sent to 255.255.255.255:10110.
    // Reuse: other programs (e.g. OpenCPN) may listen on the same port.
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), static_cast<socklen_t>(sizeof on));
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), static_cast<socklen_t>(sizeof on));
    handle_ = static_cast<std::intptr_t>(s);
}

UdpSocket::~UdpSocket() { close_native(native(handle_)); }

void UdpSocket::bind(std::uint16_t port, const std::string& address) {
    const sockaddr_in local = make_address(address, port);
    if (::bind(native(handle_), reinterpret_cast<const sockaddr*>(&local), static_cast<socklen_t>(sizeof(local))) != 0) {
        fail("bind");
    }
}

std::uint16_t UdpSocket::local_port() const {
    sockaddr_in local{};
    socklen_t length = sizeof(local);
    if (::getsockname(native(handle_), reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        fail("getsockname");
    }
    return ntohs(local.sin_port);
}

void UdpSocket::send_to(const Endpoint& to, std::span<const std::byte> data) {
    const sockaddr_in remote = make_address(to.host, to.port);
#ifdef _WIN32
    const int sent = ::sendto(native(handle_), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()),
                              0, reinterpret_cast<const sockaddr*>(&remote), static_cast<int>(sizeof(remote)));
#else
    const auto sent = ::sendto(native(handle_), data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&remote),
                               static_cast<socklen_t>(sizeof(remote)));
#endif
    if (sent < 0) {
        fail("sendto");
    }
}

std::optional<Datagram> UdpSocket::receive_from(std::span<std::byte> buffer, std::chrono::milliseconds timeout) {
#ifdef _WIN32
    WSAPOLLFD request{};
    request.fd = native(handle_);
    request.events = POLLRDNORM;
    const int ready = WSAPoll(&request, 1, static_cast<int>(timeout.count()));
#else
    pollfd request{};
    request.fd = native(handle_);
    request.events = POLLIN;
    const int ready = ::poll(&request, 1, static_cast<int>(timeout.count()));
#endif
    if (ready < 0) {
        fail("poll");
    }
    if (ready == 0) {
        return std::nullopt;
    }

    sockaddr_in from{};
    socklen_t from_length = sizeof(from);
#ifdef _WIN32
    const int received = ::recvfrom(native(handle_), reinterpret_cast<char*>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
    if (received < 0) {
        const int error = WSAGetLastError();
        // ICMP "port unreachable" from an earlier send (peer not running yet)
        if (error == WSAECONNRESET) return std::nullopt;
        if (error != WSAEMSGSIZE) fail("recvfrom");
        return Datagram{buffer.size(), Endpoint{to_string(from.sin_addr), ntohs(from.sin_port)}};
    }
#else
    const auto received =
        ::recvfrom(native(handle_), buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&from), &from_length);
    if (received < 0) {
        if (errno == EINTR || errno == ECONNREFUSED) return std::nullopt;
        fail("recvfrom");
    }
#endif
    return Datagram{static_cast<std::size_t>(received), Endpoint{to_string(from.sin_addr), ntohs(from.sin_port)}};
}

}  // namespace boat::hal
