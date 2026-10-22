# NAVALM 5.1 development preview (unreleased)

- Refined the rear v/d correction tables from 0.5 to 0.1 arcminute/hour rate
  steps, retaining the 0.0–30.5 range in five readable pages. Printed correction
  magnitudes use explicit half-up rounding to 0.1 arcminute.
- Extended optional row shading to increments, rate/altitude corrections and
  compact Ageton tables, using the same checkbox and gray level.
- Moved Annual book to the final desktop tab and increased optional hourly-row
  shading slightly to 8% gray.
- Added optional alternating light-gray hourly-row shading to the annual book,
  selectable by checkbox or `--shade-rows`, for both PDF and LaTeX exports.
- Added a selectable annual PDF practice-book builder in a new FLTK Annual book
  tab and separate `navalm-book` command, preserving existing CLI/web entry points.
- Added optional standalone LaTeX source export with Latin Modern fonts and the
  same compact page geometry. Compile separately with TeX; direct PDF export
  continues to need no TeX installation.
- Added hourly three-day spreads, navigational stars, Greenwich events,
  increments/rates, correction components and an optional calculator guide.
- Added an independently generated compact Ageton variant: half-minute rows,
  six-place log scale, sign/branch worksheet and worked checks in 25 pages.
- Put refraction/dip/parallax corrections at the front and minute/second
  Increments and Corrections at the back with a worked GHA example. Separate
  day and hour columns remove slash-prefixed hours.
- Typeset aligned numeric columns with larger tabular figures and measured prose
  wrapping. Three increments blocks per page and a combined corrections page
  reduce wasted space. Added month/section bookmarks and an index.
- Native generation reserves the shared engine, shows progress/cancellation and
  installs the completed PDF atomically. It requires no external TeX installation.

This is not a complete standard Nautical Almanac. Actual NAO Concise tables,
complete combined Moon limb corrections and other official-book sections remain
unfinished. See ANNUAL.md for conventions, numerical audit and validation limits.

# Changes in NAVALM 5.0

Released 21 October 2026.

- Added a native FLTK workbench with almanac and hourly tables, worked sight
  reductions, saved-sight editing, stationary/running fixes, and engine setup.
  The CLI, ncurses terminal interface and `-d` browser interface remain available
  and share the existing C astronomy and sight-reduction engines.
- Matched the browser's universal plotting sheet in the native view: assumed
  positions, intercept arrows, LOPs, running advances, grid, compass rose and
  enlarged cocked hat. Added detachable/reattachable plotting, anchored zoom,
  pan, Fit, layer visibility and opacity controls.
- Added transparent SVG and geographic GeoJSON vector exports for chart overlays.
- Added an offline Natural Earth land/coastline underlay, a cancellable world-map
  downloader, WGS84 land-shapefile loading and OpenCPN coastline import. This
  provides geographic context; it does not display chart depths, hazards or ENC
  symbols, and it is not a complete OpenCPN chart catalogue/downloader.
- Native DR and Hs fields display degrees SPACE decimal minutes, with decimal
  degrees also accepted. New DR fields remain blank without saved defaults;
  explicitly entered latitude 0, longitude 0 is still valid.
- Invalid stored inputs, below-horizon reductions and parallel/no-solution LOPs
  return recoverable errors. Correcting the inputs allows another calculation.
- Fixed a Windows coastline-rendering overflow: broad map views could exceed
  FLTK's 20-contour buffer. The replacement fill preserves islands and holes
  without that buffer. Added local Windows diagnostic logging.
- Added a portable Windows 10/11 x64 executable with FLTK/C++ support linked
  statically and the JPL DE440s ephemeris and world map embedded. First launch
  shows progress while extracting bundled data; no download is required.
  Windows system DLLs are still used. Keep `.local/` beside the executable to
  preserve saved sights and settings when updating or moving the program.

Validation: shared core, input, stationary/running fix, map, web/native vector
parity, CLI and live web API checks passed on macOS. Native callbacks, map
installation/cancellation, OpenCPN import and plotting were exercised on macOS;
map/input regressions also passed AddressSanitizer and UndefinedBehaviorSanitizer.
The Windows x64 PE, system-DLL imports and exact embedded data were inspected.
The user subsequently retested the updated Windows release and reported that it
seemed fine. That user runtime evidence supplements the cross-build checks; the
Windows download remains unsigned.

# Changes in NAVALM 4.2

- Added a universal plotting sheet to browser fixes, showing each sight's
  assumed position, intercept, azimuth, perpendicular LOP, running-fix
  advance, and a magnified cocked hat for three nonparallel lines.
- Fixed duplicate displayed sight rows after editing: superseded list
  requests can no longer append their rows to a newer refresh. Saving an
  edit starts only one list refresh. Stored records were not duplicated.
- Plot geometry uses the fix solver's longitude scale, adaptive grid
  intervals near the poles, and the inset's actual magnification.
- Added `make check-plot` and `make check-list` regression checks.
- `v4.1a` preserves the plotting-sheet source before the display fix.

# Changes in NAVALM 4.1

- ICE F7-style paper-fix almanac in the browser and terminal: GHA, Dec,
  Hc, Zn, refraction, semidiameter, parallax in altitude, and correction sum.
- Uses the exact DR from Setup and current UTC time, with editable date
  and time and a Now control. Shows the selected body, a supplementary
  Sun when Hc is at least −12°, and GHA Aries.
- Standard-atmosphere correction estimates follow ICE's lower-limb
  conventions for Sun/Moon. Dip and index error remain separate.
- Existing detailed almanac data remains available; P toggles the
  terminal view. Sight-reduction calculations and astronomy engines are
  unchanged.
- Added `make check-paper` with the supplied 1988 ICE Sun/Aries example,
  Moon and star checks, horizon limits, and invalid inputs. The example
  matches at displayed precision except ΔT: 56.2 s versus ICE's 56.1 s.

# Changes in NAVALM 4.0p

The precision branch. NAVALM now carries two astronomy engines and lets
you choose between them at run time.

- **New precise engine:** NOVAS C3.1, from the U.S. Naval Observatory,
  reading JPL's DE440s planetary and lunar ephemeris. NOVAS supplies the
  IAU 2000A nutation, IAU 2006 precession, light-time iteration,
  aberration and gravitational deflection; DE440s supplies the positions
  as the Chebyshev polynomials JPL published.
- **The XEphem engine is still here** and still selectable, as
  `--engine classic`. It needs no data file and has no date limit, so it
  remains the fallback outside 1849-2150 and on installations that cannot
  carry 30 MB of coefficients.
- `--engine precise | classic` and `--ephemeris PATH` work with any
  command. `-v` reports the active engine, the ephemeris file it opened
  and the dates it covers.
- **New `tools/mkde440`:** repacks JPL's SPK kernel into the flat file
  NAVALM reads, copying the coefficients verbatim. `make ephem-fetch`
  downloads de440s.bsp; `make ephem` repacks it. Neither file is in the
  repository.
- **New `make classic-only`:** builds without NOVAS or the ephemeris at
  all, for targets that cannot carry the data file.
- **New `tools/engine_diff.py`:** runs both engines over the same dates
  and reports the differences in arcminutes.
- Missing or out-of-range ephemeris data is now reported with a message
  naming the span and the way out, instead of exiting silently.
- The two engines agree to about 0.01 arcminute, so printed tables are
  essentially unchanged. The precise engine was checked against Skyfield
  (0.0005 arcsecond worst case, 1900-2149) and its repacked coefficients
  against jplephem (0.24 mm). USNO's own checkout program, built from the
  vendored NOVAS sources, reproduces the USNO reference output digit for
  digit. See VALIDATION.md.
- Licensing is unchanged: NAVALM stays MIT. NOVAS is public-domain USNO
  work and DE440s is public-domain JPL data; neither imposes conditions.
  Both are credited in THIRD_PARTY_NOTICES.md.

# Changes in NAVALM 3.3c

- Publication review: AIR declination now repeats the hemisphere when it
  changes across the equator, and changes to `air_daily.inc` trigger a rebuild.
- Every stored sight now has an edit button. In the web interface the
  Sights list carries "edit" beside "delete"; it opens the sight on the
  Sight tab with every field filled in, including the ones that normally
  come from Setup, so changing one thing (sea to artificial horizon, say)
  no longer means retyping the shot. Saving updates that sight in place
  rather than adding another, and Cancel edit backs out.
- The Sight tab gained a "This sight's settings" block holding index error
  and arc, horizon, height of eye and units, temperature, pressure, DR,
  and course/speed/set/drift. It starts from the Setup defaults and
  belongs to that sight alone; editing it never changes Setup. It is
  collapsed for a new sight and open while editing.
- The terminal interface gained the same thing: E on the LOPs screen opens
  that LOP in the sight screen, titled "editing LOP n", where S updates it
  in place instead of saving a new one.
- The active-DR line on the Sight tab now shows the sight's own DR when it
  has one, and says whether it came from the sight or from Setup.

# Changes in NAVALM 3.2c

- Right ascension is now shown as degrees, minutes and seconds
  (DDD MM SS.s) instead of decimal degrees, in the CLI one-body and star
  lookups, in the terminal interface almanac screen, and in the web
  almanac lookup.
- The terminal interface and the web almanac lookup now show RA at all;
  previously only the CLI printed it. /api/almanac gained an "ra" field
  in decimal degrees for anything driving the server directly.

# Changes in NAVALM 3.1c

- The web AIR table is now laid out like the printed Air Almanac daily
  pages: separate GREENWICH A.M. and P.M. pages at ten-minute steps, Sun
  and Aries GHA to 0.1', planets and Moon to 1', declination degrees and
  the hemisphere repeated on the hour and whenever they change, an
  unchanged value shown as a dot, and a page heading of the form
  "(DAY 001) GREENWICH A. M. 2026 JANUARY 1 (THURSDAY)".
- Planet columns follow the printed rule: a planet closer than 20 degrees
  of elongation is omitted and the "TOO CLOSE TO THE SUN FOR OBSERVATION"
  note appears. Where all four navigational planets qualify, all four are
  shown, with a note that the printed pages carry three.
- Column headings carry the planet magnitude, as the printed pages do.
- Both daily tables now open on the Almanac tab. Neither button opens a
  new tab by itself; each rendered table ends with an explicit link to a
  printable standalone page, and that link is the only thing that opens a
  new tab.
- New endpoint GET /daily/air?y=&mo=&d= returns the printable Air Almanac
  page; /daily/naval is unchanged.
- Single-body lookups return to degrees and hundredths of a minute
  (DDD MM.mm) in the terminal interface, in the CLI one-body output, and
  in the web Almanac lookup. Printed-style daily tables stay at tenths,
  matching the almanac pages themselves.
- NavAlmanac gained elongation and magnitude fields to support the planet
  selection above.

# Changes in NAVALM 3.0c

- Replace the NAV89/NAV48/NAV50-derived astronomy engine and its lunar,
  planetary, and star data with XEphem's libastro and SKY2000-derived
  navigation-star records.
- Use the MIT license for current NAVALM contributions; preserve
  XEphem's MIT license and original source credits.
- Add an explicit informational-use warning, no-warranty disclaimer,
  acceptance language, and assumption of risk and liability, subject to
  applicable law.
- Update both READMEs, source licensing references, browser and terminal
  notices, and the provenance and third-party documentation.
- Correct Atria to Alpha Trianguli Australis and Suhail to Lambda Velorum
  after finding ambiguous common-name matches in the uploaded catalog.
- Preserve the 3.0c version designation of the supplied source.
- Remove compiled objects from the published source tree.

Earlier versions and the v2.8g release retain their historical
provenance and applicable permissions; this license transition is not
retroactive. See PROVENANCE.md. Validation is recorded in VALIDATION.md.
