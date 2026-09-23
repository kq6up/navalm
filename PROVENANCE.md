# NAVALM provenance and astronomical methods

NAVALM's astronomical engine is a C adaptation derived from the available
NAV48/NAV50 RPL implementation. Chris Maness reports using AI-assisted
translation of that RPL as a development input. The source comparison below
supports that engineering lineage. It is not a judicial determination of
copyright infringement or of which individual elements are copyrightable.

## Credits

- **Dr. Thomas R. Metcalf:** original NAV48 celestial-navigation software.
- **Sparcom Corporation:** original distributor; the 1992 manual asserts
  copyright in the manual and accompanying software.
- **Eddie C. Dost:** HP49/50 port and preservation of the family permission.
- **Olivier M.P. Coignard:** NAV50 updates and the supplied readable
  `SPARCOM.hpprg` source.
- **Jean Meeus:** published astronomical methods used in this lineage.
- **P. Bretagnon and G. Francou:** VSOP87 planetary theory cited by the
  original manual.
- **M. Chapront-Touzé and J. Chapront:** ELP lunar theory underlying the
  truncated lunar method presented by Meeus.
- **Chris Maness:** NAVALM project, C adaptation, and subsequent integration.

The supplied NAV50 README describes extraction of the HP49/50 library into
218 subprograms before modification. This supports the recovered-library
history; it does not identify every intermediate reverse-engineering step.
Andrés Capdevila's separate HP50g patch exists, but was not established as
an input to this project's source.

## Material examined

Audit date: 23 September 2026.

| Supplied archive | Contents relevant to provenance | SHA-256 |
| --- | --- | --- |
| `nav50(2).zip` | `SPARCOM.hpprg`, compiled program, README PDF, star data | `62e42f964786756b682f737898b14491917689ac9c5d7eb71f207d28e39f097a` |
| `nav-251(1).zip` | ROM dump, HP49/50 library, original manual, permission email | `ce0ab414f049b89e2283c2981dffce9621bb1e4328a372b92de40b75c7250958` |
| `nav48_l.zip` | Metcalf's Tricks and Shortcuts supplement and custom permission | `6f4213a422daa1297aacafb6014662ad8b34a1f000f466b5944a1ce617bc13a7` |

The first two archives match the corresponding HPCALC downloads byte for
byte. The comparison used NAVALM's GitHub source, verified against the
local source archive using Git blob hashes:

| File | Git blob SHA |
| --- | --- |
| `nav_engine_split.c` | `858b60aff1136268af2405f664a9c5b1a93dc9ac` |
| `moon_data.c` | `8286759f8c99ef62e002afcf9a82873f25f24ca9` |
| `planet_data.c` | `7f19bc6d1bb34b9359b54eef8039fd67cd0cca12` |

## Comparison results

The lunar table comparison parsed coefficient/multiplier tuples from the
C arrays and the RPL lists and compared every tuple in sequence.

| NAVALM table | RPL source | Rows | Result |
| --- | --- | ---: | --- |
| `moon_lon` | `MOONGD` longitude list | 59 | Exact numeric match, same order |
| `moon_lat` | `MOONGD` latitude list | 60 | Exact numeric match, same order |
| `moon_dist` | `MHP` distance list | 46 | Exact numeric match, same order |

The solar/planetary comparison preserved all coordinate and polynomial-order
groups in the RPL lists. All 765 amplitude/phase/frequency triples match
the C arrays in order and grouping, allowing only numeric-format rounding
with tolerance `max(1e-12, abs(RPL value) * 2e-15)`.

| RPL data list | C series | Triples |
| --- | --- | ---: |
| `sundata` | `nav_sun` | 28 |
| `eardata` | `nav_ear` | 96 |
| `vendata` | `nav_ven` | 82 |
| `mardata` | `nav_mar` | 155 |
| `jupdata` | `nav_jup` | 155 |
| `satdata` | `nav_sat` | 169 |
| `merdata` | `nav_mer` | 80 |

Manual inspection also found these correspondences:

| RPL routine | C routine(s) | Correspondence |
| --- | --- | --- |
| `X09B` | `margs` | Same lunar argument polynomials, including the rounded coefficients, auxiliary arguments, and eccentricity expression |
| `MOONGD`, `X0D2`, `MHP` | `moon_tt`, `msum` | Corresponding lunar series evaluation, eccentricity weighting, and additive corrections |
| `X04F` | `nut`, `addnut` | Same compact nutation approximation and conversion sequence |
| `X050` | `aberr` | Corresponding aberration correction through ecliptic coordinates |
| `X047`, `X051` | `precess` | Corresponding precession polynomials and matrix entries, specialized in C to a J2000 starting epoch |
| `DOPLANET`, `X08C` | `eplanet`, `ecoord` | Corresponding coordinate series evaluation |
| `X08A` | `planet_app` | Corresponding Earth subtraction and one-step light-time adjustment |

These are correspondences, not a claim of identical whole-program behavior.
For example, the C engine has a different time-scale implementation,
including a leap-second table, and different interfaces and error handling.
The audit did not establish the origin of every UI, sight-reduction, or
position-fix routine.

## Meeus and the distinction between mathematics and implementation

The Moon routine follows the truncated ELP-2000/82 family presented in
Meeus's *Astronomical Algorithms*. The original manual's bibliography,
printed page I-165 (PDF page 88), explicitly cites:

- Jean Meeus, *Astronomical Algorithms*, Willmann-Bell, 1991.
- Jean Meeus, *Astronomical Formulae for Calculators*, Willmann-Bell, 1988.
- P. Bretagnon and G. Francou, “Planetary Theories in Rectangular and
  Spherical Variables. VSOP87 Solutions,” *Astronomy and Astrophysics*
  202, 309–315, 1988.

A common scientific method explains many matching equations and constants.
Those matches alone do not prove copying of protected expression. Here,
the developer's reported use of RPL as translation input, the complete
matching selected data sets, and the routine correspondences support
attribution as an adaptation of NAV48/NAV50 rather than a claim of an
independently developed implementation.

Copyright in software concerns protectable expression, not mathematical
algorithms themselves. Whether any particular translated portion carries
upstream copyright requires analysis beyond numerical matching. This
document therefore records technical provenance without claiming that
every matching formula or number is owned by Metcalf.

## Permissions

See [LICENSE](LICENSE) for the custom grant limited to Chris Maness's
rights, preserved original permission statements, and the separate scope
of each upstream notice. Neither a public-domain dedication nor GPL
licensing of the upstream software has been established.

## References

- [Original port and ROM archive](https://www.hpcalc.org/details/7082)
- [NAV50 source archive](https://www.hpcalc.org/details/9643)
- [Original manual](https://literature.hpcalc.org/community/pp-celestial.pdf)
- [Independent Meeus/ELP implementation and description](https://www.celestialprogramming.com/meeus-elp82.html)
- [US Copyright Office Circular 61](https://www.copyright.gov/circs/circ61.pdf)
