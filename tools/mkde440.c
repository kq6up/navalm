/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* mkde440.c - repack a JPL SPK kernel into navalm's compact ephemeris file.
 *
 * Build-time tool.  Reads an SPK (DAF) kernel such as JPL's de440s.bsp and
 * writes the Chebyshev coefficient blocks navalm needs into a single flat
 * file with a fixed header, so the shipped binary never has to parse the
 * SPICE container format.
 *
 * The coefficients are copied verbatim.  Nothing is refitted, resampled or
 * rounded: the output holds exactly the double-precision polynomial
 * coefficients JPL published, for the subset of bodies navalm uses.
 *
 *   cc -O2 -o mkde440 mkde440.c
 *   ./mkde440 de440s.bsp de440s.nav
 *
 * SPK/DAF layout references:
 *   NAIF, "DAF Required Reading" and "SPK Required Reading"
 *   (https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/).
 *
 * File record:  LOCIDW[8] ND[i32] NI[i32] LOCIFN[60] FWARD[i32] BWARD[i32]
 *               FREE[i32] LOCFMT[8] ...
 * Summary record (1024 bytes): NEXT, PREV, NSUM as doubles, then NSUM
 *               summaries of ND doubles + NI ints packed into
 *               ND + (NI+1)/2 doubles.  For SPK, ND=2 and NI=6: the doubles
 *               are the segment start and end epochs (TDB seconds past
 *               J2000) and the ints are target, centre, frame, data type,
 *               and the first and last word addresses of the segment.
 * The name record follows immediately after each summary record.
 *
 * SPK type 2 segment: N records of RSIZE doubles, then the four-double
 *               trailer INIT, INTLEN, RSIZE, N.  Each record is MID,
 *               RADIUS, then (RSIZE-2)/3 Chebyshev coefficients for each
 *               of x, y and z.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "nav_eph440.h"

#define DAF_RECLEN 1024
#define J2000_JD   2451545.0

/* Bodies navalm needs, by NAIF code.  Uranus, Neptune and Pluto are in the
 * kernel but navalm has no use for them, so they are left out. */
static const int wanted[] = {
    NAV_EPH_MERCURY_BARY, NAV_EPH_VENUS_BARY, NAV_EPH_EMB,
    NAV_EPH_MARS_BARY,    NAV_EPH_JUPITER_BARY, NAV_EPH_SATURN_BARY,
    NAV_EPH_SUN,          NAV_EPH_MOON,       NAV_EPH_EARTH
};
#define NWANTED ((int)(sizeof wanted / sizeof wanted[0]))

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

static double rd_f64(const unsigned char *p)
{
    uint64_t v = 0;
    double out;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    memcpy(&out, &v, sizeof out);
    return out;
}

static void wr_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void wr_i32(unsigned char *p, int32_t v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    wr_u32(p, u);
}

static void wr_u64(unsigned char *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) { p[i] = (unsigned char)(v & 0xff); v >>= 8; }
}

static void wr_f64(unsigned char *p, double d)
{
    uint64_t v;
    memcpy(&v, &d, sizeof v);
    wr_u64(p, v);
}

/* ---- segment table ---- */

typedef struct {
    int    target, center, frame, type;
    long   start, end;          /* 1-based double-word addresses */
    double et_begin, et_end;
} Segment;

static int read_at(FILE *f, long byte_off, void *buf, size_t n)
{
    if (fseek(f, byte_off, SEEK_SET) != 0) return 1;
    return fread(buf, 1, n, f) != n;
}

int main(int argc, char **argv)
{
    const char *inpath, *outpath;
    FILE *in = NULL, *out = NULL;
    unsigned char rec[DAF_RECLEN];
    unsigned char *hdr = NULL;
    int nd, ni, fward, ss, i, k, nseg = 0, nout = 0, rc = 1;
    long next;
    char locidw[9], locfmt[9];
    Segment *segs = NULL;
    int segcap = 0;
    double jd_begin = 0.0, jd_end = 0.0;
    long hdr_bytes, data_pos;

    if (argc != 3) {
        fprintf(stderr,
            "usage: %s <input.bsp> <output.nav>\n\n"
            "Repacks the Chebyshev coefficients navalm needs out of a JPL SPK\n"
            "kernel (for example de440s.bsp) into navalm's flat ephemeris file.\n",
            argv[0]);
        return 2;
    }
    inpath = argv[1];
    outpath = argv[2];

    if (!(in = fopen(inpath, "rb"))) {
        fprintf(stderr, "mkde440: cannot open %s\n", inpath);
        return 1;
    }

    /* ---- file record ---- */
    if (read_at(in, 0, rec, DAF_RECLEN)) {
        fprintf(stderr, "mkde440: short read on the DAF file record\n");
        goto done;
    }
    memcpy(locidw, rec, 8); locidw[8] = 0;
    memcpy(locfmt, rec + 88, 8); locfmt[8] = 0;
    for (i = 7; i >= 0 && (locidw[i] == ' ' || !locidw[i]); i--) locidw[i] = 0;
    for (i = 7; i >= 0 && (locfmt[i] == ' ' || !locfmt[i]); i--) locfmt[i] = 0;

    if (strcmp(locidw, "DAF/SPK") != 0) {
        fprintf(stderr, "mkde440: %s is not a DAF/SPK kernel (found \"%s\")\n",
                inpath, locidw);
        goto done;
    }
    if (strcmp(locfmt, "LTL-IEEE") != 0) {
        fprintf(stderr,
            "mkde440: %s is %s; only little-endian (LTL-IEEE) kernels are read.\n"
            "Convert it with NAIF's bingo or re-download the little-endian file.\n",
            inpath, locfmt);
        goto done;
    }

    nd    = rd_i32(rec + 8);
    ni    = rd_i32(rec + 12);
    fward = rd_i32(rec + 76);
    if (nd != 2 || ni != 6) {
        fprintf(stderr, "mkde440: unexpected SPK summary shape ND=%d NI=%d "
                        "(expected 2 and 6)\n", nd, ni);
        goto done;
    }
    ss = nd + (ni + 1) / 2;     /* summary size, in doubles */

    /* ---- walk the summary records ---- */
    next = fward;
    while (next != 0) {
        double nsum;
        int n;
        if (read_at(in, (next - 1) * (long)DAF_RECLEN, rec, DAF_RECLEN)) {
            fprintf(stderr, "mkde440: short read on summary record %ld\n", next);
            goto done;
        }
        nsum = rd_f64(rec + 16);
        n = (int)nsum;
        if (n < 0 || n > (DAF_RECLEN - 24) / (ss * 8)) {
            fprintf(stderr, "mkde440: summary record %ld claims %d summaries\n",
                    next, n);
            goto done;
        }
        if (nseg + n > segcap) {
            Segment *g;
            segcap = (nseg + n) * 2 + 8;
            if (!(g = realloc(segs, (size_t)segcap * sizeof *segs))) {
                fprintf(stderr, "mkde440: out of memory\n");
                goto done;
            }
            segs = g;
        }
        for (i = 0; i < n; i++) {
            const unsigned char *p = rec + 24 + (size_t)i * ss * 8;
            Segment *s = &segs[nseg++];
            s->et_begin = rd_f64(p);
            s->et_end   = rd_f64(p + 8);
            s->target   = rd_i32(p + 16);
            s->center   = rd_i32(p + 20);
            s->frame    = rd_i32(p + 24);
            s->type     = rd_i32(p + 28);
            s->start    = rd_i32(p + 32);
            s->end      = rd_i32(p + 36);
        }
        next = (long)rd_f64(rec);
    }

    printf("%s: DAF/SPK, %d segments\n", inpath, nseg);

    /* ---- header ---- */
    hdr_bytes = NAV_EPH_HDR_BYTES + (long)NWANTED * NAV_EPH_DIR_BYTES;
    if (!(hdr = calloc(1, (size_t)hdr_bytes))) {
        fprintf(stderr, "mkde440: out of memory\n");
        goto done;
    }
    if (!(out = fopen(outpath, "wb"))) {
        fprintf(stderr, "mkde440: cannot create %s\n", outpath);
        goto done;
    }
    if (fseek(out, hdr_bytes, SEEK_SET) != 0) {
        fprintf(stderr, "mkde440: cannot seek past the header\n");
        goto done;
    }
    data_pos = hdr_bytes;

    printf("\n%-10s %7s %7s %10s %7s %6s %9s %10s\n",
           "body", "target", "centre", "interval", "coeffs", "recs", "degree", "bytes");

    for (k = 0; k < NWANTED; k++) {
        Segment *s = NULL;
        unsigned char trailer[32], *dir;
        double init, intlen, rsize_d, n_d;
        long nrec, rsize, ncoef, nbytes, remaining;
        double seg_jd0, seg_jd1;

        for (i = 0; i < nseg; i++)
            if (segs[i].target == wanted[k]) { s = &segs[i]; break; }
        if (!s) {
            fprintf(stderr, "mkde440: kernel has no segment for NAIF body %d\n",
                    wanted[k]);
            goto done;
        }
        if (s->type != 2) {
            fprintf(stderr, "mkde440: NAIF body %d is SPK type %d; "
                            "only type 2 (Chebyshev position) is handled\n",
                    s->target, s->type);
            goto done;
        }
        if (s->frame != 1) {
            fprintf(stderr, "mkde440: NAIF body %d is in frame %d, not J2000/ICRF (1)\n",
                    s->target, s->frame);
            goto done;
        }

        /* type 2 trailer: INIT, INTLEN, RSIZE, N */
        if (read_at(in, (s->end - 4) * 8L, trailer, 32)) {
            fprintf(stderr, "mkde440: short read on the type 2 trailer for %d\n",
                    s->target);
            goto done;
        }
        init    = rd_f64(trailer);
        intlen  = rd_f64(trailer + 8);
        rsize_d = rd_f64(trailer + 16);
        n_d     = rd_f64(trailer + 24);
        rsize = (long)rsize_d;
        nrec  = (long)n_d;

        if (rsize < 8 || (rsize - 2) % 3 != 0 || nrec < 1) {
            fprintf(stderr, "mkde440: body %d has an implausible record geometry "
                            "(RSIZE=%ld N=%ld)\n", s->target, rsize, nrec);
            goto done;
        }
        if (s->end - s->start + 1 != nrec * rsize + 4) {
            fprintf(stderr, "mkde440: body %d segment length %ld does not match "
                            "N*RSIZE+4 = %ld\n", s->target,
                    s->end - s->start + 1, nrec * rsize + 4);
            goto done;
        }
        ncoef = (rsize - 2) / 3;

        seg_jd0 = J2000_JD + init / 86400.0;
        seg_jd1 = seg_jd0 + (double)nrec * intlen / 86400.0;
        if (k == 0) { jd_begin = seg_jd0; jd_end = seg_jd1; }
        else {
            if (seg_jd0 > jd_begin) jd_begin = seg_jd0;
            if (seg_jd1 < jd_end)   jd_end   = seg_jd1;
        }

        /* copy the coefficient block verbatim */
        nbytes = nrec * rsize * 8;
        if (fseek(in, (s->start - 1) * 8L, SEEK_SET) != 0) {
            fprintf(stderr, "mkde440: cannot seek to the data for body %d\n", s->target);
            goto done;
        }
        remaining = nbytes;
        while (remaining > 0) {
            unsigned char buf[65536];
            size_t want = (size_t)(remaining < (long)sizeof buf ? remaining : (long)sizeof buf);
            if (fread(buf, 1, want, in) != want) {
                fprintf(stderr, "mkde440: short read copying body %d\n", s->target);
                goto done;
            }
            if (fwrite(buf, 1, want, out) != want) {
                fprintf(stderr, "mkde440: short write copying body %d\n", s->target);
                goto done;
            }
            remaining -= (long)want;
        }

        dir = hdr + NAV_EPH_HDR_BYTES + (size_t)k * NAV_EPH_DIR_BYTES;
        wr_i32(dir,      s->target);
        wr_i32(dir + 4,  s->center);
        wr_i32(dir + 8,  (int32_t)nrec);
        wr_i32(dir + 12, (int32_t)rsize);
        wr_i32(dir + 16, (int32_t)ncoef);
        wr_i32(dir + 20, 0);
        wr_f64(dir + 24, seg_jd0);
        wr_f64(dir + 32, intlen / 86400.0);
        wr_u64(dir + 40, (uint64_t)data_pos);
        data_pos += nbytes;
        nout++;

        printf("%-10s %7d %7d %9.3fd %7ld %6ld %9ld %10ld\n",
               nav_eph_body_name(s->target), s->target, s->center,
               intlen / 86400.0, ncoef, nrec, ncoef - 1, nbytes);
    }

    memcpy(hdr, NAV_EPH_MAGIC, 8);
    wr_u32(hdr + 8,  NAV_EPH_FORMAT_VERSION);
    wr_u32(hdr + 12, (uint32_t)nout);
    wr_f64(hdr + 16, jd_begin);
    wr_f64(hdr + 24, jd_end);
    wr_f64(hdr + 32, NAV_EPH_AU_KM);
    wr_f64(hdr + 40, NAV_EPH_EMRAT);
    wr_u32(hdr + 48, 0);
    wr_u32(hdr + 52, 0);

    if (fseek(out, 0, SEEK_SET) != 0 ||
        fwrite(hdr, 1, (size_t)hdr_bytes, out) != (size_t)hdr_bytes) {
        fprintf(stderr, "mkde440: cannot write the header\n");
        goto done;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "mkde440: error closing %s\n", outpath);
        out = NULL;
        goto done;
    }
    out = NULL;

    printf("\nwrote %s\n", outpath);
    printf("  bodies     %d\n", nout);
    printf("  JD range   %.1f to %.1f\n", jd_begin, jd_end);
    printf("  size       %.1f MB\n", data_pos / 1048576.0);
    rc = 0;

done:
    if (in) fclose(in);
    if (out) { fclose(out); remove(outpath); }
    free(segs);
    free(hdr);
    return rc;
}
