#include "boat/nav/magnetic_model.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <numbers>
#include <sstream>
#include <utility>

namespace boat::nav {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kWgs84A = 6378.137;               // km
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kReferenceRadius = 6371.2;        // km (geomagnetic reference sphere)

using Table = std::vector<std::vector<double>>;

Table square(int degree) {
    return Table(static_cast<std::size_t>(degree) + 1, std::vector<double>(static_cast<std::size_t>(degree) + 1, 0.0));
}

bool is_leap(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

}  // namespace

double decimal_year(int year, int month, int day) {
    static constexpr int kDaysBefore[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const int doy = kDaysBefore[(month - 1) % 12] + day + (month > 2 && is_leap(year) ? 1 : 0);
    return year + (doy - 1) / (is_leap(year) ? 366.0 : 365.0);
}

MagneticModel::MagneticModel(double epoch, int degree, Table g, Table h, Table g_dot, Table h_dot, std::string name)
    : epoch_(epoch),
      degree_(degree),
      name_(std::move(name)),
      g_(std::move(g)),
      h_(std::move(h)),
      g_dot_(std::move(g_dot)),
      h_dot_(std::move(h_dot)) {
    const auto size = static_cast<std::size_t>(degree_) + 1;
    for (const Table* t : {&g_, &h_, &g_dot_, &h_dot_}) {
        if (t->size() != size) throw MagneticModelError("coefficient table has wrong size");
        for (const auto& row : *t) {
            if (row.size() != size) throw MagneticModelError("coefficient table has wrong size");
        }
    }
}

MagneticModel MagneticModel::from_cof(std::istream& in) {
    std::string line;
    if (!std::getline(in, line)) throw MagneticModelError("empty magnetic model file");

    double epoch = 0.0;
    std::string name;
    {
        std::istringstream header(line);
        if (!(header >> epoch >> name)) throw MagneticModelError("invalid WMM.COF header: " + line);
    }

    struct Entry {
        int n, m;
        double g, h, gd, hd;
    };
    std::vector<Entry> entries;
    int degree = 0;
    std::size_t line_number = 1;
    while (std::getline(in, line)) {
        ++line_number;
        if (line.find("9999") != std::string::npos) break;
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        Entry e{};
        if (!(fields >> e.n >> e.m >> e.g >> e.h >> e.gd >> e.hd) || e.n < 1 || e.m < 0 || e.m > e.n || e.n > 30) {
            throw MagneticModelError("WMM.COF line " + std::to_string(line_number) + ": invalid coefficients");
        }
        degree = std::max(degree, e.n);
        entries.push_back(e);
    }
    if (entries.empty()) throw MagneticModelError("WMM.COF contains no coefficients");

    Table g = square(degree), h = square(degree), gd = square(degree), hd = square(degree);
    for (const auto& e : entries) {
        const auto n = static_cast<std::size_t>(e.n), m = static_cast<std::size_t>(e.m);
        g[n][m] = e.g;
        h[n][m] = e.h;
        gd[n][m] = e.gd;
        hd[n][m] = e.hd;
    }
    return MagneticModel(epoch, degree, std::move(g), std::move(h), std::move(gd), std::move(hd), name);
}

MagneticField MagneticModel::field(core::GeoPoint position, double height_km, double decimal_year) const {
    const double dt = decimal_year - epoch_;
    const double lat = position.lat_deg * kDegToRad;
    const double lon = position.lon_deg * kDegToRad;

    // Geodetic -> geocentric spherical coordinates
    const double e2 = kWgs84F * (2.0 - kWgs84F);
    const double sin_lat = std::sin(lat);
    const double rc = kWgs84A / std::sqrt(1.0 - e2 * sin_lat * sin_lat);
    const double p = (rc + height_km) * std::cos(lat);
    const double z = (rc * (1.0 - e2) + height_km) * sin_lat;
    const double r = std::hypot(p, z);
    const double lat_gc = std::asin(z / r);

    // Colatitude for the Legendre recursion
    const double ct = std::sin(lat_gc);   // cos(theta)
    const double st = std::cos(lat_gc);   // sin(theta)

    const auto size = static_cast<std::size_t>(degree_) + 1;
    Table P = square(degree_), dP = square(degree_);   // Gauss-normalised, derivative w.r.t. theta
    P[0][0] = 1.0;
    for (std::size_t n = 1; n < size; ++n) {
        for (std::size_t m = 0; m <= n; ++m) {
            if (n == m) {
                P[n][m] = st * P[n - 1][m - 1];
                dP[n][m] = st * dP[n - 1][m - 1] + ct * P[n - 1][m - 1];
            } else if (n == 1) {  // m == 0
                P[n][m] = ct * P[n - 1][m];
                dP[n][m] = ct * dP[n - 1][m] - st * P[n - 1][m];
            } else {
                const double nn = static_cast<double>(n), mm = static_cast<double>(m);
                const double k = ((nn - 1) * (nn - 1) - mm * mm) / ((2 * nn - 1) * (2 * nn - 3));
                const double p2 = n - 2 >= m ? P[n - 2][m] : 0.0;
                const double dp2 = n - 2 >= m ? dP[n - 2][m] : 0.0;
                P[n][m] = ct * P[n - 1][m] - k * p2;
                dP[n][m] = ct * dP[n - 1][m] - st * P[n - 1][m] - k * dp2;
            }
        }
    }

    // Schmidt semi-normalisation factors (applied to the coefficients)
    Table S = square(degree_);
    S[0][0] = 1.0;
    for (std::size_t n = 1; n < size; ++n) {
        const double nn = static_cast<double>(n);
        S[n][0] = S[n - 1][0] * (2 * nn - 1) / nn;
        for (std::size_t m = 1; m <= n; ++m) {
            const double mm = static_cast<double>(m);
            S[n][m] = S[n][m - 1] * std::sqrt((nn - mm + 1) * (m == 1 ? 2.0 : 1.0) / (nn + mm));
        }
    }

    double br = 0.0, bt = 0.0, bp = 0.0;
    const double aor = kReferenceRadius / r;
    double ar = aor * aor;
    for (std::size_t n = 1; n < size; ++n) {
        ar *= aor;  // (a/r)^(n+2)
        for (std::size_t m = 0; m <= n; ++m) {
            const double g = (g_[n][m] + dt * g_dot_[n][m]) * S[n][m];
            const double h = (h_[n][m] + dt * h_dot_[n][m]) * S[n][m];
            const double cm = std::cos(static_cast<double>(m) * lon);
            const double sm = std::sin(static_cast<double>(m) * lon);
            const double t1 = g * cm + h * sm;
            const double t2 = g * sm - h * cm;
            br += (static_cast<double>(n) + 1.0) * ar * t1 * P[n][m];
            bt -= ar * t1 * dP[n][m];
            bp += static_cast<double>(m) * ar * t2 * P[n][m];
        }
    }
    if (std::abs(st) > 1e-10) bp /= st;  // at the poles the east component is undefined anyway

    // Geocentric (north = -bt, down = -br) -> geodetic frame
    const double x_gc = -bt;
    const double z_gc = -br;
    const double psi = lat_gc - lat;
    MagneticField f;
    f.north_nt = x_gc * std::cos(psi) - z_gc * std::sin(psi);
    f.east_nt = bp;
    f.down_nt = x_gc * std::sin(psi) + z_gc * std::cos(psi);
    f.declination_deg = std::atan2(f.east_nt, f.north_nt) * kRadToDeg;
    f.inclination_deg = std::atan2(f.down_nt, std::hypot(f.north_nt, f.east_nt)) * kRadToDeg;
    return f;
}

}  // namespace boat::nav
