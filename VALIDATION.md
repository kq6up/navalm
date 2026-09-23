# NAVALM 3.0c publication validation

Checks performed on 23 September 2026 against the supplied 3.0c archive,
with the documentation, notices, and two catalog selections updated.

- `make page`, `make stars`, and `make nogui`: passed.
- CLI version: `navalm version 3.0c`.
- Almanac smoke checks at 2026-09-23 12:00:00 UTC: Sun, Moon, Mercury,
  Venus, Mars, Jupiter, Saturn, and Aries returned finite output.
- All 58 named stars returned output; names are unique.
- Atria now reports southern declination near 69 degrees; Suhail near
  43.5 degrees south. Their catalog records are Alpha Trianguli Australis
  (HR 6217) and Lambda Velorum (HR 3634), respectively.
- All 58 selected catalog records, after removing the prepended
  navigation name, match records in the cited XEphem SKY2k65.edb.
- Plain text, HTML, and LaTeX daily tables were generated and checked for
  the safety notice and corrected star names. LaTeX was not rendered
  to PDF in this review.
- Artificial-horizon Sun sight reduction: passed using the README
  example inputs.
- Local HTTP server: home page contained the warning, complete safety
  terms, and acknowledgment; /api/version returned 3.0c.
- `--notice` reproduces the full SAFETY.md body.
- Regenerating navweb_page.h and nav_stars_x.c produces identical bytes.
- All 66 vendored libastro C/header files match the upstream Git hashes
  documented in PROVENANCE.md; original notices are retained.
- No legacy engine/data files or compiled objects are included in the
  publication tree.

The ncurses build was attempted but this environment has no ncurses.h;
the full-screen interface was not compiled or interactively tested here.
The CLI and HTTP checks used the no-ncurses build.

These are build, consistency, and smoke checks. They are not an
independent astronomical accuracy validation, a safety certification,
or a guarantee that the program is free of defects. Prior claims of
specific agreement with 2.8g were removed because this publication
review did not reproduce them.
