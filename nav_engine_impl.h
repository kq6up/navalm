/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_engine_impl.h - internal interface each astronomy engine provides.
 *
 * nav_engine_sel.c dispatches the public nav_engine.h calls to one of
 * these sets.  Quantities that do not depend on the engine - the calendar,
 * delta T, body names and the star catalogue index - live in
 * nav_engine_sel.c and are shared.
 *
 *   nav_x_*   XEphem libastro          (nav_engine_x.c)
 *   nav_n_*   NOVAS C3.1 + JPL DE440s  (nav_engine_n.c)
 */
#ifndef NAV_ENGINE_IMPL_H
#define NAV_ENGINE_IMPL_H

#include <stddef.h>
#include "nav_engine.h"

/* Records why the last almanac call failed, for the caller to report.
 * Implemented in nav_engine_sel.c. */
void nav_engine_set_error(const char *msg);

double nav_x_gast_deg(double jd_ut);
double nav_x_gha_aries_deg(double jd_ut);
int    nav_x_moon_geocentric(double jd_ut, double *lon_deg, double *lat_deg,
                             double *distance_km);
int    nav_x_almanac(NavBody body, double jd_ut, NavAlmanac *out);
int    nav_x_star_almanac(size_t index, double jd_ut, NavStarAlmanac *out);
int    nav_x_catalog_star_almanac(const NavStarCat *, double jd_ut, NavStarAlmanac *out);

#ifndef NAVALM_NO_PRECISE
double nav_n_gast_deg(double jd_ut);
double nav_n_gha_aries_deg(double jd_ut);
int    nav_n_moon_geocentric(double jd_ut, double *lon_deg, double *lat_deg,
                             double *distance_km);
int    nav_n_almanac(NavBody body, double jd_ut, NavAlmanac *out);
int    nav_n_star_almanac(size_t index, double jd_ut, NavStarAlmanac *out);
int    nav_n_catalog_star_almanac(const NavStarCat *, double jd_ut, NavStarAlmanac *out);
#endif

#endif
