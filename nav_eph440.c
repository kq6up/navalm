/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_eph440.c - reader for navalm's compact JPL ephemeris file.
 *
 * Evaluates the JPL Chebyshev position polynomials written by
 * tools/mkde440.c.  The file is read through a small per-body record cache
 * rather than loaded whole, so a daily table costs a handful of short reads
 * even though the ephemeris itself is tens of megabytes.
 *
 * See nav_eph440.h for the file layout.
 */

#include "nav_eph440.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define J2000_JD 2451545.0
#define MAXDEPTH 4

typedef struct {
    int    naif, center, nrec, rsize, ncoef;
    double init_jd, intlen_d;
    long   off;
    double *cache;        /* rsize doubles of the cached record */
    long   cached;        /* record index held in cache, -1 if none */
} Body;

struct NavEph {
    FILE   *f;
    char   *path;
    int     nbody;
    Body   *b;
    double  jd_begin, jd_end, au_km, emrat;
};

/* ---- little-endian decoding, independent of host byte order ---- */

static uint32_t rd_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_i32(const unsigned char *p)
{
    uint32_t v = rd_u32(p);
    int32_t out;
    memcpy(&out, &v, sizeof out);
    return out;
}

static uint64_t rd_u64(const unsigned char *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static double rd_f64(const unsigned char *p)
{
    uint64_t v = rd_u64(p);
    double out;
    memcpy(&out, &v, sizeof out);
    return out;
}

static void fail(char *err, size_t n, const char *msg)
{
    if (err && n) { strncpy(err, msg, n - 1); err[n - 1] = 0; }
}

const char *nav_eph_body_name(int naif)
{
    switch (naif) {
        case NAV_EPH_SSB:          return "SSB";
        case NAV_EPH_MERCURY_BARY: return "Mercury";
        case NAV_EPH_VENUS_BARY:   return "Venus";
        case NAV_EPH_EMB:          return "EMB";
        case NAV_EPH_MARS_BARY:    return "Mars";
        case NAV_EPH_JUPITER_BARY: return "Jupiter";
        case NAV_EPH_SATURN_BARY:  return "Saturn";
        case NAV_EPH_SUN:          return "Sun";
        case NAV_EPH_MOON:         return "Moon";
        case NAV_EPH_EARTH:        return "Earth";
    }
    return "?";
}

int nav_eph_open(const char *path, NavEph **out, char *err, size_t errlen)
{
    NavEph *e = NULL;
    unsigned char hdr[NAV_EPH_HDR_BYTES];
    unsigned char *dir = NULL;
    int i, nbody;
    size_t dirbytes;
    char msg[512];

    if (out) *out = NULL;
    if (!path || !*path || !out) {
        fail(err, errlen, "no ephemeris path given");
        return 1;
    }

    if (!(e = calloc(1, sizeof *e))) {
        fail(err, errlen, "out of memory opening the ephemeris");
        return 1;
    }

    if (!(e->f = fopen(path, "rb"))) {
        snprintf(msg, sizeof msg, "cannot open ephemeris file %s", path);
        fail(err, errlen, msg);
        goto bad;
    }
    if (fread(hdr, 1, sizeof hdr, e->f) != sizeof hdr) {
        snprintf(msg, sizeof msg, "%s is too short to be an ephemeris file", path);
        fail(err, errlen, msg);
        goto bad;
    }
    if (memcmp(hdr, NAV_EPH_MAGIC, 7) != 0) {
        snprintf(msg, sizeof msg,
                 "%s is not a navalm ephemeris file (bad magic); "
                 "build it with tools/mkde440", path);
        fail(err, errlen, msg);
        goto bad;
    }
    if (rd_u32(hdr + 8) != NAV_EPH_FORMAT_VERSION) {
        snprintf(msg, sizeof msg,
                 "%s is ephemeris format version %u, this build expects %u",
                 path, rd_u32(hdr + 8), NAV_EPH_FORMAT_VERSION);
        fail(err, errlen, msg);
        goto bad;
    }

    nbody = (int)rd_u32(hdr + 12);
    if (nbody < 1 || nbody > 64) {
        snprintf(msg, sizeof msg, "%s claims %d bodies", path, nbody);
        fail(err, errlen, msg);
        goto bad;
    }
    e->nbody    = nbody;
    e->jd_begin = rd_f64(hdr + 16);
    e->jd_end   = rd_f64(hdr + 24);
    e->au_km    = rd_f64(hdr + 32);
    e->emrat    = rd_f64(hdr + 40);

    dirbytes = (size_t)nbody * NAV_EPH_DIR_BYTES;
    if (!(dir = malloc(dirbytes)) || !(e->b = calloc((size_t)nbody, sizeof *e->b))) {
        fail(err, errlen, "out of memory reading the ephemeris directory");
        goto bad;
    }
    if (fread(dir, 1, dirbytes, e->f) != dirbytes) {
        snprintf(msg, sizeof msg, "%s has a truncated directory", path);
        fail(err, errlen, msg);
        goto bad;
    }

    for (i = 0; i < nbody; i++) {
        const unsigned char *p = dir + (size_t)i * NAV_EPH_DIR_BYTES;
        Body *b = &e->b[i];
        b->naif     = rd_i32(p);
        b->center   = rd_i32(p + 4);
        b->nrec     = rd_i32(p + 8);
        b->rsize    = rd_i32(p + 12);
        b->ncoef    = rd_i32(p + 16);
        b->init_jd  = rd_f64(p + 24);
        b->intlen_d = rd_f64(p + 32);
        b->off      = (long)rd_u64(p + 40);
        b->cached   = -1;

        if (b->nrec < 1 || b->rsize < 8 || b->ncoef < 2 ||
            b->rsize != 2 + 3 * b->ncoef || b->intlen_d <= 0.0) {
            snprintf(msg, sizeof msg,
                     "%s has an implausible record geometry for %s",
                     path, nav_eph_body_name(b->naif));
            fail(err, errlen, msg);
            goto bad;
        }
        if (!(b->cache = malloc((size_t)b->rsize * sizeof(double)))) {
            fail(err, errlen, "out of memory allocating the record cache");
            goto bad;
        }
    }

    free(dir);
    if (!(e->path = malloc(strlen(path) + 1))) {
        fail(err, errlen, "out of memory");
        goto bad;
    }
    strcpy(e->path, path);
    *out = e;
    return 0;

bad:
    free(dir);
    nav_eph_close(e);
    return 1;
}

void nav_eph_close(NavEph *e)
{
    int i;
    if (!e) return;
    if (e->b) {
        for (i = 0; i < e->nbody; i++) free(e->b[i].cache);
        free(e->b);
    }
    if (e->f) fclose(e->f);
    free(e->path);
    free(e);
}

double nav_eph_jd_begin(const NavEph *e) { return e ? e->jd_begin : 0.0; }
double nav_eph_jd_end  (const NavEph *e) { return e ? e->jd_end   : 0.0; }
const char *nav_eph_path(const NavEph *e) { return (e && e->path) ? e->path : ""; }

static Body *find_body(NavEph *e, int naif)
{
    int i;
    for (i = 0; i < e->nbody; i++)
        if (e->b[i].naif == naif) return &e->b[i];
    return NULL;
}

/* Loads record 'idx' for body b into its cache. */
static int load_record(NavEph *e, Body *b, long idx)
{
    unsigned char *raw;
    int i;

    if (b->cached == idx) return 0;

    if (!(raw = malloc((size_t)b->rsize * 8))) return 1;
    if (fseek(e->f, b->off + idx * (long)b->rsize * 8, SEEK_SET) != 0 ||
        fread(raw, 1, (size_t)b->rsize * 8, e->f) != (size_t)b->rsize * 8) {
        free(raw);
        b->cached = -1;
        return 1;
    }
    for (i = 0; i < b->rsize; i++)
        b->cache[i] = rd_f64(raw + (size_t)i * 8);
    free(raw);
    b->cached = idx;
    return 0;
}

int nav_eph_state(NavEph *e, int naif, double jd_tdb,
                  double pos[3], double vel[3])
{
    Body *b;
    long idx;
    double et, mid, radius, tau, t[2], dt[2], tk, dtk, sum, dsum;
    int c, k;

    if (!e || !pos) return 1;
    if (!(b = find_body(e, naif))) return 2;
    if (jd_tdb < e->jd_begin || jd_tdb > e->jd_end) return 3;

    idx = (long)((jd_tdb - b->init_jd) / b->intlen_d);
    if (idx < 0) idx = 0;
    if (idx > b->nrec - 1) idx = b->nrec - 1;
    if (load_record(e, b, idx)) return 4;

    et     = (jd_tdb - J2000_JD) * 86400.0;
    mid    = b->cache[0];
    radius = b->cache[1];
    if (radius <= 0.0) return 4;

    tau = (et - mid) / radius;
    /* A date on a record boundary can land a hair outside [-1,1]; the
     * polynomial is still valid there, but clamp to keep the recurrence
     * well conditioned. */
    if (tau < -1.0) tau = -1.0;
    if (tau >  1.0) tau =  1.0;

    for (c = 0; c < 3; c++) {
        const double *co = &b->cache[2 + (size_t)c * b->ncoef];

        /* Chebyshev of the first kind by the standard recurrence, together
         * with its derivative, summed from the low-order end. */
        t[0]  = 1.0;  t[1]  = tau;
        dt[0] = 0.0;  dt[1] = 1.0;
        sum   = co[0] * t[0];
        dsum  = 0.0;
        if (b->ncoef > 1) {
            sum  += co[1] * t[1];
            dsum += co[1] * dt[1];
        }
        for (k = 2; k < b->ncoef; k++) {
            tk  = 2.0 * tau * t[1]  - t[0];
            dtk = 2.0 * t[1] + 2.0 * tau * dt[1] - dt[0];
            sum  += co[k] * tk;
            dsum += co[k] * dtk;
            t[0]  = t[1];   t[1]  = tk;
            dt[0] = dt[1];  dt[1] = dtk;
        }
        pos[c] = sum;
        if (vel) vel[c] = dsum / radius;   /* km/s */
    }
    return 0;
}

int nav_eph_state_ssb(NavEph *e, int naif, double jd_tdb,
                      double pos[3], double vel[3])
{
    double p[3], v[3];
    int body = naif, depth = 0, i, rc;

    if (!e || !pos) return 1;
    pos[0] = pos[1] = pos[2] = 0.0;
    if (vel) vel[0] = vel[1] = vel[2] = 0.0;
    if (naif == NAV_EPH_SSB) return 0;

    /* Walk up the chain of centres: the Moon and the Earth are stored
     * against the EMB, everything else against the barycentre. */
    while (body != NAV_EPH_SSB) {
        Body *b;
        if (++depth > MAXDEPTH) return 5;
        if (!(b = find_body(e, body))) return 2;
        if ((rc = nav_eph_state(e, body, jd_tdb, p, vel ? v : NULL)) != 0)
            return rc;
        for (i = 0; i < 3; i++) {
            pos[i] += p[i];
            if (vel) vel[i] += v[i];
        }
        body = b->center;
    }
    return 0;
}
