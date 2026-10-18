/* SPDX-License-Identifier: MIT
 * Desktop input and plotting helpers. Astronomy stays in NAVALM's C core.
 */
#ifndef NAVFLTK_MODEL_H
#define NAVFLTK_MODEL_H
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include "nav_engine.h"
#include "navrc.h"
#include "navsight.h"
}
namespace desktop {
inline double number(const std::string &s, const char *name, double lo, double hi) {
    char *end;
    errno = 0;
    double v = std::strtod(s.c_str(), &end);
    while (std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end == s.c_str() || *end || errno || !std::isfinite(v) || v < lo || v > hi)
        throw std::runtime_error(std::string(name) + ": enter a number from " + std::to_string(lo) +
                                 " to " + std::to_string(hi) + ".");
    return v;
}
/* Decimal degrees or two numbers: degrees and minutes. Hemisphere is separate. */
inline double angle(const std::string &s, const char *name, double limit) {
    char *end;
    errno = 0;
    double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || errno || !std::isfinite(d) || d < 0)
        throw std::runtime_error(std::string(name) +
                                 ": use positive degrees, or degrees and minutes.");
    std::string rest(end);
    if (rest.find_first_not_of(" \t") != std::string::npos) {
        double m = number(rest, name, 0, 59.999999999);
        if (std::floor(d) != d)
            throw std::runtime_error(std::string(name) +
                                     ": whole degrees are required before minutes.");
        d += m / 60;
    }
    if (d > limit)
        throw std::runtime_error(std::string(name) + ": out of range.");
    return d;
}
/* Keep entry fields in degrees and decimal minutes, including after reload.
   Up to seven fractional minute digits retain the old decimal display's
   precision; ordinary sextant entries stay short (e.g. 94 37.2). */
inline std::string angle_entry(double value, int degree_width = 3) {
    double whole = std::floor(std::fabs(value));
    double minutes = std::round((std::fabs(value) - whole) * 60 * 1e7) / 1e7;
    if (minutes >= 60) {
        whole += 1;
        minutes = 0;
    }
    char b[64];
    std::snprintf(b, sizeof b, "%0*d %010.7f", degree_width, (int)whole, minutes);
    std::string text(b);
    while (text.back() == '0' && text[text.size() - 2] != '.')
        text.pop_back();
    return text;
}
inline void date_time(const std::string &date, const std::string &time, Sight &s) {
    int n = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d%n", &s.y, &s.mo, &s.d, &n) != 3 ||
        n != (int)date.size() || s.y < 1600 || s.y > 2399 || s.mo < 1 || s.mo > 12)
        throw std::runtime_error("UTC date: use YYYY-MM-DD (1600 through 2399).");
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (s.y % 4 == 0 && (s.y % 100 != 0 || s.y % 400 == 0))
        days[1] = 29;
    if (s.d < 1 || s.d > days[s.mo - 1])
        throw std::runtime_error("UTC date: that day does not exist.");
    n = 0;
    if (std::sscanf(time.c_str(), "%d:%d:%lf%n", &s.h, &s.mi, &s.s, &n) != 3 ||
        n != (int)time.size() || s.h < 0 || s.h > 23 || s.mi < 0 || s.mi > 59 ||
        !std::isfinite(s.s) || s.s < 0 || s.s >= 60)
        throw std::runtime_error("UTC time: use HH:MM:SS, with seconds below 60.");
}
inline void check_engine(const Sight &s) {
    if (!nav_engine_covers(navsight_jd(&s)))
        throw std::runtime_error("This UTC date is outside the loaded ephemeris. Choose Classic or "
                                 "another ephemeris file.");
}
inline Reduction reduce(const Sight &s) {
    char err[512]{};
    if (navsight_validate(&s, err, sizeof err))
        throw std::runtime_error(err);
    check_engine(s);
    Reduction r{};
    if (navsight_reduce(&s, &r, err, sizeof err))
        throw std::runtime_error(err);
    return r;
}
struct Line {
    double e, n, ue, un, ae, an, original_e, original_n;
};
/* Same tangent plane and average AP latitude as navsight_fix(). */
inline std::vector<Line> geometry(const std::vector<Reduction> &red, const FixResult &f) {
    double lat = 0;
    for (auto &r : red)
        lat += r.ap_lat;
    lat /= red.size();
    double c = std::cos(lat * M_PI / 180);
    std::vector<Line> lines;
    for (size_t i = 0; i < red.size(); ++i) {
        auto &r = red[i];
        double z = r.zn * M_PI / 180, b = f.advanced_brg[i] * M_PI / 180;
        Line l{};
        l.ue = std::sin(z);
        l.un = std::cos(z);
        l.original_e = (r.ap_lon - f.lon) * 60 * c;
        l.original_n = (r.ap_lat - f.lat) * 60;
        l.ae = l.original_e + f.advanced_nm[i] * std::sin(b);
        l.an = l.original_n + f.advanced_nm[i] * std::cos(b);
        l.e = l.ae + r.intercept_nm * l.ue;
        l.n = l.an + r.intercept_nm * l.un;
        lines.push_back(l);
    }
    return lines;
}
} // namespace desktop
#endif
