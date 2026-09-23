/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
#ifndef NAVRC_H
#define NAVRC_H

#include "navsight.h"

/* Setup defaults, kept in ~/.navalmrc (override with NAVALMRC).
 *
 * These live in their own file on purpose.  The LOP store is rewritten
 * whole every time a sight is saved, so anything else kept in it would be
 * dropped by an older binary; a separate file keeps both forward and
 * backward compatible. */

typedef struct {
    double ie_min;
    int    ie_on_arc;
    int    artificial;      /* 1 = artificial horizon by default */
    double he;
    int    he_metres;
    int    limb;            /* +1 lower, 0 center, -1 upper */
    double temp_c;
    double press_mb;
    int    have_dr;
    double dr_lat, dr_lon;  /* east positive */
    int    have_motion;
    double course, speed, set, drift;
} NavDefaults;

const char *navrc_path(void);
void navrc_init(NavDefaults *d);            /* factory values */
int  navrc_load(NavDefaults *d);            /* missing file is not an error */
int  navrc_save(const NavDefaults *d, char *err, size_t errn);
void navrc_apply(const NavDefaults *d, Sight *si);   /* seed a sight */
void navrc_from_sight(NavDefaults *d, const Sight *si);
void navrc_print(const NavDefaults *d, FILE *out);

#endif
