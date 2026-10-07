#include "boat/hal/can_bus.hpp"

#if defined(__linux__)

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace boat::hal {

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw CanError(what + ": " + std::strerror(errno));
}

}  // namespace

SocketCanBus::SocketCanBus(const std::string& interface_name) {
    if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
        throw CanError("invalid CAN interface name '" + interface_name + "'");
    }
    fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd_ < 0) fail("cannot open CAN socket");

    ifreq request{};
    std::strncpy(request.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd_, SIOCGIFINDEX, &request) < 0) {
        const int saved = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved;
        fail("unknown CAN interface '" + interface_name + "'");
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        const int saved = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved;
        fail("cannot bind to CAN interface '" + interface_name + "'");
    }
}

SocketCanBus::~SocketCanBus() {
    if (fd_ >= 0) ::close(fd_);
}

void SocketCanBus::send(const CanFrame& frame) {
    can_frame out{};
    out.can_id = frame.extended ? (frame.id & CAN_EFF_MASK) | CAN_EFF_FLAG : (frame.id & CAN_SFF_MASK);
    out.can_dlc = std::min<std::uint8_t>(frame.dlc, 8);
    std::memcpy(out.data, frame.data.data(), out.can_dlc);
    if (::write(fd_, &out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
        fail("CAN send failed");
    }
}

std::optional<CanFrame> SocketCanBus::receive(std::chrono::milliseconds timeout) {
    pollfd descriptor{};
    descriptor.fd = fd_;
    descriptor.events = POLLIN;
    const int ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready < 0) {
        if (errno == EINTR) return std::nullopt;
        fail("CAN poll failed");
    }
    if (ready == 0) return std::nullopt;

    can_frame in{};
    const ssize_t size = ::read(fd_, &in, sizeof(in));
    if (size < 0) {
        if (errno == EINTR || errno == EAGAIN) return std::nullopt;
        fail("CAN read failed");
    }
    if (size != static_cast<ssize_t>(sizeof(in))) return std::nullopt;
    if ((in.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG)) != 0) return std::nullopt;  // error / remote frames

    CanFrame frame;
    frame.extended = (in.can_id & CAN_EFF_FLAG) != 0;
    frame.id = in.can_id & (frame.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    frame.dlc = std::min<std::uint8_t>(in.can_dlc, 8);
    std::memcpy(frame.data.data(), in.data, frame.dlc);
    return frame;
}

}  // namespace boat::hal

#else  // not Linux

namespace boat::hal {

SocketCanBus::SocketCanBus(const std::string&) { throw CanError("SocketCAN is only available on Linux"); }
SocketCanBus::~SocketCanBus() = default;
void SocketCanBus::send(const CanFrame&) { throw CanError("SocketCAN is only available on Linux"); }
std::optional<CanFrame> SocketCanBus::receive(std::chrono::milliseconds) {
    throw CanError("SocketCAN is only available on Linux");
}

}  // namespace boat::hal

#endif
