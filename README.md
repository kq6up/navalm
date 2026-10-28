# NAVALM

**A nautical almanac and celestial sight-reduction program with terminal, web, and native desktop interfaces.**

NAVALM brings almanac calculations, worked sextant-sight reductions, and position fixes together in one C program. Use it from the command line, in a full-screen terminal, or through its built-in browser interface.

NAVALM 5.0 adds an optional **native FLTK workbench**: `make desktop`
builds `./navalm-fltk`, and `make desktop-app` packages `NAVALM.app` on macOS.
It includes sight reductions, saved observations, fixes, and a geographic
vector plotting overlay with transparent SVG export. See [DESKTOP.md](DESKTOP.md)
for building, state paths, validation, and the offline coastline underlay.
See [MAPS.md](MAPS.md) for downloads/OpenCPN coastlines and [WINDOWS.md](WINDOWS.md)
for the portable Windows x64 desktop build.
The existing CLI, `--gui`, and `-d` browser interface remain available.

Current development version: **5.4**; latest published release: **5.3**.

Version 5.2 includes a custom annual practice-book
preview with selectable sections, compact Ageton tables and optional LaTeX source
export alongside the self-contained PDF. Build `make navalm-book`
or use **Annual book** in the desktop or `./navalm -d` browser interface.
The browser saves books under your home folder and offers a download link.
See [ANNUAL.md](ANNUAL.md) for scope,
conventions and the official-book sections that are still missing.

**Informational and educational use only. Do not rely on NAVALM where an error could cause death, injury, or property damage. By using it, you accept the [safety and assumption-of-risk terms](SAFETY.md), to the extent permitted by law.**

NAVALM 5.0 adds the FLTK workbench, a detachable plotting sheet with zoom and transparent vector exports, offline coastlines, and a portable Windows build. [Download the Windows x64 ZIP](https://github.com/kq6up/navalm/releases/download/v5.3/NAVALM-Windows-x64.zip) or see the [release notes](CHANGELOG.md). It includes the ICE F7-style almanac for working a fix on paper. It retains the two astronomy engines introduced in 4.0p: XEphem libastro and NOVAS C3.1 with the JPL DE440s ephemeris; either can be selected at run time. The older v2.8g release still contains the legacy engine and is not covered by this license transition.

Version 5.2 also adds a **Planetarium** tab to both interfaces: a sky chart
of 58 navigation stars, three/four-star sight planning, altitude/brightness
filters and morning/evening planning times. **Version 5.3** adds the Sun, Moon,
Mercury, Venus, Mars, Jupiter and Saturn, with distinct markers and selection
to start the matching sight. **Version 5.4** adds offline constellation figures,
separate constellation-name/supporting-star toggles, brighter native planet
labels and a detachable full-screen sky window. Browser users can expand the
chart or enter full screen. See [PLANETARIUM.md](PLANETARIUM.md).

Check **Reduce at DR (no whole-degree LHA)** on the Sight tab to keep the full
DR latitude and longitude. Leave it unchecked for the usual whole-degree
HO 229 assumed position. The choice is saved with each sight. The CLI accepts
`--at-dr`; `--ho229` restores the default method when editing a saved sight.

## Features

- **Almanac:** Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Aries, and navigation stars.
- **Paper-fix summary:** GHA, Dec, Hc, Zn, and altitude-correction estimates at the Setup DR, with editable UTC date and time.
- **Daily tables:** plain text, HTML, and LaTeX output, plus NAVAL hourly and AIR ten-minute views in the browser.
- **Sight reduction:** sea and artificial horizons, altitude corrections, worked HO 229-style steps, or direct spherical reduction at the entered DR.
- **Position fixes:** saved lines of position, least-squares fixes, and running fixes with course/speed and set/drift.
- **Navigation-friendly input:** degrees and decimal minutes, flexible DR coordinates, and partial star-name matching.

## Astronomy engines

NAVALM carries two engines and will use either on request.

| | `--engine precise` (default) | `--engine classic` |
|---|---|---|
| Source | NOVAS C3.1 with JPL DE440s | XEphem libastro |
| Theory | JPL Chebyshev coefficients, IAU 2000A/2006 reduction | VSOP87 and the Moshier lunar theory |
| Accuracy | milliarcseconds | arcseconds |
| Dates | 1849 to 2150 | unbounded |
| Needs a data file | yes, about 30 MB | no |

In practice the two agree to about 0.01 arcminute, which is the resolution
the tables print, so the printed numbers rarely change. What changes is the
error budget: the precise engine's is milliarcseconds and traceable to
JPL's published ephemeris, rather than the truncation error of an analytic
series. See [VALIDATION.md](VALIDATION.md) for the measurements.

The classic engine remains useful: it needs no data file, and it covers
dates outside the DE440s span.

### Choosing an engine

The engine is chosen with a command-line flag, and the flag is the only
place it lives: there is no engine setting in `~/.navalmrc`, on the
terminal Setup screen, or in the browser interface. `--engine` works with
every command and every interface in the `navalm` executable. The separate
FLTK workbench has its own runtime engine selector.

```sh
./navalm sun 2026 9 9 12 0 0                    # precise, the default
./navalm --engine classic sun 2026 9 9 12 0 0   # same sight, XEphem engine
./navalm --engine classic -t 2026-09-09         # daily table
./navalm --engine classic --gui                 # terminal interface
./navalm --engine classic -d 8080               # browser interface
./navalm -v                                     # which engine is active now
```

`--engine` and `--ephemeris` may appear anywhere on the command line. They
are lifted out before the command is read, so they never disturb the
positional arguments a command expects.

To make the classic engine your default, alias it:

```sh
alias navalm='navalm --engine classic'
```

### Setting up the precise engine

The ephemeris is a build input, not part of the repository. Fetch and
repack it once:

```sh
make ephem-fetch     # downloads ephem/de440s.bsp from JPL, about 31 MB
make ephem           # repacks it into ephem/de440s.nav, about 30 MB
```

`make ephem` runs `tools/mkde440`, which copies the Chebyshev coefficients
NAVALM needs out of JPL's SPK kernel into a flat file the program reads
directly. The coefficients are copied verbatim; nothing is refitted,
resampled or rounded.

Without `ephem/de440s.nav` the precise engine reports what is missing and
how to supply it. Use `--engine classic` to run without it, or build
`make classic-only` to leave the precision engine out altogether.

NAVALM looks for the ephemeris in `$NAVALM_EPHEMERIS`, then `de440s.nav`
in the current directory, `ephem/`, `~/.navalm/` and
`/usr/local/share/navalm/`. `--ephemeris PATH` overrides all of that.

`./navalm -v` reports which engine is active and, for the precise engine,
which file it opened and the dates it covers.

## Build

### Requirements

- A C99 compiler, such as GCC or Clang, and the C math library.
- `make`, a POSIX shell (`sh`), `sed`, and an archiver (`ar`).
- `awk` if regenerating the navigation-star table with `make stars`.
- ncurses headers and libraries for the full-screen terminal interface.
- Git if cloning the repository; a downloaded source archive also works.

### Download and compile

```sh
git clone https://github.com/kq6up/navalm.git
cd navalm
make page
make
make ephem-fetch     # once: downloads the JPL ephemeris
make ephem           # once: repacks it for NAVALM
./navalm -v
```

If you downloaded a source archive, extract it and enter its source directory, then run the same build commands starting with `make page`.

**Run `make page` before `make`.** The first command runs `mkpage.sh` to convert `page.html` into `navweb_page.h`, embedding the browser interface in the program. The second compiles the C sources and creates the `navalm` executable. Plain `make` does not regenerate the page header automatically.

The normal build includes all three interfaces. Run `./navalm -v` to check the version; this source reports `navalm version 5.4`. The executable is built in the source directory and can be run there without an installation step.

### Build without ncurses

For a system without ncurses development files, build the command-line and browser interfaces with:

```sh
make page
make nogui
./navalm -v
```

This build supports the almanac, sight reductions, saved sights, fixes, and web server. The full-screen `--gui` interface requires the normal ncurses build.

### Rebuild after changes

After updating the source or editing the browser page:

```sh
make page
make
```

For a clean rebuild, run `make clean` first. Use `make nogui` instead of the final `make` if you are building without ncurses.

If compilation reports `ncurses.h: No such file or directory` or cannot link `-lncurses`, install the ncurses development files for your system or use the build without ncurses above.

## Daily tables in the web interface

Both tables open on the Almanac tab itself. The NAVAL button gives the
whole-hour daily table; the AIR button gives Air Almanac style daily pages,
A.M. and P.M., at ten-minute steps, with the planet columns the printed
pages would carry. Under each table is a link to a printable standalone
page, and that link is the only thing that opens a new tab:

    GET /daily/naval?y=2026&mo=9&d=23
    GET /daily/air?y=2026&mo=9&d=23

Single-body lookups show degrees and hundredths of a minute; daily tables
stay at tenths, as the printed almanacs do. Right ascension is shown as
degrees, minutes and seconds (DDD MM SS.s).

## Choose an interface

| Interface | Command | Use |
| --- | --- | --- |
| Command line | `./navalm sun 2026 9 23 12 0 0` | Individual calculations, scripts, and text output |
| Full-screen terminal | `./navalm --gui` | Menu-driven almanac, sights, fixes, and setup |
| Browser | `./navalm -d` | Open `http://127.0.0.1:8080/` in a browser |

The terminal interface needs a terminal of at least 72 columns by 20 rows. The browser interface is served by NAVALM itself; no separate web server or application framework is needed. Press Ctrl+C in the launching terminal to stop the web server.

## Quick examples

Dates and times in these examples are **UTC**.

### Almanac lookups and daily tables

```sh
# Sun at a specified date and time
./navalm sun 2026 9 23 12 0 0

# A navigation star
./navalm star Sirius 2026 9 23 12 0 0

# List available stars
./navalm list-stars

# Daily almanac table
./navalm -t 2026-09-23

# Export the daily table as HTML or LaTeX
./navalm -th 2026-09-23 > almanac.html
./navalm -tx 2026-09-23 > almanac.tex
```

To turn the LaTeX output into a PDF, run `pdflatex almanac.tex` with a LaTeX installation providing `geometry`, `times`, and `graphicx`. LaTeX is optional and is not required to build or run NAVALM.

### Reduce a sextant sight

This example reduces an artificial-horizon Sun sight and prints the worked corrections and reduction:

```sh
./navalm sight sun 2026 9 19 21 38 05 --hs 94 37.2 --ah \
  --ie 0 --temp 30 --press 1013 --dr 34 2.5 N 117 21.9 W
```

Here, `--hs` supplies the sextant reading in degrees and minutes, `--ah` selects an artificial horizon, and `--dr` supplies the dead-reckoning position. Temperature is in degrees Celsius and pressure is in millibars. Add `--save` to store the sight as a line of position, or `--brief` for a compact result.

### Almanac for a paper fix

Set your DR latitude and longitude in **Setup**, then open **Almanac**.
The display starts at the current UTC date and time; edit either field for
an observation, or choose **Now** to return to the current time. The DR is
used directly, without rounding it to an assumed position.

The ICE F7-style summary puts GHA, declination, computed altitude (Hc),
azimuth (Zn), refraction, semidiameter, parallax in altitude, and their
correction sum together. It shows the selected body, a supplementary Sun
when its Hc is at least −12°, and GHA Aries. **Additional almanac data**
retains the detailed body information. The terminal Almanac screen uses
the same summary; press **P** to switch between summary and details.

These are predicted corrections at the DR, using a standard atmosphere
and the lower limb for the Sun and Moon. Sum means Refr + SD + PA, in
arcminutes. Apply dip and index error separately; the observed-altitude
corrections in the sight reducer remain separate. Corrections are blank
when Hc is −0.5° or lower, and azimuth is blank at the zenith or nadir.

The correction conventions follow the recovered ICE 0.51 F7 navigation
display, including lunar semidiameter augmentation and the solar limb
increment. NAVALM retains its own selected astronomy engine and time
model. For the example 1988-07-01 00:00 UTC at 40° N, 75° W, the precise
engine matches the supplied ICE Sun/Aries table at its displayed
precision; ΔT reads 56.2 seconds rather than ICE's historical 56.1.
NAVALM currently assumes DUT1 = 0.

After building the precise engine and ephemeris, run `make check-paper`
for the screenshot comparison and horizon, Moon, star, and input checks.

### Browser plotting sheet

Select two or more saved sights in **Sights**, then choose **Fix from
selected**. The fix includes a universal plotting sheet: each sight's own
HO 229 assumed position, an intercept arrow toward or away from the body,
and its perpendicular line of position. Dotted segments show motion from
course/speed and set/drift to the common fix time. Three nonparallel lines
also get a cocked-hat inset, labeled with its actual magnification.

The chart uses the fix solver's local plane and mean assumed latitude for
longitude scaling. Grid intervals adapt to the spread of the sights and
latitude. It is a regional plotting aid; it does not change the sight
reduction or fix calculation. Exact poles cannot use this projection.

Run `make check-plot` with Node.js installed to check plot geometry against
recorded precise-engine stationary and running fixes. The fixture sights
in `tests/fixtures/plotting.json` are synthetic test observations.

### Work with saved sights

```sh
# List saved lines of position
./navalm lop list

# Show the worked reduction for sight 1
./navalm lop show 1

# Compute a fix from saved sights 1, 2, and 3
./navalm fix 1 2 3
```

The numbered examples require those sights to have been saved first. Running fixes also support vessel course and speed, current set and drift, and an explicit fix time; see the full reference for the options.

## Settings and saved data

| Data | Default location | Override |
| --- | --- | --- |
| Command-line and terminal defaults | `~/.navalmrc` | `NAVALMRC` environment variable |
| Command-line and terminal saved sights | `~/.naval.save` | `NAVALM_SAVE` environment variable |
| Browser settings and sights | Browser local storage | Use the web interface's JSON export/import controls for sights |

The browser's settings and sight list are independent of the command-line files. Export sights before clearing browser data or moving to another browser.

The web server listens on `127.0.0.1` by default. Use `./navalm -d 9000` for another port. It has no authentication or TLS; if using `--listen 0.0.0.0` for LAN access, keep it on a trusted network, or use an SSH tunnel for remote access.

## Navigation safety, warranty, and liability

NAVALM, its documentation, and all generated results are provided for
informational and educational purposes only. They are not certified
navigation products. Do not rely on them for navigation or any decision
where an incorrect result, failure, or delay could cause death, personal
injury, or loss of or damage to property. Independent verification does
not make NAVALM suitable for safety-critical use.

By using NAVALM, you acknowledge this warning and agree, to the maximum
extent permitted by applicable law, to use it entirely at your own risk
and to assume all responsibility and liability for your use, your
interpretation of its results, your decisions, and the resulting
consequences. If you do not accept these terms, do not use NAVALM.

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE SOFTWARE,
DOCUMENTATION, DATA, AND OUTPUT ARE PROVIDED "AS IS" AND "AS AVAILABLE",
WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, ACCURACY,
COMPLETENESS, RELIABILITY, AVAILABILITY, AND NONINFRINGEMENT.

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE AUTHORS, COPYRIGHT
HOLDERS, CONTRIBUTORS, AND DISTRIBUTORS SHALL NOT BE LIABLE FOR ANY CLAIM,
LOSS, DAMAGE, OR OTHER LIABILITY ARISING FROM OR IN CONNECTION WITH
NAVALM OR ITS USE OR INABILITY TO BE USED, WHETHER IN CONTRACT, TORT
(INCLUDING NEGLIGENCE), OR OTHERWISE. THIS INCLUDES DEATH, PERSONAL
INJURY, PROPERTY DAMAGE, NAVIGATION ERRORS, LOSS OF DATA OR PROFITS, AND
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES.

Nothing in this notice excludes or limits liability or rights that cannot
lawfully be excluded or limited. This notice does not promise that a
waiver or disclaimer is enforceable in every jurisdiction or circumstance.

Copyright permissions are granted by LICENSE and the applicable
third-party licenses. This safety and risk notice does not amend the MIT
license, add a copyright field-of-use restriction, or reduce anyone's
rights under the upstream XEphem MIT license.

## License and acknowledgments

NAVALM contributions are licensed under the [MIT license](LICENSE), and
4.0p does not change that. XEphem libastro and the selected XEphem
star-catalog records retain the [upstream MIT
notice](libastro/LICENSE.XEphem). Commercial use is permitted under MIT;
the prior project noncommercial grant is not the license of this
replacement source tree. Preserve the applicable copyright and permission
notices when distributing source or binaries. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The two components added in 4.0p are both public domain and neither
imposes conditions on NAVALM's own terms. **NOVAS C3.1** is a work of the
U.S. Naval Observatory; its documentation asks only that users acknowledge
the USNO Astronomical Applications Department, which NAVALM does here and
in `--notice`. The **JPL DE440s ephemeris** is US Government data produced
by the Jet Propulsion Laboratory, California Institute of Technology,
under contract with NASA, and is freely redistributable; it is data rather
than software and is not covered by the MIT grant. Both are credited in
full in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The former Metcalf-derived astronomical engine and tables have been
removed. We gratefully remember **Dr. Thomas R. Metcalf**, whose handheld
celestial-navigation software inspired this project, and acknowledge the
NAV48/NAV50 community and his family for preserving that legacy.

[PROVENANCE.md](PROVENANCE.md) records the replacement, upstream revision,
catalog sources, historical acknowledgments, and the scope of the review.
The new license does not retroactively relicense older versions or
third-party material in Git history.

## Further documentation

See [README.txt](README.txt) for the full command reference, input formats, altitude-correction formulas, running-fix workflow, web API endpoints, and version history.

The browser page is maintained in `page.html`; regenerate its embedded header with `make page` rather than editing `navweb_page.h` directly. Build the `navalm` executable on the machine where you intend to run it; compiled binaries are excluded from version control.

See [CHANGELOG.md](CHANGELOG.md) for the 3.0c transition and [VALIDATION.md](VALIDATION.md) for the publication checks.
