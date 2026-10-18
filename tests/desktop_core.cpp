/* SPDX-License-Identifier: MIT */
#include "navfltk_plot.h"
#include <functional>

#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace desktop;
static void require(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
static void rejects(std::function<void()> f) {
    try {
        f();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("invalid input was accepted");
}
int main() {
    try {
        char pattern[] = "/tmp/navalm-core-test-XXXXXX";
        char *dir = mkdtemp(pattern);
        if (!dir)
            throw std::runtime_error("mkdtemp failed");
        fs::path tmp = dir;
        struct Cleanup {
            fs::path path;
            ~Cleanup() {
                std::error_code e;
                fs::remove_all(path, e);
            }
        } cleanup{tmp};
        auto logfile = tmp / "sights.save";
        setenv("NAVALM_SAVE", logfile.c_str(), 1);
        setenv("NAVALMRC", (tmp / "defaults.rc").c_str(), 1);
        require(std::fabs(angle("94 37.2", "Hs", 180) - (94 + 37.2 / 60)) < 1e-10,
                "degrees and minutes");
        for (const char *s : {"", "nan", "inf", "1x", "-1", "91", "34 60", "34.5 12", "34 1 junk"})
            rejects([&] { angle(s, "Latitude", 90); });
        for (const char *s : {"nan", "", "1e500", "junk"})
            rejects([&] { number(s, "value", 0, 1); });
        Sight s{};
        date_time("2024-02-29", "23:59:59.5", s);
        for (const char *d :
             {"2026-02-29", "2026-04-31", "2026-13-01", "2026-01-00", "2026-10-18x"})
            rejects([&] { date_time(d, "12:00:00", s); });
        for (const char *t : {"24:00:00", "12:60:00", "12:00:nan", "12:00:60", "12:00:00x"})
            rejects([&] { date_time("2026-10-18", t, s); });
        std::puts("PASS desktop input validation");
        nav_engine_set_ephemeris_path("ephem/de440s.nav");
        char err[512]{};
        nav_engine_select("precise");
        if (nav_engine_init(err, sizeof err))
            throw std::runtime_error(err);
        date_time("2026-09-19", "21:38:05", s);
        std::strcpy(s.body, "sun");
        s.hs_deg = 94 + 37.2 / 60;
        s.artificial = 1;
        s.limb = 1;
        s.temp_c = 30;
        s.press_mb = 1013;
        s.dr_lat = 34 + 2.5 / 60;
        s.dr_lon = -(117 + 21.9 / 60);
        auto red = reduce(s);
        require(std::fabs(red.intercept_nm - 6.8) < .06, "README intercept");
        SightStore store{};
        int id = navsight_store_add(&store, &s);
        require(id == 1 && store.n == 1, "add observation");
        std::strcpy(store.v[0].label, "edited");
        if (navsight_store_save(&store, err, sizeof err))
            throw std::runtime_error(err);
        navsight_store_free(&store);
        if (navsight_store_load(&store, err, sizeof err))
            throw std::runtime_error(err);
        require(store.n == 1 && std::string(store.v[0].label) == "edited", "store roundtrip");
        navsight_store_free(&store);
        std::puts("PASS shared precise reduction and store persistence");
        fs::create_directories(".build/desktop-test");
        for (const char *name : {"stationary", "running"}) {
            fs::copy_file(fs::path("tests/fixtures") / (std::string("desktop-") + name + ".save"),
                          logfile, fs::copy_options::overwrite_existing);
            if (navsight_store_load(&store, err, sizeof err))
                throw std::runtime_error(err);
            std::vector<Sight> sights(store.v, store.v + store.n);
            navsight_store_free(&store);
            std::vector<Reduction> r;
            for (auto &v : sights)
                r.push_back(reduce(v));
            FixResult f{};
            if (navsight_fix(sights.data(), r.data(), sights.size(), 0, 0, 0, 0, 0, 0, 0, &f, err,
                             sizeof err))
                throw std::runtime_error(err);
            double expected_lat = std::string(name) == "stationary" ? 34.036674 : 34.035766;
            double expected_lon = std::string(name) == "stationary" ? -117.360222 : -117.360097;
            require(std::fabs(f.lat - expected_lat) < .000002 &&
                        std::fabs(f.lon - expected_lon) < .000002,
                    "recorded precise fix");
            auto o = overlay(sights, r, f);
            auto lines = geometry(r, f);
            for (size_t i = 0; i < lines.size(); ++i) {
                auto &l = lines[i];
                require(std::fabs(-(l.e * l.ue + l.n * l.un) - f.residual_nm[i]) < 1e-6,
                        "plotted LOP residual");
                auto q = o.geographic(l.e, l.n);
                auto p = o.plane(q);
                require(std::fabs(p.first - l.e) < 1e-8 && std::fabs(p.second - l.n) < 1e-8,
                        "geographic projection round trip");
            }
            bool visible[] = {true, true, true, true};
            auto image = svg(o, visible, .55);
            require(image.find("opacity=\"0.55\"") != std::string::npos, "SVG opacity");
            require(image.find("<rect fill=") == std::string::npos, "SVG transparency");
            visible[0] = false;
            require(svg(o, visible, 1).find("id=\"graticule\"") == std::string::npos,
                    "hide grid layer");
            auto prefix = fs::path(".build/desktop-test") / name;
            std::ofstream(prefix.string() + ".svg") << image;
            std::ofstream(prefix.string() + ".geojson") << geojson(o);
            if (navsight_fix(sights.data(), r.data(), sights.size(), 1, 2026, 9, 20, 4, 0, 0, &f,
                             err, sizeof err))
                throw std::runtime_error(err);
            require(f.fh == 4, "explicit fix time");
            std::printf("PASS %s fix: recorded coordinates, running vectors, solver residuals, "
                        "transparent SVG/GeoJSON\n",
                        name);
        }
        nav_engine_select("classic");
        nav_engine_init(err, sizeof err);
        require(std::isfinite(reduce(s).intercept_nm), "classic reduction");
        s.y = 2300;
        nav_engine_select("precise");
        nav_engine_init(err, sizeof err);
        rejects([&] { reduce(s); });
        nav_engine_shutdown();
        std::puts("All desktop core checks passed.");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Desktop core check failed: %s\n", e.what());
        return 1;
    }
}
