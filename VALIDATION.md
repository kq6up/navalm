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
