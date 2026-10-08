#include "boat/nmea0183/ais.hpp"

#include <algorithm>
#include <vector>

#include "boat/core/nav_math.hpp"

namespace boat::nmea0183 {

namespace {

class Bits {
public:
    Bits(std::string_view payload, int fill_bits) {
        bits_.reserve(payload.size() * 6);
        for (const char c : payload) {
            int v = static_cast<unsigned char>(c) - 48;
            if (v > 40) v -= 8;
            for (int b = 5; b >= 0; --b) bits_.push_back(((v >> b) & 1) != 0);
        }
        const auto drop = static_cast<std::size_t>(fill_bits < 0 || fill_bits > 5 ? 0 : fill_bits);
        if (drop <= bits_.size()) bits_.resize(bits_.size() - drop);
    }

    [[nodiscard]] std::size_t size() const { return bits_.size(); }

    [[nodiscard]] std::uint32_t u(std::size_t start, std::size_t len) const {
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < len; ++i) {
            v = (v << 1) | (start + i < bits_.size() && bits_[start + i] ? 1U : 0U);
        }
        return v;
    }

    [[nodiscard]] std::int32_t s(std::size_t start, std::size_t len) const {
        const std::uint32_t v = u(start, len);
        if (len < 32 && (v & (1U << (len - 1))) != 0) {
            return static_cast<std::int32_t>(static_cast<std::int64_t>(v) - (std::int64_t{1} << len));
        }
        return static_cast<std::int32_t>(v);
    }

    [[nodiscard]] std::string text(std::size_t start, std::size_t chars) const {
        std::string out;
        for (std::size_t i = 0; i < chars; ++i) {
            const std::uint32_t c = u(start + i * 6, 6);
            out.push_back(static_cast<char>(c < 32 ? c + 64 : c));
        }
        // '@' is padding in AIS text fields
        while (!out.empty() && (out.back() == '@' || out.back() == ' ')) out.pop_back();
        return out;
    }

private:
    std::vector<bool> bits_;
};

constexpr double kMpsPerKnot = core::kMpsPerKnot;

void position(core::AisReport& r, const Bits& b, std::size_t lon_at, std::size_t lat_at) {
    const std::int32_t lon = b.s(lon_at, 28);
    const std::int32_t lat = b.s(lat_at, 27);
    // 181 deg / 91 deg = not available (units: 1/10000 minute)
    if (lon != 181 * 600000 && lat != 91 * 600000) {
        const double lon_deg = lon / 600000.0;
        const double lat_deg = lat / 600000.0;
        if (lon_deg >= -180.0 && lon_deg <= 180.0 && lat_deg >= -90.0 && lat_deg <= 90.0) {
            r.position = core::GeoPoint{lat_deg, lon_deg};
        }
    }
}

void motion(core::AisReport& r, const Bits& b, std::size_t sog_at, std::size_t cog_at, std::size_t hdg_at) {
    const std::uint32_t sog = b.u(sog_at, 10);
    if (sog != 1023) r.sog_mps = sog / 10.0 * kMpsPerKnot;
    const std::uint32_t cog = b.u(cog_at, 12);
    if (cog < 3600) r.cog_deg = cog / 10.0;
    const std::uint32_t hdg = b.u(hdg_at, 9);
    if (hdg < 360) r.heading_deg = hdg;
}

void dimensions(core::AisReport& r, const Bits& b, std::size_t at) {
    const auto bow = b.u(at, 9);
    const auto stern = b.u(at + 9, 9);
    const auto port = b.u(at + 18, 6);
    const auto starboard = b.u(at + 24, 6);
    if (bow + stern > 0) r.length_m = bow + stern;
    if (port + starboard > 0) r.beam_m = port + starboard;
}

void set_text(std::optional<std::string>& field, std::string value) {
    if (!value.empty()) field = std::move(value);
}

}  // namespace

namespace {

std::optional<core::AisReport> decode_report(const Bits& b, std::uint32_t type) {
    core::AisReport r;
    r.mmsi = b.u(8, 30);
    switch (type) {
        case 1:
        case 2:
        case 3:
            if (b.size() < 137) return std::nullopt;
            r.nav_status = static_cast<std::uint8_t>(b.u(38, 4));
            position(r, b, 61, 89);
            motion(r, b, 50, 116, 128);
            return r;
        case 4:
            if (b.size() < 168) return std::nullopt;
            r.station = core::AisStation::BaseStation;
            position(r, b, 79, 107);
            return r;
        case 21: {
            if (b.size() < 272) return std::nullopt;
            r.station = core::AisStation::AtoN;
            r.aton_type = static_cast<std::uint8_t>(b.u(38, 5));
            std::string name = b.text(43, 20);
            // Name extension: up to 14 more characters after bit 272
            if (b.size() >= 278) name += b.text(272, std::min<std::size_t>(14, (b.size() - 272) / 6));
            while (!name.empty() && (name.back() == '@' || name.back() == ' ')) name.pop_back();
            set_text(r.name, std::move(name));
            position(r, b, 164, 192);
            dimensions(r, b, 219);
            r.off_position = b.u(259, 1) == 1;
            r.virtual_aton = b.u(269, 1) == 1;
            return r;
        }
        case 5:
            if (b.size() < 420) return std::nullopt;
            set_text(r.callsign, b.text(70, 7));
            set_text(r.name, b.text(112, 20));
            r.ship_type = static_cast<std::uint8_t>(b.u(232, 8));
            dimensions(r, b, 240);
            return r;
        case 18:
        case 19:
            if (b.size() < 133) return std::nullopt;
            r.class_b = true;
            position(r, b, 57, 85);
            motion(r, b, 46, 112, 124);
            if (type == 19 && b.size() >= 301) {
                set_text(r.name, b.text(143, 20));
                r.ship_type = static_cast<std::uint8_t>(b.u(263, 8));
                dimensions(r, b, 271);
            }
            return r;
        case 24: {
            r.class_b = true;
            const std::uint32_t part = b.u(38, 2);
            if (part == 0 && b.size() >= 160) {
                set_text(r.name, b.text(40, 20));
                return r;
            }
            if (part == 1 && b.size() >= 162) {
                r.ship_type = static_cast<std::uint8_t>(b.u(40, 8));
                set_text(r.callsign, b.text(90, 7));
                dimensions(r, b, 132);
                return r;
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

}  // namespace

std::optional<AisMessage> AisDecoder::decode_message(std::string_view payload, int fill_bits) {
    const Bits b(payload, fill_bits);
    if (b.size() < 38) return std::nullopt;
    const std::uint32_t type = b.u(0, 6);
    if (type == 12 || type == 14) {
        // Safety related text: 6-bit characters after the header (12: after the destination)
        const std::size_t start = type == 12 ? 72 : 40;
        if (b.size() < start + 6) return std::nullopt;
        core::AisSafetyMessage m;
        m.mmsi = b.u(8, 30);
        m.addressed = type == 12;
        m.text = b.text(start, (b.size() - start) / 6);
        if (m.text.empty()) return std::nullopt;
        return AisMessage{std::nullopt, std::move(m)};
    }
    auto r = decode_report(b, type);
    if (!r) return std::nullopt;
    return AisMessage{std::move(r), std::nullopt};
}

std::optional<AisMessage> AisDecoder::feed(int fragment_count, int fragment_number,
                                                std::string_view sequence_id, char channel,
                                                std::string_view payload, int fill_bits) {
    if (fragment_count <= 1) return decode_message(payload, fill_bits);

    const auto now = std::chrono::steady_clock::now();
    // Drop fragments that never completed (lost sentence)
    for (auto it = pending_.begin(); it != pending_.end();) {
        it = now - it->second.started > std::chrono::seconds(5) ? pending_.erase(it) : std::next(it);
    }

    const std::string key = std::string(sequence_id) + channel;
    if (fragment_number == 1) {
        pending_[key] = Pending{fragment_count, 2, std::string(payload), now};
        return std::nullopt;
    }
    auto it = pending_.find(key);
    if (it == pending_.end() || it->second.next != fragment_number || it->second.expected != fragment_count) {
        if (it != pending_.end()) pending_.erase(it);
        return std::nullopt;
    }
    it->second.payload += payload;
    ++it->second.next;
    if (fragment_number < fragment_count) return std::nullopt;
    const std::string full = std::move(it->second.payload);
    pending_.erase(it);
    return decode_message(full, fill_bits);
}

}  // namespace boat::nmea0183
