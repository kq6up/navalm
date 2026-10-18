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
        std::printf("PASS Natural Earth: %zu polygons, %zu coordinates\n", world->polygons.size(),
                    world->points);
    }
    std::puts("PASS offline map: multipart/holes, clipping, geographic anchors, malformed files, "
              "OpenCPN paths");
}
