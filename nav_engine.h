/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
#ifndef NAV_ENGINE_H
#define NAV_ENGINE_H

#include <stddef.h>
#include "nav_stars_x.h"

typedef enum {
    NAV_SUN, NAV_MOON, NAV_MERCURY, NAV_VENUS, NAV_MARS, NAV_JUPITER, NAV_SATURN, NAV_ARIES
} NavBody;

typedef struct {
    double ra_deg;       /* apparent geocentric right ascension, degrees */
    double dec_deg;      /* apparent geocentric declination, degrees */
    double gha_deg;      /* Greenwich hour angle, degrees */
    double hp_deg;       /* horizontal parallax, degrees (0 if not supplied) */
    double sd_deg;       /* semidiameter, degrees (Sun/Moon where supplied) */
    double distance;     /* AU for Sun/planets, km for Moon */
    double elong_deg;    /* elongation from the Sun, degrees (0 for the Sun) */
    double magnitude;    /* apparent visual magnitude */
} NavAlmanac;

double nav_julian_date(int year,int month,int day,int hour,int minute,double second);
double nav_delta_t_seconds(double jd_ut);
double nav_delta_t_with_dut1_seconds(double jd_ut,double dut1_seconds);
double nav_gast_deg(double jd_ut);
double nav_gha_aries_deg(double jd_ut);
int nav_moon_geocentric(double jd_ut,double *lon_deg,double *lat_deg,double *distance_km);
int nav_almanac(NavBody body,double jd_ut,NavAlmanac *out);
const char *nav_body_name(NavBody body);

typedef struct {
    const char *name;
    double sha_deg;      /* sidereal hour angle, degrees */
    double ra_deg;       /* apparent right ascension, degrees */
    double dec_deg;      /* apparent declination, degrees */
    double gha_deg;      /* Greenwich hour angle, degrees */
    double magnitude;
} NavStarAlmanac;

size_t nav_star_count_get(void);
const char *nav_star_name(size_t index);
int nav_star_find(const char *name);
int nav_star_almanac(size_t index,double jd_ut,NavStarAlmanac *out);
/* Arbitrary J2000 catalogue star, for display layers; does not extend the navigation catalogue. */
int nav_catalog_star_almanac(const NavStarCat *,double jd_ut,NavStarAlmanac *out);

/* ------------------------------------------------------------- engines
 *
 * navalm_p carries two astronomy engines behind the calls above:
 *
 *   NAV_ENGINE_PRECISE   NOVAS C3.1 reading JPL DE440s Chebyshev
 *                        coefficients.  Milliarcsecond level.  Needs the
 *                        ephemeris file built by tools/mkde440.
 *   NAV_ENGINE_CLASSIC   XEphem libastro: VSOP87 and the Moshier lunar
 *                        theory.  Arcsecond level, no data file.
 *
 * The precise engine is the default.  Select one before the first almanac
 * call, then call nav_engine_init(), which is where a missing or unreadable
 * ephemeris is reported.
 */
typedef enum { NAV_ENGINE_PRECISE = 0, NAV_ENGINE_CLASSIC = 1 } NavEngine;

/* Accepts "precise", "novas", "de440s", "jpl", or "classic", "libastro",
 * "xephem", "vsop87".  Returns 0 if the name was recognised and the engine
 * is available in this build. */
int nav_engine_select(const char *name);
NavEngine nav_engine_current(void);
const char *nav_engine_name(void);

/* One line naming the engine and, for the precise engine, the ephemeris
 * file and the dates it covers. */
const char *nav_engine_detail(void);

/* Overrides where the precise engine looks for its ephemeris.  Without
 * this, $NAVALM_EPHEMERIS is tried, then de440s.nav in the current
 * directory, ephem/, ~/.navalm/ and /usr/local/share/navalm/. */
void nav_engine_set_ephemeris_path(const char *path);

/* Prepares the selected engine.  Returns 0 on success; on failure writes a
 * sentence to err saying what is missing and how to supply it. */
int nav_engine_init(char *err, size_t errlen);
void nav_engine_shutdown(void);

/* Why the last nav_almanac() or nav_star_almanac() call failed, or "" if
 * nothing has failed.  The commonest cause is a date outside the span of
 * the ephemeris the precise engine is reading. */
const char *nav_engine_last_error(void);

/* Whether the selected engine can answer for this UT Julian date.  The
 * classic engine covers any date; the precise engine is limited to its
 * ephemeris file. */
int nav_engine_covers(double jd_ut);

#endif
