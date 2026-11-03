# Lunars — NavAlm 5.7.0

The native **Lunars** tab sits immediately before **Chrono**. The console uses
exactly the same calculation model. The web interface does not yet have a lunar
form. Annual book lunar-distance pages remain a separate future option.

## A lunar with a known approximate position

1. Select **Precise**. Enter your AP, watch date/time and the **raw sextant
   distance**, in degrees and decimal minutes, for example `9 01.5`. Use the
   watch's UTC date and time, including its error. Longitude has an E/W selector.
2. Select the other body or navigation star. Moon **Near** is the default:
   use the Moon's edge nearest the other body. **Far** means the opposite edge;
   choose the illuminated edge you actually touched. These distance contacts
   are distinct from the lower/upper limb used for an altitude. A star is a
   centre contact. Planets default to centre; select an edge only if observed.
3. Enter index error as a positive magnitude and choose **On** or **Off** the
   arc, plus temperature, pressure and observer elevation. Elevation is the
   approximate height above the WGS84 ellipsoid; eye height belongs in the
   altitude rows. No sea-horizon dip is applied to the angular lunar distance.
4. Leave both **measured** boxes clear if you did not measure altitudes.
   NavAlm calculates the topocentric geometry again at every trial UTC. Stored
   altitude readings are ignored; they never become fictitious position data.
5. Enter an initial **UTC minus watch** correction, normally zero, and a search
   half span, initially 600 seconds. A positive result means **add** that many
   seconds to the original watch reading. **Use Chrono estimate** supplies the
   starting correction from the selected clock's model; it is applied once.
6. Choose **Solve lunar UTC**. Review the derived UTC, position, distance rate,
   residuals and uncertainty in the report. A slow-changing distance gives a
   poor clock estimate even with a good sextant reading. Changing any input
   invalidates the previous result and disables its export/Chrono transfer.

**Predict at start** shows the model at the starting correction. It does not
change any recorded Hs or watch time, and is not a solved UTC. **Example** loads
a fictional, explicitly far-limb Moon–Jupiter example at a round AP, without
writing a file. Its limb selection is part of that example; new observations
default to Near.

## Real altitude shots and separate times

Check **Moon measured** or **Other measured** only for an actual observation.
Enter that shot's raw Hs and its own original watch date/time. All shots must
use the same watch and clock-error series. A shared trial clock correction is
added to every original timestamp; their intervals remain intact, including
midnight crossings. Support is limited to shots within 12 hours of the lunar.
The model assumes the clock error is constant over that session.

Each altitude has its own lower/centre/upper limb, sea/artificial/short-waterline
horizon, eye height and units. Artificial-horizon Hs is the doubled angle.
Short-waterline correction includes eye-height geometry; do not also apply sea
dip. The **…** button beside each source checkbox opens that altitude's IE,
weather and short-waterline settings. By default they follow the main fields;
**Own** marks an independent override. Loaded independent settings are retained.

With a fixed AP, real altitudes are reported as independent residual checks;
they do not change the AP or the recovered time. One real altitude with a lunar
cannot determine latitude, longitude and clock correction together. With **two
real altitudes**, select **refine AP** for the joint three-angle solution. The
solver fits both real altitudes at their own corrected epochs and the lunar at
its corrected epoch. It lists converged branches nearest the supplied AP first.
Check the other branches rather than accepting a distant position by accident.
The bounded multistart search does not guarantee discovery of every global root.

**Use DR motion** advances positions between the lunar and real altitude epochs
using the course/speed and set/drift loaded from saved Defaults or **Use sight
DR**. The AP refers to the lunar epoch. This is a constant-motion local model;
review the loaded values in the report. Leave it clear for a stationary observer.
Synthetic altitudes always refer to the corrected lunar epoch and supplied AP.

## Saving, audit and Chrono

**Save inputs** writes a `.lunar` text profile with original watch timestamps,
raw angles, limb choices, measured/synthetic flags and settings. **Load** restores
those inputs, not a trusted cached solution. **Export report** writes the full
calculation and all recovered branches. Inputs remain unchanged when solving.

After solving, select a branch and choose **Prepare Chrono comparison**. Select
a named clock in Chrono first. NavAlm fills the raw watch reading, the separate
derived UTC and a source note with the formal time uncertainty. Review it, then
use Chrono's **Add comparison**. Nothing is added to the log automatically.
Chrono currently weights active comparisons equally; do not treat a lunar's
uncertainty as equivalent to a precise WWV comparison. Weak solutions cannot be
sent directly to Chrono. Do not feed a lunar back as an independent reference
for the same data used to derive it.

## Console

```sh
./navalm lunar --example > example.lunar
./navalm lunar example.lunar > lunar-report.txt
```

Profiles use `navalm_lunar=1`, unique `key=value` lines, decimal degrees and
**east-positive** longitude. `distance.watch`, `moon.watch`, and `other.watch`
are full `YYYY-MM-DDTHH:MM:SS.ssssss` watch epochs. `moon.measured=0` and
`other.measured=0` exclude their stored Hs/epochs from the observation constraints.
Contact values are `1` near, `0` centre, `-1` far; altitude limb values are `1`
lower, `0` centre, `-1` upper. Unknown/duplicate keys, invalid dates and nonfinite
numbers are rejected. Start with the generated example for the complete schema.

## Model, limits and checks

The forward model uses NOVAS C3.1 with the JPL DE440s ephemeris, a WGS84 observer,
light-time/aberration and topocentric parallax. It refracts directions toward
the zenith with the Bennett apparent-altitude formula, then computes angular
separation with three-dimensional vectors. For selected edges it extremizes
points on the refracted disc circumference. It does not subtract scalar altitude
corrections directly from a distance. The Moon's augmented semidiameter uses a
1738.1 km smooth mean limb radius. Planet edges use equatorial radii; oblateness,
lunar terrain and illuminated phase/photocentre shifts are not modeled.

Supported raw distances are 1–150 degrees; the complete discs must remain
between 5 and 89.8 degrees altitude at every required epoch. Latitude is limited
to ±85 degrees. UTC is taken as UT1, with polar motion zero; the ephemeris date
range and engine's TT offset apply. These limits are checked during the search.
Formal uncertainty propagates the entered angle sigmas through the local model;
it excludes uncertain AP, refraction, limb and instrument systematics. Small
formal residuals from three fitted angles do not validate those assumptions.

`make check-lunar` tests independent JPL Horizons centre separation, signed
contact/index conventions, Sun and star lunars, fixed AP, known clock-offset
recovery, measured/synthetic mixtures, separate dates, artificial horizon,
motion, real position branches, profile persistence and invalid inputs.
`./navalm-fltk --lunar-self-test` exercises native controls and Chrono handoff
with isolated fictional data. See [reference fixture](tests/lunar-horizons.md).
