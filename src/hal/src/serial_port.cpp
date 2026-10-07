#include "boat/hal/serial_port.hpp"

#include <stdexcept>
#include <system_error>

#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace boat::hal {

#ifdef __linux__

namespace {

speed_t to_speed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: throw std::runtime_error("serial: unsupported baud rate " + std::to_string(baud));
    }
}

}  // namespace

SerialPort::SerialPort(const std::string& device, int baud) {
    const speed_t speed = to_speed(baud);
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) throw std::system_error(errno, std::system_category(), "open " + device);

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        const int error = errno;
        ::close(fd_);
        throw std::system_error(error, std::system_category(), "tcgetattr " + device);
    }
    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB | CRTSCTS | PARENB);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        const int error = errno;
        ::close(fd_);
        throw std::system_error(error, std::system_category(), "tcsetattr " + device);
    }
    tcflush(fd_, TCIFLUSH);
}

SerialPort::~SerialPort() {
    if (fd_ >= 0) ::close(fd_);
}

std::optional<std::size_t> SerialPort::read(std::span<std::byte> buffer, std::chrono::milliseconds timeout) {
    pollfd fd{};
    fd.fd = fd_;
    fd.events = POLLIN;
    const int ready = ::poll(&fd, 1, static_cast<int>(timeout.count()));
    if (ready < 0) throw std::system_error(errno, std::system_category(), "serial poll");
    if (ready == 0) return std::nullopt;
    if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) throw std::runtime_error("serial device lost");
    const auto n = ::read(fd_, buffer.data(), buffer.size());
    if (n < 0) throw std::system_error(errno, std::system_category(), "serial read");
    return static_cast<std::size_t>(n);
}

void SerialPort::write_all(std::span<const std::byte> data) {
    std::size_t done = 0;
    while (done < data.size()) {
        const auto n = ::write(fd_, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            throw std::system_error(errno, std::system_category(), "serial write");
        }
        done += static_cast<std::size_t>(n);
    }
}

#else

void SerialPort::write_all(std::span<const std::byte> /*data*/) {}

SerialPort::SerialPort(const std::string& device, int /*baud*/) {
    throw std::runtime_error("serial port " + device + ": only supported on Linux");
}

SerialPort::~SerialPort() = default;

std::optional<std::size_t> SerialPort::read(std::span<std::byte> /*buffer*/, std::chrono::milliseconds /*timeout*/) {
    return std::nullopt;
}

#endif

}  // namespace boat::hal
