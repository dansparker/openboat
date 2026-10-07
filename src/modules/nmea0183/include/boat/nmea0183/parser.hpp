#pragma once

// NMEA 0183 sentence parser. Validates the checksum and publishes the
// decoded values on the DataBus.
//
// Supported: RMC GGA VTG HDT HDG HDM DPT DBT MWV VHW MTW, !VDM (AIS).
// !VDO (own ship's AIS) is deliberately ignored, otherwise the own boat
// would appear as a target right under the boat symbol.

#include <optional>
#include <string_view>

#include "boat/core/data_bus.hpp"
#include "boat/nmea0183/ais.hpp"

namespace boat::nmea0183 {

struct ParserOptions {
    // Applied to DBT (and to DPT when the sentence has no offset).
    // > 0: depth below waterline, < 0: depth below keel. See core::Depth.
    double depth_offset_m = 0.0;
    // Sentences without a checksum are rejected unless this is set
    // (some cheap GPS mice omit it).
    bool accept_missing_checksum = false;
};

struct ParserStats {
    unsigned long accepted = 0;
    unsigned long bad_checksum = 0;
    unsigned long unsupported = 0;
    unsigned long malformed = 0;
};

// True if the line carries a valid "*hh" checksum (or none and allowed).
[[nodiscard]] bool checksum_ok(std::string_view line, bool accept_missing);

class Parser {
public:
    explicit Parser(core::DataBus& bus, ParserOptions options = {}) : bus_(bus), options_(options) {}

    // One line, with or without trailing CR/LF. Returns true if it was
    // understood and published.
    bool feed(std::string_view line);

    [[nodiscard]] const ParserStats& stats() const { return stats_; }

private:
    core::DataBus& bus_;
    ParserOptions options_;
    ParserStats stats_;
    AisDecoder ais_;
    std::optional<double> last_variation_deg_;
};

}  // namespace boat::nmea0183
