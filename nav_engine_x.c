/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_engine_x.c - navalm 3.0c astronomy engine.
 *
 * Implements the nav_engine.h interface on top of XEphem's libastro
 * (Copyright (c) 1990-2020 Elwood Charles Downey, MIT licence; the full
 * text is in libastro/LICENSE.XEphem).  libastro supplies VSOP87/Chapront planetary routines,
 * the Moshier lunar implementation, nutation, aberration
 * and precession; this file only converts between that library's
 * conventions and the almanac quantities navalm works in.
 *
 * The calendar, delta T, body naming and the star catalogue index are
 * shared with the precision engine and live in nav_engine_sel.c; what is
 * left here is only what libastro itself answers.
 *
 * Conventions here:
 *   - positions are apparent geocentric, equinox and equator of date
 *   - GHA = GAST - RA(apparent), where GAST is libastro's GMST plus the
 *     equation of the equinoxes, so GHA pairs with an apparent RA
 *   - HP  = arcsin(equatorial Earth radius / geocentric distance)
 *   - SD  = half of libastro's apparent angular diameter
 */

#include "nav_engine.h"
#include "nav_engine_impl.h"
#include "nav_stars_x.h"
#include "libastro/astro.h"
#include "libastro/preferences.h"

#include <math.h>
#include <string.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AU_KM        149597870.7
#define EARTH_RAD_KM 6378.137

static double norm360(double x){ x = fmod(x,360.0); return x < 0 ? x + 360.0 : x; }

/* JD (UT) <-> libastro modified julian date (days since 1899 Dec 31 12:00) */
static double jd_to_mj(double jd){ return jd - MJD0; }

/* libastro asks for delta T through deltat(); its own table stops in the
 * 2010s and over-extrapolates by several seconds for the 2020s, so this
 * replaces it (libastro/deltat.c is left out of the build).  Argument and
 * result match libastro: modified julian date in, seconds out. */
double deltat(double mj)
{
    return nav_delta_t_seconds(mj + MJD0);
}

/* Greenwich apparent sidereal time, degrees */
double nav_x_gast_deg(double jd_ut)
{
    double mj = jd_to_mj(jd_ut);
    double tt_mj = mj + deltat(mj)/86400.0;
    double gmst_hr, deps, dpsi, eps;

    utc_gst(mjd_day(mj),mjd_hr(mj),&gmst_hr);
    nutation(tt_mj,&deps,&dpsi);
    obliquity(tt_mj,&eps);
    /* equation of the equinoxes */
    return norm360(gmst_hr*15.0 + raddeg(dpsi*cos(eps+deps)));
}

double nav_x_gha_aries_deg(double jd_ut)
{
    return nav_x_gast_deg(jd_ut);
}

int nav_x_moon_geocentric(double jd_ut,double *lon_deg,double *lat_deg,double *distance_km)
{
    double mj = jd_to_mj(jd_ut);
    double tt_mj = mj + deltat(mj)/86400.0;
    double lam,bet,rho,msp,mdp;

    moon(tt_mj,&lam,&bet,&rho,&msp,&mdp);     /* rho: geocentric distance, AU */
    if(lon_deg)      *lon_deg = norm360(raddeg(lam));
    if(lat_deg)      *lat_deg = raddeg(bet);
    if(distance_km)  *distance_km = rho * AU_KM;
    return 0;
}

static int body_to_plcode(NavBody body,int *code)
{
    switch(body){
        case NAV_SUN:     *code = SUN;     return 0;
        case NAV_MOON:    *code = MOON;    return 0;
        case NAV_MERCURY: *code = MERCURY; return 0;
        case NAV_VENUS:   *code = VENUS;   return 0;
        case NAV_MARS:    *code = MARS;    return 0;
        case NAV_JUPITER: *code = JUPITER; return 0;
        case NAV_SATURN:  *code = SATURN;  return 0;
        default:          return 1;
    }
}

/* libastro defaults to topocentric equatorial coordinates; almanac work wants
 * geocentric, so say so once before the first obj_cir(). */
static void want_geocentric(void)
{
    static int done = 0;
    if(!done){ pref_set(PREF_EQUATORIAL,PREF_GEO); done = 1; }
}

static void now_at(Now *np,double jd_ut)
{
    want_geocentric();
    memset(np,0,sizeof *np);
    np->n_mjd = jd_to_mj(jd_ut);
    np->n_epoch = EOD;          /* apparent place, equinox of date */
    np->n_lat = 0.0;
    np->n_lng = 0.0;
    np->n_temp = 10.0;
    np->n_pressure = 0.0;       /* no refraction: we want geocentric values */
    np->n_elev = 0.0;
    np->n_tz = 0.0;
}

int nav_x_almanac(NavBody body,double jd_ut,NavAlmanac *out)
{
    Now nw;
    Obj o;
    int code;
    double gast,dist_km;

    if(!out) return 1;
    memset(out,0,sizeof *out);
    gast = nav_x_gast_deg(jd_ut);

    if(body == NAV_ARIES){
        out->ra_deg = 0.0;
        out->dec_deg = 0.0;
        out->gha_deg = gast;
        return 0;
    }
    if(body_to_plcode(body,&code)) return 1;

    now_at(&nw,jd_ut);
    memset(&o,0,sizeof o);
    o.o_type = PLANET;
    o.pl_code = code;
    o.pl.plo_moon = X_PLANET;
    strcpy(o.o_name,nav_body_name(body));

    if(obj_cir(&nw,&o)) return 1;

    out->ra_deg  = norm360(raddeg(o.s_ra));
    out->dec_deg = raddeg(o.s_dec);
    out->gha_deg = norm360(gast - out->ra_deg);
    out->sd_deg  = o.s_size / 2.0 / 3600.0;          /* s_size: arcsec diameter */

    dist_km = o.s_edist * AU_KM;
    if(dist_km > 0.0)
        out->hp_deg = raddeg(asin(EARTH_RAD_KM/dist_km));

    out->distance = (body == NAV_MOON) ? dist_km : o.s_edist;
    out->elong_deg = (body == NAV_SUN) ? 0.0 : fabs(o.s_elong);
    out->magnitude = get_mag(&o);
    return 0;
}

/* ------------------------------------------------------------ stars */

int nav_x_star_almanac(size_t index,double jd_ut,NavStarAlmanac *out)
{
    const NavStarCat *sc;
    Now nw;
    Obj o;
    double gast,dec_rad;

    if(!out || index >= nav_star_cat_count) return 1;
    sc = &nav_star_cat[index];

    now_at(&nw,jd_ut);
    memset(&o,0,sizeof o);
    o.o_type = FIXED;
    o.f_class = 'S';
    strcpy(o.o_name,sc->name);
    o.f_RA  = (float)degrad(sc->ra2000_deg);
    o.f_dec = (float)degrad(sc->dec2000_deg);
    o.f_epoch = J2000;
    set_fmag(&o,sc->magnitude);

    /* .edb proper motions are milliarcseconds per year, the RA rate being the
     * great-circle value; libastro wants radians per day, with the RA rate
     * expressed along the RA axis (hence the division by cos dec). */
    dec_rad = degrad(sc->dec2000_deg);
    o.f_pmRA  = (float)(1.327e-11 * sc->pm_ra_mas / cos(dec_rad));
    o.f_pmdec = (float)(1.327e-11 * sc->pm_dec_mas);

    if(obj_cir(&nw,&o)) return 1;

    gast = nav_x_gast_deg(jd_ut);
    out->name      = sc->name;
    out->ra_deg    = norm360(raddeg(o.s_ra));
    out->dec_deg   = raddeg(o.s_dec);
    out->sha_deg   = norm360(360.0 - out->ra_deg);
    out->gha_deg   = norm360(gast - out->ra_deg);
    out->magnitude = sc->magnitude;
    return 0;
}
