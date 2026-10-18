# Third-party notices

NAVALM's own source is MIT (see [LICENSE](LICENSE)). The astronomy components
are MIT or public domain. The optional desktop also links FLTK under its
LGPL license with a static-linking exception; Windows builds include LLVM
and MinGW runtime components under their accompanying notices.

| Component | Status | What NAVALM owes |
|---|---|---|
| XEphem / libastro | MIT | Keep the copyright and permission notice |
| NOVAS C3.1 (USNO) | Public domain, US Government work | Nothing required; acknowledgement requested |
| JPL DE440s ephemeris | Public domain, US Government data | Nothing required; citation customary |
| SKY2000 / navigation-star data | MIT, via XEphem | Keep the XEphem notice |
| FLTK 1.4.5 (optional desktop) | LGPL-2.0-or-later with static-linking exception | Include COPYING and preserve the exception |
| Natural Earth land map (optional desktop) | Public domain | Source credit included in MAPS.md |
| LLVM / MinGW support (Windows desktop) | Apache-2.0 with LLVM exception and runtime-specific notices | Include the runtime notices |

## XEphem / libastro

Copyright (c) 1990-2020 Elwood Charles Downey.

Distributed under the MIT license. The complete original grant and
disclaimer are in [libastro/LICENSE.XEphem](libastro/LICENSE.XEphem);
retain that file with source and binary distributions, together with
applicable source notices. NAVALM's root LICENSE does not replace or
remove the upstream notice.

Source: https://github.com/XEphem/XEphem
Verified matching revision: 780234cc4aa98eeb7d96acfd4ba515fa4565fae9.

Original contributor credits inside the bundled files are preserved.
These include S. L. Moshier and Michael Sternberg for the lunar
implementation, and the Robert W. Berger N3EMO copyright notice in
libastro/earthsat.c. No claim is made that Chris Maness authored the
vendored library or its astronomical theories.

libastro remains in the tree and is still a selectable engine
(`--engine classic`), so this notice continues to apply in full.

## NOVAS C3.1

Naval Observatory Vector Astrometry Software, C Edition, Version C3.1
(March 2011). U.S. Naval Observatory, Astronomical Applications
Department, Washington, DC.

NOVAS is a work of the United States Government and is in the public
domain in the United States. It carries no licence and imposes no
conditions. Its README states: "NOVAS has no licensing requirements. If
you use NOVAS in an application, an acknowledgement of the Astronomical
Applications Department of the U.S. Naval Observatory would be
appropriate."

NAVALM makes that acknowledgement here, in `--notice`, and in the README.

The User's Guide, included as `novas/NOVAS_C3.1_Guide.pdf`, asks to be
cited as:

> Bangert, J., Puatua, W., Kaplan, G., Bartlett, J., Harris, W.,
> Fredericks, A., & Monet, A. 2011, User's Guide to NOVAS Version C3.1
> (Washington, DC: USNO).

### What is vendored, and what is built

The complete NOVAS C3.1 distribution is vendored under `novas/`,
byte-identical to the USNO release, including the User's Guide and the
`checkout-*` validation programs. No NOVAS source file has been edited.

Only four of its files are compiled into NAVALM: `novas.c`,
`novascon.c`, `nutation.c` and `readeph0.c`. In particular
`solsys1.c`, `solsys2.c`, `solsys3.c` and `eph_manager.c` are **not**
built. NAVALM supplies its own `solarsystem()` and `solarsystem_hp()` in
`nav_solsys440.c`, reading JPL DE440s. (NOVAS 3.1's own `eph_manager`
could not read DE440 in any case: `ephem_open` hardcodes record lengths
for DE200, DE403, DE405, DE406 and DE421 and rejects anything else.)

NOVAS and libastro both export `nutation()` and `refract()`. Rather than
patch either vendored tree, the Makefile compiles the NOVAS objects with
`-Dnutation=novas_nutation -Drefract=novas_refract`. Both source trees
therefore remain exactly as their upstream authors released them.

## JPL DE440s planetary and lunar ephemeris

Jet Propulsion Laboratory, California Institute of Technology, under
contract with NASA. DE440s is the short-span edition of DE440, covering
1849 to 2150.

The ephemeris is observational and computational data produced by the US
Government, not software, and is in the public domain in the United
States. It is freely redistributable and is not covered by, nor does it
affect, NAVALM's MIT grant.

Reference:

> Park, R. S., Folkner, W. M., Williams, J. G., & Boggs, D. H. 2021,
> "The JPL Planetary and Lunar Ephemerides DE440 and DE441",
> The Astronomical Journal, 161:105.

Source file, a SPICE SPK kernel:

    https://ssd.jpl.nasa.gov/ftp/eph/planets/bsp/de440s.bsp
    32,726,016 bytes
    SHA-256 c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2

`ephem/de440s.bsp` is a build input and is not part of the repository;
`make ephem-fetch` downloads it. `tools/mkde440` then repacks the
coefficients NAVALM needs into `ephem/de440s.nav`, copying the
double-precision Chebyshev coefficients verbatim. Nothing is refitted,
resampled or rounded, so a position NAVALM reports is computed from the
coefficients JPL published.

No NAIF/SPICE toolkit code is used or included. `tools/mkde440.c` is an
original reader written against NAIF's published format documentation.

If you redistribute `de440s.nav`, this JPL credit should travel with it.

## Navigation-star data

navstars.edb is a selection from XEphem's SKY2k65.edb; nav_stars_x.c is
generated from it. The XEphem distribution's MIT notice is retained.

The source catalog credits SKY2000 Master Catalog Version 4 (2002):
J. R. Myers, C. B. Sande, A. C. Miller, W. H. Warren Jr., and D. A.
Tracewell, Goddard Space Flight Center, Flight Dynamics Division.
XEphem's edition limits entries to magnitude 6.5 and adds common names.
NAVALM selects 58 entries and prepends navigation names.

Both engines read this same catalogue, so star places differ between
them only in the reduction, not in the underlying data.

See [PROVENANCE.md](PROVENANCE.md) for exact source references and the
Atria and Suhail selection corrections.

## Distribution

Include LICENSE, libastro/LICENSE.XEphem, THIRD_PARTY_NOTICES.md,
SAFETY.md, and PROVENANCE.md with distributed source or accompanying
binary documentation. Preserve existing third-party source notices.
The MIT copyright and permission notices must accompany copies or
substantial portions of the corresponding software.

Nothing in NOVAS or the JPL ephemeris requires a notice to be carried,
but the USNO acknowledgement and the JPL citation above are the
customary courtesy and cost nothing to keep.

Metcalf and NAV48/NAV50 are acknowledged as historical inspiration in
PROVENANCE.md. Their legacy permission statements are not presented as
the license of XEphem, of NOVAS, or of this source tree.

## Optional native desktop dependencies

FLTK 1.4.5: https://www.fltk.org/ and https://github.com/fltk/fltk/tree/release-1.4.5
Its LGPL-2.0-or-later license includes an exception for static linking of the
library into applications. The unmodified FLTK COPYING file accompanies native
binary distributions. No FLTK source files are modified by this workbench.

Natural Earth land data are public domain. See MAPS.md for the pinned source,
checksum and geographic limitations. OpenCPN/OSM coastlines remain user-supplied;
they are read locally and are not included in NAVALM's binary packages.

The Windows build uses LLVM-MinGW. Its statically linked libc++, libc++abi,
libunwind and compiler support carry LLVM's Apache-2.0 license with LLVM
exceptions, plus MinGW runtime notices. The portable ZIP includes these notices
in licenses/. Toolchain source: https://github.com/mstorsjo/llvm-mingw
