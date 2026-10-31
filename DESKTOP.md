# NAVALM FLTK navigation workbench

The optional desktop calls the existing C astronomy, sight-reduction, store,
and fix APIs directly. `make` still builds the CLI, terminal, and web program.

For Ubuntu packages, native launch commands and virtual-display testing, see
[LINUX.md](LINUX.md).

## Build and open

Requires a C/C++17 compiler and FLTK 1.3.8 or 1.4.5. The optional private
bootstrap builds static FLTK 1.4.5 into `.deps/fltk`. macOS prefers this private
build; Linux prefers `fltk-config` on PATH, falling back to the private build.
An explicit `FLTK_CONFIG` overrides either default.

```sh
make fltk-deps        # once; needs CMake, curl, tar, shasum and internet
make desktop         # native executable: ./navalm-fltk
make desktop-app     # macOS bundle: open NAVALM.app
make check-desktop   # input, store and vector geometry checks; no GUI required
make desktop-checks-app # macOS native callback test bundle
open 'NAVALM Checks.app' # run it; shows a pass window on success
```

To use another installed FLTK: `make desktop FLTK_CONFIG=/path/to/fltk-config`.
The private bootstrap pins and verifies the official 1.4.5 source SHA-256.
FLTK documentation: https://www.fltk.org/doc-1.4/basics.html
FLTK source/license: https://github.com/fltk/fltk/tree/release-1.4.5

The precise engine uses `ephem/de440s.nav` or a bundled copy. Use the existing
`make ephem-fetch && make ephem` if neither exists. The app clearly reports
Classic when no usable JPL ephemeris is available. The engine selector and
Charts + setup page show the selected engine and ephemeris details.

## Use

- **Almanac:** UTC date/time, body or catalogue star, DR; paper almanac or a
  complete hourly table, with text export.
- **Planetarium (5.4):** view the 58 navigation stars, Sun, Moon and five planets for a DR and UTC time;
  suggest three/four stars by bearing spread, brightness and altitude. Jump to
  morning/evening sight windows and carry a selected object into a new observation.
  Toggle constellation figures, their names and supporting stars independently.
  Detach sky opens a resizable window with F11/full screen and Reattach sky.
  See [PLANETARIUM.md](PLANETARIUM.md) for orientation and planning conventions.
- **Annual book (5.2):** choose a year and sections, then export a vector
  PDF or editable LaTeX source with progress and cancellation. LaTeX compilation
  is a separate step requiring TeX. Calculator formulas and compact Ageton tables
  can be included independently. Optional alternate-row shading adds a faint gray
  background to alternate hourly, increments, corrections and Ageton table rows.
  Actual NAO Concise tables and complete Moon limb
  tables remain unfinished. See [ANNUAL.md](ANNUAL.md).
- **Sight reduction:** enter degrees, a space, then decimal minutes:
  `94 37.2` for Hs, `34 02.5` latitude, `117 21.9` longitude. Fields display
  this format after examples, defaults and saved-sight reloads. Decimal degrees
  are also accepted (`34.0416667` is about `34 02.5`), with separate latitude
  and longitude hemispheres, sea/artificial horizon, limb, index error,
  atmosphere, optional vessel motion and current. The full worked reduction
  uses the same formatter as the CLI. Sun example loads the README observation.
  New sights leave DR blank when there are no saved DR defaults. Both fields
  are required; an explicitly entered latitude 0, longitude 0 is valid.
- **Saved sights + plotting:** select 2–16 observations, stationary or running
  fix, optional explicit UTC fix time, residuals, uncertainty, vector diagram.
  The sheet follows the web app's colours, geometry, compass rose, AP markers,
  intercept arrows, labels, scale bar and enlarged three-LOP cocked hat. Choose
  **Detach sheet…** to move the sheet into its own resizable window. Use
  **Reattach sheet** (or close that window) to return it to the workbench.
  Zoom with **− / +**, the mouse wheel, or the `+` / `-` keys after clicking
  the plot. Drag to pan; **Fit**, double-click, or `0` restores the full sheet.
  Zoom ranges from 25% to 800% of Fit. Layer/opacity changes and recalculated
  fixes update the detached view; exports always contain the complete sheet.
  Edit preserves a sight ID. Save as new deliberately adds a new ID.
  Invalid saved inputs, below-horizon reductions and parallel LOPs return an
  explanation and let you edit the sights and try again.
- **Charts + setup:** ephemeris selection, state paths, an offline coastline
  underlay, cancellable world-map download, OpenCPN coastline import, and a
  WGS84 land shapefile picker. **Map** independently toggles the underlay in both
  plotting views. The included Natural Earth map works offline. See MAPS.md.
- **Windows portable build:** see WINDOWS.md and tools/build-windows.sh. The x64
  executable statically links FLTK/C++ support and embeds the map and ephemeris.

State is isolated from your usual CLI files: `.local/desktop-sights.save` and
`.local/desktop-defaults.rc`, beside the executable or beside `NAVALM.app`.
`NAVALM_SAVE` and `NAVALMRC` explicitly override these paths, allowing sharing
with the CLI. The native app must be in a writable folder. Use one writer per
log at a time; this first version does not lock logs between applications.

## Transparent geographic vector overlay

`navfltk_plot.h` holds the geographic fix data. `navfltk_sheet.h` ports the
web app's 760 × 772 plotting-sheet drawing rules into shared vector primitives;
FLTK and SVG use that same scene. Native text rasterization and thin stroke
rounding can differ slightly from a browser. The original web source is unchanged.
Graticule/compass, LOPs/motion, labels, and fix/detail have separate visibility
switches. Opacity is shared by both native views and SVG export. The numerical
fix report still includes the uncertainty ellipse dimensions.

Transparent SVG export contains layer classes (`.graticule`, `.lines-of-position`, `.labels`,
`.fix-and-detail`) and projection metadata, with no
sheet-wide background fill. Small white label halos and the scale-bar plaque
match the web sheet. It is a local plotting sheet, not an automatically
georeferenced chart image. GeoJSON exports the main panel's geographic paths
and labels, with style properties. The enlarged cocked hat and page furniture
are excluded from geographic exports because they are not chart positions.
The diagram uses mean AP latitude for longitude scaling, including advanced
LOPs, so its signed LOP distances match the C solver's residuals.

The first offline underlay and background downloader are implemented. The next
chart work is a regional catalogue, coverage index, and raster/ENC decoder.
Natural Earth and OpenCPN coastline polygons use the same local plane as the
UGP; depths and nautical chart symbols are not rendered. Downloads validate
polygon structure before atomically replacing the previous map.

The geographic paths must be reprojected for the chart projection, especially
Mercator; placing a local-tangent SVG over a chart without reprojection will
not align correctly. This version has no raster or ENC chart decoder. The
plot remains a regional aid; it rejects antimeridian and near-pole fixes that
this local-plane implementation cannot display reliably.

## Licensing

NAVALM's existing license and notices continue to apply. FLTK is LGPL-2.0-or-later
with its static-linking exception; see FLTK's `COPYING`, included in this
sandbox app bundle. Private downloaded dependencies, state, ephemeris, and
build products are ignored by Git. This is a local, ad-hoc signed app, not a
notarized release.


## Verified in this sandbox

- Native macOS launch, almanac rendering, input and calendar validation,
  Sun reduction, saved-sight add/update/reload, defaults, catalogue stars,
  runtime engine switching and stale-result clearing.
- Precise README Sun example: 6.8 NM toward.
- Recorded synthetic stationary and running fixes; geographic vector LOP
  residuals agree with the C solver within 0.000001 NM. Tests also cover
  explicit fix time, layer visibility, opacity, SVG and GeoJSON generation.
- Web/native parity checks compare LOPs, APs, intercepts, compass ticks,
  geographic grid and enlarged cocked hat for both fixtures within 0.15 pixel
  (web SVG coordinates are rounded to tenths). `check-desktop` needs Node.js.
- Native detach/reattach, pointer-anchored zoom, zoom limits, Fit,
  synchronized layer/opacity controls, and fix recalculation while detached.
- Offline SHP parsing, holes, clipping, malformed-file rejection and geographic
  map/vector alignment; real Natural Earth and installed OpenCPN coastlines.
- Native background HTTPS download, cancellation preserving the prior map,
  map visibility in both views, and a synthetic coastal fix preview. The
  dedicated Checks.app performs a real map download; check-desktop is offline.
- Native callback checks completed with an empty stderr report after fixing
  display/buffer destruction order. See `.build/desktop-test/ui-checks.txt`.
- Existing `make check-paper check-plot check-list` all passed.
- Existing CLI Sun calculation under both engines and worked sight passed.
- Existing `./navalm -d 18842` started normally; the browser interface and
  its computed almanac rendered successfully.

The CLI/terminal/web interface and astronomy calculations are preserved. Shared
saved-file code now uses a platform-specific atomic replacement on Windows;
POSIX continues to use rename().
The ncurses interface remains in the normal build; this change did not perform
an interactive terminal-interface walkthrough. The offline coastline underlay is implemented; nautical ENC/raster chart
rendering remains future work. Native file-picker exports are wired
but have not been exercised through every system-dialog path.

The Sight tab has a **Reduce at DR (no whole-degree LHA)** checkbox. Checked
sights use direct spherical reduction at their full entered DR; unchecked sights
use the existing whole-degree HO 229 assumed position. Saved sights retain the
choice, and fixes and plots can combine both methods.

## Defaults and workflow

See [DEFAULTS.md](DEFAULTS.md) for the dedicated Defaults tab, startup DR reuse,
almanac handoff and nearer-waterline corrections. Saved sights keep their own
settings. The Annual book tab remains last.
