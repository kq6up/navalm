/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_engine_sel.c - engine selection, and the parts of the almanac that do
 * not depend on which engine is in use.
 *
 * navalm_p carries two astronomy engines:
 *
 *   precise   NOVAS C3.1 reading JPL DE440s Chebyshev coefficients.
 *             Milliarcsecond-level, and the reason this tree exists.
 *             Needs the ephemeris file built by tools/mkde440.
 *   classic   XEphem libastro: VSOP87 for the planets and the Moshier
 *             lunar theory.  Self-contained, no data file, arcsecond-level.
 *
 * Both are always built.  The calendar, delta T, body naming and the star
 * catalogue index are shared, so the engines differ only where the
 * astronomy does.
 */

#include "nav_engine.h"
#include "nav_engine_impl.h"
#include "nav_stars_x.h"

#ifndef NAVALM_NO_PRECISE
#include "nav_eph440.h"
#include "nav_solsys440.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAV_PATH_MAX 1024

#ifdef NAVALM_NO_PRECISE
/* A build without the precision engine has only one engine to default to. */
static NavEngine g_engine = NAV_ENGINE_CLASSIC;
#else
static NavEngine g_engine = NAV_ENGINE_PRECISE;
#endif
static char g_ephpath[NAV_PATH_MAX];
static char g_detail[NAV_PATH_MAX + 128];
static int  g_precise_ready = 0;
static char g_error[512];

/* ------------------------------------------------------------ calendar
 *
 * Julian date from a civil date, by the standard algorithm (Meeus,
 * Astronomical Algorithms, chapter 7), with the Gregorian reform at
 * 1582 October 15.  Written out here rather than taken from a library so
 * that both engines share one calendar.
 */
double nav_julian_date(int year, int month, int day,
                       int hour, int minute, double second)
{
    long a, b;
    double jd;

    if (month <= 2) { year -= 1; month += 12; }

    /* Gregorian after 1582 Oct 15, Julian on or before 1582 Oct 4. */
    if (year > 1582 || (year == 1582 && (month > 10 ||
        (month == 10 && day >= 15)))) {
        a = year / 100;
        b = 2 - a + a / 4;
    } else {
        b = 0;
    }

    jd = (double)(long)(365.25 * (year + 4716))
       + (double)(long)(30.6001 * (month + 1))
       + day + b - 1524.5;

    return jd + (hour + minute / 60.0 + second / 3600.0) / 24.0;
}

/* ------------------------------------------------------------ delta T
 *
 * TT - UT1 = 32.184s + (TAI - UTC) - DUT1.
 *
 * TAI - UTC is the leap-second count, which is known exactly, so for any
 * modern date this is better than any polynomial: the only unknown is DUT1,
 * which is under a second and which the caller may supply.  Before the leap
 * second era the polynomial fits of Espenak and Meeus are used instead.
 *
 * Leap seconds through 2025: TAI - UTC has stood at 37s since 2017 Jan 1.
 * Update the table below if another leap second is ever announced.
 */
typedef struct { double jd; double tai_utc; } LeapEntry;

static const LeapEntry leap_table[] = {
    {2441317.5, 10.0}, {2441499.5, 11.0}, {2441683.5, 12.0}, {2442048.5, 13.0},
    {2442413.5, 14.0}, {2442778.5, 15.0}, {2443144.5, 16.0}, {2443509.5, 17.0},
    {2443874.5, 18.0}, {2444239.5, 19.0}, {2444786.5, 20.0}, {2445151.5, 21.0},
    {2445516.5, 22.0}, {2446247.5, 23.0}, {2447161.5, 24.0}, {2447892.5, 25.0},
    {2448257.5, 26.0}, {2448804.5, 27.0}, {2449169.5, 28.0}, {2449534.5, 29.0},
    {2450083.5, 30.0}, {2450630.5, 31.0}, {2451179.5, 32.0}, {2453736.5, 33.0},
    {2454832.5, 34.0}, {2456109.5, 35.0}, {2457204.5, 36.0}, {2457754.5, 37.0}
};
#define NLEAP ((int)(sizeof leap_table / sizeof leap_table[0]))

static double deltat_poly(double year)
{
    double t, u;
    if(year < 1600.0){ u = (year - 2000.0)/100.0; return 62.92 + 32.217*u + 55.89*u*u; }
    if(year < 1700.0){ t = year - 1600.0; return 120.0 - 0.9808*t - 0.01532*t*t + t*t*t/7129.0; }
    if(year < 1800.0){ t = year - 1700.0; return 8.83 + 0.1603*t - 0.0059285*t*t + 0.00013336*t*t*t - t*t*t*t/1174000.0; }
    if(year < 1860.0){ t = year - 1800.0; return 13.72 - 0.332447*t + 0.0068612*t*t + 0.0041116*t*t*t
                                                  - 0.00037436*t*t*t*t + 0.0000121272*t*t*t*t*t; }
    if(year < 1900.0){ t = year - 1860.0; return 7.62 + 0.5737*t - 0.251754*t*t + 0.01680668*t*t*t
                                                  - 0.0004473624*t*t*t*t; }
    if(year < 1920.0){ t = year - 1900.0; return -2.79 + 1.494119*t - 0.0598939*t*t + 0.0061966*t*t*t
                                                  - 0.000197*t*t*t*t; }
    if(year < 1941.0){ t = year - 1920.0; return 21.20 + 0.84493*t - 0.076100*t*t + 0.0020936*t*t*t; }
    if(year < 1961.0){ t = year - 1950.0; return 29.07 + 0.407*t - t*t/233.0 + t*t*t/2547.0; }
    t = year - 1975.0;
    return 45.45 + 1.067*t - t*t/260.0 - t*t*t/718.0;
}

static double tai_minus_utc(double jd_ut)
{
    int i;
    if(jd_ut < leap_table[0].jd) return 0.0;
    for(i = NLEAP-1; i >= 0; i--)
        if(jd_ut >= leap_table[i].jd) return leap_table[i].tai_utc;
    return leap_table[0].tai_utc;
}

double nav_delta_t_with_dut1_seconds(double jd_ut, double dut1_seconds)
{
    if(jd_ut < leap_table[0].jd){
        double year = 2000.0 + (jd_ut - 2451545.0)/365.25;
        return deltat_poly(year);
    }
    return 32.184 + tai_minus_utc(jd_ut) - dut1_seconds;
}

double nav_delta_t_seconds(double jd_ut)
{
    return nav_delta_t_with_dut1_seconds(jd_ut, 0.0);
}

/* ------------------------------------------------------------ naming */

const char *nav_body_name(NavBody body)
{
    switch(body){
        case NAV_SUN:     return "Sun";
        case NAV_MOON:    return "Moon";
        case NAV_MERCURY: return "Mercury";
        case NAV_VENUS:   return "Venus";
        case NAV_MARS:    return "Mars";
        case NAV_JUPITER: return "Jupiter";
        case NAV_SATURN:  return "Saturn";
        case NAV_ARIES:   return "Aries";
    }
    return "?";
}

/* ------------------------------------------------------- star catalogue */

size_t nav_star_count_get(void){ return nav_star_cat_count; }

const char *nav_star_name(size_t index)
{
    return (index < nav_star_cat_count) ? nav_star_cat[index].name : "";
}

/* letters and digits only, case-folded: "al nair", "Al Na'ir", "ALNAIR" match */
static void squash(const char *s, char *out, size_t n)
{
    size_t k = 0;
    if(!s){ out[0] = 0; return; }
    for(; *s && k+1 < n; s++)
        if(isalnum((unsigned char)*s)) out[k++] = (char)tolower((unsigned char)*s);
    out[k] = 0;
}

int nav_star_find(const char *name)
{
    char want[64], have[64];
    size_t i;
    if(!name || !*name) return -1;
    squash(name, want, sizeof want);
    if(!want[0]) return -1;
    for(i = 0; i < nav_star_cat_count; i++){
        squash(nav_star_cat[i].name, have, sizeof have);
        if(!strcmp(have, want)) return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------ engine selection */

void nav_engine_set_ephemeris_path(const char *path)
{
    if (!path) { g_ephpath[0] = 0; return; }
    snprintf(g_ephpath, sizeof g_ephpath, "%s", path);
    g_precise_ready = 0;
}

int nav_engine_select(const char *name)
{
    char k[32];
    squash(name, k, sizeof k);

    if (!strcmp(k, "novas") || !strcmp(k, "precise") || !strcmp(k, "precision") ||
        !strcmp(k, "de440") || !strcmp(k, "de440s") || !strcmp(k, "jpl")) {
#ifdef NAVALM_NO_PRECISE
        nav_engine_set_error("this navalm was built without the precision "
                             "engine (make classic-only); only --engine "
                             "classic is available");
        return 1;
#else
        g_engine = NAV_ENGINE_PRECISE;
        return 0;
#endif
    }
    if (!strcmp(k, "libastro") || !strcmp(k, "classic") || !strcmp(k, "xephem") ||
        !strcmp(k, "vsop87")) {
        g_engine = NAV_ENGINE_CLASSIC;
        nav_engine_set_error(NULL);
        return 0;
    }
    return 1;
}

NavEngine nav_engine_current(void) { return g_engine; }

const char *nav_engine_name(void)
{
    return (g_engine == NAV_ENGINE_PRECISE) ? "precise" : "classic";
}

#ifndef NAVALM_NO_PRECISE
/* Where to look for the ephemeris, in order, when no path was given. */
static const char *default_paths[] = {
    "de440s.nav",
    "ephem/de440s.nav",
    "/usr/local/share/navalm/de440s.nav",
    "/usr/share/navalm/de440s.nav"
};

static int try_open(const char *path, char *err, size_t errlen)
{
    if (!path || !*path) return 1;
    return nav_solsys_open(path, err, errlen);
}
#endif

int nav_engine_init(char *err, size_t errlen)
{
    if (err && errlen) err[0] = 0;

    if (g_engine == NAV_ENGINE_CLASSIC) {
        snprintf(g_detail, sizeof g_detail,
                 "XEphem libastro: VSOP87 planets, Moshier lunar theory");
        return 0;
    }

#ifdef NAVALM_NO_PRECISE
    if (err && errlen)
        snprintf(err, errlen, "this build has no precision engine");
    return 1;
#else
    if (g_precise_ready) return 0;

    {
        const char *env = getenv("NAVALM_EPHEMERIS");
        char home[NAV_PATH_MAX];
        const char *h;
        size_t i;

        if (g_ephpath[0]) {
            /* An explicitly chosen file must be the one used: no silent
             * fallback to some other ephemeris on the search path. */
            if (try_open(g_ephpath, err, errlen)) return 1;
        } else if (env && *env) {
            if (try_open(env, err, errlen)) return 1;
        } else {
            int opened = 0;
            for (i = 0; i < sizeof default_paths / sizeof default_paths[0]; i++)
                if (!try_open(default_paths[i], NULL, 0)) { opened = 1; break; }
            if (!opened && (h = getenv("HOME")) && *h) {
                snprintf(home, sizeof home, "%s/.navalm/de440s.nav", h);
                if (!try_open(home, NULL, 0)) opened = 1;
            }
            if (!opened) {
                if (err && errlen)
                    snprintf(err, errlen,
                        "no DE440s ephemeris found (looked for de440s.nav in the "
                        "current directory, ephem/, ~/.navalm/ and "
                        "/usr/local/share/navalm/).\n"
                        "Build it with:  make ephem\n"
                        "or point at one with --ephemeris PATH or $NAVALM_EPHEMERIS,\n"
                        "or run the self-contained engine with --engine classic.");
                return 1;
            }
        }
    }

    {
        NavEph *e = nav_solsys_handle();
        snprintf(g_detail, sizeof g_detail,
                 "NOVAS C3.1 with JPL DE440s (%s, JD %.1f to %.1f)",
                 nav_eph_path(e), nav_eph_jd_begin(e), nav_eph_jd_end(e));
    }
    g_precise_ready = 1;
    return 0;
#endif
}

void nav_engine_set_error(const char *msg)
{
    if (!msg) { g_error[0] = 0; return; }
    snprintf(g_error, sizeof g_error, "%s", msg);
}

const char *nav_engine_last_error(void) { return g_error; }

int nav_engine_covers(double jd_ut)
{
#ifndef NAVALM_NO_PRECISE
    if (g_engine == NAV_ENGINE_PRECISE) {
        NavEph *e = nav_solsys_handle();
        char msg[512];
        if (!e) {
            nav_engine_set_error("the precision engine has no ephemeris open");
            return 0;
        }
        /* One day of slack either side absorbs the UT-to-TDB offset. */
        if (jd_ut < nav_eph_jd_begin(e) - 1.0 ||
            jd_ut > nav_eph_jd_end(e) + 1.0) {
            snprintf(msg, sizeof msg,
                     "date is outside the ephemeris: %s covers JD %.1f to %.1f "
                     "(about %d to %d).  Use --engine classic for dates "
                     "outside it.",
                     nav_eph_path(e), nav_eph_jd_begin(e), nav_eph_jd_end(e),
                     (int)(2000 + (nav_eph_jd_begin(e) - 2451545.0) / 365.25),
                     (int)(2000 + (nav_eph_jd_end(e)   - 2451545.0) / 365.25));
            nav_engine_set_error(msg);
            return 0;
        }
    }
#else
    (void)jd_ut;
#endif
    nav_engine_set_error(NULL);
    return 1;
}

const char *nav_engine_detail(void)
{
    if (!g_detail[0]) nav_engine_init(NULL, 0);
    return g_detail;
}

void nav_engine_shutdown(void)
{
#ifndef NAVALM_NO_PRECISE
    nav_solsys_close();
    g_precise_ready = 0;
#endif
}

/* --------------------------------------------------------- dispatch */

#ifdef NAVALM_NO_PRECISE
#define DISPATCH(call_n, call_x) return call_x
#else
#define DISPATCH(call_n, call_x) \
    return (g_engine == NAV_ENGINE_PRECISE) ? (call_n) : (call_x)
#endif

double nav_gast_deg(double jd_ut)
{
    DISPATCH(nav_n_gast_deg(jd_ut), nav_x_gast_deg(jd_ut));
}

double nav_gha_aries_deg(double jd_ut)
{
    DISPATCH(nav_n_gha_aries_deg(jd_ut), nav_x_gha_aries_deg(jd_ut));
}

int nav_moon_geocentric(double jd_ut, double *lon_deg, double *lat_deg,
                        double *distance_km)
{
    DISPATCH(nav_n_moon_geocentric(jd_ut, lon_deg, lat_deg, distance_km),
             nav_x_moon_geocentric(jd_ut, lon_deg, lat_deg, distance_km));
}

int nav_almanac(NavBody body, double jd_ut, NavAlmanac *out)
{
    DISPATCH(nav_n_almanac(body, jd_ut, out),
             nav_x_almanac(body, jd_ut, out));
}

int nav_star_almanac(size_t index, double jd_ut, NavStarAlmanac *out)
{
    DISPATCH(nav_n_star_almanac(index, jd_ut, out),
             nav_x_star_almanac(index, jd_ut, out));
}

int nav_catalog_star_almanac(const NavStarCat *star,double jd_ut,NavStarAlmanac *out)
{
    DISPATCH(nav_n_catalog_star_almanac(star,jd_ut,out),
             nav_x_catalog_star_almanac(star,jd_ut,out));
}
