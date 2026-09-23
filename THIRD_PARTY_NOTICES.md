# Third-party notices

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

## Navigation-star data

navstars.edb is a selection from XEphem's SKY2k65.edb; nav_stars_x.c is
generated from it. The XEphem distribution's MIT notice is retained.

The source catalog credits SKY2000 Master Catalog Version 4 (2002):
J. R. Myers, C. B. Sande, A. C. Miller, W. H. Warren Jr., and D. A.
Tracewell, Goddard Space Flight Center, Flight Dynamics Division.
XEphem's edition limits entries to magnitude 6.5 and adds common names.
NAVALM selects 58 entries and prepends navigation names.

See [PROVENANCE.md](PROVENANCE.md) for exact source references and the
Atria and Suhail selection corrections.

## Distribution

Include LICENSE, libastro/LICENSE.XEphem, THIRD_PARTY_NOTICES.md,
SAFETY.md, and PROVENANCE.md with distributed source or accompanying
binary documentation. Preserve existing third-party source notices.
The MIT copyright and permission notices must accompany copies or
substantial portions of the corresponding software.

Metcalf and NAV48/NAV50 are acknowledged as historical inspiration in
PROVENANCE.md. Their legacy permission statements are not presented as
the license of XEphem or of this replacement source tree.
