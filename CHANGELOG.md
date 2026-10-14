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
