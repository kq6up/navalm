# Annual practice almanac — 5.3

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
The browser interface also has an **Annual book** tab at the end of its tabs:

```sh
make                         # builds navalm and the companion navalm-book
./navalm -d
```

Choose a year, sections, row shading and PDF or LaTeX source, then select
**Generate book**. Each export is saved in a new private folder under the home
directory of the user running the server, for example
`~/NAVALM-2026-Ab12Cd/NAVALM-2026.pdf`. The page shows progress, the full saved
path, and a download link. Existing books are preserved. The link serves the
latest completed export in the current server session; older files remain on
disk. Reloading the page reconnects to the current job. With a remote browser,
the saved folder is on the server; **Download** also copies the file to the
browser's normal download location.

Keep `navalm-book` beside `navalm` when moving the web installation. The server
executes that companion directly, with no shell, in a separate process so sight
calculations remain responsive. It uses the server's `--engine` and
`--ephemeris` settings. PDF generation needs no TeX installation; LaTeX output
is editable source to compile separately. Only one book can run at a time.

`make check-web-book` exercises real HTTP generation and downloads with an
isolated home directory, including error recovery and concurrent almanac use.
It requires Python 3 and the usual `ephem/de440s.nav` file.

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

## Optional LaTeX export

The desktop also offers **Export LaTeX source…**. It uses the selected year and
sections, the same calculated data and page geometry, and Latin Modern fonts.
The result is a standalone editable `.tex` file; NAVALM does not run a compiler.
The direct PDF option remains self-contained.

```sh
./navalm-book --year 2027 --format latex --output main.tex
pdflatex -interaction=nonstopmode -halt-on-error main.tex
pdflatex -interaction=nonstopmode -halt-on-error main.tex
```

Compile twice for bookmarks. A TeX distribution must provide `fontenc`, `lmodern`,
`xcolor`, `geometry` and `hyperref` (normally included in TeX Live/MiKTeX). The source uses
positioned text and rules to preserve the compact tables; it is not a separate
flowing-table redesign. Font sizes, labels and the preamble are editable, but
regenerating overwrites source edits. Latin Modern advances are measured for
wrapping and column alignment; no font programs are bundled with NAVALM.

The revised 2027 LaTeX comparison was compiled with pdfLaTeX.
The exported PDF has embedded fonts, 335 pages and 17 bookmarks.
Representative page styles were rendered for visual inspection. Both export
formats are covered by generator and native-worker checks.

## Page design and conventions

The built-in PDF writer produces vector text and rules on 7 × 10 inch pages.
It requires no TeX installation, browser, PDF printer or downloaded fonts on the
user's machine. Standard PDF Times fonts provide tabular digits; prose wraps by
measured font advances, rather than a fixed character count. Text is checked
against printable margins. PDF viewers supply their standard font equivalents.

Three-day daily spreads follow the familiar division of Aries/planets/stars
and Sun/Moon/events, with separate day and UTC-hour columns. Columns expand
when neighbouring sections are omitted. The **Alternate row shading** checkbox
adds a subtle 8% gray background to every other row in Aries/planet, Sun/Moon,
increments, hourly-rate corrections, dip, refraction, parallax and compact Ageton
tables, in either export format. Hourly shading starts afresh each day; reference
shading starts afresh in each table block. Bands stay within each table, leaving
instructions and worksheets clear. Leave it unchecked for plain white pages; the CLI equivalent
is `--shade-rows`. Refraction, dip and parallax are at the
front; the minute/second **Increments and Corrections** tables are at the back,
with a worked GHA example.
One combined page per elapsed minute puts GHA increments (all 60 seconds),
v/d corrections and a small seconds-adjustment table together. The 60-page
section retains rate magnitudes 0.0–30.5 arcminutes/hour in **0.1 steps**.
Read the whole-minute correction beside the rate, add the seconds component
(interpolating the small table in rate and seconds), round the sum to 0.1
arcminute, then apply the original v/d sign. For declination, omit the GHA
increment. Correction components print to **0.1 arcminute** with half-up rounding.
Rounding the components separately can affect the last digit; before final
rounding, the component rounding and seconds interpolation can together differ
from the unrounded calculation by up to 0.1 arcminute.
The seconds table includes 60s as an interpolation endpoint, not another minute.
The unrounded correction is rate * (60 * minute + seconds) / 3600. GHA increments
also use explicit half-up rounding so exact half-step entries are consistent.
The dip, refraction and parallax component tables share one page.
A complete 2027 preview is
**335 pages**: 2 introductory, 244 daily, 60 increments/corrections, 1 corrections,
1 calculator guide, 26 Ageton, and 1 section index. PDF bookmarks identify months
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

The optional section occupies **26 pages**: two plain-language guide pages,
a one-page pencil reduction form and 23 table pages. Seven writing boxes are
provided for every logarithm entry. Arithmetic operands and results are stacked
in aligned pencil columns, with companion B lookups beside R and Hc.
The guide defines the table angle as less than 90 degrees, then separately gives
the t <90 copy rule and t >90 exception (180 minus the table angle) for K.
Four half-degree blocks per sheet give
half-minute spacing. Complementary angles supply the 45–90 degree range by
reading the opposite A/B letter when entering from the c side. The guide gives
both forward and inverse examples. A standalone booklet adds two introductory
pages and an index (29 pages total).

The scale is **1,000,000**, ten times the historical scale:

```
A(x) = -1,000,000 log10(abs(sin x))
B(x) = -1,000,000 log10(abs(cos x))
```

Entries are rounded integers, some seven digits long. Never mix them with
traditional 100,000-scale tables. The printed procedure now uses traditional
**t, K, K~L, Hc and Z**. K always has declination's N/S name and is obtuse when
t exceeds 90 degrees. K~L is nonnegative: subtract magnitudes for the same N/S
name, add for contrary names. This is the traditional auxiliary-angle convention;
it is not the signed `LatAP - LatQ` quantity on some modern worksheets.

The form records the table side, K's angle and name, the K~L combination,
above-horizon test, acute/obtuse Z choice and elevated-pole-to-true-bearing
conversion. Z is measured from north at N latitude, south at S latitude;
Zn is always clockwise from true north. At the equator use the N conversion.
The engine's internal signed auxiliary angle is unchanged. Its previous prose
label "gap" and the ambiguous "SWAP" instruction have been removed from the
printed guide. Do not mix the previous true-north Z rule with this form.

Use another method for singular t, near-zero declination, R above 87 degrees,
Hc outside 5–85, or required INF/negative logs. The form separately flags a
reduced lookup angle below 3 or above 87 degrees as CAUTION: cross-check using
another method. These are conservative review prompts, not inherited accuracy
bounds from a different Ageton edition. Half-minute grid spacing is not
a bound on final altitude error. Prepared inputs are assumed: the compact form
covers the Ageton reduction, not time, almanac or sextant corrections.

`make check-book` compares reductions made with the printed rounded integers
against independent direct spherical-triangle calculations. The printed K/K~L
procedure is separately executed for every accepted test case, including both
latitude hemispheres, E/W body directions and acute/obtuse t. Seed 2112027;
200,000 uniform random cases per domain, repeated for nearest and interpolated
lookups. The solar-range domain is latitude ±70, declination ±30, LHA 0–360;
the full-sky domain is latitude/declination ±89, LHA 0–360. After excluding cases
outside the stated method limits, sampled maximum Hc errors were **0.944′ nearest**
and **0.255′ interpolated**. Maximum Zn errors were **7.012′ nearest** and
**2.993′ interpolated**. These are sample results, not guaranteed bounds or a
manual usability assessment. A further 6,336 targeted boundary cases exercise
near-zero and near-right-angle inputs: 1,876 proceed on paper and 4,460 stop.
Sampled maximum errors in this boundary set are 0.467′ Hc and 2.246′ Zn.
The audit distinguishes finite printed lookups from software limiting values;
a required printed INF remains a stop. Both methods accept approximately 45%
of the full random domains; most other cases are below the horizon or outside the Hc limits.

## Validation status and source conventions

The macOS native export worker and control restoration, core reductions,
and the existing desktop checks were exercised. Generator cancellation and atomic
file replacement passed automated checks. Whole-year and leap-year PDFs
were generated; representative page styles were rendered and visually checked,
and printable text bounds were checked across the full preview. The Windows
cross-build is a compilation check, not proof that this new feature runs on Windows.
The 5.2 release includes this practice-book generator; it remains incomplete
compared with the official Nautical Almanac.

The familiar three-day organization was studied from the [1981 USCG examination
reference](https://www.dco.uscg.mil/Portals/9/NMC/pdfs/examinations/10_1981_nautical_almanac.pdf)
and a [public mirror](https://lapware.org/files/forDownload/nmc-10-1981-nautical-almanac.pdf).
[USNO's current description](https://aa.usno.navy.mil/publications/na) identifies
the official publication's contents. Its [history](https://aa.usno.navy.mil/publications/na_history)
places introduction of NAO Concise tables in 1989, so the 1981 reference does not
establish the modern concise section. [Ageton's divided-triangle functions](https://www.siranah.de/html/sail040u.htm)
provided mathematical background. Tables, instructions and worksheet here are
independently generated; no scanned pages or publisher explanatory text are embedded.
