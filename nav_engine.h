#ifndef NAV_ENGINE_H
#define NAV_ENGINE_H

#include <stddef.h>

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

#endif
