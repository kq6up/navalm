# Independent lunar centre-direction fixture

Retrieved from NASA/JPL Horizons on 2026-11-03. This is a numerical reference
for unrefracted topocentric centre separation, separate from forward/inverse
round trips. It does not independently validate the atmospheric or limb model.

- UTC: 2026-11-03 13:20:00
- WGS84 site: longitude −117 degrees, latitude +34 degrees, elevation 0 km
- Moon (301): azimuth 125.621217007°, airless elevation 53.863377070°
- Jupiter (599): azimuth 130.840614253°, airless elevation 62.171922069°
- Angular separation from these directions: **8.749512888734568°**
- Test tolerance: 0.00005° (0.18 arcsecond), allowing the small differences in
  Earth-orientation/time assumptions and ephemeris realization.

Use the [official Horizons API](https://ssd-api.jpl.nasa.gov/doc/horizons.html)
and its [observer-quantity definitions](https://ssd.jpl.nasa.gov/horizons/manual.html#obsquan).
GET `https://ssd.jpl.nasa.gov/api/horizons.api` with these URL-encoded parameters:

```text
format=json
COMMAND='301'                      # repeat with '599'
EPHEM_TYPE='OBSERVER'
CENTER='coord@399'
COORD_TYPE='GEODETIC'
SITE_COORD='-117,34,0'
START_TIME='2026-11-03 13:20:00'
STOP_TIME='2026-11-03 13:21:00'
STEP_SIZE='1 m'
QUANTITIES='2,4,13'
ANG_FORMAT='DEG'
CSV_FORMAT='YES'
EXTRA_PREC='YES'
APPARENT='AIRLESS'
```

For azimuth A and altitude h, form the unit vector
`(cos(h) cos(A), cos(h) sin(A), sin(h))` and compute the angle using
`atan2(norm(cross(moon,jupiter)), dot(moon,jupiter))`. Centre directions are
independent of the different adopted mean Moon radii in the two systems.
