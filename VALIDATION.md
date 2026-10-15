# NAVALM 4.2 publication validation

Checks performed on 15 October 2026 for the browser plotting sheet and
saved-sight display fix.

- Reproduced the reported edit workflow in the actual browser: three
  saved sights became six displayed rows after updating one. Two list
  refreshes overlapped and appended the same rows. The underlying list
  still contained three records.
- Added a request generation guard to stop superseded list refreshes,
  and removed the redundant refresh when saving an edit. Repeated browser
  edits now leave three displayed rows and three exported records. An
  unselected sight remains unselected after editing.
- `make check-list` first failed on the old source with six rows instead
  of three, then passed with the fix. It covers overlapping refreshes,
  stale responses arriving last, empty-list updates, and failed requests.
  These are controlled JavaScript/DOM-double checks, separate from the
  live browser checks above.
- `make page`, the ncurses build, `make check-paper`, and
  `make check-plot` passed. The browser reports 4.2. Plotting checks compare
  signed LOP residuals with recorded precise-engine stationary and running
  fixes within 0.0002 NM, and cover two lines, parallel lines, small
  advances, escaped names, invalid inputs and bounded near-pole grids.
- Both stationary and running three-star plotting sheets were exercised
  in the actual browser with synthetic observations. The running case
  advances the earlier sights by 6.4 and 2.1 NM. The plot uses the same
  mean assumed latitude as the solver for its longitude scale.
- Astronomy engines, altitude corrections and fix solver are unchanged.
  Ephemeris files remain ignored. `v4.1a` is the source checkpoint
  made before the sight-list correction.

# NAVALM 4.1 publication validation

Checks performed on 14 October 2026 for the paper-fix almanac.

- `make page`, the ncurses build, `make nogui`, `make classic-only`, and
  `make check-paper`: passed. CLI version reports 4.1.
- `make check-paper` compares the supplied ICE screen for 1988-07-01
  00:00 UTC, DR 40 N / 75 W. Sun/Aries values agree at displayed precision,
  including Refr −10.6', SD 15.8', PA 0.1', and Sum 5.3'. NAVALM's time
  model gives ΔT 56.184 s (56.2 displayed), versus ICE's historical 56.1.
- Core checks cover Moon semidiameter augmentation and parallax, Vega,
  invalid star/date/coordinates, a valid zero DR, zenith/nadir geometry,
  and ephemeris coverage errors.
- Live HTTP checks covered every supported body, Vega, finite results,
  and invalid DR, date, star, and ephemeris-range requests.
- The actual browser interface was exercised with Setup DR changes,
  editable UTC date/time, Now, and Sun/Moon/Vega tables. The terminal
  interface compiled; its new interactive screen was not exercised.
- Astronomy engine code and the observed-sight reducer are unchanged.
  These checks validate the new summary against the supplied example,
  rather than providing new independent astrometric certification.
- The previously observed browser saved-sight row duplication after
  editing is outside this change; exported records remained correct.

# NAVALM 4.0p publication validation

Checks performed on 14 October 2026. 4.0p adds a second astronomy engine:
NOVAS C3.1 reading JPL DE440s. Unlike earlier releases, this round did
include independent astronomical cross-checks, described below.

## Build

- `make`, `make nogui`, `make classic-only`, `make ephem`: all passed with
  no compiler warnings under Apple clang 21 (arm64).
- NOVAS and libastro both export `nutation()` and `refract()`. The Makefile
  compiles the NOVAS objects with `-Dnutation=novas_nutation
  -Drefract=novas_refract`, so both vendored trees remain byte-identical to
  their upstream releases. A symbol-table comparison of `novas.a` against
  `libastro.a` confirmed those two were the only collisions.
- The complete NOVAS C3.1 distribution under `novas/` was byte-compared
  against the USNO release archive: all 26 files identical.

## Ephemeris repacking

`tools/mkde440` copies JPL's Chebyshev coefficients verbatim out of
de440s.bsp. Positions read back through `nav_eph440.c` were compared
against the `jplephem` Python package reading the original kernel, at six
dates spanning 1849 to 2150 for all nine retained bodies:

    worst position difference   2.4e-07 km   (0.24 mm)
    worst velocity difference   5.0e-13 km/s

That is double-precision rounding at Saturn's distance, so the repacked
file and the original kernel are the same numbers.

## Apparent places

NAVALM's precise engine was compared against Skyfield 1.55, which performs
its own light-time, aberration, deflection and precession-nutation
reduction from the same kernel. Eight dates from 1900 to 2149, seven
bodies, comparing apparent RA and declination of date at a common TT:

    worst angular separation    0.0005 arcsec

That is the print precision of the comparison harness, not a measured
disagreement.

USNO's own `checkout-stars` validation program, built from the vendored
sources, reproduces `novas/checkout-stars-usno.txt` digit for digit. (The
files differ only in line endings; the USNO reference is CRLF.)

## Engine cross-check

`tools/engine_diff.py` runs both engines over the same dates and bodies.
Eight dates from 1950 to 2140, in arcminutes:

    body      worst dGHA   worst dDec
    sun            0.010        0.000
    moon           0.010        0.010
    mercury        0.010        0.010
    venus          0.010        0.010
    mars           0.030        0.010
    jupiter        0.010        0.010
    saturn         0.010        0.000
    aries          0.000        0.000

All 58 navigation stars agreed to 0.01' in SHA and declination, except
Polaris at 0.61' of SHA. That is the usual ill-conditioning of SHA within
a degree of the pole, not a position disagreement: Polaris' declination
agrees to 0.01'.

The two engines therefore agree to about the resolution the tables print.
The precision engine's value is not that it moves the printed numbers; it
is that its error budget is milliarcseconds and traceable to JPL, rather
than the truncation error of an analytic series.

## Known limitations

- DE440s carries barycentres, not planet centres, for Mars, Jupiter and
  Saturn; the satellite ephemerides would be needed for centres. The
  offset reaches roughly 0.05 arcsecond for Saturn and 0.02 for Jupiter,
  some three orders of magnitude below the 0.1 arcminute the tables print.
- The star catalogue carries no parallax or radial velocity, so both are
  passed to NOVAS as zero. The largest resulting error is about 0.75
  arcsecond, for Rigil Kentaurus, the nearest navigation star; for most of
  the 58 it is well under 0.1 arcsecond. This is below the printed
  resolution but is the precision engine's weakest point, and is the
  obvious thing to improve next.
- Planetary magnitudes use the Mallama and Hilton (2018) phase functions,
  including Saturn's ring opening angle. Magnitudes are a photometric
  model, not an ephemeris quantity, and differ from printed almanac values
  by a few hundredths.
- DUT1 is assumed zero unless supplied, so UT1 is taken as UTC. This
  affects GHA by up to 0.9 second of time, about 0.2 arcminute, and is a
  property of the time argument rather than of either engine.
- The ephemeris covers JD 2396752.5 to 2506352.5, about 1849 to 2150.
  Outside that the precise engine refuses with a message naming the span
  and suggesting `--engine classic`; the classic engine is unbounded.

## Error paths

Exercised and confirmed to report rather than fail silently: missing
ephemeris file, explicit `--ephemeris` path that does not exist, date
outside the ephemeris for the single-body, star and daily-table commands,
unknown `--engine` name, and `--engine precise` on a `classic-only` build.
`-v` reports the active engine, and on a build or installation without an
ephemeris says so instead of erroring.

## What these checks are not

They establish that NAVALM's precise engine reproduces JPL's published
coefficients and agrees with two independent implementations of the
apparent-place reduction. They are not a safety certification, not a
guarantee the program is free of defects, and not a substitute for the
official printed almanac in safety-critical use. See SAFETY.md.

# NAVALM 3.3c publication validation

Checks performed on 23 September 2026 in the build sandbox.

- `make`, `make nogui`, `make page`: passed, no compiler warnings.
- Version 3.3c reported by the CLI, the terminal interface and
  /api/version; the served page carries the new edit controls.
- Web edit round-trip, exercised by running the page's own JavaScript
  under Node with a DOM stand-in: a star sight carrying non-default index
  error, on-arc flag, artificial horizon, height of eye in metres, upper
  limb, temperature, pressure, DR, course, speed, set and drift was
  written into the form with fillSight() and read back with formSight().
  Every field survived unchanged.
- Terminal interface edit: E on the LOPs screen opened LOP 1 titled
  "editing LOP 1" with the fields filled and the footer offering
  "S update this LOP". Switching the horizon from artificial to sea with a
  10 ft height of eye and pressing S reported "LOP 1 updated", and
  ~/.naval.save changed to horizon=sea he=10.00 with no new LOP added.
  The reduction then correctly refused the sight, since Hs 94 with a sea
  horizon puts Ha above 90 degrees.
- Sight and fix regression unchanged: artificial-horizon Sun sight
  Ho 47 33.8' with a 6.8 NM Toward intercept; three-star running fix
  N 34 01.2' W 117 18.8', RMS 0.03 NM.

# NAVALM 3.2c publication validation

Checks performed on 23 September 2026 in the build sandbox.

- `make`, `make nogui`, `make page`: passed, no compiler warnings.
- Version reported as 3.2c by the CLI, the terminal interface header and
  /api/version.
- RA formatting, 2026-09-19 21:38:05 UTC:
    CLI   Sun   RA  177 13 03.1
    CLI   Vega  RA  279 27 48.3
    CLI   Moon  RA  063 55 13.1   (2026-01-01 00:00 UTC)
    TUI   Sun   RA  180 42 19.3"  (screen clock time)
    web   /api/almanac returns ra=177.217523 and renders 177 13 03.1
  Cross-check: GAST 323.32643755 minus GHA 146.10891500 equals
  177.21752254, which is the RA printed, so the displayed RA and GHA stay
  consistent with each other.
- Aries prints no RA, as before.
- Sight and fix regression unchanged: artificial-horizon Sun sight
  Ho 47 33.8' with a 6.8 NM Toward intercept; three-star running fix
  N 34 01.2' W 117 18.8', RMS 0.03 NM.

# NAVALM 3.1c publication validation

Checks performed on 23 September 2026 in the build sandbox.

- `make`, `make nogui`, and `make page`: passed, no compiler warnings.
- CLI version: `navalm version 3.1c`; web `/api/version` and the terminal
  interface header both report 3.1c.
- Air Almanac page compared against the supplied USNO Air Almanac 2026 PDF
  for 2026 January 1 (page "(DAY 001) GREENWICH A. M."):
    Sun GHA      179 10.04  (printed 179 10.1)
    Sun Dec      S23 01.03  (printed S23 01.0)
    Aries GHA    100 39.73  (printed 100 39.7)
    Jupiter      347 32.28 / N21 58.75  (printed 347 32 / N21 59)
    Saturn       103 16.87 / S 3 35.78  (printed 103 17 / S 3 36)
    Moon         036 44.52 / N26 24.22  (printed 36 45 / N26 25)
  Planet selection matched the printed page: Jupiter and Saturn tabulated,
  Venus and Mars omitted as too close to the Sun, with the same note.
  Magnitudes differ slightly from the printed values (Jupiter -2.5 here,
  -2.7 printed), which is a magnitude-model difference, not a position one.
  Moon declination runs about half an arcminute below the printed page at
  some rows; Sun, Aries and the planets agree to the printed rounding.
- Sight and fix regression, unchanged from 3.0c:
    artificial-horizon Sun sight   Ho 47 33.8', intercept 6.8 NM Toward
    three-star running fix         N 34 01.2' W 117 18.8', RMS 0.03 NM
- Terminal interface: almanac screen now shows GHA 146 06.53' and
  Dec N 01 12.33' for the 2026-09-19 21:38:05 UTC Sun check.
- Web endpoints exercised: /api/version, /api/daily?style=naval,
  /api/daily?style=air, /daily/naval, /daily/air, /api/reduce, /api/fix.
- The page renders both daily tables in place; the only new tab comes from
  the explicit printable-page link.

# NAVALM 3.0c publication validation

Checks performed on 23 September 2026 against the supplied 3.0c archive,
with the documentation, notices, and two catalog selections updated.

- `make page`, `make stars`, and `make nogui`: passed.
- CLI version: `navalm version 3.0c`.
- Almanac smoke checks at 2026-09-23 12:00:00 UTC: Sun, Moon, Mercury,
  Venus, Mars, Jupiter, Saturn, and Aries returned finite output.
- All 58 named stars returned output; names are unique.
- Atria now reports southern declination near 69 degrees; Suhail near
  43.5 degrees south. Their catalog records are Alpha Trianguli Australis
  (HR 6217) and Lambda Velorum (HR 3634), respectively.
- All 58 selected catalog records, after removing the prepended
  navigation name, match records in the cited XEphem SKY2k65.edb.
- Plain text, HTML, and LaTeX daily tables were generated and checked for
  the safety notice and corrected star names. LaTeX was not rendered
  to PDF in this review.
- Artificial-horizon Sun sight reduction: passed using the README
  example inputs.
- Local HTTP server: home page contained the warning, complete safety
  terms, and acknowledgment; /api/version returned 3.0c.
- `--notice` reproduces the full SAFETY.md body.
- Regenerating navweb_page.h and nav_stars_x.c produces identical bytes.
- All 66 vendored libastro C/header files match the upstream Git hashes
  documented in PROVENANCE.md; original notices are retained.
- No legacy engine/data files or compiled objects are included in the
  publication tree.

The ncurses build was attempted but this environment has no ncurses.h;
the full-screen interface was not compiled or interactively tested here.
The CLI and HTTP checks used the no-ncurses build.

These are build, consistency, and smoke checks. They are not an
independent astronomical accuracy validation, a safety certification,
or a guarantee that the program is free of defects. Prior claims of
specific agreement with 2.8g were removed because this publication
review did not reproduce them.
# 3.3c publication checks (2026-09-23)

The uploaded 3.3c source was checked before publishing:

- Both the no-ncurses and full ncurses builds completed. The final ncurses
  build used local ncurses headers and static libraries, with no warnings.
- CLI Sun output displayed GHA/declination to hundredths of a minute and
  RA as degrees, minutes and seconds.
- HTTP almanac smoke checks covered Sun, Moon, Mercury, Venus, Mars,
  Jupiter, Saturn and Aries; each returned a numeric RA. Both standalone
  daily-page endpoints returned complete HTML.
- A jsdom check of the served page verified saved-sight replacement,
  cancel without changing storage, unchanged Setup, and consistent inline
  AIR/NAVAL tables with explicit links to printable pages. This was DOM
  simulation, not a visual browser test.
- The AIR table contained two half-days and 144 ten-minute rows. A
  September 23 equator-crossing regression confirmed that the Sun's N/S
  change is printed even when its rounded degrees and minutes do not change.
- A terminal session checked the almanac display and edited/saved an
  existing LOP, retaining its ID and fields without adding another record.

The publication review also added the missing `air_daily.inc` build
dependency. These checks do not establish astronomical accuracy or safety.
Earlier validation notes below describe their respective versions.
