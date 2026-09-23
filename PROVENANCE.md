# NAVALM 3.0c provenance and acknowledgments

## Current implementation

NAVALM 3.0c replaces the former NAV89/NAV48/NAV50-derived astronomical
engine and its data tables with XEphem's libastro and a star catalog
selected from XEphem's SKY2k65.edb. The Metcalf-derived engine and tables
are no longer included in the current source tree or build.

| Removed legacy files | Replacement |
| --- | --- |
| nav_engine_split.c | nav_engine_x.c and libastro/ |
| moon_data.c, moon_data.h | XEphem libastro/moon.c |
| planet_data.c, planet_data.h | XEphem's solar and planetary routines and tables |
| star_data.c, star_data.h | navstars.edb and generated nav_stars_x.c/.h |

Chris Maness supplied the 3.0c replacement and reports that the
Metcalf-derived code was removed. The publication review on 23 September
2026 confirmed the seven legacy files are absent and the Makefile uses
the replacement. Existing command handling, interfaces, sight reduction,
and storage code remain; this review is not a forensic certification of
the independent origin of every retained line or a clean-room rewrite.
The earlier provenance investigation concerned the removed astronomy
engine, and did not establish Metcalf authorship of those other modules.

## XEphem source

Upstream: https://github.com/XEphem/XEphem

Reference commit: `780234cc4aa98eeb7d96acfd4ba515fa4565fae9`.

All 66 bundled libastro C and header files match the Git blob hashes at
that commit. This identifies a verified matching upstream revision; it
does not claim knowledge of the original download date. The MIT text in
[libastro/LICENSE.XEphem](libastro/LICENSE.XEphem) matches upstream
apart from a trailing blank line. Original source comments and credits
are preserved.

The library includes VSOP87 and Chapront planetary routines, S. L.
Moshier's lunar implementation adapted for XEphem by Michael Sternberg,
and coordinate, nutation, precession, and apparent-place calculations.
The uploaded description of the Moon routine as "full ELP2000-82B" has
been corrected to reflect the actual bundled implementation. No
particular accuracy is guaranteed.

NAVALM's adapter selects geocentric apparent coordinates and converts
them to GHA, declination, horizontal parallax, and semidiameter.
Bundled libastro/deltat.c is excluded from the build: nav_engine_x.c
supplies the time-scale calculation using a leap-second table for modern
dates and polynomial estimates for earlier dates. Future leap seconds
require table maintenance; historical and extrapolated results have
additional limitations.

## Star catalog

Source: [XEphem SKY2k65.edb at the reference commit](https://github.com/XEphem/XEphem/blob/780234cc4aa98eeb7d96acfd4ba515fa4565fae9/GUI/xephem/catalogs/SKY2k65.edb).

The underlying catalog is SKY2000 Master Catalog Version 4 (2002),
credited to J. R. Myers, C. B. Sande, A. C. Miller, W. H. Warren Jr.,
and D. A. Tracewell, NASA/Goddard Space Flight Center. XEphem supplies
the magnitude-limited edition and additional common names.

navstars.edb retains 58 source records, with NAVALM's chosen navigation
name prepended. tools/mknavstars.sh converts these records into the C
table; run `make stars` after editing them.

The publication review corrected two ambiguous common-name selections:
Atria now uses Alpha Trianguli Australis (HR 6217), and Suhail uses
Lambda Velorum (HR 3634). The upload had selected Alpha Trianguli and
Canopus, respectively. Catalog fields come from the cited XEphem source.
Acrux and Rigil Kentaurus use individual bright components; results may
differ from almanacs representing a combined pair.

## In memory of Dr. Thomas R. Metcalf

Dr. Thomas R. Metcalf's celestial-navigation software helped make
sophisticated navigation calculations accessible on handheld calculators
and inspired NAVALM's development. We remember his work with gratitude
and respect.

We also acknowledge the preservation and continuation of that legacy:
Sparcom's original distribution, Eddie C. Dost's HP49/50 port,
Olivier M. P. Coignard's NAV50 updates, and the Metcalf family's support
for keeping the work available.

This acknowledgment honors the project's historical inspiration. It is
not an attribution of the current XEphem engine to Metcalf and does not
imply endorsement by his family, Sparcom, the port maintainers, or XEphem.

## License transition and historical versions

The current NAVALM contributions are offered under [MIT](LICENSE),
with XEphem's MIT copyright and permission notice retained separately.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the
[safety and assumption-of-risk notice](SAFETY.md).

The former NAVALM custom noncommercial grant and NAV48/NAV50 permissions
are not the license of this replacement source tree. This update does
not retroactively relicense third-party material in older commits,
archives, or the v2.8g release, which still contain the old engine.
Their source and permission history remain in Git history. Use the
current branch for the replacement implementation; do not treat an old
release archive as MIT merely because today's README says MIT.
