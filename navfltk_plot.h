/* SPDX-License-Identifier: MIT
 * Geographic vector overlay, independent of FLTK and of any chart provider.
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
struct VectorPath {
    std::vector<GeoPoint> points;
    int layer, color;
    bool dashed;
};
struct VectorLabel {
    GeoPoint point;
    std::string text;
    int layer, color;
};
struct Overlay {
    double lat = 0, lon = 0, grid_lat = 0, extent = 10;
    std::vector<VectorPath> paths;
    std::vector<VectorLabel> labels;
    GeoPoint geographic(double e, double n) const {
        return {lat + n / 60, lon + e / (60 * std::cos(grid_lat * M_PI / 180))};
    }
    std::pair<double, double> plane(GeoPoint p) const {
        return {(p.lon - lon) * 60 * std::cos(grid_lat * M_PI / 180), (p.lat - lat) * 60};
    }
};
inline Overlay overlay(const std::vector<Sight> &s, const std::vector<Reduction> &r,
                       const FixResult &f) {
    Overlay o;
    o.lat = f.lat;
    o.lon = f.lon;
    for (auto &a : r)
        o.grid_lat += a.ap_lat;
    o.grid_lat /= r.size();
    auto lines = geometry(r, f);
    for (auto &l : lines)
        o.extent =
            std::max(o.extent, 1.3 * std::max({std::fabs(l.e), std::fabs(l.n), std::fabs(l.ae),
                                               std::fabs(l.an), std::fabs(l.original_e),
                                               std::fabs(l.original_n)}));
    o.extent = std::max(o.extent, 1.5 * f.semi_major_nm);
    auto path = [&](std::vector<std::pair<double, double>> points, int layer, int color,
                    bool dashed = false) {
        VectorPath p;
        p.layer = layer;
        p.color = color;
        p.dashed = dashed;
        for (auto &v : points)
            p.points.push_back(o.geographic(v.first, v.second));
        o.paths.push_back(p);
    };
    double raw = o.extent / 4, power = std::pow(10, std::floor(std::log10(raw)));
    double step =
        power * (raw / power <= 1 ? 1 : (raw / power <= 2 ? 2 : (raw / power <= 5 ? 5 : 10)));
    for (double v = std::ceil(-o.extent / step) * step; v <= o.extent; v += step) {
        path({{v, -o.extent}, {v, o.extent}}, 0, 0);
        path({{-o.extent, v}, {o.extent, v}}, 0, 0);
        char b[64];
        auto p = o.geographic(0, v);
        navsight_fmt_lat(p.lat, b, sizeof b);
        o.labels.push_back({o.geographic(-o.extent + 0.3, v), b, 0, 0});
        p = o.geographic(v, 0);
        navsight_fmt_lon(p.lon, b, sizeof b);
        o.labels.push_back({o.geographic(v, -o.extent + 0.3), b, 0, 0});
    }
    // Compass circle stays vector geometry in the geographic scene.
    std::vector<std::pair<double, double>> circle;
    for (int i = 0; i <= 72; ++i) {
        double a = i * 5 * M_PI / 180;
        circle.push_back({o.extent * .8 * std::sin(a), o.extent * .8 * std::cos(a)});
    }
    path(circle, 0, 0);
    o.labels.push_back({o.geographic(0, o.extent * .85), "N / 000 T", 0, 0});
    for (size_t i = 0; i < lines.size(); ++i) {
        auto &l = lines[i];
        double t = 3 * o.extent, cross = o.extent * .015;
        path({{l.e - t * l.un, l.n + t * l.ue}, {l.e + t * l.un, l.n - t * l.ue}}, 1,
             1 + (int)i % 5);
        path({{l.ae, l.an}, {l.e, l.n}}, 1, 1 + (int)i % 5, true);
        path({{l.original_e, l.original_n}, {l.ae, l.an}}, 1, 1 + (int)i % 5, true);
        path({{l.ae - cross, l.an}, {l.ae + cross, l.an}}, 1, 1 + (int)i % 5);
        path({{l.ae, l.an - cross}, {l.ae, l.an + cross}}, 1, 1 + (int)i % 5);
        // Label near the closest point of the LOP to the fix, always in view.
        double along = l.e * l.un - l.n * l.ue;
        char b[128];
        std::snprintf(b, sizeof b, "#%d %s  %.1f NM %s", s[i].id,
                      !std::strcmp(s[i].body, "star") ? s[i].star : s[i].body,
                      std::fabs(r[i].intercept_nm), r[i].intercept_nm >= 0 ? "T" : "A");
        o.labels.push_back(
            {o.geographic(l.e - along * l.un, l.n + along * l.ue), b, 2, 1 + (int)i % 5});
    }
    if (f.have_ellipse) {
        std::vector<std::pair<double, double>> pts;
        double b = f.ellipse_brg * M_PI / 180;
        for (int i = 0; i <= 72; ++i) {
            double a = i * 5 * M_PI / 180, u = f.semi_major_nm * std::cos(a),
                   v = f.semi_minor_nm * std::sin(a);
            pts.push_back({u * std::sin(b) + v * std::cos(b), u * std::cos(b) - v * std::sin(b)});
        }
        path(pts, 3, 6, true);
    }
    double cross = o.extent * .025;
    path({{-cross, -cross}, {cross, cross}}, 3, 6);
    path({{-cross, cross}, {cross, -cross}}, 3, 6);
    o.labels.push_back({o.geographic(cross * 1.6, -cross * 1.6), "FIX", 3, 6});
    return o;
}
inline const char *vector_color(int n) {
    const char *colors[] = {"#9aaeb6", "#007f89", "#b85826", "#6255a5",
                            "#337cac", "#8b6436", "#c22f4a"};
    return colors[n % 7];
}
inline std::string xml(std::string s) {
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
/* Transparent SVG, including projection metadata; no screenshot or background. */
inline std::string svg(const Overlay &o, const bool visible[4], double opacity) {
    std::ostringstream out;
    out << std::setprecision(12);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 1000\">\n";
    out << "<title>NAVALM Universal Plotting Chart vector overlay</title>\n<metadata>"
        << "Local tangent plane; WGS84 longitude/latitude anchors; origin_lat=" << o.lat
        << "; origin_lon=" << o.lon << "; scale_lat=" << o.grid_lat
        << "; half_extent_nm=" << o.extent
        << ". Reproject vectors for the target chart. Not a raster chart.</metadata>\n";
    out << "<defs><clipPath id=\"sheet\"><rect width=\"1000\" "
           "height=\"1000\"/></clipPath></defs>\n<g clip-path=\"url(#sheet)\" opacity=\""
        << opacity << "\">\n";
    auto xy = [&](GeoPoint g) {
        auto p = o.plane(g);
        return std::make_pair(500 + 500 * p.first / o.extent, 500 - 500 * p.second / o.extent);
    };
    const char *names[] = {"graticule", "lines-of-position", "labels", "fix-and-uncertainty"};
    for (int layer = 0; layer < 4; ++layer)
        if (visible[layer]) {
            out << "<g id=\"" << names[layer] << "\">\n";
            for (auto &p : o.paths)
                if (p.layer == layer) {
                    out << "<polyline fill=\"none\" stroke=\"" << vector_color(p.color)
                        << "\" stroke-width=\"" << (layer == 0 ? 1 : 2) << "\"";
                    if (p.dashed)
                        out << " stroke-dasharray=\"6 5\"";
                    out << " points=\"";
                    for (auto &g : p.points) {
                        auto q = xy(g);
                        out << q.first << "," << q.second << " ";
                    }
                    out << "\"/>\n";
                }
            for (auto &l : o.labels)
                if (l.layer == layer) {
                    auto q = xy(l.point);
                    out << "<text x=\"" << q.first + 4 << "\" y=\"" << q.second - 5
                        << "\" font-family=\"sans-serif\" font-size=\"14\" fill=\""
                        << vector_color(l.color) << "\">" << xml(l.text) << "</text>\n";
                }
            out << "</g>\n";
        }
    out << "</g></svg>\n";
    return out.str();
}
/* Geographic paths for a later chart renderer, preserving layer identities. */
inline std::string geojson(const Overlay &o) {
    std::ostringstream out;
    out << std::setprecision(12) << "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    for (auto &p : o.paths) {
        if (!first)
            out << ",";
        first = false;
        out << "{\"type\":\"Feature\",\"properties\":{\"layer\":" << p.layer << ",\"color\":\""
            << vector_color(p.color) << "\",\"dashed\":" << (p.dashed ? "true" : "false")
            << "},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[";
        bool point = true;
        for (auto &g : p.points) {
            if (!point)
                out << ",";
            point = false;
            out << "[" << g.lon << "," << g.lat << "]";
        }
        out << "]}}";
    }
    out << "]}\n";
    return out.str();
}
} // namespace desktop
#endif
