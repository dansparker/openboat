#include "boat/hal/tcp_socket.hpp"

#include "boat/hal/udp_socket.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
int poll_native(pollfd* fds, unsigned long n, int timeout_ms) { return WSAPoll(fds, n, timeout_ms); }
void set_blocking(NativeSocket s, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &mode);
}
bool connect_in_progress(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
int last_error() { return errno; }
void close_native(NativeSocket s) { ::close(s); }
int poll_native(pollfd* fds, nfds_t n, int timeout_ms) { return ::poll(fds, n, timeout_ms); }
void set_blocking(NativeSocket s, bool blocking) {
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}
bool connect_in_progress(int error) { return error == EINPROGRESS; }
#endif

NativeSocket native(std::intptr_t handle) { return static_cast<NativeSocket>(handle); }

[[noreturn]] void fail(const char* what) { throw std::system_error(last_error(), std::system_category(), what); }

int to_ms(std::chrono::milliseconds timeout) { return static_cast<int>(timeout.count()); }

}  // namespace

TcpSocket::~TcpSocket() { close(); }

bool TcpSocket::connected() const { return native(handle_) != kInvalidSocket && handle_ != -1; }

void TcpSocket::close() {
    if (connected()) close_native(native(handle_));
    handle_ = -1;
}

void TcpSocket::connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
    close();
    const std::string address_text = resolve_ipv4(host);  // also starts Winsock
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, address_text.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid address '" + address_text + "'");
    }

    const NativeSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket) fail("socket");
    handle_ = static_cast<std::intptr_t>(s);

    set_blocking(s, false);
    if (::connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (!connect_in_progress(last_error())) {
            const int error = last_error();
            close();
            throw std::system_error(error, std::system_category(), "connect");
        }
        pollfd fd{};
        fd.fd = s;
        fd.events = POLLOUT;
        const int ready = poll_native(&fd, 1, to_ms(timeout));
        int error = 0;
        socklen_t length = sizeof(error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        if (ready <= 0 || error != 0) {
            close();
            if (ready <= 0) throw std::runtime_error("connect to " + host + ": timeout");
            throw std::system_error(error, std::system_category(), "connect");
        }
    }
    set_blocking(s, true);
    const int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
}

void TcpSocket::send_all(std::span<const std::byte> data) {
    if (!connected()) throw std::runtime_error("send: not connected");
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto n = ::send(native(handle_), reinterpret_cast<const char*>(data.data() + sent),
                              static_cast<int>(data.size() - sent), 0);
        if (n <= 0) fail("send");
        sent += static_cast<std::size_t>(n);
    }
}

std::optional<std::size_t> TcpSocket::receive(std::span<std::byte> buffer, std::chrono::milliseconds timeout) {
    if (!connected()) throw std::runtime_error("receive: not connected");
    pollfd fd{};
    fd.fd = native(handle_);
    fd.events = POLLIN;
    const int ready = poll_native(&fd, 1, to_ms(timeout));
    if (ready < 0) fail("poll");
    if (ready == 0) return std::nullopt;
    const auto n = ::recv(native(handle_), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (n < 0) fail("recv");
    return static_cast<std::size_t>(n);
}

}  // namespace boat::hal
