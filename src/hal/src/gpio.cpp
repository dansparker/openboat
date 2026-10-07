#include "boat/hal/gpio.hpp"

#include <stdexcept>
#include <system_error>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace boat::hal {

#if defined(__linux__)

GpioOutput::GpioOutput(const std::string& chip, unsigned line, bool active_low, const std::string& consumer) {
    const int chip_fd = ::open(chip.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) throw std::system_error(errno, std::generic_category(), "open " + chip);

    gpio_v2_line_request req{};
    req.offsets[0] = line;
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT | (active_low ? GPIO_V2_LINE_FLAG_ACTIVE_LOW : 0);
    // Start inactive: the buzzer must not sound while the system boots
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 0;
    req.config.attrs[0].mask = 1;
    std::strncpy(req.consumer, consumer.c_str(), sizeof(req.consumer) - 1);

    const int rc = ::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    const int err = errno;
    ::close(chip_fd);
    if (rc < 0) throw std::system_error(err, std::generic_category(), "request GPIO line " + std::to_string(line));
    fd_ = req.fd;
}

GpioOutput::~GpioOutput() {
    if (fd_ >= 0) {
        try {
            set(false);
        } catch (...) {
        }
        ::close(fd_);
    }
}

void GpioOutput::set(bool active) {
    gpio_v2_line_values values{};
    values.mask = 1;
    values.bits = active ? 1 : 0;
    if (::ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        throw std::system_error(errno, std::generic_category(), "set GPIO");
    }
}

#else  // not Linux

GpioOutput::GpioOutput(const std::string&, unsigned, bool, const std::string&) {
    throw std::runtime_error("GPIO is only available on Linux");
}
GpioOutput::~GpioOutput() = default;
void GpioOutput::set(bool) {}

#endif

}  // namespace boat::hal
