# Defaults and planning-page handoff (5.5.0)

The native **Defaults** tab and browser **Defaults** page hold DR, index error
and on/off arc, horizon type, eye height/units, preferred limb, temperature,
pressure, reduction method, vessel course/speed and current set/drift.
A new installation starts with **Sea horizon**. Choosing and saving artificial
horizon keeps that preference for later sessions. UTC starts at the current
time; Hs is entered for each observation.

In the native app, **Save defaults** on Sight reduction copies the current sight
settings without requiring Hs. On Defaults, **Use sight settings** copies them
into the defaults form; choose **Save defaults** to keep them. Saving defaults
does not rewrite an observation already being edited. **New / defaults** starts
a fresh sight. Native settings remain in the existing local defaults RC file.
Browser settings remain in that browser's local storage, separate from native
and console settings.

Sight reduction, Almanac and Planetarium load the saved DR at startup, including
explicit 0 latitude or longitude. Almanac and Planetarium have saved-DR and
current-sight-DR controls; Defaults can load saved DR into all three pages.
These controls copy coordinates and hemispheres, leaving UTC and other page
entries alone. Recalculate after changing DR. Planning-page edits stay local to
that page until explicitly copied or saved.

**Use object for a sight** on Almanac validates the current object, UTC and DR,
then opens a new observation with those values and blank Hs. Sun/Moon keep your
preferred limb; other objects use their centre. Instrument/atmosphere settings
come from saved defaults and the current reduction-method choice is retained.
Aries is a reference point, so it cannot be used as an observed body. The button
never saves a sight automatically. Planetarium handoff follows the same pattern.

DR is the observer's estimated position. With **Reduce at DR** checked, the
calculation position is that exact DR. With the box unchecked, the reducer
calculates a whole-degree LHA assumed position for that particular object and
time. A computed AP is not stored as a general position default. Course/speed
and set/drift advance LOPs to a running-fix time; they do not silently move a
page's entered DR. Set is the direction the current flows toward.

## Dip short of the horizon

Choose **Short of horizon**, enter eye height and the **distance in nautical
miles to the nearer waterline** used as the altitude reference. The reducer uses
both values for one combined dip correction. This replaces ordinary sea-horizon
dip; it is not added to it. Artificial horizon instead halves Hs after index
correction and applies no dip.

The formula is from [NGA Bowditch, American Practical Navigator (2019), Volume II,
Table 14 and its explanation](https://msi.nga.mil/api/publications/download?key=16693975%2FSFH00000%2FBowditch_Vol_2_LoRes_2019.pdf):

```
dip_arcmin = 60 * degrees(atan(h_ft / (6076.1 * d_nm)
                             + 0.8321 * d_nm / (2 * 3440.1)))
```

The 0.8321 factor represents standard terrestrial refraction. Eye height is
converted from metres when needed. For example, 10 ft and 0.5 NM give about
11.5 arcminutes of dip. Distance must be positive and no greater than the visible
horizon implied by the same height/refraction model; otherwise choose Sea
horizon. The model does not compensate for unusual refraction or an incorrectly
identified waterline. Existing sights with no distance retain ordinary dip.

CLI: add `--short-horizon NM` with `--he HEIGHT ft|m` on a sight or LOP edit.
`--sea` and `--ah` clear the short-horizon setting. Saved sights retain the
waterline distance and reapply it when reopened. The HTTP reduction/fix parameter
is `short_horizon` (NM); it defaults to 0, meaning ordinary sea-horizon dip.
The RC defaults file also accepts `short_horizon=NM` and `at_dr=0|1`.
