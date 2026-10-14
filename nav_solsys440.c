/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_solsys440.c - NOVAS solar system access backed by JPL DE440s.
 *
 * NOVAS reaches the planetary ephemeris through two functions declared in
 * solarsystem.h, which an application is expected to supply.  The USNO
 * distribution ships three versions: solsys1 (JPL binary via eph_manager),
 * solsys2 (JPL Fortran), and solsys3 (a closed-form Sun/Earth theory).
 * This is a fourth, reading the repacked DE440s coefficients through
 * nav_eph440.  solsys1.c, solsys2.c, solsys3.c and eph_manager.c are
 * therefore left out of the navalm build.
 *
 * NOVAS body numbers: 1-9 are Mercury through Pluto, 10 is the Sun, 11 is
 * the Moon, and 3 is the Earth.  Origin 0 is the solar system barycentre,
 * 1 the centre of the Sun, 2 the centre of the Earth.  Positions come back
 * in AU and velocities in AU/day, equatorial J2000 (ICRF).
 *
 * Note on the outer planets: DE440s carries barycentres for Mars, Jupiter
 * and Saturn, not planet centres, which would need the satellite
 * ephemerides.  The barycentre offset reaches roughly 0.05 arcseconds for
 * Saturn and 0.02 for Jupiter - some three orders of magnitude below the
 * 0.1 arcminute an almanac prints.  See VALIDATION.md.
 */

#include "nav_eph440.h"
#include "nav_solsys440.h"
#include "solarsystem.h"

#include <stddef.h>
#include <string.h>

static NavEph *g_eph = NULL;

int nav_solsys_open(const char *path, char *err, size_t errlen)
{
    nav_solsys_close();
    return nav_eph_open(path, &g_eph, err, errlen);
}

void nav_solsys_close(void)
{
    if (g_eph) { nav_eph_close(g_eph); g_eph = NULL; }
}

NavEph *nav_solsys_handle(void) { return g_eph; }

static int novas_body_to_naif(short int body)
{
    switch (body) {
        case  1: return NAV_EPH_MERCURY_BARY;
        case  2: return NAV_EPH_VENUS_BARY;
        case  3: return NAV_EPH_EARTH;
        case  4: return NAV_EPH_MARS_BARY;
        case  5: return NAV_EPH_JUPITER_BARY;
        case  6: return NAV_EPH_SATURN_BARY;
        case 10: return NAV_EPH_SUN;
        case 11: return NAV_EPH_MOON;
        default: return -1;        /* Uranus, Neptune and Pluto are not carried */
    }
}

/* Barycentric state in AU and AU/day. */
static int state_au(int naif, double jd_tdb, double pos[3], double vel[3])
{
    double p[3], v[3];
    int i, rc;

    if ((rc = nav_eph_state_ssb(g_eph, naif, jd_tdb, p, v)) != 0) return rc;
    for (i = 0; i < 3; i++) {
        pos[i] = p[i] / NAV_EPH_AU_KM;
        vel[i] = v[i] * 86400.0 / NAV_EPH_AU_KM;
    }
    return 0;
}

static short int solsys_at(double jd_tdb, short int body, short int origin,
                           double *position, double *velocity)
{
    double bp[3], bv[3], op[3], ov[3];
    int naif, i, origin_naif;

    if (!position || !velocity) return 1;
    if (!g_eph) return 1;
    if (origin < 0 || origin > 2) return 2;

    if ((naif = novas_body_to_naif(body)) < 0) return 2;
    if (state_au(naif, jd_tdb, bp, bv) != 0) return 3;

    switch (origin) {
        case 0:  origin_naif = NAV_EPH_SSB;   break;
        case 1:  origin_naif = NAV_EPH_SUN;   break;
        default: origin_naif = NAV_EPH_EARTH; break;
    }

    if (origin_naif == NAV_EPH_SSB) {
        memcpy(position, bp, sizeof bp);
        memcpy(velocity, bv, sizeof bv);
        return 0;
    }
    if (state_au(origin_naif, jd_tdb, op, ov) != 0) return 3;
    for (i = 0; i < 3; i++) {
        position[i] = bp[i] - op[i];
        velocity[i] = bv[i] - ov[i];
    }
    return 0;
}

short int solarsystem (double tjd, short int body, short int origin,
                       double *position, double *velocity)
{
    return solsys_at(tjd, body, origin, position, velocity);
}

short int solarsystem_hp (double tjd[2], short body, short origin,
                          double *position, double *velocity)
{
    /* The split date guards precision in NOVAS' own arithmetic; the
     * Chebyshev evaluation needs nothing finer than the sum. */
    return solsys_at(tjd[0] + tjd[1], body, origin, position, velocity);
}
