# NAVALM

**A nautical almanac and celestial sight-reduction program for Unix/Linux.**

NAVALM brings almanac calculations, worked sextant-sight reductions, and position fixes together in one C program. Use it from the command line, in a full-screen terminal, or through its built-in browser interface.

Current version: **2.8g**.

## Features

- **Almanac:** Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Aries, and navigation stars.
- **Daily tables:** plain text, HTML, and LaTeX output, plus NAVAL hourly and AIR ten-minute views in the browser.
- **Sight reduction:** sea and artificial horizons, altitude corrections, and worked HO 229-style reduction steps.
- **Position fixes:** saved lines of position, least-squares fixes, and running fixes with course/speed and set/drift.
- **Navigation-friendly input:** degrees and decimal minutes, flexible DR coordinates, and partial star-name matching.

## Build

### Requirements

- A C99 compiler, such as GCC or Clang, and the C math library.
- `make`, a POSIX shell (`sh`), and `sed`.
- ncurses headers and libraries for the full-screen terminal interface.
- Git if cloning the repository; a downloaded source archive also works.

### Download and compile

```sh
git clone https://github.com/kq6up/navalm.git
cd navalm
make page
make
./navalm -v
```

If you downloaded a source archive, extract it and enter its source directory, then run the same build commands starting with `make page`.

**Run `make page` before `make`.** The first command runs `mkpage.sh` to convert `page.html` into `navweb_page.h`, embedding the browser interface in the program. The second compiles the C sources and creates the `navalm` executable. Plain `make` does not regenerate the page header automatically.

The normal build includes all three interfaces. Run `./navalm -v` to check the version; this source reports `navalm version 2.8g`. The executable is built in the source directory and can be run there without an installation step.

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

## Further documentation

See [README.txt](README.txt) for the full command reference, input formats, altitude-correction formulas, running-fix workflow, web API endpoints, and version history.

The browser page is maintained in `page.html`; regenerate its embedded header with `make page` rather than editing `navweb_page.h` directly. Build the `navalm` executable on the machine where you intend to run it; compiled binaries are excluded from version control.
