/* SPDX-License-Identifier: MIT */
#ifndef NAVSKY_H
#define NAVSKY_H
#include "nav_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NAVSKY_MAX_STARS 128
#define NAVSKY_BODY_COUNT 7
#define NAVSKY_MAX_CONTEXT 768
#define NAVSKY_MAX_FIGURES 89
#define NAVSKY_MAX_PATHS 800
#define NAVSKY_MAX_POINTS 8192
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
    const char *constellation;     /* IAU region name, empty for solar-system bodies */
    int is_context;               /* recognition aid only; cannot become a sight */
    NavBody body;                  /* meaningful only when is_body is set */
} NavSkyStar;
typedef struct { double altitude, azimuth; } NavSkyPoint;
typedef struct { unsigned short start,count,figure; } NavSkyPath;
typedef struct { const char *name; double altitude,azimuth; int visible; } NavSkyFigure;
typedef struct {
    NavSkyStar context[NAVSKY_MAX_CONTEXT];
    size_t context_count;
    NavSkyFigure figures[NAVSKY_MAX_FIGURES];
    size_t figure_count;
    NavSkyPath paths[NAVSKY_MAX_PATHS];
    NavSkyPoint points[NAVSKY_MAX_POINTS];
    size_t path_count,point_count;
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
/* Combined chart index: navigation stars, solar-system bodies, then context stars. */
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
