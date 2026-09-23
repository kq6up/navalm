NAVALM 3.0c - console nautical almanac and sight worker

3.0c replaces the former NAV89/NAV48/NAV50-derived astronomical engine and
its data tables with XEphem libastro and a catalog selected from XEphem's
SKY2k65.edb. The removed Metcalf-derived engine and tables are no longer
included. Current NAVALM contributions use MIT (LICENSE); XEphem retains
its own MIT notice (libastro/LICENSE.XEphem). See PROVENANCE.md for the
review scope and historical acknowledgment of Dr. Thomas R. Metcalf.

INFORMATIONAL AND EDUCATIONAL USE ONLY. Do not rely on NAVALM where an
error could cause death, injury, or property damage. By using it, you
accept the safety and assumption-of-risk terms in SAFETY.md, to the
maximum extent permitted by law. No express or implied warranty is given.

It is for Linux/Unix console use and does not require GCC4TI.

Build:
  make page
  make

Without ncurses headers/libraries, use make nogui after make page.

Version:
  ./navalm -v

Daily fixed-width table:
  ./navalm -t 2026-09-08
  ./navalm -t 2026 9 8

The -t display is patterned after the traditional Nautical Almanac daily pages:
24 hourly UTC rows for Aries and the navigational planets, a Sun/Moon section,
and a navigation-star SHA/declination table for 00:00 UTC.

Other examples:
  ./navalm moon 2026 9 1 12 0 0
  ./navalm sun 2026 8 29 16 15 53 --ap 34 2.5 N 117 21.9 W
  ./navalm star Sirius 2026 9 3 12 0 0
  ./navalm list-stars

Bodies:
  sun moon mercury venus mars jupiter saturn aries

NAVALM 1.1 additions
--------------------
-t daily tables now include Nautical Almanac interpolation quantities:
  * planet v and d at the foot of each planet column (12:00-13:00 UTC),
  * Moon v and d on every hourly row,
  * Sun d at the foot of the Sun column.
Planet/Sun v is referenced to a 15-degree hourly increment.
Moon v is referenced to 14 degrees 19.0 minutes per hour, matching the traditional increment tables.
d is the absolute hourly change in declination.

NAVALM 1.2 additions
--------------------
Table output formats.
The daily table can be emitted in three formats.  The data, the column layout
and the interpolation quantities are identical in all three; only the markup
differs.

  ./navalm -t  2026-09-08     fixed-width ASCII (unchanged default)
  ./navalm -th 2026-09-08     standalone HTML document
  ./navalm -tx 2026-09-08     standalone LaTeX document

Both new flags accept the same date forms as -t (YYYY-MM-DD or YYYY MM DD)
and write to standard output:

  ./navalm -th 2026-09-08 > almanac.html
  ./navalm -tx 2026-09-08 > almanac.tex && pdflatex almanac.tex

-th writes a complete HTML file with an embedded stylesheet; no external
assets are needed.

-tx writes a complete LaTeX article using only geometry, times and
graphicx.  It is laid out landscape with one section per page (planets,
Sun/Moon, stars), and each table is scaled up to fill its page: the
\navfit macro in the preamble enlarges the boxed table until it reaches
either the text width or the height left free below that page's heading,
whichever binds first.  The aspect ratio is preserved, so column
proportions and the font shape are unchanged; only the scale differs.
The result is a three-page, large-print daily page usable at the chart
table.


NAVALM 2.0 additions
--------------------
2.0 adds the sight workflow: sextant altitude to observed altitude, HO 229
style sight reduction, a stored set of lines of position, and running fixes
with course/speed and set/drift.  Everything from 1.2 is unchanged.

Hs -> Ho and reduction
----------------------
  ./navalm sight sun 2026 9 19 21 38 05 --hs 94 37.2 --ah \
        --ie 0 --temp 30 --press 1013 --dr 34 2.5 N 117 21.9 W

  ./navalm sight star Vega 2026 9 20 3 15 30 --hs 79 51.1 --he 10 ft \
        --dr 34 2.5 N 117 21.9 W --limb center --save --label "evening Vega"

Options:
  --hs DEG MIN          sextant altitude (required)
  --ie MIN [on|off]     index error magnitude and whether it is on or off the
                        arc; on the arc subtracts.  A negative value with no
                        keyword is taken as on the arc.
  --ah                  artificial horizon: halve the reading, no dip
  --he VAL [ft|m]       height of eye for a sea horizon (default feet)
  --limb ll|ul|center   Sun and Moon; center is the artificial-horizon
                        coincidence case
  --temp C  --tempf F   temperature (default 10 C)
  --press MB --inhg IN  pressure (default 1010 mb)
  --dr ...              DR position; without it the sight is corrected but
                        not reduced
  --course DEG --speed KT     vessel motion, used when the LOP is advanced
  --set DEG --drift KT        current, added to the motion
  --label TEXT          a note kept with the sight
  --save                append to the LOP store
  --brief               one-line summary instead of the worked steps

The worked output follows the printed forms: Hs, IC, the halving or the dip,
Ha, R0, the temperature/pressure factor f, R, SD, parallax, Ho; then the
almanac hourly row with its increment and v/d corrections; then the assumed
position, the HO 229 block (Tab Hc, d, Dec inc, d corr, second-difference
residual, Hc, Z, Zn) and the intercept.

Formulas are the ones in the Nautical Almanac explanation pages:
  dip         0.97' * sqrt(feet)  or  1.76' * sqrt(metres)
  refraction  R0 = 0.0167 deg / tan(Ha + 7.32/(Ha + 4.32))
              f  = 0.28 * P / (T + 273)      R = f * R0
  parallax    asin(sin HP * cos H)
  Moon SD     augmented by (1 + sin HP * sin H)

The LOP store
-------------
  ./navalm lop list                     numbered summary of every stored sight
  ./navalm lop show 3                   the full worked steps again
  ./navalm lop edit 3 --hs 94 37.4      change anything and re-reduce
  ./navalm lop advance 3 --course 270 --speed 6 [--set 90 --drift 1.2]
  ./navalm lop advance 3 --clear        stop advancing that LOP
  ./navalm lop delete 3
  ./navalm lop clear --yes

The store is a plain text file, ~/.naval.save, or whatever NAVALM_SAVE names.
One line per sight, key=value, holding only what the observer supplied; the
reduction is redone every time the sight is used, so an edited sight is
re-reduced rather than patched and the file survives engine changes.

Fixes and running fixes
-----------------------
  ./navalm fix 1 2                      intersection of two LOPs
  ./navalm fix 1 2 3                    least-squares fix with an error ellipse
  ./navalm fix 1 2 3 --at 2026 9 20 3 19 0

Each LOP is advanced from its own time to the fix time (the latest sight, or
--at) using that LOP's course and speed plus any set and drift, exactly as an
LOP is advanced on the plotting sheet.  The output lists each LOP with how far
it was advanced and its residual, then the fix, the RMS residual and, with
three or more LOPs, the 1-sigma error ellipse.

A stationary observer needs no course and speed; the LOPs are simply crossed.


NAVALM 2.8g additions
----------------------
* TUI Almanac GHA and SHA are now displayed in navigational degrees and
  decimal minutes (DDD MM.m') instead of decimal degrees.
* One-body CLI almanac output now uses the same DDD MM.m' convention for
  GHA/SHA and N/S degrees-minutes for declination; HP/SD remain arcminutes.
* Web Almanac single-body, NAVAL and AIR displays were audited to keep
  GHA/SHA/declination in degrees and decimal minutes. Decimal degrees are
  retained only where they are useful as intermediate/diagnostic quantities.
* Fresh web setups now start at the public Agua Mansa reference site; the
  active DR remains shown explicitly on the sight page. Existing browser
  setup values in localStorage are not overwritten.

NAVALM 2.7g additions
----------------------
ChatGPT maintenance build based on 2.6g.

  * The Sight tab now shows the active DR coordinates actually being sent to
    the reducer.  They are labelled as live Setup values so placeholders or
    example coordinates cannot be mistaken for the working DR.
  * The Almanac tab now offers two daily-table views in addition to the
    single-body lookup:
      - NAVAL daily table: whole-hour UTC rows.
      - AIR 10-minute table: 10-minute UTC rows for Aries, Sun, Moon and the
        navigational planets.  The AIR view deliberately shows all four
        navigational planets rather than trying to choose the three used on
        a particular printed Air Almanac page.
  * New web endpoint:
      GET /api/daily?style=naval|air&y=2026&mo=9&d=22
    It returns an HTML table fragment for direct display in the web GUI.
  * CLI setup output now prints DR coordinates in the same suffix-hemisphere
    order accepted by --dr/--ap, e.g. 34 02.5 N 117 21.9 W.

NAVALM 2.5 web interface
-------------------------
A web interface, served by navalm itself.  No Python, no framework, nothing
beyond the C library: navweb.c and one page embedded in the binary.

  ./navalm -d                    http://127.0.0.1:8080/
  ./navalm -d 9000               another port
  ./navalm -d --listen 0.0.0.0   reachable from a phone on your LAN

The server is stateless.  It reduces sights and solves fixes on request and
keeps nothing: the browser holds the sight list and the defaults in its own
localStorage, so the web interface never reads or writes ~/.naval.save or
~/.navalmrc.  Sights export and re-import as JSON from the Sights tab.

Tabs: Sight, Almanac, Sights, Fix, Setup.  The Sight tab recomputes as you type and
shows the worksheet typeset, with real fractions for (Hs + IC)/2, for the
refraction, for the f factor and for the d correction, and the substituted
numbers under each formula.  Star names use the same fuzzy matching as
everywhere else, offered as buttons when more than one fits.  The Fix tab
takes the sights ticked on the Sights tab, advances each from its own time
using its own course, speed, set and drift, and shows the residuals, the
fix, the RMS and the error ellipse, with a button to adopt the fix as the DR.

Endpoints, if you want to drive it from something else:

  GET /api/version
  GET /api/stars?q=pol
  GET /api/almanac?body=sun&y=..&mo=..&d=..&h=..&mi=..&s=..
  GET /api/almanac?body=star&star=Vega&y=..&mo=..&d=..&h=..&mi=..&s=..
  GET /api/daily?style=naval|air&y=..&mo=..&d=..
  GET /api/reduce?body=sun&y=..&mo=..&d=..&h=..&mi=..&s=..&hs=94.62
                 &horizon=ah|sea&he=..&heunit=ft|m&limb=ll|ul|center
                 &ie=..&iearc=on|off&temp=..&press=..&drlat=..&drlon=..
  GET /api/fix?n=3&body0=..&y0=..&...&body1=..&...

Hs and the DR are decimal degrees on the wire; every response is JSON.

Network care: it binds 127.0.0.1 unless --listen says otherwise, caps the
request size and the header count, serves GET only, and never shells out.
There is no TLS and no authentication, so keep it on a trusted network or
reach it through an ssh tunnel.

The page lives in page.html and is embedded as navweb_page.h.  After editing
the page:

  make page && make

NAVALM 2.4 additions
--------------------
Fixes and additions to the 2.1 interface (2.2 and 2.3 were not released
separately; everything below is in 2.4):

  * The DR field accepts many more forms and reports what it cannot read,
    instead of silently ignoring an entry that was not in one exact format
    (see "Entering a DR" below).
  * A fix can be adopted as the DR, either as the Setup default for the next
    sights or as the DR of the LOPs that made it, on the command line and in
    the interface (see "Adopting a fix as the DR" below).
  * Star names match on the first few letters, in any case, and offer a
    numbered choice when more than one star fits (see "Naming a star").

Naming a star
-------------
Star names are matched in three passes: the exact name first, then the first
letters, then anywhere in the name.  Spaces, hyphens, apostrophes and case
are ignored throughout, so "al nair", "Al-Nair" and "alnair" all reach
Al Na'ir.

  ./navalm star polaris 2026 9 20 3 15 30      exact
  ./navalm star polar   2026 9 20 3 15 30      first letters
  ./navalm sight star vega ...                 same in a sight

When several stars fit, the command line lists them and stops:

  $ ./navalm star pol 2026 9 20 3 15 30
  "pol" matches 2 stars:
    1  Polaris
    2  Pollux
  use more letters

In the interface the same list appears with numbers next to it and you press
1, 2 or 3 to choose; ESC cancels.  Above nine matches it asks for another
letter instead of filling the screen.

NAVALM 2.1 additions
--------------------
A full-screen console interface and a setup file.  Every 2.0 command still
works exactly as before; the interface is an extra front end over the same
functions, so the two cannot drift apart.

  ./navalm --gui

Mono ncurses, no colour, numbered menu entries in the style of the TI-89,
function keys to jump between screens and ESC to back out.  It needs a
terminal of at least 72x20 and works fine over ssh.

  MAIN MENU
    1  Almanac        body, date, time -> GHA / Dec / SD / HP
    2  New sight      Hs -> Ho, HO 229 reduction, save as an LOP
    3  LOPs           list, detail, advance, delete, mark for a fix
    4  Fix            cross the marked LOPs
    5  Daily table    the -t / -th / -tx pages
    6  Setup          defaults: IE, horizon, T/P, DR, motion
  F1 Almanac  F2 Sight  F3 LOPs  F5 Table  F6 Setup  F10 Quit

Sight screen: keys 1-0 edit the fields, and the worked sight is recomputed
after every change and shown in the pane beside them, so Hs, the corrections,
the almanac interpolation, the HO 229 block and the intercept are all live.
N sets the time to now, D reloads the setup defaults, S saves the sight as an
LOP, PgUp/PgDn scroll the pane.

LOPs screen: up/down to move, SPACE to mark, ENTER for the full worked detail,
A to set course/speed/set/drift for advancing, D to delete, F to fix the
marked LOPs.  The fix screen shows how far each LOP was advanced, its
residual, the fix, the RMS and the error ellipse.

Daily table: the table is wider than a terminal window, so that screen drops
out of the interface, writes the table to the terminal, and returns when you
press Enter.

Setup defaults
--------------
  ./navalm setup                       show the current defaults
  ./navalm setup --ie 1.2 on --he 10 ft --temp 20 --press 1013 \
        --dr 34 2.5 N 117 21.9 W       change and save them

Defaults live in ~/.navalmrc (override with NAVALMRC).  They seed every new
sight, on the command line and in the interface, so a routine sight needs
only the body, the time and Hs:

  ./navalm sight sun 2026 9 19 21 38 05 --hs 94 37.2 --ah --brief

Any option given on the command line still overrides the default for that
sight.

Why a second file: the LOP store is rewritten whole every time a sight is
saved, so anything else kept in it would be dropped by an older binary.
Keeping defaults in ~/.navalmrc leaves ~/.naval.save a pure LOP log, and both
files stay readable by navalm 2.0.

Entering a DR
-------------
In the interface the DR (key 9 on the sight screen, key 9 in Setup) accepts
any of these, and says so plainly if it cannot read what you typed:

  34 2.5 N 117 21.9 W        34 2.5N 117 21.9W       34.0415 N 117.3645 W
  34.0415 -117.3645           34 02 29.5 N 117 21 52.2 W

Two numbers are decimal degrees, four are degrees and minutes, six are
degrees, minutes and seconds; hemisphere letters may appear anywhere, and
without them the sign is used (north and east positive).

Adopting a fix as the DR
------------------------
  ./navalm fix 1 2 3 --update-dr      the fix becomes the DR in ~/.navalmrc
  ./navalm fix 1 2 3 --update-lops    the fix becomes the DR of those LOPs

In the interface, the fix screen offers the same two:

  U  use this fix as the DR in Setup, for the next sights
  L  use it as the DR of these LOPs and re-reduce them

--update-lops is the second round of the classic procedure: reduce against
the DR, take the fix, then re-reduce the same sights against it so the
assumed positions sit close to the answer.  The sights themselves are
untouched, so this can be repeated and undone by setting the DR back.

Building without ncurses
------------------------
  make          builds with the interface (links -lncurses)
  make nogui    builds everything except --gui

A binary built with "make nogui" reports that the interface was not compiled
in if --gui is given; every other command is unaffected.


Provenance and licensing
------------------------
The former engine and lunar, planetary, and star tables were removed.
See PROVENANCE.md for the replacement map, verified upstream revision,
star-catalog source, and review limitations. XEphem's original source
credits and MIT notice are preserved in libastro/.

The astronomy now uses XEphem's VSOP87/Chapront routines and Moshier lunar
implementation, plus its precession, nutation, and apparent-place code.
The adapter requests geocentric coordinates. libastro/deltat.c is
excluded from the build in favor of NAVALM's leap-second-based modern
TT-UT1 calculation and historical polynomial estimates. The leap-second
table requires maintenance; historical and future results have limits.

The star table is generated from navstars.edb with make stars. Atria and
Suhail selections were corrected before publication to use Alpha
Trianguli Australis and Lambda Velorum. Acrux and Rigil Kentaurus use
individual bright components rather than combined pairs.

No accuracy certification is implied by replacing the engine or passing
build and smoke checks. See VALIDATION.md for the checks actually run.

In memory of Dr. Thomas R. Metcalf: his calculator navigation software
inspired NAVALM. We honor his work and acknowledge Sparcom, Eddie C.
Dost, Olivier M. P. Coignard, and the Metcalf family for preserving and
continuing that legacy. This historical acknowledgment implies no
endorsement and does not attribute XEphem's implementation to him.

Current NAVALM contributions: MIT, see LICENSE.
XEphem: MIT, see libastro/LICENSE.XEphem and THIRD_PARTY_NOTICES.md.
Older releases and Git history retain their applicable original terms;
this update does not retroactively relicense their third-party code.

Safety, warranty, and assumption of risk
---------------------------------------
NAVALM, its documentation, and all generated results are provided for
informational and educational purposes only. They are not certified
navigation products. Do not rely on them for navigation or any decision
where an incorrect result, failure, or delay could cause death, personal
injury, or loss of or damage to property. Independent verification does
not make NAVALM suitable for safety-critical use.

By using NAVALM, you acknowledge this warning and agree, to the maximum
extent permitted by applicable law, to use it entirely at your own risk
and to assume all responsibility and liability for your use, your
interpretation of its results, your decisions, and the resulting
consequences. If you do not accept these terms, do not use NAVALM.

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE SOFTWARE,
DOCUMENTATION, DATA, AND OUTPUT ARE PROVIDED "AS IS" AND "AS AVAILABLE",
WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, ACCURACY,
COMPLETENESS, RELIABILITY, AVAILABILITY, AND NONINFRINGEMENT.

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE AUTHORS, COPYRIGHT
HOLDERS, CONTRIBUTORS, AND DISTRIBUTORS SHALL NOT BE LIABLE FOR ANY CLAIM,
LOSS, DAMAGE, OR OTHER LIABILITY ARISING FROM OR IN CONNECTION WITH
NAVALM OR ITS USE OR INABILITY TO BE USED, WHETHER IN CONTRACT, TORT
(INCLUDING NEGLIGENCE), OR OTHERWISE. THIS INCLUDES DEATH, PERSONAL
INJURY, PROPERTY DAMAGE, NAVIGATION ERRORS, LOSS OF DATA OR PROFITS, AND
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES.

Nothing in this notice excludes or limits liability or rights that cannot
lawfully be excluded or limited. This notice does not promise that a
waiver or disclaimer is enforceable in every jurisdiction or circumstance.

Copyright permissions are granted by LICENSE and the applicable
third-party licenses. This safety and risk notice does not amend the MIT
license, add a copyright field-of-use restriction, or reduce anyone's
rights under the upstream XEphem MIT license.
