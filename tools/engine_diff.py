#!/usr/bin/env python3
"""Compare NAVALM's two astronomy engines against each other.

Runs ./navalm twice for each body and date, once with --engine precise
(NOVAS C3.1 + JPL DE440s) and once with --engine classic (XEphem
libastro), and reports the differences in arcminutes, which is the unit
the almanac tables print to one decimal.

    ./tools/engine_diff.py                 # default spread of dates
    ./tools/engine_diff.py --stars         # the 58 navigation stars too
    ./tools/engine_diff.py --navalm ./navalm --from 2020 --to 2040

The precise engine is the reference. A disagreement here is not by
itself an error in either engine: libastro's VSOP87 and Moshier lunar
theory are truncated analytic series, and differences of a few tenths of
an arcminute on the Moon are expected and documented in VALIDATION.md.
"""

import argparse
import math
import subprocess
import sys

BODIES = ["sun", "moon", "mercury", "venus", "mars", "jupiter", "saturn", "aries"]


def parse_angle(line):
    """'GHA 000 40.01' or 'Dec N05 11.89' -> degrees."""
    f = line.split()
    if len(f) < 3:
        return None
    sign = 1.0
    d = f[1]
    if d[0] in "NSEW+-":
        if d[0] in "SW-":
            sign = -1.0
        d = d[1:]
    try:
        return sign * (float(d) + float(f[2]) / 60.0)
    except ValueError:
        return None


def run(navalm, engine, args):
    out = subprocess.run([navalm, "--engine", engine] + args,
                         capture_output=True, text=True)
    vals = {}
    for line in out.stdout.splitlines():
        for key in ("GHA", "Dec", "SHA"):
            if line.startswith(key + " "):
                v = parse_angle(line)
                if v is not None:
                    vals[key] = v
    return vals


def diff_min(a, b, wrap):
    d = a - b
    if wrap:
        d = (d + 180.0) % 360.0 - 180.0
    return d * 60.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--navalm", default="./navalm")
    ap.add_argument("--stars", action="store_true")
    ap.add_argument("--from", dest="y0", type=int, default=1950)
    ap.add_argument("--to", dest="y1", type=int, default=2140)
    ap.add_argument("--steps", type=int, default=8)
    a = ap.parse_args()

    years = [a.y0 + i * (a.y1 - a.y0) // max(1, a.steps - 1) for i in range(a.steps)]
    dates = []
    for i, y in enumerate(years):
        m = (i * 5) % 12 + 1
        d = (i * 7) % 27 + 1
        h = (i * 3) % 24
        dates.append((y, m, d, h, 0, 0))

    print(f"{'date':>11} {'body':>8} {'dGHA (min)':>11} {'dDec (min)':>11}")
    worst = {}
    for dt in dates:
        for body in BODIES:
            args = [body] + [str(x) for x in dt]
            p = run(a.navalm, "precise", args)
            c = run(a.navalm, "classic", args)
            if "GHA" not in p or "GHA" not in c:
                print(f"{dt[0]}-{dt[1]:02d}-{dt[2]:02d} {body:>8}   (no result)")
                continue
            dg = diff_min(p["GHA"], c["GHA"], True)
            dd = diff_min(p.get("Dec", 0.0), c.get("Dec", 0.0), False)
            key = body
            w = worst.get(key, (0.0, 0.0))
            worst[key] = (max(w[0], abs(dg)), max(w[1], abs(dd)))
            print(f"{dt[0]}-{dt[1]:02d}-{dt[2]:02d} {body:>8} {dg:11.3f} {dd:11.3f}")

    if a.stars:
        names = subprocess.run([a.navalm, "list-stars"],
                               capture_output=True, text=True).stdout.split("\n")
        names = [n for n in names if n.strip()]
        dt = dates[len(dates) // 2]
        print(f"\n{'star':>16} {'dSHA (min)':>11} {'dDec (min)':>11}")
        ws, wd, wn = 0.0, 0.0, ""
        for n in names:
            args = ["star", n] + [str(x) for x in dt]
            p = run(a.navalm, "precise", args)
            c = run(a.navalm, "classic", args)
            if "SHA" not in p or "SHA" not in c:
                continue
            ds = diff_min(p["SHA"], c["SHA"], True)
            dd = diff_min(p.get("Dec", 0.0), c.get("Dec", 0.0), False)
            if abs(ds) > ws:
                ws, wn = abs(ds), n
            wd = max(wd, abs(dd))
            print(f"{n:>16} {ds:11.3f} {dd:11.3f}")
        print(f"\nworst star: {wn}  dSHA {ws:.3f}'  worst dDec {wd:.3f}'")

    print(f"\n{'body':>8} {'worst dGHA':>11} {'worst dDec':>11}   (arcminutes)")
    for body in BODIES:
        if body in worst:
            print(f"{body:>8} {worst[body][0]:11.3f} {worst[body][1]:11.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
