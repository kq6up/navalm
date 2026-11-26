# Lunar occultations and time recovery (6.0.0)

**Occultations** and **Occultation UTC** are adjacent native FLTK tabs between
Lunars and Chrono. The command line exposes the same calculation core. The web
interface retains its existing features; these new tabs are native desktop tools.
These are lunar **stellar** occultations, not asteroid occultations or planetary
limb contacts. No internet connection or additional catalog download is needed.

## Find an event

1. Enter the observer's AP, elevation in metres, UTC start and up to 31 days.
   The AP is the position at the event, not an inferred fix. Load saved DR or
   copy the current sight DR. Elevation is height above the ellipsoid, not
   sextant eye height. A ship must use its predicted position at the contact.
2. Choose faintest visual magnitude (maximum 6.5), minimum geometric altitude,
   and whether to require night. Night defaults to Sun below -6 degrees;
   turn it off to include daylight events. These visibility filters do not
   guarantee that the star is observable against the illuminated Moon.
3. Find occultations. Long searches have progress and cancellation. Sort by
   time, magnitude (brightest first), most direct, or closest to grazing.
   Filter to direct events or near-limb events / near misses.
4. Select a row. D is disappearance, R reappearance. A star's un-refracted
   altitude, Sun altitude, position angle (north through east), bright/dark
   limb and contact's normal angular rate are in the details. Asterisks mark
   contacts that meet visibility filters **and** fall in the search interval.
   The other contact is retained for context even when outside these limits.
5. Select D or R and Use for time recovery. Only marked contacts can be sent.
   This copies a prediction, **not a synthetic observation**: the recorded
   watch fields are not filled or replaced. Export list saves the current
   filtered/sorted prediction list, with full aliases and model settings.

`b` is minimum topocentric separation divided by apparent lunar radius:
0 crosses the centre, 1 is tangent. We label `b >= 0.90` **near-limb**, and
rank grazing proximity by `abs(b - 1)`. This broad planning band is not a
physical definition of a graze. A true terrain graze occupies a narrow strip
near the limit and may have many disappearances/reappearances. Near misses
within the entered angular margin (default 30 arcseconds) are shown separately;
they have **no predicted smooth-limb contact** and cannot recover a clock time.
The track illustration is schematic; it is not north-up or a lunar terrain map.

## Recover UTC from a contact

Use a predicted event, or enter an exact catalogue name/alias and an approximate
UTC within three hours of local conjunction. Enter your actual watch date and
fractional-second reading on the UTC calendar/scale, and select D or R.
For a local-time watch, convert both date and time to the UTC scale first.

The solver calculates the selected physical event at the fixed AP and gives
**UTC minus watch**, the correction to add to that watch. This is clock recovery,
not astronomical delta T (TT minus UT1), and does not solve longitude or an AP.
A single contact cannot independently determine both position and clock error.

- Reaction delay is positive for a late recording. If you pressed the button
  0.25 s after the contact, enter +0.25 s. Correction is predicted contact UTC
  minus recorded watch **plus** reaction delay. A known negative delay is allowed.
- Limb offset is an optional correction in arcseconds, positive outward,
  increasing lunar radius. Default zero. Do not tune it to get a desired answer.
- DUT1 is UT1 minus UTC in seconds. Default zero means UTC is approximated as
  UT1; no IERS service is queried. Enter an independently known value for
  precise work. TT is derived from UTC using NavAlm's leap-second table;
  DUT1 affects Earth's rotation, not TT. Polar motion is taken as zero.
- Timing sigma is your estimated one-sigma random recording uncertainty.
  Limb/catalog sigma is an **assumption**, in arcseconds, for residual normal
  limb relief and stellar-position error. No terrain model or astrometric
  covariance is loaded. Defaults (0.2 s, 2 arcseconds) are examples, not a
  calibrated instrument or a claim of absolute accuracy.
- AP sigma is one-sigma uncertainty in **each** independent north/east axis,
  in NM. The calculation differentiates the angular contact residual with
  respect to those axes, then divides the combined angular uncertainty by
  the **normal contact rate**, not just the Moon's total sky motion.
- Combined local linear sigma includes timing, limb/catalog and position.
  It is also shown as clock-only longitude uncertainty in NM at the AP
  latitude and at the equator (seconds × 0.25 × cos(latitude)). This is not
  a position fix accuracy. The linear budget is unreliable near tangency;
  near-limb, slow, low-altitude and catalogue double-star cases are flagged.
  Unknown DUT1, unmodelled terrain, wrong identification, unresolved components,
  reaction bias and other systematic errors may exceed the assumed budget.

Recover UTC never changes the original watch input. Save/load `.occ` preserves
raw inputs and model parameters, without saving a derived answer as an observation.
Export report provides an audit of the result. After reviewing the contact/AP and
uncertainty, tick the review box and Prepare Chrono comparison. Select a named
clock first on Chrono. This prepares fields only: **Add comparison** is still
required to save it. The paired reference UTC includes the reaction delay so the
original delayed watch reading is preserved and the correction is applied once.
Sight reduction can then use the existing derived Chrono correction option.

## Astronomy and catalogue provenance

The core uses the selected **Precise** engine, NOVAS C3.1 full accuracy and the
bundled JPL DE440s ephemeris. Classic-only builds reject this feature politely.
NOVAS computes time-dependent topocentric Moon and star places, including light
time, aberration, precession, nutation, proper motion, and lunar parallax for the
observer's location/elevation. Contacts satisfy angular separation = topocentric
semidiameter for a **1737.4 km mean lunar sphere**. Candidate screening uses
10-minute lunar samples and an enlarged 0.8-degree cone. Exact NOVAS geometry is
minimized and each crossing bracketed/refined, so a short near-tangent event
between samples is not lost merely for having no sampled point inside the disk.
The planner pads the interval by six hours and filters the actual contact epochs.

At a physical stellar contact the incoming star direction coincides with the
occulting limb. A common, locally invertible atmospheric refraction transformation
preserves this coincidence. Thus no sextant Hs, refraction/dip/IE corrections or
synthetic altitudes are required to clear a distance. Visibility altitudes are
geometric. Near the horizon atmospheric image quality and detectability still
matter; new searches default to at least 5 degrees altitude.

All **8,881 valid J2000 records** from the unchanged XEphem `SKY2k65.edb` are
compiled into the executable, with exact aliases and catalogue double-star flags.
One malformed upstream record (`V15956`, displaced/missing fields) is excluded
and printed by `python3 tools/mkoccult.py`; no guessed astrometry is substituted.
Proper motions use mu-alpha cos(dec) as in the source. Missing parallax/radial
velocity are zero; this historical catalogue is not Gaia-quality astrometry.
Double-star flags are useful warnings, not a complete component/orbit model.
See [catalogue provenance and license](data/sky/README.md),
[PROVENANCE.md](PROVENANCE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Generated `navoccult_data.inc` is reproducible without downloading anything.

No lunar terrain profile, libration-dependent limb map, diffraction light curve,
planetary disk model or asteroid orbital catalogue is included. In particular,
this version ranks near-grazing geometry but does not predict the sequence of
mountain/valley contacts. A small root-finding tolerance and fractional-second
printing do not establish matching physical accuracy. Use independently checked
predictions/observations for critical purposes; see [SAFETY.md](SAFETY.md).

## Command line

```sh
./navalm occultations --date 2026-03-27 --lat 39.8 --lon -105 \
  --days 1 --magnitude 4 --sort time
./navalm occultations --date 2026-11-26 --lat 34.0216667 --lon -117.3133333 \
  --days 7 --sort grazing --filter grazing
./navalm occultation --star 'HD 62345' --near 2026-03-27T04:20:00Z \
  --contact D --watch 2026-03-27T04:04:17.762 \
  --lat 39.8 --lon -105 --reaction .25
./navalm occultation --help
```

The final worked example deliberately gives a raw watch slow by 60 seconds,
with a 0.25-second reaction delay. It is not a real observation. Decimal latitude
is north-positive; longitude east-positive. Input validation rejects malformed
calendar dates, nonfinite numbers, unsupported dates and missing stars/contacts.
No command changes saved sights or Chrono logs.

## Validation and reference material

`make check-occult` exercises the core and actual CLI, including clock sign,
reaction delay, D/R branches, DUT1, position/limb uncertainty, sorting, interval
edges, a short near-tangent event, near misses, cancellation and invalid input.
`./navalm-fltk --occult-self-test` exercises native fields, sorting/filtering,
input persistence and Chrono callbacks using isolated test state.

An independent published comparison is IOTA/RASC's 2026 kappa Geminorum
(ZC 1170) disappearance at Denver (39.8 N, 105 W): March 27 **04:05.3 UT**, rounded
to 0.1 minute. NavAlm with the mean sphere and DUT1=0 gives **04:05:17.5 UTC**.
Agreement with this rounded check is not validation of lunar terrain or of
subsecond absolute accuracy.

Primary references:

- [IOTA/RASC 2026 lunar occultations and worked Denver/Boulder example](https://occultations.org/publications/rasc/2026/lunar26.pdf).
- [IOTA 2026 grazing predictions and terrain-dependent limits](https://occultations.org/publications/rasc/2026/nam26grz.pdf).
- [USNO NOVAS software](https://aa.usno.navy.mil/software/index?file=novas_info);
  the unmodified C3.1 routine documentation is also vendored under `novas/`.
- [Universidad Autonoma de Madrid lunar occultation predictions](https://astroserver.ftmc.uam.es/lunar_occultations/lunar_occultations.html),
  which describes mean-sphere timings and departures due to lunar relief.
