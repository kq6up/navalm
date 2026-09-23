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
 * Conventions here:
 *   - positions are apparent geocentric, equinox and equator of date
 *   - GHA = GAST - RA(apparent), where GAST is libastro's GMST plus the
 *     equation of the equinoxes, so GHA pairs with an apparent RA
 *   - HP  = arcsin(equatorial Earth radius / geocentric distance)
 *   - SD  = half of libastro's apparent angular diameter
 */

#include "nav_engine.h"
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

double nav_julian_date(int year,int month,int day,int hour,int minute,double second)
{
    double mj;
    cal_mjd(month,(double)day,year,&mj);
    return mj + MJD0 + (hour + minute/60.0 + second/3600.0)/24.0;
}

/* ---------------------------------------------------------------- delta T
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

/* Espenak and Meeus polynomial fits, used outside the leap-second era */
static double deltat_poly(double year)
{
    double t,u;
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
    /* 1961 to the start of the leap-second table */
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

double nav_delta_t_with_dut1_seconds(double jd_ut,double dut1_seconds)
{
    if(jd_ut < leap_table[0].jd){
        double year = 2000.0 + (jd_ut - 2451545.0)/365.25;
        return deltat_poly(year);
    }
    return 32.184 + tai_minus_utc(jd_ut) - dut1_seconds;
}

double nav_delta_t_seconds(double jd_ut)
{
    return nav_delta_t_with_dut1_seconds(jd_ut,0.0);
}

/* libastro asks for delta T through deltat(); its own table stops in the
 * 2010s and over-extrapolates by several seconds for the 2020s, so this
 * replaces it (libastro/deltat.c is left out of the build).  Argument and
 * result match libastro: modified julian date in, seconds out. */
double deltat(double mj)
{
    return nav_delta_t_seconds(mj + MJD0);
}

/* Greenwich apparent sidereal time, degrees */
double nav_gast_deg(double jd_ut)
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

double nav_gha_aries_deg(double jd_ut)
{
    return nav_gast_deg(jd_ut);
}

int nav_moon_geocentric(double jd_ut,double *lon_deg,double *lat_deg,double *distance_km)
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

int nav_almanac(NavBody body,double jd_ut,NavAlmanac *out)
{
    Now nw;
    Obj o;
    int code;
    double gast,dist_km;

    if(!out) return 1;
    memset(out,0,sizeof *out);
    gast = nav_gast_deg(jd_ut);

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
    return 0;
}

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

/* ------------------------------------------------------------ stars */

size_t nav_star_count_get(void){ return nav_star_cat_count; }

const char *nav_star_name(size_t index)
{
    return (index < nav_star_cat_count) ? nav_star_cat[index].name : "";
}

/* letters and digits only, case-folded: "al nair", "Al Na'ir", "ALNAIR" match */
static void squash(const char *s,char *out,size_t n)
{
    size_t k = 0;
    if(!s){ out[0] = 0; return; }
    for(; *s && k+1 < n; s++)
        if(isalnum((unsigned char)*s)) out[k++] = (char)tolower((unsigned char)*s);
    out[k] = 0;
}

int nav_star_find(const char *name)
{
    char want[64],have[64];
    size_t i;
    if(!name || !*name) return -1;
    squash(name,want,sizeof want);
    if(!want[0]) return -1;
    for(i=0;i<nav_star_cat_count;i++){
        squash(nav_star_cat[i].name,have,sizeof have);
        if(!strcmp(have,want)) return (int)i;
    }
    return -1;
}

int nav_star_almanac(size_t index,double jd_ut,NavStarAlmanac *out)
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

    gast = nav_gast_deg(jd_ut);
    out->name      = sc->name;
    out->ra_deg    = norm360(raddeg(o.s_ra));
    out->dec_deg   = raddeg(o.s_dec);
    out->sha_deg   = norm360(360.0 - out->ra_deg);
    out->gha_deg   = norm360(gast - out->ra_deg);
    out->magnitude = sc->magnitude;
    return 0;
}
