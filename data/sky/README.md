# Offline constellation identification data

Upstream: [XEphem](https://github.com/XEphem/XEphem), revision
`780234cc4aa98eeb7d96acfd4ba515fa4565fae9` (the bundled libastro reference).

These two original files are bundled unchanged:

| File | Upstream path | Git blob | SHA256 |
|---|---|---|---|
| classicfigs.csf | GUI/xephem/auxil/classicfigs.csf | `3ed3cfa1dfd7c0688e19182347e0517b76b0d257` | `ced1aa83feeab048088e846b54654de185829de9e36afbfa3351075f3418cba9` |
| SKY2k65.edb | GUI/xephem/catalogs/SKY2k65.edb | `f3de970a7092816933f05503d05117b4d9e7ad99` | `2861b267875624cea5d7e80299b6706c704ffaf4fd20b9b7d22a840d5f08a3f3` |

Copyright (c) 1990-2020 Elwood Charles Downey. Distributed under the XEphem
MIT license: [complete grant and disclaimer](../../libastro/LICENSE.XEphem).
XEphem credits Chris Marriott with the original constellation figures and
Elwood Downey with their incorporation. The underlying stellar data are
SKY2000 Master Catalog Version 4 (2002), with XEphem aliases; see
[PROVENANCE.md](../../PROVENANCE.md).

Run `python3 tools/mksky.py` from the repository to regenerate
`navsky_data.inc`. No download or Python runtime is needed by NAVALM itself.
The generator matches J2000 figure vertices to the catalogue, reuses the 58
navigation stars, and adds 712 display-only supporting vertices. There are
780 segments in 89 figures: all 88 constellations, with Serpens in two parts.
Figure segments are rendered as solid identification guides, including those
marked dashed upstream. These are traditional stick figures, not official
IAU constellation boundaries. Membership text uses libastro's boundary lookup.

One variable star, chi Cygni, is absent from the magnitude-limited catalogue.
Its supporting vertex uses the figure file's approximate J2000 position
(19.8422h, +32.9 degrees), with no proper motion. Its placeholder magnitude
is used only to fit the internal catalogue structure: supporting markers have
fixed size and no magnitude is exposed. It is not a navigation sight target.
All other supporting positions use the same selected astronomy engine and
proper-motion treatment as the navigation catalogue. Lines are sampled on
short great-circle arcs and clipped at the horizon before projection.

Supporting stars are only the vertices needed for these figures, not a complete
faint-star catalogue. They never enter the planner, navigation list or sight
handoff. Below-horizon portions are not drawn. All identification layers are
bundled into the executable and remain usable offline.
