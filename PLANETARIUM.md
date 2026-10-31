# Planetarium and star planner (5.4)

The **Planetarium** tab is available in the FLTK workbench and the `navalm -d`
browser interface, immediately before Annual book. It uses NAVALM's existing
58-star catalogue, including Polaris, plus the Sun, Moon, Mercury, Venus, Mars,
Jupiter and Saturn, using the selected C astronomy engine.
It works offline with the same ephemeris requirements as the almanac.

## Plan a round of sights

1. Enter a UTC date/time and DR latitude/longitude. The desktop uses separate
   hemisphere selectors; the browser accepts `34 02.5 N`, `N 34° 02.5′`, or
   signed decimal degrees. Copy DR from the current sight (desktop) or Setup
   (browser) when useful. Press **Update sky** after editing entries.
2. Choose minimum/maximum altitude, faintest magnitude and a three- or four-star
   set. Defaults are 15–75 degrees and magnitude 3.0. Smaller magnitudes mean
   brighter stars. Gold markers identify the suggested set; dim markers lie
   outside your filters. The catalogue can also list below-horizon stars. The Sun is a yellow disc,
   Moon a pale disc, and planets orange rings. These bodies remain visible
   regardless of star filters, and their labels remain when star names are hidden.
   Body symbols are schematic, not angular size or lunar phase.
3. **Next morning sights** or **Next evening sights** finds the next rising or
   setting Sun altitude of −8 degrees in the following 48 hours. It updates UTC
   and recalculates the stars. A polar date may have no such crossing.
4. Click a marker or a catalogue row to read Hc, true bearing and magnitude.
   **Use selected object for a sight** opens a new, unsaved observation with that
   star or body, planned UTC and DR. Enter the actual observation time and measured Hs
   before reducing it. The planner never supplies a synthetic sextant reading.

The chart is a view **looking up**: north at the top, east on the left, west on
the right. The rim is the horizon and the centre is the zenith. Rings mark
0°, 30° and 60° altitude. The browser can save a standalone sky SVG.
The native table displays Hc as degrees and decimal minutes; bearings are
decimal degrees true.

## Find the constellations

Three independent checkboxes control the identification layers:

- **Constellations** draws the traditional connecting lines.
- **Constellation names** labels visible figures in pale blue. Navigation-star
  and body names take priority where labels would collide.
- **Supporting stars** adds the smaller, muted stars needed to complete the shapes.

Lines and supporting stars start enabled; constellation names start hidden.
The star-name checkbox is independent; body labels remain visible. Selecting a
navigation star also shows its constellation in the details. Supporting stars
are identification aids and do not enter the 58-star list, planner or sight form.
These layers work offline. Figures cover all 88 constellations (Serpens has two
parts). Only portions above the horizon are shown. See [data provenance](data/sky/README.md)
for source, licensing and the one approximate variable-star vertex.

## A larger sky view

In FLTK, **Detach sky…** opens the same live canvas in a separate resizable
window. Its checkboxes stay synchronized with the docked controls. Choose
**Full screen (F11)** or press F11; Esc returns to the window. **Reattach sky**
or closing the detached window returns it to the Planetarium tab, preserving
selections, data and layers. **Use selected for a sight** returns to the sight form.
UTC and DR inputs stay on the main Planetarium tab; Update sky refreshes the
live detached canvas too.

In the browser, **Expand sky** fills the page with the chart and its controls.
**Full screen** also requests browser full screen where supported; an expanded
view remains available if the browser restricts full screen. **Restore sky** or
Esc returns to the normal layout. Saved SVGs include the chosen layers.

## What the suggested set means

Every combination of three or four eligible stars is considered. The score
weights bearing geometry (65%), balance around the horizon (20%), and a mix
of brightness and moderate altitude (15%). The geometry component is the
normalised determinant of the bearing-normal matrix, on a 0–100 scale. It
favours crossing lines of position over parallel ones. The balance term favours
bearings around the observer. The altitude preference peaks at 45°.

This is a planning heuristic. The geometry number is **not an accuracy estimate**.
It assumes comparable sight errors and does not model clouds, obstructions,
Moon glare, haze, observer skill, vessel motion or actual horizon visibility.
Day/night status is shown to help judge visibility; daylight does not hide stars
from the diagram. If fewer stars match the filters, all available candidates
are shown and the shortfall is reported.

Altitudes are **geocentric geometric Hc**, consistent with the almanac/reducer;
they do not include observer parallax (noticeable for the Moon), refraction, dip
or limb corrections. They are planning coordinates rather than an apparent view
through a telescope. See [USNO celestial-navigation conventions](https://aa.usno.navy.mil/data/celnav).
The star planner remains star-only; solar-system bodies can be selected manually.
Bearings are measured clockwise from true north. Exactly at the zenith/nadir a
bearing is undefined, and such a star is excluded from the suggested set.

The Sun −8° choice is a convenient planning instant within nautical twilight,
not a formal twilight boundary or a guarantee of visibility. The light labels
use geometric Sun altitudes of 0°, −6°, −12° and −18°. USNO defines civil,
nautical and astronomical twilight boundaries at −6°, −12° and −18°:
[USNO definitions](https://aa.usno.navy.mil/faq/RST_defs).
USNO's [navigational star chart](https://aa.usno.navy.mil/data/Nav_Star_Chart)
provides a useful traditional identification reference.

## Validation

`make check-sky` checks cardinal directions, east/west longitude, zenith/nadir,
Gregorian UTC conversion, filter/selection invariants, polar no-event handling,
and rising/setting Sun −8° crossings. Random cases compare all 58 stars in both
engines with a separate spherical-coordinate formula (13,920 comparisons).
A further 1,680 Sun/Moon/planet positions are checked against independent
spherical formulas; body markers do not enter the suggested star sets.
Another 170,880 supporting-star positions and the visible great-circle paths
are checked across both engines, including polar and horizon cases. Native
callbacks check layer synchronization, live canvas ownership, resizing, full
screen, reattachment and stale-data rejection.
It also exercises the real HTTP endpoint and browser input/projection/error
handling, including overlapping requests. These checks validate the coordinate
conversion and planner integration; the underlying ephemeris validation remains
in VALIDATION.md. Real macOS native and browser views were exercised. The Windows
package is cross-built and inspected; the user reported successful Windows
5.4 testing on 28 October 2026. See LINUX.md for Ubuntu CI coverage.

## Native sky zoom (5.5.0)

Use **Zoom + / Zoom −**, the mouse wheel, or +/− with the chart focused.
Zoom is limited to 4.5× (about 5% of the original plotted sky area). Drag to
pan; **Centre selected** brings the selected navigation object into view.
**All sky**, 0 or Home restores the full horizon with zenith at the centre.
The same projection and hit testing apply to stars, planets and constellation
layers. Zoom, pan, selection and layer choices survive detach/resize/full screen
and reattachment. These zoom controls apply to the native FLTK chart.

The page starts at saved DR and offers saved/current-sight DR buttons.
See [DEFAULTS.md](DEFAULTS.md) for coordinate reuse and sight handoff.
