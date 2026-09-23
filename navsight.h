#ifndef NAVSIGHT_H
#define NAVSIGHT_H

#include <stddef.h>
#include <stdio.h>

/* ------------------------------------------------------------------
   navalm 2.0 sight / LOP layer

   A Sight holds only what the observer supplied.  Everything else is
   recomputed from it, so an edited sight is re-reduced rather than
   patched, and the save file survives changes to the engine.
   ------------------------------------------------------------------ */

#define NAVSIGHT_LABEL 48
#define NAVSIGHT_NAME  32

typedef struct {
    int    id;
    char   body[NAVSIGHT_NAME];   /* sun moon mercury venus mars jupiter saturn star */
    char   star[NAVSIGHT_NAME];   /* star name when body is "star" */

    int    y, mo, d, h, mi;       /* UTC */
    double s;

    double hs_deg;                /* sextant altitude, degrees */
    double ie_min;                /* index error magnitude, arcminutes */
    int    ie_on_arc;             /* 1 = on arc (IC subtracts), 0 = off arc */

    int    artificial;            /* 1 = artificial horizon (halve, no dip) */
    double he;                    /* height of eye, sea horizon only */
    int    he_metres;             /* 1 = metres, 0 = feet */

    int    limb;                  /* +1 lower, 0 center, -1 upper */
    double temp_c;
    double press_mb;

    double dr_lat;                /* degrees, north positive */
    double dr_lon;                /* degrees, EAST positive */

    char   label[NAVSIGHT_LABEL];

    int    has_motion;            /* running fix: vessel motion for advancing */
    double course, speed;         /* degrees true, knots */
    double set, drift;            /* degrees true, knots */
} Sight;

typedef struct {
    /* Hs -> Ho */
    double hs, ic_min, halved, dip_min, ha;
    double r0_min, f, r_min, sd_min, sd_raw_min, par_min, hp_min, ho;
    int    used_sd, used_par;

    /* almanac, hourly row + increments */
    int    hour_h;
    double frac_hour;
    double gha_hour, inc_deg, v_min, vcorr_min, gha;
    double dec_hour, d_min, dcorr_min, dec;
    double sha_deg;               /* stars */
    double gha_aries_hour, gha_aries;
    int    is_star, has_v;
    double gha_exact, dec_exact;  /* engine at the exact instant, for the check line */

    /* assumed position and HO 229 */
    double ap_lat, ap_lon;
    int    lha;
    int    same_name;
    int    dec_deg_whole;
    double tab_hc_min, d_tab_min, dec_inc_min, dcorr229_min, secdiff_min, hc_min;
    double z, zn;

    double intercept_nm;          /* + toward, - away */
} Reduction;

/* Reduce a sight.  Returns 0 on success, non-zero with a message in err. */
int  navsight_reduce(const Sight *si, Reduction *r, char *err, size_t errn);

/* Printing */
void navsight_print_correction(FILE *out, const Sight *si, const Reduction *r);
void navsight_print_reduction(FILE *out, const Sight *si, const Reduction *r);
void navsight_print_brief(FILE *out, const Sight *si, const Reduction *r);

/* Store: ~/.naval.save (override with NAVALM_SAVE) */
typedef struct {
    Sight *v;
    size_t n, cap;
} SightStore;

const char *navsight_store_path(void);
int  navsight_store_load(SightStore *st, char *err, size_t errn);
int  navsight_store_save(const SightStore *st, char *err, size_t errn);
void navsight_store_free(SightStore *st);
int  navsight_store_add(SightStore *st, const Sight *si);      /* assigns id */
Sight *navsight_store_find(SightStore *st, int id);
int  navsight_store_delete(SightStore *st, int id);

/* Running fix.  Advances each selected LOP to fix_* using its own motion,
   then solves for the position.  n>=2.  For n>=3 an uncertainty is given. */
typedef struct {
    double lat, lon;              /* fix, east-positive longitude */
    double residual_nm[16];       /* per-LOP distance from the fix to its LOP */
    double advanced_nm[16];       /* how far each LOP was advanced */
    double advanced_brg[16];
    double rms_nm;
    double semi_major_nm, semi_minor_nm, ellipse_brg;   /* 1-sigma, n>=3 */
    int    have_ellipse;
    int    fy, fmo, fd, fh, fmi;  /* fix time, UTC */
    double fs;
} FixResult;

int navsight_fix(const Sight *sel, const Reduction *red, size_t n,
                 int have_at, int ay, int amo, int ad, int ah, int ami, double as,
                 FixResult *out, char *err, size_t errn);

/* Fuzzy star lookup.
 * Tries, in order: the engine's exact match, then a case-insensitive prefix
 * match, then a substring match, ignoring spaces, hyphens and apostrophes.
 * Fills idx[] with up to max matches and returns how many were found (which
 * may be larger than max).  One match means it resolved; more means the
 * caller should ask which one. */
size_t navsight_star_match(const char *query,int *idx,size_t max);

/* Formatting helpers (shared with main) */
const char *navsight_fmt_lat(double deg,char *buf,size_t n);
const char *navsight_fmt_lon(double deg,char *buf,size_t n);
const char *navsight_fmt_gha(double deg,char *buf,size_t n);

/* Command entry point: handles "sight", "lop" and "fix"; -1 if not ours */
int navsight_command(int argc,char **argv);

/* Helpers shared with main() */
double navsight_jd(const Sight *si);
int    navsight_body_index(const char *name);   /* -1 if not a body name */

#endif
