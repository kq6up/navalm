/* SPDX-License-Identifier: MIT */
#ifndef NAVLUNAR_H
#define NAVLUNAR_H
#include "navsight.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NAVLUNAR_MAX_SOLUTIONS 8
typedef struct {
    int measured;
    Sight sight;                  /* original watch epoch and raw Hs */
} NavLunarAltitude;
typedef struct {
    Sight distance;               /* other body, raw distance in hs_deg, watch epoch, AP */
    NavLunarAltitude moon, other;
    int moon_contact, other_contact; /* +1 near, 0 centre, -1 far */
    int refine_position;           /* requires both measured altitudes */
    double initial_correction_sec; /* UTC minus watch; applied ONCE */
    double search_seconds;         /* half span around initial correction */
    double observer_height_m;      /* ellipsoidal height, distinct from eye height */
    double distance_sigma_min, altitude_sigma_min;
} NavLunarRequest;
typedef struct {
    double correction_sec, additional_sec, utc_jd, lat, lon;
    double raw_distance_deg, predicted_hs[2], altitude_jd[2];
    double centre_alt_deg[2], azimuth_deg[2]; /* at distance epoch, apparent */
    double geocentric_deg, topocentric_deg, apparent_deg, moon_sd_min;
    double residual_min[3], rate_min_per_minute, sigma_time_sec;
    int weak;
} NavLunarSolution;
typedef struct {
    int count, scanned;
    NavLunarSolution solutions[NAVLUNAR_MAX_SOLUTIONS]; /* nearest AP first */
} NavLunarResult;
void navlunar_init(NavLunarRequest *q);
int navlunar_validate(const NavLunarRequest *q,char *err,size_t n);
/* Predict all raw angles for a candidate UTC correction and reference AP.
 * The supplied request is never modified. Synthetic altitude epochs always
 * equal the corrected distance epoch; measured epochs retain their intervals. */
int navlunar_predict(const NavLunarRequest *,double correction,double lat,double lon,
                     NavLunarSolution *,char *,size_t);
int navlunar_solve(const NavLunarRequest *,NavLunarResult *,char *,size_t);
void navlunar_report(FILE *,const NavLunarRequest *,const NavLunarResult *);
int navlunar_load(const char *,NavLunarRequest *,char *,size_t);
int navlunar_write(FILE *,const NavLunarRequest *);
int navlunar_command(int argc,char **argv);
#ifdef __cplusplus
}
#endif
#endif
