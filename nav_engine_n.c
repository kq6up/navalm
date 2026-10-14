/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_engine_n.c - navalm's precision astronomy engine.
 *
 * Implements the nav_engine.h quantities on NOVAS C3.1 (U.S. Naval
 * Observatory, public domain) reading JPL DE440s Chebyshev coefficients
 * through nav_eph440.  NOVAS supplies the IAU 2000A nutation, the IAU 2006
 * precession, aberration, light deflection and light-time iteration; this
 * file converts between NOVAS' conventions and the almanac quantities
 * navalm works in, and adds the things NOVAS leaves to the application:
 * semidiameter, horizontal parallax, elongation and visual magnitude.
 *
 * Conventions match nav_engine_x.c so the two engines are interchangeable:
 *   - positions are apparent geocentric, equinox and equator of date
 *   - GHA = GAST - RA(apparent)
 *   - HP  = arcsin(equatorial Earth radius / geocentric distance)
 *   - SD  = arcsin(body radius / geocentric distance)
 */

#include "nav_engine.h"
#include "nav_stars_x.h"
#include "nav_eph440.h"
#include "nav_solsys440.h"
#include "nav_engine_impl.h"
#include "novas.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG (M_PI / 180.0)
#define EARTH_RAD_KM 6378.137
#define ACC 0                       /* NOVAS full accuracy */

/* Mean radii used for semidiameter, km.  The Sun and Moon values are the
 * ones behind the Nautical Almanac's tabulated SD (the Sun's 959.63" at
 * 1 AU, the Moon's k = 0.2725076); the planets are IAU equatorial radii. */
static const double body_radius_km[] = {
    696000.0,   /* Sun     */
    1738.1,     /* Moon    */
    2439.7,     /* Mercury */
    6051.8,     /* Venus   */
    3396.19,    /* Mars    */
    71492.0,    /* Jupiter */
    60268.0     /* Saturn  */
};

static double norm360(double x){ x = fmod(x, 360.0); return x < 0 ? x + 360.0 : x; }

/* NOVAS body numbers: 1-9 planets, 10 Sun, 11 Moon. */
static int body_to_novas(NavBody body)
{
    switch (body) {
        case NAV_SUN:     return 10;
        case NAV_MOON:    return 11;
        case NAV_MERCURY: return 1;
        case NAV_VENUS:   return 2;
        case NAV_MARS:    return 4;
        case NAV_JUPITER: return 5;
        case NAV_SATURN:  return 6;
        default:          return -1;
    }
}

static int body_to_naif(NavBody body)
{
    switch (body) {
        case NAV_SUN:     return NAV_EPH_SUN;
        case NAV_MOON:    return NAV_EPH_MOON;
        case NAV_MERCURY: return NAV_EPH_MERCURY_BARY;
        case NAV_VENUS:   return NAV_EPH_VENUS_BARY;
        case NAV_MARS:    return NAV_EPH_MARS_BARY;
        case NAV_JUPITER: return NAV_EPH_JUPITER_BARY;
        case NAV_SATURN:  return NAV_EPH_SATURN_BARY;
        default:          return -1;
    }
}

/* TT and TDB from a UT Julian date.  nav_delta_t_seconds returns TT - UT1
 * (treating UTC as UT1 unless the caller supplied DUT1). */
static void times_from_ut(double jd_ut, double *jd_tt, double *jd_tdb)
{
    double tt = jd_ut + nav_delta_t_seconds(jd_ut) / 86400.0;
    double dummy, secdiff;
    tdb2tt(tt, &dummy, &secdiff);      /* secdiff = TDB - TT, periodic, < 2 ms */
    if (jd_tt)  *jd_tt  = tt;
    if (jd_tdb) *jd_tdb = tt + secdiff / 86400.0;
}

double nav_n_gast_deg(double jd_ut)
{
    double gst = 0.0;
    if (sidereal_time(jd_ut, 0.0, nav_delta_t_seconds(jd_ut),
                      1 /* apparent */, 1 /* equinox based */, ACC, &gst) != 0)
        return 0.0;
    return norm360(gst * 15.0);
}

double nav_n_gha_aries_deg(double jd_ut) { return nav_n_gast_deg(jd_ut); }

/* Apparent geocentric place of a major body. */
static int app_body(NavBody body, double jd_tt,
                    double *ra_deg, double *dec_deg, double *dis_au)
{
    object obj;
    char name[SIZE_OF_OBJ_NAME];
    double ra_h = 0.0, dec = 0.0, dis = 0.0;
    int n = body_to_novas(body);

    if (n < 0) return 1;
    snprintf(name, sizeof name, "%s", nav_body_name(body));
    if (make_object(0, (short int)n, name, NULL, &obj) != 0) return 1;
    if (app_planet(jd_tt, &obj, ACC, &ra_h, &dec, &dis) != 0) return 1;

    if (ra_deg)  *ra_deg  = norm360(ra_h * 15.0);
    if (dec_deg) *dec_deg = dec;
    if (dis_au)  *dis_au  = dis;
    return 0;
}

/* Angle between two directions given as RA/Dec in degrees. */
static double sep_deg(double ra1, double dec1, double ra2, double dec2)
{
    double c = sin(dec1 * DEG) * sin(dec2 * DEG) +
               cos(dec1 * DEG) * cos(dec2 * DEG) * cos((ra1 - ra2) * DEG);
    if (c >  1.0) c =  1.0;
    if (c < -1.0) c = -1.0;
    return acos(c) / DEG;
}

/* Saturnicentric latitude of the Earth, degrees: the ring opening angle,
 * from the IAU pole of Saturn.  Needed for Saturn's magnitude. */
static double saturn_ring_angle(double jd_tdb, double ra_deg, double dec_deg)
{
    double T  = (jd_tdb - 2451545.0) / 36525.0;
    double a0 = (40.589 - 0.036 * T) * DEG;
    double d0 = (83.537 - 0.004 * T) * DEG;
    double ra = ra_deg * DEG, dec = dec_deg * DEG;
    double s = -sin(d0) * sin(dec) - cos(d0) * cos(dec) * cos(a0 - ra);
    if (s >  1.0) s =  1.0;
    if (s < -1.0) s = -1.0;
    return asin(s) / DEG;
}

/* Apparent visual magnitude.
 *
 * Planetary phase functions from Mallama & Hilton, "Computing apparent
 * planetary magnitudes for the Astronomical Almanac" (Astronomy and
 * Computing 25, 2018), which are the relations the Astronomical Almanac
 * tabulates.  r is the heliocentric and d the geocentric distance in AU,
 * and i the phase angle in degrees.
 */
static double planet_magnitude(NavBody body, double r, double d, double i,
                               double jd_tdb, double ra_deg, double dec_deg)
{
    double base = (r > 0.0 && d > 0.0) ? 5.0 * log10(r * d) : 0.0;
    double i2 = i * i, i3 = i2 * i, i4 = i3 * i, i5 = i4 * i, i6 = i5 * i;

    switch (body) {
        case NAV_SUN:
            return (d > 0.0) ? -26.74 + 5.0 * log10(d) : -26.74;

        case NAV_MOON:
            /* r is ~1 AU; the Moon's own distance is carried in d. */
            return 0.23 + base + 0.026 * i + 4.0e-9 * i4;

        case NAV_MERCURY:
            return -0.613 + base + 6.3280e-2 * i - 1.6336e-3 * i2
                   + 3.3644e-5 * i3 - 3.4265e-7 * i4
                   + 1.6893e-9 * i5 - 3.0334e-12 * i6;

        case NAV_VENUS:
            if (i < 163.7)
                return -4.384 + base - 1.044e-3 * i + 3.687e-4 * i2
                       - 2.814e-6 * i3 + 8.938e-9 * i4;
            return 236.05828 + base - 2.81914 * i + 8.39034e-3 * i2;

        case NAV_MARS:
            if (i <= 50.0)
                return -1.601 + base + 2.267e-2 * i - 1.302e-4 * i2;
            return -0.367 + base - 0.02573 * i + 0.0003445 * i2;

        case NAV_JUPITER:
            if (i <= 12.0)
                return -9.395 + base - 3.7e-4 * i + 6.16e-4 * i2;
            else {
                double x = i / 180.0, p;
                p = 1.0 - 1.507 * x - 0.363 * x * x - 0.062 * x * x * x
                    + 2.809 * x * x * x * x - 1.876 * x * x * x * x * x;
                if (p < 1.0e-6) p = 1.0e-6;
                return -9.428 + base - 2.5 * log10(p);
            }

        case NAV_SATURN: {
            /* Globe plus rings, as seen from the Earth.  sin|B| is the ring
             * opening; at B = 0 the rings are edge-on and contribute
             * nothing. */
            double B = fabs(saturn_ring_angle(jd_tdb, ra_deg, dec_deg));
            double sB = sin(B * DEG);
            return -8.914 + base - 1.825 * sB + 0.026 * i
                   - 0.378 * sB * exp(-2.25 * i);
        }

        default:
            return 0.0;
    }
}

/* The ephemeris is finite; say so rather than failing mutely. */
static int in_range(double jd_tdb)
{
    NavEph *e = nav_solsys_handle();
    char msg[384];
    if (!e) {
        nav_engine_set_error("the precision engine has no ephemeris open");
        return 0;
    }
    if (jd_tdb < nav_eph_jd_begin(e) || jd_tdb > nav_eph_jd_end(e)) {
        snprintf(msg, sizeof msg,
                 "date is outside the ephemeris: %s covers JD %.1f to %.1f "
                 "(about %d to %d).  Use --engine classic for dates outside it.",
                 nav_eph_path(e), nav_eph_jd_begin(e), nav_eph_jd_end(e),
                 (int)(2000 + (nav_eph_jd_begin(e) - 2451545.0) / 365.25),
                 (int)(2000 + (nav_eph_jd_end(e)   - 2451545.0) / 365.25));
        nav_engine_set_error(msg);
        return 0;
    }
    nav_engine_set_error(NULL);
    return 1;
}

int nav_n_almanac(NavBody body, double jd_ut, NavAlmanac *out)
{
    double jd_tt, jd_tdb, gast;
    double ra, dec, dis_au, dist_km;
    double sun_ra, sun_dec, sun_dis;
    double bp[3], sp[3], ep[3];
    double r = 0.0, rsun = 0.0, phase = 0.0;
    int naif, k;

    if (!out) return 1;
    memset(out, 0, sizeof *out);
    if (!nav_solsys_handle()) return 1;

    times_from_ut(jd_ut, &jd_tt, &jd_tdb);
    if (!in_range(jd_tdb)) return 1;
    gast = nav_n_gast_deg(jd_ut);

    if (body == NAV_ARIES) {
        out->gha_deg = gast;
        return 0;
    }
    if (body_to_novas(body) < 0) return 1;
    if (app_body(body, jd_tt, &ra, &dec, &dis_au)) return 1;

    out->ra_deg  = ra;
    out->dec_deg = dec;
    out->gha_deg = norm360(gast - ra);

    dist_km = dis_au * NAV_EPH_AU_KM;
    if (dist_km > 0.0) {
        out->hp_deg = asin(EARTH_RAD_KM / dist_km) / DEG;
        k = (int)body;
        if (k >= 0 && k < (int)(sizeof body_radius_km / sizeof body_radius_km[0]))
            out->sd_deg = asin(body_radius_km[k] / dist_km) / DEG;
    }
    out->distance = (body == NAV_MOON) ? dist_km : dis_au;

    /* Elongation, and the geometry the magnitude needs.  Geometric
     * distances at jd_tdb are ample for both. */
    if (body == NAV_SUN) {
        out->elong_deg = 0.0;
        r = 0.0;
    } else {
        if (app_body(NAV_SUN, jd_tt, &sun_ra, &sun_dec, &sun_dis) == 0)
            out->elong_deg = sep_deg(ra, dec, sun_ra, sun_dec);

        naif = body_to_naif(body);
        if (naif > 0 &&
            nav_eph_state_ssb(nav_solsys_handle(), naif, jd_tdb, bp, NULL) == 0 &&
            nav_eph_state_ssb(nav_solsys_handle(), NAV_EPH_SUN, jd_tdb, sp, NULL) == 0 &&
            nav_eph_state_ssb(nav_solsys_handle(), NAV_EPH_EARTH, jd_tdb, ep, NULL) == 0) {
            double dx, dy, dz;
            dx = bp[0] - sp[0]; dy = bp[1] - sp[1]; dz = bp[2] - sp[2];
            r = sqrt(dx*dx + dy*dy + dz*dz) / NAV_EPH_AU_KM;
            dx = ep[0] - sp[0]; dy = ep[1] - sp[1]; dz = ep[2] - sp[2];
            rsun = sqrt(dx*dx + dy*dy + dz*dz) / NAV_EPH_AU_KM;

            /* phase angle at the body, from the triangle Sun-body-Earth */
            if (r > 0.0 && dis_au > 0.0) {
                double c = (r * r + dis_au * dis_au - rsun * rsun) /
                           (2.0 * r * dis_au);
                if (c >  1.0) c =  1.0;
                if (c < -1.0) c = -1.0;
                phase = acos(c) / DEG;
            }
        }
    }

    out->magnitude = planet_magnitude(body,
                                      (body == NAV_MOON) ? rsun : r,
                                      dis_au, phase, jd_tdb, ra, dec);
    return 0;
}

int nav_n_moon_geocentric(double jd_ut, double *lon_deg, double *lat_deg,
                          double *distance_km)
{
    double jd_tt, jd_tdb, ra, dec, dis_au, elon = 0.0, elat = 0.0;

    if (!nav_solsys_handle()) return 1;
    times_from_ut(jd_ut, &jd_tt, &jd_tdb);
    if (!in_range(jd_tdb)) return 1;
    if (app_body(NAV_MOON, jd_tt, &ra, &dec, &dis_au)) return 1;

    /* equ2ecl wants RA in hours; coord_sys 1 is the true equator and
     * equinox of date, matching the apparent place just computed. */
    if (equ2ecl(jd_tt, 1, ACC, ra / 15.0, dec, &elon, &elat) != 0) return 1;

    if (lon_deg)     *lon_deg = norm360(elon);
    if (lat_deg)     *lat_deg = elat;
    if (distance_km) *distance_km = dis_au * NAV_EPH_AU_KM;
    return 0;
}

int nav_n_star_almanac(size_t index, double jd_ut, NavStarAlmanac *out)
{
    const NavStarCat *sc;
    cat_entry star;
    char name[SIZE_OF_OBJ_NAME], cat[SIZE_OF_CAT_NAME];
    double jd_tt, jd_tdb, gast, ra_h = 0.0, dec = 0.0;

    if (!out || index >= nav_star_cat_count) return 1;
    if (!nav_solsys_handle()) return 1;
    sc = &nav_star_cat[index];

    times_from_ut(jd_ut, &jd_tt, &jd_tdb);
    if (!in_range(jd_tdb)) return 1;

    snprintf(name, sizeof name, "%s", sc->name);
    snprintf(cat, sizeof cat, "%s", "SKY");
    /* NOVAS' promora is the great-circle rate, mu_alpha * cos dec, which is
     * how the catalogue stores it.  Parallax and radial velocity are not in
     * the catalogue and are passed as zero; see VALIDATION.md for the size
     * of that omission. */
    if (make_cat_entry(name, cat, (long)index + 1,
                       sc->ra2000_deg / 15.0, sc->dec2000_deg,
                       sc->pm_ra_mas, sc->pm_dec_mas, 0.0, 0.0, &star) != 0)
        return 1;

    if (app_star(jd_tt, &star, ACC, &ra_h, &dec) != 0) return 1;

    gast = nav_n_gast_deg(jd_ut);
    out->name      = sc->name;
    out->ra_deg    = norm360(ra_h * 15.0);
    out->dec_deg   = dec;
    out->sha_deg   = norm360(360.0 - out->ra_deg);
    out->gha_deg   = norm360(gast - out->ra_deg);
    out->magnitude = sc->magnitude;
    return 0;
}
