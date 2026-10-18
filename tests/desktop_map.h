/* SPDX-License-Identifier: MIT */
#include "navfltk_map.h"
static void map_checks(const fs::path &tmp) {
    // One polygon with an interior water hole; SHP uses mixed endian fields.
    std::vector<unsigned char> bytes(320);
    auto u32 = [&](size_t at, uint32_t n, bool big = false) {
        for (int k = 0; k < 4; ++k)
            bytes[at + k] = (n >> (8 * (big ? 3 - k : k))) & 255;
    };
    auto real = [&](size_t at, double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, 8);
        for (int k = 0; k < 8; ++k)
            bytes[at + k] = (bits >> (8 * k)) & 255;
    };
    u32(0, 9994, true);
    u32(24, bytes.size() / 2, true);
    u32(28, 1000);
    u32(32, 5);
    u32(100, 1, true);
    u32(104, (bytes.size() - 108) / 2, true);
    u32(108, 5);
    for (size_t at : {size_t(36), size_t(112)}) {
        real(at, -119);
        real(at + 8, 33);
        real(at + 16, -117);
        real(at + 24, 35);
    }
    u32(144, 2);
    u32(148, 10);
    u32(152, 0);
    u32(156, 5);
    double xy[][2] = {{-119, 33},     {-119, 35},     {-117, 35},     {-117, 33},
                      {-119, 33},     {-118.2, 33.8}, {-117.8, 33.8}, {-117.8, 34.2},
                      {-118.2, 34.2}, {-118.2, 33.8}};
    for (int i = 0; i < 10; ++i) {
        real(160 + i * 16, xy[i][0]);
        real(168 + i * 16, xy[i][1]);
    }
    auto path = tmp / "land.shp";
    auto write = [&] {
        std::ofstream out(path, std::ios::binary);
        out.write((char *)bytes.data(), bytes.size());
    };
    write();
    auto land = load_land_map(path.u8string());
    require(land->polygons.size() == 1 && land->points == 10 && land->polygons[0].rings.size() == 2,
            "SHP polygon and water hole survive parsing");
    Overlay o;
    o.lat = o.grid_lat = 34;
    o.lon = -118;
    Sheet sheet;
    sheet.scale = 5;
    auto contours = map_contours(*land, o, sheet);
    require(contours.size() == 1 && contours[0].size() == 2, "projected polygon retains hole");
    for (auto &poly : contours)
        for (auto &ring : poly)
            for (auto p : ring) {
                auto b = sheet.clips[0];
                require(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= b.x - 1e-8 &&
                            p.x <= b.x + b.w + 1e-8 && p.y >= b.y - 1e-8 && p.y <= b.y + b.h + 1e-8,
                        "coastline clipped to chart bounds");
            }
    auto q = xy[6];
    auto plane = o.plane({q[1], q[0]});
    auto pixel = sheet.canvas(plane.first, plane.second);
    auto position = sheet.geographic(o, pixel);
    require(std::fabs(position.lat - q[1]) < 1e-10 && std::fabs(position.lon - q[0]) < 1e-10,
            "map and vector projection share geographic anchors");
    // Regress the Windows contour-array overflow with 64 disconnected islands,
    // each containing a water hole (128 contours in one polygon).
    std::vector<std::vector<Point>> islands;
    for (int row = 0; row < 8; ++row)
        for (int column = 0; column < 8; ++column) {
            double x = column * 20, y = row * 20;
            islands.push_back({{x, y}, {x + 16, y}, {x + 16, y + 16}, {x, y + 16}});
            islands.push_back({{x + 5, y + 5}, {x + 11, y + 5}, {x + 11, y + 11}, {x + 5, y + 11}});
        }
    auto mask = [&](const auto &rings, double left = 0, double top = 0, double scale = 1) {
        std::vector<bool> filled(160 * 160);
        for (auto span : map_fill_spans(rings, {0, 0, 160, 160}, left, top, scale)) {
            require(span.x0 >= 0 && span.x1 < 160 && span.y >= 0 && span.y < 160,
                    "fill spans stay in the native viewport");
            for (int x = span.x0; x <= span.x1; ++x)
                filled[span.y * 160 + x] = true;
        }
        return filled;
    };
    auto pixels = mask(islands);
    require(std::count(pixels.begin(), pixels.end(), true) == 64 * (16 * 16 - 6 * 6),
            "128-contour fill retains all islands and water holes");
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 160; ++x)
            require(pixels[y * 160 + x] ==
                        (x % 20 < 16 && y % 20 < 16 &&
                         !(x % 20 >= 5 && x % 20 < 11 && y % 20 >= 5 && y % 20 < 11)),
                    "raster agrees with the independent island/lagoon geometry");
    for (auto &ring : islands)
        std::reverse(ring.begin(), ring.end());
    require(mask(islands) == pixels, "land fill does not depend on ring winding");
    auto zoomed = mask(islands, -15, 17, 2);
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 160; ++x) {
            double xx = (x + .5 + 15) / 2, yy = (y + .5 - 17) / 2;
            bool inside = xx >= 0 && yy >= 0 && xx < 160 && yy < 160;
            double rx = std::fmod(xx, 20), ry = std::fmod(yy, 20);
            bool expected =
                inside && rx < 16 && ry < 16 && !(rx >= 5 && rx < 11 && ry >= 5 && ry < 11);
            require(zoomed[y * 160 + x] == expected,
                    "zoom/pan clips the same geographic land and holes");
        }
    std::puts("PASS Windows map regression: 128 contours, island/lagoon mask, winding, zoom and "
              "clipped pan");
    // Bad headers, unbounded coordinates, open rings and corrupt indices fail closed.
    auto valid = bytes;
    for (int failure = 0; failure < 6; ++failure) {
        bytes = valid;
        if (failure == 0)
            u32(24, 999, true);
        if (failure == 1)
            u32(32, 3);
        if (failure == 2)
            real(128, 1000000);
        if (failure == 3)
            u32(156, 99);
        if (failure == 4)
            real(224, -118.99);
        if (failure == 5)
            real(160, std::numeric_limits<double>::quiet_NaN());
        write();
        rejects([&] { load_land_map(path.u8string()); });
    }
    auto ini = tmp / "opencpn.ini";
    std::ofstream(ini) << "[Settings]\r\nChartDir1=/ignore^abc\r\n[ChartDirectories]\r\n"
                          "ChartDir1=/Charts with spaces^checksum\r\n";
    auto candidates = opencpn_map_paths(ini.u8string());
    require(candidates.size() == 4 &&
                candidates[0] == "/Charts with spaces/OSMSHP/basemap_medium.shp",
            "OpenCPN chart directory parser handles spaces, suffix and CRLF");
    if (fs::exists(".local/maps/ne_10m_land.shp")) {
        auto world = load_land_map(".local/maps/ne_10m_land.shp");
        require(world->points > 100000 && world->polygons.size() > 5,
                "real Natural Earth land data");
        auto coastal = map_contours(*world, o, sheet);
        require(!coastal.empty(), "real California coast intersects geographic viewport");
        Overlay origin;
        Sheet wide;
        wide.scale = .035;
        auto broad = map_contours(*world, origin, wide);
        size_t max_contours = 0, span_count = 0;
        for (const auto &polygon : broad) {
            max_contours = std::max(max_contours, polygon.size());
            span_count += map_fill_spans(polygon, wide.clips[0]).size();
        }
        require(max_contours > 20 && span_count > 0,
                "real world view exceeds Windows legacy contour limit and fills safely");
        std::printf("PASS Natural Earth: %zu polygons, %zu coordinates; broad zero-origin view %zu "
                    "contours (old Windows limit 20), %zu spans\n",
                    world->polygons.size(), world->points, max_contours, span_count);
    }
    std::puts("PASS offline map: multipart/holes, clipping, geographic anchors, malformed files, "
              "OpenCPN paths");
}
