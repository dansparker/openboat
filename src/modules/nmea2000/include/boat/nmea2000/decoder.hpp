#pragma once

// NMEA 2000 receiver: CAN identifier parsing, fast-packet reassembly and
// decoding of the navigation PGNs into DataBus types.
//
// The decoder is LISTEN-ONLY. A device that transmits on an NMEA 2000 bus
// must take part in address claiming (ISO 11783-5); a half-done
// implementation can disturb certified equipment such as the autopilot.
// Transmitting is therefore a separate, later step (see docs/adr/0002).

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "boat/core/data_bus.hpp"
#include "boat/hal/can_bus.hpp"

namespace boat::nmea2000 {

struct CanId {
    std::uint8_t priority = 0;
    std::uint32_t pgn = 0;
    std::uint8_t source = 0;
    std::uint8_t destination = 0xFF;  // 0xFF = global (PDU2 or broadcast)
};

[[nodiscard]] CanId parse_can_id(std::uint32_t id);

// PGNs that are sent as fast packets (multi-frame, up to 223 bytes).
[[nodiscard]] bool is_fast_packet(std::uint32_t pgn);

struct Message {
    CanId id;
    std::vector<std::uint8_t> data;
};

class FastPacketAssembler {
public:
    // Returns the complete message when the last frame arrives.
    std::optional<Message> push(const hal::CanFrame& frame);

private:
    struct Pending {
        std::uint8_t sequence = 0;
        std::uint8_t next_frame = 0;
        std::size_t length = 0;
        std::vector<std::uint8_t> data;
    };
    std::map<std::pair<std::uint8_t, std::uint32_t>, Pending> pending_;
};

struct DecoderOptions {
    double depth_offset_m = 0.0;  // used when PGN 128267 carries no offset
};

class Decoder {
public:
    explicit Decoder(core::DataBus& bus, DecoderOptions options = {}) : bus_(bus), options_(options) {}

    // Feeds one received frame; returns true if a supported PGN was published.
    bool feed(const hal::CanFrame& frame);
    // Decodes a complete (reassembled) message.
    bool decode(const Message& message);

private:
    core::DataBus& bus_;
    std::optional<double> last_variation_deg_;  // PGN 127258
    DecoderOptions options_;
    FastPacketAssembler fast_;
};

}  // namespace boat::nmea2000
