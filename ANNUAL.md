# Annual practice almanac — 5.1 development preview

The **Annual book** tab builds a PDF for a chosen Gregorian year with independently
selectable Sun/Moon, planets, stars, Greenwich events, increments, altitude
correction components, a calculator reduction guide and compact Ageton tables.
The calculator guide and Ageton can be included together, independently, or omitted.

This is an unfinished practice edition, not a substitute for the official
Nautical Almanac. Actual NAO Concise lookup tables, complete combined Moon limb
corrections, Polaris corrections, eclipses, star charts and standard-time maps
are not implemented. The calculator guide is explicitly labelled as NAVALM's
own formulas. Use the official publication and authorized charts for navigation;
see [SAFETY.md](SAFETY.md).

## Build and use

```sh
make desktop                 # existing FLTK workbench, now with Annual book tab
make navalm-book             # separate command-line PDF builder, no FLTK required
./navalm-book --year 2027 --output NAVALM-2027-practice.pdf
./navalm-book --ageton-only --output Ageton-practice.pdf
./navalm-book --help
make check-book
```

The CMake targets are `navalm-desktop`, `navalm-book` and `annual-book-check`.
The original `navalm` CLI, terminal and `-d` web interfaces retain their existing
entry points. Annual generation calls the shared C astronomy engine; it does not
replace its calculations. The desktop reserves that engine and disables other
calculation controls while the export worker runs. Progress and cancellation are
available. A complete PDF is written to an adjacent temporary file and then
replaces the destination; calculation errors and cancellation preserve an existing
file. The output directory must already exist and be writable.

`--engine precise|classic` and `--ephemeris FILE` select the CLI builder's engine.
The precise engine must cover the entire year and the following January 1.
Years 1600–2200 are accepted, with Classic available outside the installed JPL
coverage. Standalone Ageton books do not need ephemeris data.

## Page design and conventions

The built-in PDF writer produces vector text and rules on 7 × 10 inch pages.
It requires no TeX installation, browser, PDF printer or downloaded fonts on the
user's machine. Standard PDF Times fonts provide tabular digits; prose wraps by
measured font advances, rather than a fixed character count. Text is checked
against printable margins. PDF viewers supply their standard font equivalents.

Three-day daily spreads follow the familiar division of Aries/planets/stars
and Sun/Moon/events. Columns expand when neighbouring sections are omitted.
Increments use three minute blocks per page (20 pages), and the dip, refraction
and parallax component tables share one page. A complete 2027 preview is
**295 pages**: 2 introductory, 244 daily, 21 increments/rates, 1 corrections,
1 calculator guide, 25 Ageton, and 1 section index. PDF bookmarks identify months
and reference sections. Leap years and the final partial daily group are handled.

* UTC whole-hour apparent geocentric coordinates, rounded to 0.1 arcminute.
  DUT1 is assumed zero; future Earth rotation and leap seconds are uncertain.
* Degrees SPACE decimal minutes, with N/S repeated on every declination.
  East longitude is positive; LHA = GHA + east longitude, modulo 360.
* Signed hourly v relative to 15 degrees/hour for Sun/planets and 14 degrees
  19 minutes/hour for Moon. Signed d is the change in north-positive declination.
* Planet v/d use 12–13 UTC. Planet SHA uses middle-date noon; Greenwich transit
  is interpolated from hourly GHA. Star SHA/declination use middle-date 00 UTC.
* Event times use Greenwich longitude, sea level, 34 arcminutes of refraction,
  and the body's HP/SD; twilight is at solar center altitudes −6/−12 degrees.
  Hourly ephemerides are interpolated in the crossing search. These are approximate
  Greenwich times, not a general longitude-adjustment algorithm. `--` means no
  crossing in that UTC day.
* The altitude page supplies component approximations. In particular, it does
  not implement the official Moon limb tables or all topocentric limb effects.

## Compact Ageton variant

The optional section occupies **25 pages**: instructions, a writable sign/branch
worksheet and 23 table pages. Four half-degree blocks per sheet give half-minute
spacing. Complementary angles supply the 45–90 degree range by swapping A/B.
Worked checks and the numerical-audit notes use the final sheet's otherwise
unused half. A standalone booklet adds the two introductory pages and index
(28 pages total).

The scale is **1,000,000**, ten times the historical scale:

```
A(x) = -1,000,000 log10(abs(sin x))
B(x) = -1,000,000 log10(abs(cos x))
```

Entries are rounded integers. Never mix these values with traditional
100,000-scale tables. The procedure uses a signed meridian angle Q that can
exceed 90 degrees, rather than the signed auxiliary latitude of some variants.
The printed procedure, worksheet and table scale must be used together.

The worksheet records hemispheres, signed inputs, both t branches, Q and gap
signs, inverse A/B selection, azimuth branches, and Toward/Away. Change method
at singular endpoints, near equatorial declination, R above 87 degrees, or Hc
outside 5–85 degrees. Negative/undefined logs and below-horizon cases are rejected.
Half-minute grid spacing is not a bound on the final altitude error.

`make check-book` compares reductions made with the printed rounded integers
against independent direct spherical-triangle calculations. Seed 2112027;
200,000 uniform random cases per domain, repeated for nearest and interpolated
lookups. The solar-range domain is latitude ±70, declination ±30, LHA 0–360;
the full-sky domain is latitude/declination ±89, LHA 0–360. After excluding cases
outside the stated method limits, sampled maximum Hc errors were **0.944′ nearest**
and **0.255′ interpolated**. Maximum Zn errors were **7.012′ nearest** and
**2.993′ interpolated**. These are sample results, not guaranteed bounds or a
manual usability assessment. Both methods accept approximately 45% of the full
random domains; most other cases are below the horizon or outside the Hc limits.

## Validation status and source conventions

The macOS native export worker and control restoration, core reductions,
and the existing desktop checks were exercised. Generator cancellation and atomic
file replacement passed automated checks. Whole-year and leap-year PDFs
were generated; representative page styles were rendered and visually checked,
and printable text bounds were checked across the full preview. The Windows
cross-build is a compilation check, not proof that this new feature runs on Windows.
No 5.1 release has been published.

The familiar three-day organization was studied from the [1981 USCG examination
reference](https://www.dco.uscg.mil/Portals/9/NMC/pdfs/examinations/10_1981_nautical_almanac.pdf)
and a [public mirror](https://lapware.org/files/forDownload/nmc-10-1981-nautical-almanac.pdf).
[USNO's current description](https://aa.usno.navy.mil/publications/na) identifies
the official publication's contents. Its [history](https://aa.usno.navy.mil/publications/na_history)
places introduction of NAO Concise tables in 1989, so the 1981 reference does not
establish the modern concise section. [Ageton's divided-triangle functions](https://www.siranah.de/html/sail040u.htm)
provided mathematical background. Tables, instructions and worksheet here are
independently generated; no scanned pages or publisher explanatory text are embedded.
