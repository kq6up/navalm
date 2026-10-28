# Linux build and checks

NAVALM has a native FLTK desktop plus the existing CLI, ncurses terminal and
local web interfaces. The macOS `.app` bundle and portable Windows `.exe` are
platform-specific; on Linux build `navalm-fltk` instead.

## Ubuntu 24.04 dependencies

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake curl python3 nodejs perl \
  libncurses-dev libx11-dev libxext-dev libxft-dev libxrender-dev \
  libxinerama-dev libxcursor-dev libfontconfig1-dev
```

## Build and launch

From a fresh clone or after `git pull`:

```sh
make ephem-fetch      # once: download the JPL input
make ephem            # once: convert it for NAVALM
make fltk-deps        # once, if system FLTK is unavailable: pinned FLTK 1.4.5
make -j4 navalm navalm-book desktop
./navalm-fltk         # native desktop, in your graphical session
```

Other entry points remain available:

```sh
./navalm -v
./navalm --gui        # terminal interface
./navalm -d           # browser interface; open the printed localhost address
./navalm-book --help
```

For a checkout containing macOS or Windows build outputs, use a separate Linux
clone or `make clean` first. Do not reuse object files across operating systems.
Download the optional coastline with the desktop's **Charts + setup** controls.
The planetarium constellation figures and supporting stars are compiled in;
they require no additional data download.

## Slackware 15.0 / FLTK 1.3.8

Use Slackware 15.0's FLTK 1.3.8 package from `extra` (or the matching
[SlackBuild](https://slackbuilds.org/repository/15.0/libraries/fltk/)). No FLTK
package patch or libdecor dependency is required. From a Linux-only checkout:

```sh
make desktop
make check-desktop check-book
```

Linux prefers the system `fltk-config` on PATH even if `.deps/fltk` exists.
Force a particular installation with `make desktop FLTK_CONFIG=/usr/bin/fltk-config`.
To select the private build explicitly, use
`make desktop FLTK_CONFIG="$PWD/.deps/fltk/bin/fltk-config"`.
When changing FLTK installations after a successful build, remove `navalm-fltk`
first so it is relinked. Copied macOS/Windows libraries in `.deps` are not usable
Linux build dependencies; rebuild locally if using the private toolkit.

## Automated validation

The 5.4 code passed the complete workflow on **28 October 2026**:
[Ubuntu build and runtime result](https://github.com/kq6up/navalm/actions/runs/40701672756).
This was an actual Ubuntu runner; the release workstation itself was macOS.

[Linux build and runtime checks](https://github.com/kq6up/navalm/actions/workflows/linux.yml)
runs on Ubuntu 24.04. It builds the precise ephemeris, verifies the Natural Earth
map hash, builds all interfaces with both pinned FLTK 1.4.5 and system FLTK 1.3.8, and exercises:

- Paper almanac, sight list, plotting geometry and desktop core calculations.
- Both-engine planetarium coordinates, constellation paths, filters and twilight.
- Real HTTP endpoints for the planetarium, direct-at-DR reduction and book export.
- Annual PDF/LaTeX generation and browser JavaScript checks.
- The native `--self-test` under Xvfb, including window creation, callbacks,
  sky detach/resize/fullscreen/reattach, sight handoff, fixes and offline maps.

To run the same GUI callback checks locally without a graphical session:

```sh
sudo apt-get install -y xvfb xauth
xvfb-run -a -s '-screen 0 1920x1080x24' ./navalm-fltk --self-test
```

Run from the source directory with `ephem/de440s.nav`, the committed test
fixtures and `.local/maps/ne_10m_land.shp` available, as prepared in the workflow.
Tests use temporary navigation files. A virtual-display callback run is distinct
from manual visual testing in a Linux desktop session.
