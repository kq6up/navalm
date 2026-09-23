# NAVALM

A nautical almanac and celestial sight-reduction program for Unix/Linux, written in C. NAVALM provides a command-line interface, an optional ncurses terminal interface, and a built-in browser interface.

Current version: **2.8g**.

## Features

- Almanac calculations for the Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Aries, and navigation stars.
- Daily tables in plain text, HTML, and LaTeX; NAVAL hourly and AIR ten-minute views in the web interface.
- Sextant-altitude corrections for sea and artificial horizons, with worked sight-reduction steps.
- Stored lines of position, least-squares fixes, and running fixes with course/speed and set/drift.
- Degrees-and-decimal-minutes displays and flexible star-name matching.

## Build

You need a C99 compiler, `make`, and the C math library. The default build also requires ncurses development headers and libraries.

```sh
git clone https://github.com/kq6up/navalm.git
cd navalm
make
./navalm -v
```

To build the command-line and web interfaces without ncurses:

```sh
make nogui
```

The generated `navalm` executable is excluded from version control. Build it on the machine where you intend to run it.

## Quick start

```sh
# Daily almanac table (UTC)
./navalm -t 2026-09-23

# Single-body almanac lookup
./navalm sun 2026 9 23 12 0 0

# Full-screen terminal interface (requires the default ncurses build)
./navalm --gui

# Browser interface: open http://127.0.0.1:8080/
./navalm -d
```

Export a daily table:

```sh
./navalm -th 2026-09-23 > almanac.html
./navalm -tx 2026-09-23 > almanac.tex
```

Reduce an artificial-horizon Sun sight:

```sh
./navalm sight sun 2026 9 19 21 38 05 --hs 94 37.2 --ah \
  --ie 0 --temp 30 --press 1013 --dr 34 2.5 N 117 21.9 W
```

## Settings and saved sights

The command-line and terminal interfaces use `~/.navalmrc` for defaults and `~/.naval.save` for saved sights. Override these paths with `NAVALMRC` and `NAVALM_SAVE`.

The web interface stores its settings and sights in the browser's local storage, independently of those files. Use its JSON export/import controls to transfer saved sights.

The web server binds to `127.0.0.1` by default. It does not provide authentication or TLS; LAN access should be limited to a trusted network or an SSH tunnel.

## Development and documentation

The browser page is maintained in `page.html` and embedded in `navweb_page.h`. Regenerate it after editing:

```sh
make page
make
```

See [README.txt](README.txt) for the complete command reference, input formats, sight workflow, API endpoints, and version history.
