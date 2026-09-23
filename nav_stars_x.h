/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
#ifndef NAV_STARS_X_H
#define NAV_STARS_X_H
#include <stddef.h>

/* Navigational star catalogue, generated from XEphem's SKY2k65.edb.
 * Positions are J2000, proper motions milliarcseconds per year
 * (pm_ra is already the great-circle rate, i.e. mu_alpha * cos dec). */
typedef struct {
    const char *name;
    double ra2000_deg;
    double dec2000_deg;
    double pm_ra_mas;
    double pm_dec_mas;
    double magnitude;
} NavStarCat;

extern const NavStarCat nav_star_cat[];
extern const size_t nav_star_cat_count;
#endif
