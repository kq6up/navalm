#!/usr/bin/env python3
"""CLI transit: verify the crossing independently with the almanac lookup."""
from datetime import datetime, timedelta
import re
import subprocess

AP = ["--ap", "34", "1.3", "N", "117", "18.8", "W"]


def nav(*args, ok=True):
    p = subprocess.run(["./navalm", *args], text=True, capture_output=True)
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stderr)
    return p.stdout if ok else p.stderr


def event(output):
    return datetime.fromisoformat(re.search(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) UTC", output, re.M)[1])


def lha(body, t):
    args = [str(v) for v in (t.year, t.month, t.day, t.hour, t.minute, t.second + t.microsecond / 1e6)]
    # GHA is checked through the existing body command, independently of the event scan.
    result = nav(body, *args, *AP)
    deg, minute = map(float, re.search(r"^GHA\s+(\d+)\s+([\d.]+)", result, re.M).groups())
    return (deg + (minute - 18.8) / 60 - 117 + 180) % 360 - 180


winter = nav("transit", "sun", "2026-11-10", *AP, "--timezone", "America/Los_Angeles")
assert "11:33:11." in winter and "PST (UTC-0800)" in winter, winter
assert "UT1 assumed equal to UTC" in winter, winter
for body, day in (("sun", "2026-11-10"), ("moon", "2026-11-15")):
    t = event(nav("transit", body, day, *AP))
    assert abs(lha(body, t)) < 0.01 / 60
    step = timedelta(seconds=2)
    assert lha(body, t - step) < 0 < lha(body, t + step)
assert "No upper meridian transit" in nav("transit", "moon", "2026-11-14", *AP)
two = nav("transit", "star", "Vega", "2026-11-10", "--ap", "34", "0", "N", "130", "12.8", "W")
assert len(re.findall(r"^2026-11-10 .* UTC$", two, re.M)) == 2, two

summer = nav("transit", "sun", "2026-07-10", *AP, "--tz", "America/Los_Angeles")
assert "PDT (UTC-0700)" in summer
assert "UTC-08:00 [PST]" in nav("transit", "sun", "2026-07-10", *AP, "--tz", "PST")
assert "UTC-07:00 [PDT]" in nav("transit", "sun", "2026-07-10", *AP, "--tz", "PDT")
assert "UTC+05:30" in nav("transit", "sun", "2026-11-10", *AP, "--tz", "+05:30")
fixed_day = nav("transit", "star", "Altair", "2026-11-10", *AP, "--tz", "PST", "--local-date")
named_day = nav("transit", "star", "Altair", "2026-11-10", *AP, "--tz", "America/Los_Angeles", "--local-date")
assert event(fixed_day).day == 11 and event(named_day) == event(fixed_day)
assert event(nav("transit", "star", "Altair", "2026-11-10", *AP, "--tz", "PST")).day == 10

india = nav("transit", "sun", "2026-11-10", *AP, "--timezone", "Asia/Kolkata")
assert "2026-11-11 01:03:" in india and "UTC+0530" in india, india

# A clock change must be applied at the transit instant, not at local midnight.
for date, expected in (("2026-03-08", "PDT (UTC-0700)"), ("2026-11-01", "PST (UTC-0800)")):
    assert expected in nav("transit", "sun", date, *AP, "--tz", "America/Los_Angeles", "--local-date")
assert "does not exist" in nav("transit", "sun", "2011-12-30", *AP, "--tz", "Pacific/Apia", "--local-date", ok=False)
assert "unknown time zone" in nav("transit", "sun", "2026-11-10", *AP, "--tz", "Invalid/Zone", ok=False)
assert "invalid sight UTC date" in nav("transit", "sun", "2026-02-29", *AP, ok=False)
assert "ephemeris" in nav("transit", "sun", "1600-01-01", *AP, ok=False)
print("PASS transit: exact Sun/Moon crossings, no-transit date, star, fixed offsets, DST, date rollover and input errors")
