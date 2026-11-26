/* SPDX-License-Identifier: MIT */
#ifndef NAVOCCULT_H
#define NAVOCCULT_H
#include "nav_engine.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NAVOCCULT_MAX_EVENTS 2048
#define NAVOCCULT_RADIUS_KM 1737.4
/* Epochs are UTC Julian dates. Longitude is east positive, elevation metres. */
typedef struct {
    double start_jd, days, lat, lon, height_m, dut1_sec;
    double max_magnitude, min_alt_deg, max_sun_alt_deg, margin_arcsec;
    int night_only;
} NavOccultRequest;
typedef struct {
    int star, has_contacts, grazing, double_star;
    double closest_jd, separation_deg, sd_deg, impact, clearance_arcsec;
    double contact_jd[2], altitude_deg[2], sun_alt_deg[2], pa_deg[2], rate_arcsec_sec[2];
    int bright_limb[2], visible[2]; /* [0] disappearance, [1] reappearance */
    double closest_alt_deg, closest_sun_alt_deg;
} NavOccultEvent;
typedef struct { size_t count; NavOccultEvent events[NAVOCCULT_MAX_EVENTS]; } NavOccultResult;
typedef struct {
    double watch_jd, reaction_sec, limb_offset_arcsec;
    double timing_sigma_sec, limb_sigma_arcsec, position_sigma_nm;
    int contact; /* 0 disappearance, 1 reappearance */
} NavOccultObservation;
typedef struct {
    double utc_jd, correction_sec, rate_arcsec_sec, sigma_time_sec;
    double longitude_sigma_nm, position_sigma_sec, limb_sigma_sec;
    int weak;
} NavOccultRecovery;
/* Progress returns nonzero to cancel; results are empty on failure/cancel. */
typedef int (*NavOccultProgress)(double fraction,void *user);
size_t navoccult_star_count(void);
const NavStarCat *navoccult_star(size_t);
const char *navoccult_aliases(size_t);
int navoccult_star_find(const char *name);
void navoccult_init(NavOccultRequest *);
int navoccult_validate(const NavOccultRequest *,char *,size_t);
int navoccult_search(const NavOccultRequest *,NavOccultResult *,NavOccultProgress,void *,char *,size_t);
/* Refine one local conjunction within +/-3 hours of near_jd, including both
 * contacts outside the planner interval. Positive offset increases lunar SD. */
int navoccult_event(const NavOccultRequest *,int star,double near_jd,double limb_offset_arcsec,
                   NavOccultEvent *,char *,size_t);
int navoccult_recover(const NavOccultRequest *,const NavOccultEvent *,const NavOccultObservation *,
                     NavOccultRecovery *,char *,size_t);
void navoccult_sort(NavOccultResult *,int mode); /* 0 time, 1 magnitude, 2 central, 3 grazing */
void navoccult_report(FILE *,const NavOccultRequest *,const NavOccultResult *);
void navoccult_recovery_report(FILE *,const NavOccultRequest *,const NavOccultEvent *,
                              const NavOccultObservation *,const NavOccultRecovery *);
void navoccult_stamp(double jd,char *,size_t);
int navoccult_command(int argc,char **argv);
#ifdef __cplusplus
}
#endif
#endif
