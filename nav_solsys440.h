/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_solsys440.h - lifecycle for the DE440s-backed NOVAS solar system
 * access in nav_solsys440.c.  The solarsystem() and solarsystem_hp()
 * functions themselves are declared by NOVAS in solarsystem.h. */
#ifndef NAV_SOLSYS440_H
#define NAV_SOLSYS440_H

#include <stddef.h>
#include "nav_eph440.h"

/* Opens the ephemeris NOVAS will read.  Returns 0 on success; on failure a
 * sentence explaining why is written to err. */
int  nav_solsys_open(const char *path, char *err, size_t errlen);
void nav_solsys_close(void);

/* The open ephemeris, or NULL.  Lets the engine report the file's span and
 * read states directly without going through NOVAS. */
NavEph *nav_solsys_handle(void);

#endif
