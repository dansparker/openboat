#pragma once

// World Magnetic Model (WMM) evaluation: magnetic variation (declination)
// for a position and date.
//
// The official WMM.COF (NOAA NCEI, public domain) is bundled as data/WMM.COF;
// newer models: https://www.ncei.noaa.gov/products/world-magnetic-model
// (config "magnetic_model").
// Each model is valid for 5 years from its epoch.

#include "boat/core/marine_data.hpp"

#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace boat::nav {

class MagneticModelError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MagneticField {
    double north_nt{};   // X
    double east_nt{};    // Y
    double down_nt{};    // Z
    double declination_deg{};   // east positive = magnetic variation
    double inclination_deg{};
};

class MagneticModel {
public:
    // Parses the WMM.COF text format (header line, "n m g h gdot hdot" lines,
    // terminated by a line of 9s or end of file).
    [[nodiscard]] static MagneticModel from_cof(std::istream& in);

    // Direct construction (tests): coefficients in nT and nT/year,
    // indexed [n][m], n = 0..degree.
    MagneticModel(double epoch, int degree, std::vector<std::vector<double>> g, std::vector<std::vector<double>> h,
                  std::vector<std::vector<double>> g_dot, std::vector<std::vector<double>> h_dot,
                  std::string name = {});

    [[nodiscard]] double epoch() const { return epoch_; }
    [[nodiscard]] int degree() const { return degree_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] bool valid_for(double decimal_year) const {
        return decimal_year >= epoch_ && decimal_year < epoch_ + 5.0;
    }

    // Geodetic position (WGS-84), height above the ellipsoid in km, decimal year.
    [[nodiscard]] MagneticField field(core::GeoPoint position, double height_km, double decimal_year) const;
    [[nodiscard]] double declination_deg(core::GeoPoint position, double height_km, double decimal_year) const {
        return field(position, height_km, decimal_year).declination_deg;
    }

private:
    double epoch_{};
    int degree_{};
    std::string name_;
    std::vector<std::vector<double>> g_, h_, g_dot_, h_dot_;
};

// Decimal year for a calendar date (UTC), e.g. 2026-07-02 -> ~2026.50
[[nodiscard]] double decimal_year(int year, int month, int day);

}  // namespace boat::nav
