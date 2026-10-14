/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_eph440.h - navalm's compact JPL ephemeris file.
 *
 * The file holds JPL Chebyshev position coefficients copied verbatim out of
 * an SPK kernel by tools/mkde440.c.  Nothing is refitted or rounded, so a
 * position read back through this interface is the one JPL published; the
 * only thing the repacking changes is the container.
 *
 * Layout, all little-endian regardless of host:
 *
 *   header, 56 bytes
 *     0   char[8]  magic "NAVEPH1"
 *     8   u32      format version
 *     12  u32      number of bodies
 *     16  f64      first JD (TDB) covered by every body
 *     24  f64      last  JD (TDB) covered by every body
 *     32  f64      astronomical unit, km
 *     40  f64      Earth/Moon mass ratio
 *     48  u32      flags (0)
 *     52  u32      reserved
 *
 *   directory, 48 bytes per body
 *     0   i32      NAIF code of the body
 *     4   i32      NAIF code of the centre its coordinates are relative to
 *     8   i32      record count
 *     12  i32      doubles per record
 *     16  i32      Chebyshev coefficients per component
 *     20  i32      reserved
 *     24  f64      JD (TDB) at the start of the first record
 *     32  f64      days covered by each record
 *     40  u64      byte offset of this body's coefficient block
 *
 *   coefficient blocks, in directory order.  Each record is MID, RADIUS
 *   (both TDB seconds past J2000), then the x, y and z coefficient runs.
 *   Positions are km in the J2000/ICRF frame.
 */
#ifndef NAV_EPH440_H
#define NAV_EPH440_H

#include <stddef.h>

#define NAV_EPH_MAGIC           "NAVEPH1"
#define NAV_EPH_FORMAT_VERSION  1u
#define NAV_EPH_HDR_BYTES       56
#define NAV_EPH_DIR_BYTES       48

/* IAU 2012 astronomical unit, the value DE440 is built on. */
#define NAV_EPH_AU_KM   149597870.7
/* DE440 Earth/Moon mass ratio.  Recorded for reference; navalm carries both
 * the Moon and the Earth relative to the EMB, so it never has to derive one
 * from the other. */
#define NAV_EPH_EMRAT   81.3005682214972154

/* NAIF body codes, as used in the kernel and carried into our file. */
#define NAV_EPH_SSB            0
#define NAV_EPH_MERCURY_BARY   1
#define NAV_EPH_VENUS_BARY     2
#define NAV_EPH_EMB            3
#define NAV_EPH_MARS_BARY      4
#define NAV_EPH_JUPITER_BARY   5
#define NAV_EPH_SATURN_BARY    6
#define NAV_EPH_SUN           10
#define NAV_EPH_MOON         301
#define NAV_EPH_EARTH        399

typedef struct NavEph NavEph;

/* Opens the ephemeris file.  Returns 0 on success.  On failure a sentence
 * explaining what went wrong is written to err, which may be NULL. */
int nav_eph_open(const char *path, NavEph **out, char *err, size_t errlen);
void nav_eph_close(NavEph *e);

/* Position (km) and velocity (km/s) of a body relative to the centre it is
 * stored against, in the J2000/ICRF frame, at a TDB Julian date.
 * vel may be NULL.  Returns 0 on success, non-zero if the body is absent or
 * the date is outside the file. */
int nav_eph_state(NavEph *e, int naif, double jd_tdb,
                  double pos[3], double vel[3]);

/* As above but relative to the solar system barycentre, resolving the
 * Earth and Moon through the EMB. */
int nav_eph_state_ssb(NavEph *e, int naif, double jd_tdb,
                      double pos[3], double vel[3]);

double nav_eph_jd_begin(const NavEph *e);
double nav_eph_jd_end(const NavEph *e);
const char *nav_eph_path(const NavEph *e);

/* Name for a NAIF code, for diagnostics.  Safe to call without an open
 * ephemeris; returns "?" for codes navalm does not carry. */
const char *nav_eph_body_name(int naif);

#endif
