#pragma once

// A Module is any self-contained unit that talks to the rest of the system
// exclusively through the DataBus: sensor drivers, simulators, alerting,
// data logging, autopilot interface, ...

#include <string_view>

namespace boat::core {

class DataBus;

class Module {
public:
    virtual ~Module() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;

    // Called once; the module may spawn worker threads and publish from them.
    virtual void start(DataBus& bus) = 0;

    // Must be idempotent and block until all worker threads have finished.
    virtual void stop() = 0;
};

}  // namespace boat::core
