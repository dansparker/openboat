#include "boat/nmea2000/decoder.hpp"

#include <numbers>

#include "boat/core/heading.hpp"
#include "boat/core/marine_data.hpp"
#include "boat/core/nav_math.hpp"

namespace boat::nmea2000 {

namespace {

constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kKelvin = 273.15;

// Little-endian field readers. NMEA 2000 marks "not available" with all
// bits set (unsigned) or the maximum positive value (signed); both map to
// nullopt here, as does a field beyond the end of a short message.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> d) : d_(d) {}

    [[nodiscard]] std::uint8_t u8(std::size_t at) const { return at < d_.size() ? d_[at] : 0xFF; }

    [[nodiscard]] std::optional<std::uint32_t> u16(std::size_t at) const {
        const auto v = raw(at, 2);
        return v && *v < 0xFFFD ? v : std::nullopt;  // FFFD..FFFF reserved / n.a.
    }
    [[nodiscard]] std::optional<std::int32_t> s16(std::size_t at) const {
        const auto v = raw(at, 2);
        if (!v) return std::nullopt;
        const auto s = static_cast<std::int16_t>(*v);
        return s >= 0x7FFD ? std::nullopt : std::optional<std::int32_t>(s);
    }
    [[nodiscard]] std::optional<std::uint32_t> u32(std::size_t at) const {
        const auto v = raw(at, 4);
        return v && *v < 0xFFFFFFFDU ? v : std::nullopt;
    }
    [[nodiscard]] std::optional<std::int32_t> s32(std::size_t at) const {
        const auto v = raw(at, 4);
        if (!v) return std::nullopt;
        const auto s = static_cast<std::int32_t>(*v);
        return s >= 0x7FFFFFFD ? std::nullopt : std::optional(s);
    }

private:
    // nullopt when the message is too short for the field
    [[nodiscard]] std::optional<std::uint32_t> raw(std::size_t at, std::size_t n) const {
        if (at + n > d_.size()) return std::nullopt;
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < n; ++i) v |= static_cast<std::uint32_t>(d_[at + i]) << (8 * i);
        return v;
    }
    std::span<const std::uint8_t> d_;
};

std::optional<double> angle_deg(std::optional<std::uint32_t> raw) {
    if (!raw) return std::nullopt;
    return core::normalize_deg(*raw * 1e-4 * kRadToDeg);
}

std::optional<core::GeoPoint> lat_lon(const Reader& r, std::size_t lat_at, std::size_t lon_at) {
    const auto lat = r.s32(lat_at);
    const auto lon = r.s32(lon_at);
    if (!lat || !lon) return std::nullopt;
    const core::GeoPoint p{*lat * 1e-7, *lon * 1e-7};
    if (p.lat_deg < -90.0 || p.lat_deg > 90.0 || p.lon_deg < -180.0 || p.lon_deg > 180.0) return std::nullopt;
    return p;
}

}  // namespace

CanId parse_can_id(std::uint32_t id) {
    CanId c;
    c.priority = static_cast<std::uint8_t>((id >> 26) & 0x7);
    c.source = static_cast<std::uint8_t>(id & 0xFF);
    const std::uint32_t pf = (id >> 16) & 0xFF;
    const std::uint32_t ps = (id >> 8) & 0xFF;
    const std::uint32_t dp = (id >> 24) & 0x3;  // EDP + DP
    if (pf < 240) {
        c.pgn = (dp << 16) | (pf << 8);  // PDU1: PS is the destination
        c.destination = static_cast<std::uint8_t>(ps);
    } else {
        c.pgn = (dp << 16) | (pf << 8) | ps;
    }
    return c;
}

bool is_fast_packet(std::uint32_t pgn) {
    switch (pgn) {
        case 126208: case 126464: case 126996: case 127489: case 128275:
        case 129029: case 129038: case 129039: case 129040: case 129284:
        case 129285: case 129540: case 129794: case 129809: case 129810:
        case 130074:
            return true;
        default:
            return false;
    }
}

std::optional<Message> FastPacketAssembler::push(const hal::CanFrame& frame) {
    const CanId id = parse_can_id(frame.id);
    if (frame.dlc < 2) return std::nullopt;
    const auto seq = static_cast<std::uint8_t>(frame.data[0] >> 5);
    const auto counter = static_cast<std::uint8_t>(frame.data[0] & 0x1F);
    const auto key = std::make_pair(id.source, id.pgn);

    if (counter == 0) {
        Pending p;
        p.sequence = seq;
        p.next_frame = 1;
        p.length = frame.data[1];
        for (std::size_t i = 2; i < frame.dlc && p.data.size() < p.length; ++i) p.data.push_back(frame.data[i]);
        if (p.data.size() >= p.length) return Message{id, std::move(p.data)};
        pending_[key] = std::move(p);
        return std::nullopt;
    }

    auto it = pending_.find(key);
    if (it == pending_.end()) return std::nullopt;
    Pending& p = it->second;
    if (p.sequence != seq || p.next_frame != counter) {
        pending_.erase(it);  // lost frame: discard instead of decoding garbage
        return std::nullopt;
    }
    for (std::size_t i = 1; i < frame.dlc && p.data.size() < p.length; ++i) p.data.push_back(frame.data[i]);
    ++p.next_frame;
    if (p.data.size() < p.length) return std::nullopt;
    Message m{id, std::move(p.data)};
    pending_.erase(it);
    return m;
}

bool Decoder::feed(const hal::CanFrame& frame) {
    if (!frame.extended) return false;
    const CanId id = parse_can_id(frame.id);
    if (is_fast_packet(id.pgn)) {
        if (auto m = fast_.push(frame)) return decode(*m);
        return false;
    }
    return decode(Message{id, std::vector<std::uint8_t>(frame.data.begin(), frame.data.begin() + frame.dlc)});
}

bool Decoder::decode(const Message& m) {
    const Reader r(m.data);
    switch (m.id.pgn) {
        case 129025: {  // Position, rapid update
            const auto p = lat_lon(r, 0, 4);
            if (!p) return false;
            core::Position pos;
            pos.point = *p;
            pos.quality = core::FixQuality::Gnss;
            bus_.publish(pos);
            return true;
        }
        case 129026: {  // COG & SOG, rapid update
            const bool magnetic = (r.u8(1) & 0x03) == 1;
            const auto cog = angle_deg(r.u16(2));
            const auto sog = r.u16(4);
            if (!sog || magnetic) return false;  // magnetic COG would need variation: not used
            bus_.publish(core::CourseOverGround{cog.value_or(0.0), *sog * 0.01});
            return true;
        }
        case 127250: {  // Vessel heading
            const auto hdg = angle_deg(r.u16(1));
            if (!hdg) return false;
            const auto var = r.s16(5);
            const bool magnetic = (r.u8(7) & 0x03) == 1;
            std::optional<double> variation = last_variation_deg_;
            if (var) variation = *var * 1e-4 * kRadToDeg;
            if (magnetic) {
                bus_.publish(core::magnetic_heading(*hdg, variation, bus_));
            } else {
                bus_.publish(core::Heading{*hdg, true, variation, false});
            }
            return true;
        }
        case 127258: {  // Magnetic variation (e.g. from the GNSS); used for magnetic headings
            const auto var = r.s16(4);
            if (!var) return false;
            last_variation_deg_ = *var * 1e-4 * kRadToDeg;
            return true;
        }
        case 128267: {  // Water depth
            const auto depth = r.u32(1);
            if (!depth) return false;
            const auto offset = r.s16(5);
            const auto user = bus_.latest<core::DepthOffset>();
            const std::optional<double> sent = offset ? std::optional(*offset * 0.001) : std::nullopt;
            bus_.publish(core::Depth{*depth * 0.01,
                                     core::effective_depth_offset(user ? std::optional(user->value) : std::nullopt,
                                                                  sent, options_.depth_offset_m)});
            return true;
        }
        case 128259: {  // Speed, water referenced
            const auto stw = r.u16(1);
            if (!stw) return false;
            bus_.publish(core::SpeedThroughWater{*stw * 0.01});
            return true;
        }
        case 130306: {  // Wind data
            const auto speed = r.u16(1);
            const auto angle = angle_deg(r.u16(3));
            const auto reference = r.u8(5) & 0x07;
            if (!speed || !angle || reference != 2) return false;  // 2 = apparent
            bus_.publish(core::ApparentWind{*angle, *speed * 0.01});
            return true;
        }
        case 130310: {  // Environmental parameters (water temperature)
            const auto t = r.u16(1);
            if (!t) return false;
            bus_.publish(core::WaterTemperature{*t * 0.01 - kKelvin});
            return true;
        }
        case 130312:    // Temperature (obsolete but common)
        case 130316: {  // Temperature, extended range
            if (r.u8(2) != 0) return false;  // source 0 = sea temperature
            double kelvin = 0.0;
            if (m.id.pgn == 130312) {
                const auto t = r.u16(3);
                if (!t) return false;
                kelvin = *t * 0.01;
            } else {
                const std::uint32_t raw24 = r.u8(3) | (static_cast<std::uint32_t>(r.u8(4)) << 8) |
                                            (static_cast<std::uint32_t>(r.u8(5)) << 16);
                if (raw24 >= 0xFFFFFD) return false;
                kelvin = raw24 * 0.001;
            }
            bus_.publish(core::WaterTemperature{kelvin - kKelvin});
            return true;
        }
        case 126992: {  // System time (date: days since 1970, time: 0.0001 s since midnight)
            const auto days = r.u16(2);
            const auto time = r.u32(4);
            // Only GNSS-derived time (source 0 GPS, 1 GLONASS): a display's free-running
            // crystal clock could be just as wrong as ours
            if (!days || !time || (r.u8(1) & 0x0F) > 1) return false;
            bus_.publish(core::UtcTime{static_cast<std::int64_t>(*days) * 86400000 + *time / 10});
            return true;
        }
        case 127488: {  // Engine parameters, rapid update
            core::EngineData e;
            e.instance = r.u8(0);
            if (const auto rpm = r.u16(1)) e.rpm = *rpm * 0.25;
            bus_.publish(e);
            return true;
        }
        case 129038:    // AIS class A position report
        case 129039: {  // AIS class B position report
            core::AisReport a;
            a.class_b = m.id.pgn == 129039;
            a.mmsi = r.u32(1).value_or(0);
            if (a.mmsi == 0) return false;
            a.position = lat_lon(r, 9, 5);
            a.cog_deg = angle_deg(r.u16(14));
            if (const auto sog = r.u16(16)) a.sog_mps = *sog * 0.01;
            a.heading_deg = angle_deg(r.u16(21));
            if (!a.class_b) {
                if (const auto st = r.u8(25)) a.nav_status = static_cast<std::uint8_t>(*st & 0x0F);
            }
            bus_.publish(a);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace boat::nmea2000
