#include "boat/nmea0183/parser.hpp"

#include <charconv>
#include <string>
#include <vector>

#include "boat/core/marine_data.hpp"
#include "boat/core/nav_math.hpp"

namespace boat::nmea0183 {

namespace {

using core::kMpsPerKnot;

std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::optional<double> num(std::string_view f) {
    if (f.empty()) return std::nullopt;
    // std::from_chars for double is not available on every toolchain we
    // target (older libc++), so go through std::stod on a small copy.
    try {
        std::size_t used = 0;
        const std::string copy(f);
        const double v = std::stod(copy, &used);
        if (used != copy.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> integer(std::string_view f) {
    int v = 0;
    const auto* end = f.data() + f.size();
    const auto [ptr, ec] = std::from_chars(f.data(), end, v);
    if (f.empty() || ec != std::errc{} || ptr != end) return std::nullopt;
    return v;
}

// ddmm.mmmm + hemisphere -> signed degrees
std::optional<double> coordinate(std::string_view value, std::string_view hemi, int degree_digits) {
    if (value.size() < static_cast<std::size_t>(degree_digits) + 2) return std::nullopt;
    const auto deg = integer(value.substr(0, static_cast<std::size_t>(degree_digits)));
    const auto min = num(value.substr(static_cast<std::size_t>(degree_digits)));
    if (!deg || !min || *min >= 60.0) return std::nullopt;
    double d = *deg + *min / 60.0;
    if (hemi == "S" || hemi == "W") d = -d;
    else if (hemi != "N" && hemi != "E") return std::nullopt;
    return d;
}

std::optional<core::GeoPoint> point(std::string_view lat, std::string_view ns, std::string_view lon,
                                    std::string_view ew) {
    const auto la = coordinate(lat, ns, 2);
    const auto lo = coordinate(lon, ew, 3);
    if (!la || !lo || *la > 90.0 || *la < -90.0 || *lo > 180.0 || *lo < -180.0) return std::nullopt;
    return core::GeoPoint{*la, *lo};
}

double speed_to_mps(double value, std::string_view unit) {
    if (unit == "K") return value / 3.6;
    if (unit == "M") return value;
    return value * kMpsPerKnot;  // "N"
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string_view trim(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.remove_suffix(1);
    }
    // Some multiplexers prepend a tag block "\s:...*hh\" - skip it
    if (!line.empty() && line.front() == '\\') {
        const auto end = line.find('\\', 1);
        line = end == std::string_view::npos ? std::string_view{} : line.substr(end + 1);
    }
    return line;
}

}  // namespace

bool checksum_ok(std::string_view line, bool accept_missing) {
    line = trim(line);
    if (line.size() < 2 || (line.front() != '$' && line.front() != '!')) return false;
    const auto star = line.rfind('*');
    if (star == std::string_view::npos) return accept_missing;
    if (star + 3 != line.size()) return false;
    unsigned sum = 0;
    for (std::size_t i = 1; i < star; ++i) sum ^= static_cast<unsigned char>(line[i]);
    const int hi = hex(line[star + 1]);
    const int lo = hex(line[star + 2]);
    return hi >= 0 && lo >= 0 && sum == static_cast<unsigned>(hi * 16 + lo);
}

bool Parser::feed(std::string_view raw) {
    std::string_view line = trim(raw);
    if (line.empty()) return false;
    if (!checksum_ok(line, options_.accept_missing_checksum)) {
        ++stats_.bad_checksum;
        return false;
    }
    if (const auto star = line.rfind('*'); star != std::string_view::npos) line = line.substr(0, star);
    const auto f = split(line.substr(1));
    if (f.empty() || f[0].size() < 5) {
        ++stats_.malformed;
        return false;
    }
    const std::string_view type = f[0].substr(f[0].size() - 3);
    const auto field = [&](std::size_t i) -> std::string_view { return i < f.size() ? f[i] : std::string_view{}; };

    bool ok = false;
    if (type == "RMC") {
        // Status A = valid; mode indicator N = not valid (NMEA 2.3+)
        if (field(2) == "A" && field(12) != "N") {
            // hhmmss.ss + ddmmyy -> UTC (two-digit year: 1980..2079, GNSS era)
            const auto t = field(1);
            const auto d = field(9);
            if (t.size() >= 6 && d.size() == 6) {
                const auto hh = integer(t.substr(0, 2)), mi = integer(t.substr(2, 2));
                const auto ss = num(t.substr(4));
                const auto dd = integer(d.substr(0, 2)), mo = integer(d.substr(2, 2)), yy = integer(d.substr(4, 2));
                if (hh && mi && ss && dd && mo && yy && *mo >= 1 && *mo <= 12 && *dd >= 1 && *dd <= 31 && *hh < 24) {
                    const int year = *yy < 80 ? 2000 + *yy : 1900 + *yy;
                    bus_.publish(core::UtcTime{core::unix_ms(year, *mo, *dd, *hh, *mi, *ss)});
                }
            }
            if (const auto p = point(field(3), field(4), field(5), field(6))) {
                core::Position pos;
                pos.point = *p;
                pos.quality = field(12) == "D" ? core::FixQuality::Dgnss
                              : field(12) == "E" ? core::FixQuality::Estimated
                                                 : core::FixQuality::Gnss;
                bus_.publish(pos);
                const auto sog = num(field(7));
                const auto cog = num(field(8));
                if (sog) {
                    // COG is empty when stationary on many receivers
                    bus_.publish(core::CourseOverGround{cog ? core::normalize_deg(*cog) : 0.0, *sog * kMpsPerKnot});
                }
                if (const auto var = num(field(10))) {
                    last_variation_deg_ = field(11) == "W" ? -*var : *var;
                }
                ok = true;
            }
        }
        ok = ok || field(2) == "V";  // understood, but no fix
    } else if (type == "GGA") {
        const auto quality = integer(field(6)).value_or(0);
        if (quality > 0) {
            if (const auto p = point(field(2), field(3), field(4), field(5))) {
                core::Position pos;
                pos.point = *p;
                pos.quality = quality == 2 ? core::FixQuality::Dgnss
                              : (quality == 4 || quality == 5) ? core::FixQuality::Rtk
                              : quality == 6 ? core::FixQuality::Estimated
                              : quality == 8 ? core::FixQuality::Simulated
                                             : core::FixQuality::Gnss;
                pos.satellites = integer(field(7));
                pos.hdop = num(field(8));
                bus_.publish(pos);
            }
        }
        ok = true;
    } else if (type == "VTG") {
        const auto cog = num(field(1));
        const auto sog_kn = num(field(5));
        if (sog_kn && field(9) != "N") {
            bus_.publish(core::CourseOverGround{cog ? core::normalize_deg(*cog) : 0.0, *sog_kn * kMpsPerKnot});
        }
        ok = true;
    } else if (type == "HDT") {
        if (const auto h = num(field(1))) {
            bus_.publish(core::Heading{core::normalize_deg(*h), true, std::nullopt});
            ok = true;
        }
    } else if (type == "HDG") {
        if (const auto h = num(field(1))) {
            double mag = *h;
            if (const auto dev = num(field(2))) mag += field(3) == "W" ? -*dev : *dev;
            std::optional<double> var;
            if (const auto v = num(field(4))) var = field(5) == "W" ? -*v : *v;
            if (!var) var = last_variation_deg_;
            core::Heading hdg;
            hdg.variation_deg = var;
            hdg.is_true = var.has_value();
            hdg.heading_deg = core::normalize_deg(mag + var.value_or(0.0));
            bus_.publish(hdg);
            ok = true;
        }
    } else if (type == "HDM") {
        if (const auto h = num(field(1))) {
            core::Heading hdg;
            hdg.variation_deg = last_variation_deg_;
            hdg.is_true = last_variation_deg_.has_value();
            hdg.heading_deg = core::normalize_deg(*h + last_variation_deg_.value_or(0.0));
            bus_.publish(hdg);
            ok = true;
        }
    } else if (type == "DPT") {
        if (const auto d = num(field(1))) {
            const auto offset = num(field(2));
            bus_.publish(core::Depth{*d, offset.value_or(options_.depth_offset_m)});
            ok = true;
        }
    } else if (type == "DBT") {
        // Depth in feet (1), metres (3), fathoms (5): prefer metres
        std::optional<double> m = num(field(3));
        if (!m) {
            if (const auto ft = num(field(1))) m = *ft * 0.3048;
        }
        if (m) {
            bus_.publish(core::Depth{*m, options_.depth_offset_m});
            ok = true;
        }
    } else if (type == "MWV") {
        const auto angle = num(field(1));
        const auto speed = num(field(3));
        if (angle && speed && field(5) == "A") {
            if (field(2) == "R") {
                bus_.publish(core::ApparentWind{core::normalize_deg(*angle), speed_to_mps(*speed, field(4))});
            }
            // "T" (theoretical) is recomputed from apparent wind by the
            // WindCalculator, so a single definition is used everywhere.
            ok = true;
        }
    } else if (type == "VHW") {
        std::optional<double> stw = num(field(5));
        if (stw) {
            bus_.publish(core::SpeedThroughWater{*stw * kMpsPerKnot});
        } else if (const auto kmh = num(field(7))) {
            bus_.publish(core::SpeedThroughWater{*kmh / 3.6});
        }
        ok = true;
    } else if (type == "MTW") {
        if (const auto t = num(field(1)); t && field(2) == "C") {
            bus_.publish(core::WaterTemperature{*t});
            ok = true;
        }
    } else if (type == "VDM") {
        const auto count = integer(field(1));
        const auto number = integer(field(2));
        const auto fill = integer(field(6)).value_or(0);
        if (count && number && !field(5).empty()) {
            const char channel = field(4).empty() ? '-' : field(4).front();
            if (auto report = ais_.feed(*count, *number, field(3), channel, field(5), fill)) {
                if (report->mmsi != 0) bus_.publish(*report);
            }
            ok = true;
        }
    } else if (type == "VDO") {
        ok = true;  // own ship, see header
    } else {
        ++stats_.unsupported;
        return false;
    }

    if (ok) ++stats_.accepted;
    else ++stats_.malformed;
    return ok;
}

}  // namespace boat::nmea0183
