/* SPDX-License-Identifier: MIT */
#include "navfltk_plot.h"
#include <functional>

#include "navplatform.h"
#include <filesystem>
#include <fstream>
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
#include "desktop_map.h"
int main() {
    try {
        fs::path tmp = nav_temp_dir();
        struct Cleanup {
            fs::path path;
            ~Cleanup() {
                std::error_code e;
                fs::remove_all(path, e);
            }
        } cleanup{tmp};
        map_checks(tmp);
        auto logfile = tmp / "sights.save";
        nav_setenv("NAVALM_SAVE", logfile.u8string().c_str(), 1);
        nav_setenv("NAVALMRC", (tmp / "defaults.rc").u8string().c_str(), 1);
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
        require(angle_entry(34 + 2.5 / 60, 2) == "34 02.5" && angle_entry(117.365) == "117 21.9" &&
                    angle_entry(94.62, 2) == "94 37.2" &&
                    angle_entry(12 + 59.99999999 / 60) == "013 00.0",
                "degrees/minutes display and minute carry");
        for (double value : {0., 0.000000001, 34.0416666666667, 89.999999999, 117.365, 180.})
            require(std::fabs(angle(angle_entry(value), "angle", 180) - value) < 1e-9,
                    "angle display roundtrip preserves precision");
        std::puts("PASS desktop input validation and degrees/minutes display roundtrip");
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
        auto direct = s;
        direct.reduce_at_dr = 1;
        auto dr = reduce(direct);
        require(dr.ap_lat == s.dr_lat && dr.ap_lon == s.dr_lon &&
                    std::fabs(dr.lha_deg - std::round(dr.lha_deg)) > .01,
                "direct reduction preserves fractional DR and LHA");
        require(dr.ho == red.ho, "reduction method does not change altitude corrections");
        auto short_sight=s;short_sight.artificial=0;short_sight.hs_deg=s.hs_deg/2;
        short_sight.he=10;short_sight.short_horizon_nm=.5;
        auto short_red=reduce(short_sight);
        require(std::abs(short_red.dip_min-11.5)<.05,"Bowditch 2019 Table 14: 10 feet, 0.5 NM gives 11.5 arcminutes");
        short_sight.he=3.048;short_sight.he_metres=1;
        require(std::abs(reduce(short_sight).dip_min-short_red.dip_min)<1e-10,"short dip feet/metres agree");
        short_sight.short_horizon_nm=5;rejects([&]{reduce(short_sight);});
        short_sight.short_horizon_nm=-1;rejects([&]{reduce(short_sight);});
        short_sight.short_horizon_nm=.5;short_sight.artificial=1;rejects([&]{reduce(short_sight);});
        std::puts("PASS short-horizon dip: Bowditch reference, units and invalid/beyond-horizon rejection");
        // Independent spherical cosine law and azimuth check, including hemispheres,
        // meridian crossing, the date line and polar latitudes.
        constexpr double radians = 3.14159265358979323846 / 180;
        for (double latitude : {-90., -74.3, -34.12, 0., 34.0416666666667, 74.3, 90.}) {
            for (double longitude : {-180., -117.365, -10.2, 0., 65.1, 179.9}) {
                auto test = direct;
                test.dr_lat = latitude;
                test.dr_lon = longitude;
                double h = (red.gha + longitude) * radians;
                double phi = latitude * radians, dec = red.dec * radians;
                double expected = std::asin(std::sin(phi)*std::sin(dec) +
                    std::cos(phi)*std::cos(dec)*std::cos(h)) / radians;
                Reduction result{};
                int status = navsight_reduce(&test, &result, err, sizeof err);
                if (expected < -1) {
                    require(status != 0, "below-horizon direct reduction rejected");
                    continue;
                }
                require(status == 0 && std::fabs(result.hc_min / 60 - expected) <= .05/60 + 1e-10,
                        "direct Hc agrees with independent spherical calculation");
                if (std::fabs(latitude) < 90) {
                    double z = std::acos(std::clamp((std::sin(dec) - std::sin(phi)*
                        std::sin(expected*radians))/(std::cos(phi)*std::cos(expected*radians)), -1., 1.)) / radians;
                    if (std::sin(h) > 0) z = 360 - z;
                    require(std::fabs(std::remainder(result.zn-z,360)) < 1e-7,
                            "direct azimuth quadrant agrees with cosine law");
                }
            }
        }
        auto overhead = direct;
        overhead.dr_lat = red.dec;
        overhead.dr_lon = std::remainder(-red.gha, 360);
        Reduction undefined{};
        require(navsight_reduce(&overhead, &undefined, err, sizeof err) != 0 &&
                    std::string(err).find("undefined") != std::string::npos,
                "zenith has no arbitrary azimuth");
        std::puts("PASS direct-at-DR position, fractional LHA, Hc, azimuth and singularity checks");
        auto zero = s;
        zero.dr_lat = zero.dr_lon = 0;
        require(!navsight_validate(&zero, err, sizeof err), "zero/zero coordinates are valid");
        Reduction invalid_red{};
        require(navsight_reduce(&zero, &invalid_red, err, sizeof err) != 0 &&
                    std::string(err).find("below the horizon") != std::string::npos,
                "unsuitable zero DR returns a reduction error");
        for (int bad = 0; bad < 5; ++bad) {
            auto invalid = s;
            if (bad == 0)
                invalid.dr_lat = std::numeric_limits<double>::quiet_NaN();
            if (bad == 1)
                invalid.dr_lon = 181;
            if (bad == 2)
                invalid.mo = 0;
            if (bad == 3)
                invalid.s = std::numeric_limits<double>::infinity();
            if (bad == 4)
                invalid.hs_deg = std::numeric_limits<double>::quiet_NaN();
            require(navsight_reduce(&invalid, &invalid_red, err, sizeof err) != 0,
                    "shared reducer rejects invalid saved inputs before numeric conversions");
            rejects([&] { reduce(invalid); });
        }
        Sight pair[] = {s, s};
        Reduction parallel[] = {red, red};
        FixResult no_fix{};
        require(navsight_fix(pair, parallel, 2, 0, 0, 0, 0, 0, 0, 0, &no_fix, err, sizeof err) !=
                        0 &&
                    std::string(err).find("parallel") != std::string::npos,
                "no solution returns a polite error");
        parallel[1].zn = std::numeric_limits<double>::quiet_NaN();
        require(navsight_fix(pair, parallel, 2, 0, 0, 0, 0, 0, 0, 0, &no_fix, err, sizeof err) != 0,
                "nonfinite LOP cannot reach the solver");
        parallel[1].zn = red.zn + 40;
        require(navsight_fix(pair, parallel, 2, 1, 2026, 0, 19, 12, 0, 0, &no_fix, err,
                             sizeof err) != 0,
                "invalid explicit fix UTC is rejected");
        std::puts("PASS fix input regression: valid zero/zero, unsuitable DR, invalid stored data, "
                  "parallel LOPs and fix UTC");
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
        require(store.v[0].reduce_at_dr == 0, "old store defaults to HO 229");
        store.v[0].reduce_at_dr = 1;
        if (navsight_store_save(&store, err, sizeof err)) throw std::runtime_error(err);
        navsight_store_free(&store);
        if (navsight_store_load(&store, err, sizeof err)) throw std::runtime_error(err);
        require(store.v[0].reduce_at_dr == 1 && reduce(store.v[0]).ap_lat == store.v[0].dr_lat,
                "saved direct method survives reloading and re-reduction");
        store.v[0]=s;store.v[0].artificial=0;store.v[0].hs_deg=s.hs_deg/2;
        store.v[0].he=10;store.v[0].short_horizon_nm=.5;
        if(navsight_store_save(&store,err,sizeof err))throw std::runtime_error(err);
        navsight_store_free(&store);
        if(navsight_store_load(&store,err,sizeof err))throw std::runtime_error(err);
        require(store.v[0].short_horizon_nm==.5 && std::abs(reduce(store.v[0]).dip_min-11.5)<.05,
                "short-horizon distance survives save/reopen and re-reduction");
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
            auto sheet = make_sheet(o);
            auto count = [](const Sheet &d, const char *role) {
                return std::count_if(d.marks.begin(), d.marks.end(),
                                     [&](const Mark &m) { return m.role == role; });
            };
            require(count(sheet, "compass-tick") == 72 && count(sheet, "detail-lop") == 3 &&
                        count(sheet, "assumed-position") == 3 &&
                        count(sheet, "intercept-arrow") == 3,
                    "web sheet compass, AP markers, arrows and enlarged cocked hat");
            auto mixed_sights = sights;
            mixed_sights[0].reduce_at_dr = 1;
            auto mixed_red = r;
            mixed_red[0] = reduce(mixed_sights[0]);
            FixResult mixed_fix{};
            require(!navsight_fix(mixed_sights.data(), mixed_red.data(), mixed_sights.size(),
                        0,0,0,0,0,0,0,&mixed_fix,err,sizeof err), "mixed AP/DR fix succeeds");
            auto mixed_overlay = overlay(mixed_sights,mixed_red,mixed_fix);
            require(mixed_overlay.reductions[0].ap_lat == sights[0].dr_lat &&
                        std::fabs(mixed_fix.lat-f.lat)*60 < .15 &&
                        std::fabs(mixed_fix.lon-f.lon)*60 < .15,
                    "mixed fix and plotting retain the direct DR reference");
            const bool mixed_visible[4] = {true,true,true,true};
            auto mixed_svg = svg(mixed_overlay, mixed_visible, 1);
            require(mixed_svg.find("DR 1") != std::string::npos, "direct plotting marker is labelled DR");
            auto two = o;
            two.sights.pop_back();
            two.reductions.pop_back();
            require(count(make_sheet(two), "detail-lop") == 0, "two LOPs have no cocked hat inset");
            auto parallel = o;
            for (auto &v : parallel.reductions)
                v.zn = 90;
            require(count(make_sheet(parallel), "detail-lop") == 0,
                    "parallel LOPs have no cocked hat inset");
            auto polar = o;
            polar.lat = polar.grid_lat = 89.9999;
            for (auto &v : polar.reductions)
                v.ap_lat = 89.9999;
            require(make_sheet(polar).marks.size() < 400, "near-pole grid remains bounded");
            bool visible[] = {true, true, true, true};
            auto image = svg(o, visible, .55);
            require(image.find("opacity=\"0.55\"") != std::string::npos, "SVG opacity");
            require(image.find("<rect fill=") == std::string::npos, "SVG transparency");
            visible[0] = false;
            require(svg(o, visible, 1).find("class=\"graticule\"") == std::string::npos,
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
