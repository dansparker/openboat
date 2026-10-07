#pragma once

// One GPIO output line via the Linux GPIO character device (/dev/gpiochipN,
// uAPI v2) - no sysfs (deprecated) and no libgpiod dependency.
// On other platforms the constructor throws.

#include <string>

namespace boat::hal {

class GpioOutput {
public:
    // Throws std::system_error / std::runtime_error.
    GpioOutput(const std::string& chip, unsigned line, bool active_low = false, const std::string& consumer = "openboat");
    ~GpioOutput();
    GpioOutput(const GpioOutput&) = delete;
    GpioOutput& operator=(const GpioOutput&) = delete;

    void set(bool active);  // throws std::system_error

private:
    int fd_{-1};
};

}  // namespace boat::hal
