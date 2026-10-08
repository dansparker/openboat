#pragma once

// AIS decoder for !AIVDM sentences (ITU-R M.1371).
// Supported message types: 1/2/3 (class A position), 4 (base station),
// 5 (class A static), 12/14 (safety messages), 18/19 (class B position),
// 21 (aid to navigation), 24 (class B static, parts A and B).
// Multi-sentence messages are reassembled per sequential message id.

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "boat/core/marine_data.hpp"

namespace boat::nmea0183 {

// One decoded message: a report about a station or a safety message
struct AisMessage {
    std::optional<core::AisReport> report;
    std::optional<core::AisSafetyMessage> safety;
};

class AisDecoder {
public:
    // Feeds the comma-separated fields of one VDM/VDO sentence (without the
    // talker/type field and without the checksum). Returns a report when a
    // complete message was decoded.
    std::optional<AisMessage> feed(int fragment_count, int fragment_number, std::string_view sequence_id,
                                   char channel, std::string_view payload, int fill_bits);

    // Decodes a complete, de-armored payload (exposed for tests).
    [[nodiscard]] static std::optional<AisMessage> decode_message(std::string_view payload, int fill_bits);
    // Station reports only (kept for the tests of the position/static types)
    [[nodiscard]] static std::optional<core::AisReport> decode_payload(std::string_view payload, int fill_bits) {
        const auto m = decode_message(payload, fill_bits);
        return m ? m->report : std::nullopt;
    }

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
