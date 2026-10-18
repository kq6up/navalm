/* SPDX-License-Identifier: MIT
 * Geographic fix data, independent of FLTK and of any chart provider.
 */
#ifndef NAVFLTK_PLOT_H
#define NAVFLTK_PLOT_H
#include "navfltk_model.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
namespace desktop {
struct GeoPoint {
    double lat, lon;
};
struct Overlay {
    double lat = 0, lon = 0, grid_lat = 0;
    std::vector<Sight> sights;
    std::vector<Reduction> reductions;
    FixResult fix{};
    GeoPoint geographic(double e, double n) const {
        return {lat + n / 60, lon + e / (60 * std::cos(grid_lat * M_PI / 180))};
    }
    std::pair<double, double> plane(GeoPoint p) const {
        return {(p.lon - lon) * 60 * std::cos(grid_lat * M_PI / 180), (p.lat - lat) * 60};
    }
};
inline Overlay overlay(const std::vector<Sight> &s, const std::vector<Reduction> &r,
                       const FixResult &f) {
    if (s.size() != r.size() || r.size() < 2 || r.size() > 16)
        throw std::runtime_error("A plotting sheet needs 2–16 reduced sights.");
    Overlay o;
    o.lat = f.lat;
    o.lon = f.lon;
    o.sights = s;
    o.reductions = r;
    o.fix = f;
    for (auto &a : r)
        o.grid_lat += a.ap_lat;
    o.grid_lat /= r.size();
    return o;
}
inline std::string xml(const std::string &s) {
    std::string v;
    for (char c : s) {
        if (c == '&')
            v += "&amp;";
        else if (c == '<')
            v += "&lt;";
        else if (c == '>')
            v += "&gt;";
        else if (c == '\"')
            v += "&quot;";
        else
            v += c;
    }
    return v;
}
} // namespace desktop
#include "navfltk_sheet.h"
#endif
