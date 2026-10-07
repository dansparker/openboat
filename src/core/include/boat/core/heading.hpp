#pragma once

// Magnetic compass heading -> Heading on the bus. The variation sent by the
// device wins; without one the World Magnetic Model value from the bus is
// used (a few minutes old at most). With neither the heading stays magnetic.

#include <chrono>
#include <optional>

#include "boat/core/data_bus.hpp"
#include "boat/core/marine_data.hpp"
#include "boat/core/nav_math.hpp"

namespace boat::core {

[[nodiscard]] inline Heading magnetic_heading(double magnetic_deg, std::optional<double> device_variation_deg,
                                              DataBus& bus) {
    Heading h;
    h.variation_deg = device_variation_deg;
    if (!h.variation_deg) {
        if (const auto m = bus.latest<MagneticVariation>(); m && is_fresh(*m, std::chrono::minutes(5))) {
            h.variation_deg = m->value.variation_deg;
            h.variation_from_model = true;
        }
    }
    h.is_true = h.variation_deg.has_value();
    h.heading_deg = normalize_deg(magnetic_deg + h.variation_deg.value_or(0.0));
    return h;
}

}  // namespace boat::core
