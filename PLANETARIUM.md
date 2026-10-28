# Planetarium and star planner (5.2)

The **Planetarium** tab is available in the FLTK workbench and the `navalm -d`
browser interface, immediately before Annual book. It uses NAVALM's existing
58-star catalogue, including Polaris, and the selected C astronomy engine.
It works offline with the same ephemeris requirements as the almanac.

## Plan a round of sights

1. Enter a UTC date/time and DR latitude/longitude. The desktop uses separate
   hemisphere selectors; the browser accepts `34 02.5 N`, `N 34° 02.5′`, or
   signed decimal degrees. Copy DR from the current sight (desktop) or Setup
   (browser) when useful. Press **Update sky** after editing entries.
2. Choose minimum/maximum altitude, faintest magnitude and a three- or four-star
   set. Defaults are 15–75 degrees and magnitude 3.0. Smaller magnitudes mean
   brighter stars. Gold markers identify the suggested set; dim markers lie
   outside your filters. The catalogue can also list below-horizon stars.
3. **Next morning sights** or **Next evening sights** finds the next rising or
   setting Sun altitude of −8 degrees in the following 48 hours. It updates UTC
   and recalculates the stars. A polar date may have no such crossing.
4. Click a marker or a catalogue row to read Hc, true bearing and magnitude.
   **Use selected star for a sight** opens a new, unsaved observation with that
   star, planned UTC and DR. Enter the actual observation time and measured Hs
   before reducing it. The planner never supplies a synthetic sextant reading.

The chart is a view **looking up**: north at the top, east on the left, west on
the right. The rim is the horizon and the centre is the zenith. Rings mark
0°, 30° and 60° altitude. The browser can save a standalone sky SVG.
The native table displays Hc as degrees and decimal minutes; bearings are
decimal degrees true.

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

Altitudes are geometric Hc, with no refraction, dip or sextant corrections.
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
It also exercises the real HTTP endpoint and browser input/projection/error
handling, including overlapping requests. These checks validate the coordinate
conversion and planner integration; the underlying ephemeris validation remains
in VALIDATION.md. Real macOS native and browser views were exercised. The Windows
package is cross-built and inspected; this feature still needs a Windows run.
