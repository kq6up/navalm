/* SPDX-License-Identifier: MIT
 * Offline WGS84 land polygons (ESRI Polygon/PolygonZ shapefiles).
 * Natural Earth and OpenCPN OSM basemaps use this format. No GIS runtime.
 */
#ifndef NAVFLTK_MAP_H
#define NAVFLTK_MAP_H
#include "navfltk_plot.h"
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
namespace desktop {
struct LandPolygon {
    double west, south, east, north;
    std::vector<std::vector<GeoPoint>> rings;
};
struct LandMap {
    std::string path, name;
    size_t points = 0;
    std::vector<LandPolygon> polygons;
};
inline uint32_t map_u32(const unsigned char *p, bool big = false) {
    if (big)
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline double map_double(const unsigned char *p) {
    uint64_t bits = uint64_t(map_u32(p)) | (uint64_t(map_u32(p + 4)) << 32);
    double value;
    std::memcpy(&value, &bits, 8);
    return value;
}
inline std::shared_ptr<LandMap> load_land_map(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open coastline map: " + path);
    auto size = file.tellg();
    if (size < 100 || size > 256 * 1024 * 1024)
        throw std::runtime_error("Choose a WGS84 land shapefile between 100 bytes and 256 MB "
                                 "(OpenCPN low/medium or Natural Earth).");
    file.seekg(0);
    unsigned char header[100];
    if (!file.read((char *)header, 100) || map_u32(header, true) != 9994 ||
        map_u32(header + 28) != 1000 ||
        uint64_t(map_u32(header + 24, true)) * 2 != uint64_t(size) ||
        (map_u32(header + 32) != 5 && map_u32(header + 32) != 15))
        throw std::runtime_error("Not a supported Polygon/PolygonZ shapefile.");
    auto map = std::make_shared<LandMap>();
    map->path = path;
    size_t consumed = 100;
    while (consumed < size_t(size)) {
        unsigned char h[8];
        if (!file.read((char *)h, 8))
            throw std::runtime_error("Truncated shapefile record.");
        size_t bytes = size_t(map_u32(h + 4, true)) * 2;
        if (bytes < 4 || bytes > 64 * 1024 * 1024 || bytes > size_t(size) - consumed - 8)
            throw std::runtime_error("Invalid shapefile record size.");
        std::vector<unsigned char> record(bytes);
        if (!file.read((char *)record.data(), bytes))
            throw std::runtime_error("Truncated map data.");
        consumed += bytes + 8;
        const auto *p = record.data();
        uint32_t type = map_u32(p);
        if (type == 0)
            continue;
        if ((type != 5 && type != 15) || bytes < 44)
            throw std::runtime_error("Map contains non-polygon records.");
        LandPolygon poly{
            map_double(p + 4), map_double(p + 12), map_double(p + 20), map_double(p + 28), {}};
        if (!std::isfinite(poly.west) || !std::isfinite(poly.south) || !std::isfinite(poly.east) ||
            !std::isfinite(poly.north) || poly.west < -180.0001 || poly.east > 180.0001 ||
            poly.south < -90.0001 || poly.north > 90.0001 || poly.west > poly.east ||
            poly.south > poly.north)
            throw std::runtime_error(
                "Map must use geographic WGS84 longitude/latitude, not projected metres.");
        size_t parts = map_u32(p + 36), count = map_u32(p + 40);
        if (!parts || parts > count || parts > 1000000 || count > 4000000 ||
            44 + parts * 4 + count * 16 > bytes || map_u32(p + 44) != 0)
            throw std::runtime_error("Invalid map polygon part table.");
        const auto *coordinates = p + 44 + parts * 4;
        for (size_t k = 0; k < parts; ++k) {
            size_t first = map_u32(p + 44 + k * 4),
                   last = k + 1 < parts ? map_u32(p + 48 + k * 4) : count;
            if (first >= last || last > count || last - first < 4)
                throw std::runtime_error("Invalid polygon ring.");
            std::vector<GeoPoint> ring;
            ring.reserve(last - first);
            for (size_t i = first; i < last; ++i) {
                double lon = map_double(coordinates + i * 16),
                       lat = map_double(coordinates + i * 16 + 8);
                if (!std::isfinite(lon) || !std::isfinite(lat) || lon < poly.west - .0001 ||
                    lon > poly.east + .0001 || lat < poly.south - .0001 || lat > poly.north + .0001)
                    throw std::runtime_error("Invalid map coordinate.");
                ring.push_back({lat, lon});
            }
            if (std::fabs(ring.front().lat - ring.back().lat) > 1e-7 ||
                std::fabs(ring.front().lon - ring.back().lon) > 1e-7)
                throw std::runtime_error("Map polygon ring is not closed.");
            poly.rings.push_back(std::move(ring));
        }
        map->points += count;
        if (map->points > 12000000 || map->polygons.size() > 1000000)
            throw std::runtime_error("Map exceeds supported size.");
        map->polygons.push_back(std::move(poly));
    }
    if (map->polygons.empty())
        throw std::runtime_error("Map contains no land polygons.");
    map->name = path.find("ne_10m_land") != std::string::npos
                    ? "Natural Earth 1:10 million — land and coastline"
                    : "WGS84 land and coastline — offline";
    return map;
}
// Clip in sheet coordinates before passing vertices to native drawing APIs.
inline std::vector<Point> clip_map_ring(std::vector<Point> ring, Box box) {
    for (int edge = 0; edge < 4; ++edge) {
        std::vector<Point> next;
        auto inside = [&](Point p) {
            return edge == 0   ? p.x >= box.x
                   : edge == 1 ? p.x <= box.x + box.w
                   : edge == 2 ? p.y >= box.y
                               : p.y <= box.y + box.h;
        };
        auto intersect = [&](Point a, Point b) {
            if (edge < 2) {
                double x = edge == 0 ? box.x : box.x + box.w;
                return Point{x, a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x)};
            }
            double y = edge == 2 ? box.y : box.y + box.h;
            return Point{a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y), y};
        };
        if (ring.empty())
            break;
        Point previous = ring.back();
        bool was = inside(previous);
        for (auto p : ring) {
            bool now = inside(p);
            if (now != was)
                next.push_back(intersect(previous, p));
            if (now)
                next.push_back(p);
            previous = p;
            was = now;
        }
        ring = std::move(next);
    }
    return ring;
}
using LandContours = std::vector<std::vector<std::vector<Point>>>;
inline LandContours map_contours(const LandMap &map, const Overlay &o, const Sheet &sheet) {
    LandContours contours;
    Box b = sheet.clips[0];
    GeoPoint nw = sheet.geographic(o, {b.x, b.y}), se = sheet.geographic(o, {b.x + b.w, b.y + b.h});
    double cl = std::cos(o.grid_lat * M_PI / 180);
    for (const auto &poly : map.polygons)
        for (double shift : {-360., 0., 360.}) {
            if (poly.north < se.lat || poly.south > nw.lat || poly.east + shift < nw.lon ||
                poly.west + shift > se.lon)
                continue;
            std::vector<std::vector<Point>> rings;
            for (const auto &r : poly.rings) {
                std::vector<Point> ring;
                ring.reserve(r.size());
                for (auto p : r)
                    ring.push_back(
                        sheet.canvas((p.lon + shift - o.lon) * 60 * cl, (p.lat - o.lat) * 60));
                ring = clip_map_ring(std::move(ring), b);
                if (ring.size() >= 3)
                    rings.push_back(std::move(ring));
            }
            if (!rings.empty())
                contours.push_back(std::move(rings));
        }
    return contours;
}
inline std::vector<std::string> opencpn_map_paths(const std::string &ini) {
    std::ifstream file(ini);
    std::vector<std::string> out;
    std::string line, section;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() > 1 && line[0] == '[') {
            section = line;
            continue;
        }
        if (section != "[ChartDirectories]" || line.rfind("ChartDir", 0) != 0)
            continue;
        auto at = line.find('=');
        if (at == std::string::npos)
            continue;
        std::string root = line.substr(at + 1);
        at = root.find('^');
        if (at != std::string::npos)
            root.resize(at);
        for (const auto &suffix : {"/OSMSHP/basemap_medium.shp", "/OSMSHP/basemap_low.shp",
                                   "/basemap_medium.shp", "/basemap_low.shp"})
            out.push_back(root + suffix);
    }
    return out;
}
constexpr const char *world_map_url = "https://raw.githubusercontent.com/nvkelso/"
                                      "natural-earth-vector/v5.1.2/10m_physical/ne_10m_land.shp";
} // namespace desktop
#endif
