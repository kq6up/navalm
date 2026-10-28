/* SPDX-License-Identifier: MIT */
#ifndef NAVSKY_H
#define NAVSKY_H
#include "nav_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NAVSKY_MAX_STARS 128
#define NAVSKY_BODY_COUNT 7
typedef struct {
    double jd, latitude, longitude; /* east longitude positive */
    double min_altitude, max_altitude, max_magnitude;
    int requested;                 /* 3 or 4 suggested stars */
} NavSkyOptions;
typedef struct {
    const char *name;
    double altitude, azimuth, magnitude;
    int eligible, selected, azimuth_defined;
    int is_body;                   /* Sun, Moon or planet; excluded from star sets */
    NavBody body;                  /* meaningful only when is_body is set */
} NavSkyStar;
typedef struct {
    NavSkyStar stars[NAVSKY_MAX_STARS];
    size_t count;                  /* navigation stars only */
    NavSkyStar bodies[NAVSKY_BODY_COUNT];
    size_t body_count;
    int eligible, selected;
    double sun_altitude, geometry, score; /* geometric altitude, normalized scores */
    const char *light;
} NavSky;
/* Geometric altitude and true bearing; no refraction, dip or sextant corrections. */
void navsky_horizontal(double gha,double dec,double lat,double lon,
                       double *alt,double *az,int *az_defined);
/* Combined chart index: catalogue stars, followed by solar-system bodies. */
const NavSkyStar *navsky_object(const NavSky *,size_t index);
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
