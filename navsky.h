/* SPDX-License-Identifier: MIT */
#ifndef NAVSKY_H
#define NAVSKY_H
#include "nav_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NAVSKY_MAX_STARS 128
typedef struct {
    double jd, latitude, longitude; /* east longitude positive */
    double min_altitude, max_altitude, max_magnitude;
    int requested;                 /* 3 or 4 suggested stars */
} NavSkyOptions;
typedef struct {
    const char *name;
    double altitude, azimuth, magnitude;
    int eligible, selected, azimuth_defined;
} NavSkyStar;
typedef struct {
    NavSkyStar stars[NAVSKY_MAX_STARS];
    size_t count;
    int eligible, selected;
    double sun_altitude, geometry, score; /* geometric altitude, normalized scores */
    const char *light;
} NavSky;
/* Geometric altitude and true bearing; no refraction, dip or sextant corrections. */
void navsky_horizontal(double gha,double dec,double lat,double lon,
                       double *alt,double *az,int *az_defined);
int navsky_compute(const NavSkyOptions *,NavSky *,char *error,size_t capacity);
/* Next rising (morning=1) or setting (0) crossing of Sun altitude -8 deg,
 * searched over 48 hours. This is a planning instant, not a visibility forecast. */
int navsky_twilight(double jd,double lat,double lon,int morning,double *result,
                    char *error,size_t capacity);
int navsky_jd(int y,int m,int d,int h,int minute,double sec,double *jd);
void navsky_utc(double jd,char *date,size_t dn,char *time,size_t tn);
#ifdef __cplusplus
}
#endif
#endif
