# NAVALM FLTK navigation workbench

The optional desktop calls the existing C astronomy, sight-reduction, store,
and fix APIs directly. `make` still builds the CLI, terminal, and web program.

## Build and open

Requires a C/C++17 compiler and FLTK 1.4. On this sandbox a private FLTK 1.4.5
is already built in `.deps/fltk`; it is statically linked into the application.

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
- **Sight reduction:** decimal degrees or `degrees minutes`, separate latitude
  and longitude hemispheres, sea/artificial horizon, limb, index error,
  atmosphere, optional vessel motion and current. The full worked reduction
  uses the same formatter as the CLI. Sun example loads the README observation.
- **Saved sights + plotting:** select 2–16 observations, stationary or running
  fix, optional explicit UTC fix time, residuals, uncertainty, vector diagram.
  Edit preserves a sight ID. Save as new deliberately adds a new ID.
- **Charts + setup:** ephemeris file selection, state paths, chart roadmap.
  Chart downloads, a chart underlay, and chart compositing are future work.

State is isolated from your usual CLI files: `.local/desktop-sights.save` and
`.local/desktop-defaults.rc`, beside the executable or beside `NAVALM.app`.
`NAVALM_SAVE` and `NAVALMRC` explicitly override these paths, allowing sharing
with the CLI. The native app must be in a writable folder. Use one writer per
log at a time; this first version does not lock logs between applications.

## Transparent geographic vector overlay

`navfltk_plot.h` builds geographic paths independently of FLTK and of any chart
provider. Every point has longitude/latitude coordinates. The FLTK renderer
projects them to the same local tangent plane used by NAVALM's fix solver.
Graticule/compass, LOPs/motion, labels, and fix/ellipse have separate visibility
switches. Opacity is shared by the native view and SVG export.

Transparent SVG export contains named groups and projection metadata, with no
background rectangle. It is a local plotting sheet, not an automatically
georeferenced chart image. GeoJSON exports the geographic line paths with
layer, color, and dashed properties; display labels remain in the SVG scene.
The diagram uses mean AP latitude for longitude scaling, including advanced
LOPs, so its signed LOP distances match the C solver's residuals.

Future chart work should add these independent components:

1. Provider/catalogue adapters with explicit region selection and source terms.
2. A cancellable background download queue with temporary files and atomic
   completion, independent of the FLTK event loop.
3. A local chart index holding coverage, edition, projection, and file paths.
4. A chart canvas that projects chart content and these geographic vectors into
   one viewport, drawing the vectors in a separate compositing layer.

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
- Native callback checks completed with an empty stderr report after fixing
  display/buffer destruction order. See `.build/desktop-test/ui-checks.txt`.
- Existing `make check-paper check-plot check-list` all passed.
- Existing CLI Sun calculation under both engines and worked sight passed.
- Existing `./navalm -d 18842` started normally; the browser interface and
  its computed almanac rendered successfully.

The CLI/terminal/web source and the astronomy engine were not modified.
The ncurses interface remains in the normal build; this change did not perform
an interactive terminal-interface walkthrough. Chart downloading and chart
underlay rendering remain unimplemented. Native file-picker exports are wired
but have not been exercised through every system-dialog path.
