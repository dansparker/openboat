#pragma once

// AIS decoder for !AIVDM sentences (ITU-R M.1371).
// Supported message types: 1/2/3 (class A position), 5 (class A static),
// 18/19 (class B position), 24 (class B static, parts A and B).
// Multi-sentence messages are reassembled per sequential message id.

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "boat/core/marine_data.hpp"

namespace boat::nmea0183 {

class AisDecoder {
public:
    // Feeds the comma-separated fields of one VDM/VDO sentence (without the
    // talker/type field and without the checksum). Returns a report when a
    // complete message was decoded.
    std::optional<core::AisReport> feed(int fragment_count, int fragment_number, std::string_view sequence_id,
                                        char channel, std::string_view payload, int fill_bits);

    // Decodes a complete, de-armored payload (exposed for tests).
    [[nodiscard]] static std::optional<core::AisReport> decode_payload(std::string_view payload, int fill_bits);

private:
    struct Pending {
        int expected = 0;
        int next = 1;
        std::string payload;
        std::chrono::steady_clock::time_point started;
    };
    std::map<std::string, Pending> pending_;
};

}  // namespace boat::nmea0183
