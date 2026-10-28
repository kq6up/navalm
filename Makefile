CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lm
CXX ?= c++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
# Keep the native macOS bundle preference; use distribution FLTK on Linux so
# stale/copied .deps libraries cannot override the installed system package.
# An explicit FLTK_CONFIG (command line or environment) always takes priority.
ifeq ($(shell uname -s),Darwin)
FLTK_CONFIG ?= $(if $(wildcard .deps/fltk/bin/fltk-config),.deps/fltk/bin/fltk-config,fltk-config)
else
FLTK_CONFIG ?= $(if $(shell command -v fltk-config 2>/dev/null),fltk-config,$(if $(wildcard .deps/fltk/bin/fltk-config),.deps/fltk/bin/fltk-config,fltk-config))
endif

# The ncurses interface (./navalm --gui) is built by default.
# If curses is unavailable:  make nogui
GUI_CFLAGS = -DNAVALM_GUI
GUI_SRC    = navgui.c
GUI_LIBS   = -lncurses

SRC = navalm.c navsight.c navcmd.c navrc.c navweb.c navsky.c \
      nav_engine_sel.c nav_engine_x.c nav_engine_n.c \
      nav_solsys440.c nav_eph440.c nav_stars_x.c
HDR = navfile.h nav_notice.h nav_version.h nav_engine.h nav_engine_impl.h nav_stars_x.h \
      navsight.h navrc.h navsky.h navsky_data.inc navweb_page.h navweb_book.h air_daily.inc nav_eph440.h nav_solsys440.h

# NOVAS C3.1, U.S. Naval Observatory, vendored unmodified under novas/.
# solsys1.c, solsys2.c, solsys3.c and eph_manager.c are deliberately NOT
# built: nav_solsys440.c supplies solarsystem() and solarsystem_hp() from
# the JPL DE440s coefficients instead.  See THIRD_PARTY_NOTICES.md.
NOVAS_SRC = novas/novas.c novas/novascon.c novas/nutation.c novas/readeph0.c
NOVAS_OBJ = $(NOVAS_SRC:.c=.o)
# NOVAS and libastro both export nutation() and refract().  Renaming NOVAS'
# two at compile time lets both vendored trees stay byte-identical to their
# upstream releases.  Code that wants NOVAS' versions asks for
# novas_nutation() / novas_refract(); nav_engine_x.c keeps libastro's.
NOVAS_CFLAGS = -O2 -w -std=c99 -Inovas \
               -Dnutation=novas_nutation -Drefract=novas_refract

# XEphem libastro, vendored under libastro/ (MIT, see libastro/LICENSE.XEphem)
# deltat.c is excluded: nav_engine_sel.c supplies deltat() from the leap-second
# table, which is exact for modern dates (libastro's own table stops in the 2010s).
ASTRO_SRC = $(filter-out libastro/deltat.c,$(wildcard libastro/*.c))
ASTRO_OBJ = $(ASTRO_SRC:.c=.o)
ASTRO_CFLAGS = -O2 -w -std=c99 -Ilibastro

INCLUDES = -I. -Inovas

# The ephemeris the precise engine reads.  de440s.bsp is a build input, not
# part of the repository; fetch it once with "make ephem-fetch".
EPHEM_BSP ?= ephem/de440s.bsp
EPHEM_NAV ?= ephem/de440s.nav
EPHEM_URL  = https://ssd.jpl.nasa.gov/ftp/eph/planets/bsp/de440s.bsp

all: navalm

# Optional native desktop. C objects are shared directly, without a subprocess
# or a web server. The normal CLI build remains independent of FLTK.
DESKTOP_SRC = navsight.c navrc.c navsky.c nav_engine_sel.c nav_engine_x.c nav_engine_n.c \
              nav_solsys440.c nav_eph440.c nav_stars_x.c
DESKTOP_OBJ = $(addprefix .build/desktop/,$(DESKTOP_SRC:.c=.o))

.build/desktop/%.o: %.c $(HDR)
	@mkdir -p .build/desktop
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

navalm-fltk: navfltk.cpp navfltk_sky.h navfltk_model.h navfltk_plot.h navfltk_sheet.h navfltk_map.h navplatform.h navdownload.cpp navdownload.h navbook.cpp navbook.h navbook_lm_metrics.h $(DESKTOP_OBJ) libastro.a novas.a
	@command -v $(FLTK_CONFIG) >/dev/null || { echo 'FLTK missing: run make fltk-deps, or set FLTK_CONFIG.'; exit 1; }
	$(CXX) $(CXXFLAGS) -I. `$(FLTK_CONFIG) --cxxflags` -o $@ navfltk.cpp navdownload.cpp navbook.cpp \
	  $(DESKTOP_OBJ) libastro.a novas.a `$(FLTK_CONFIG) --ldflags`

desktop: navalm-fltk

navalm-book: navbook_main.cpp navbook.cpp navbook.h navbook_lm_metrics.h $(DESKTOP_OBJ) libastro.a novas.a
	$(CXX) $(CXXFLAGS) -I. -o $@ navbook_main.cpp navbook.cpp $(DESKTOP_OBJ) libastro.a novas.a $(LDLIBS)

.build/annual-book-check: tests/annual_book.cpp navbook.cpp navbook.h navbook_lm_metrics.h $(DESKTOP_OBJ) libastro.a novas.a
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/annual_book.cpp navbook.cpp $(DESKTOP_OBJ) libastro.a novas.a $(LDLIBS)

check-book: .build/annual-book-check
	./.build/annual-book-check

fltk-deps:
	sh tools/bootstrap-fltk.sh

desktop-app: navalm-fltk
	sh tools/package-desktop.sh

.build/desktop-core: tests/desktop_core.cpp tests/desktop_map.h navfltk_model.h navfltk_plot.h navfltk_sheet.h navfltk_map.h navplatform.h navdownload.cpp navdownload.h $(DESKTOP_OBJ) libastro.a novas.a
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/desktop_core.cpp $(DESKTOP_OBJ) libastro.a novas.a $(LDLIBS)

check-desktop: .build/desktop-core
	./.build/desktop-core
	node tests/desktop_sheet.js

# Launch this bundle through Finder/Launch Services for native callback checks.
# Results appear in .build/desktop-test/ui-checks.txt and ui-errors.txt.
desktop-checks-app: desktop-app
	rm -rf 'NAVALM Checks.app'
	cp -R NAVALM.app 'NAVALM Checks.app'
	mv 'NAVALM Checks.app/Contents/MacOS/navalm-fltk' 'NAVALM Checks.app/Contents/MacOS/navalm-checks'
	/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable navalm-checks' 'NAVALM Checks.app/Contents/Info.plist'
	/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier org.navalm.desktop.checks' 'NAVALM Checks.app/Contents/Info.plist'
	/usr/libexec/PlistBuddy -c 'Set :CFBundleName NAVALM Checks' 'NAVALM Checks.app/Contents/Info.plist'
	codesign --force --deep --sign - 'NAVALM Checks.app'

libastro/%.o: libastro/%.c
	$(CC) $(ASTRO_CFLAGS) -c -o $@ $<

libastro.a: $(ASTRO_OBJ)
	ar rcs $@ $(ASTRO_OBJ)

novas/%.o: novas/%.c
	$(CC) $(NOVAS_CFLAGS) -c -o $@ $<

novas.a: $(NOVAS_OBJ)
	ar rcs $@ $(NOVAS_OBJ)

navalm: $(SRC) $(GUI_SRC) $(HDR) libastro.a novas.a navalm-book
	$(CC) $(CFLAGS) $(INCLUDES) $(GUI_CFLAGS) -o $@ $(SRC) $(GUI_SRC) \
	      libastro.a novas.a $(LDLIBS) $(GUI_LIBS)

nogui: $(SRC) $(HDR) libastro.a novas.a navalm-book
	$(CC) $(CFLAGS) $(INCLUDES) -o navalm $(SRC) libastro.a novas.a $(LDLIBS)

# Build without the precision engine: no NOVAS, no ephemeris file, the
# XEphem engine only.  Useful where the data file cannot be carried.
classic-only: $(filter-out nav_engine_n.c nav_solsys440.c nav_eph440.c,$(SRC)) $(GUI_SRC) $(HDR) libastro.a
	$(CC) $(CFLAGS) -I. -DNAVALM_NO_PRECISE $(GUI_CFLAGS) -o navalm \
	      $(filter-out nav_engine_n.c nav_solsys440.c nav_eph440.c,$(SRC)) \
	      $(GUI_SRC) libastro.a $(LDLIBS) $(GUI_LIBS)

# ---- ephemeris -----------------------------------------------------------

tools/mkde440: tools/mkde440.c nav_eph440.c nav_eph440.h
	$(CC) $(CFLAGS) -I. -o $@ tools/mkde440.c nav_eph440.c

ephem: tools/mkde440
	@test -f $(EPHEM_BSP) || { \
	  echo "$(EPHEM_BSP) not found."; \
	  echo "Fetch it once with:  make ephem-fetch"; \
	  echo "or download $(EPHEM_URL)"; \
	  exit 1; }
	./tools/mkde440 $(EPHEM_BSP) $(EPHEM_NAV)

ephem-fetch:
	@mkdir -p ephem
	curl -L -o $(EPHEM_BSP) $(EPHEM_URL)
	@echo "Now run: make ephem"

# ---- generated sources ---------------------------------------------------

stars: navstars.edb tools/mknavstars.sh
	{ echo '/* generated by tools/mknavstars.sh from navstars.edb - do not edit */'; \
	  echo '/* XEphem catalog data: see libastro/LICENSE.XEphem and THIRD_PARTY_NOTICES.md. */'; \
	  echo '#include "nav_stars_x.h"'; echo; \
	  echo 'const NavStarCat nav_star_cat[] = {'; \
	  ./tools/mknavstars.sh navstars.edb; \
	  echo '};'; \
	  echo 'const size_t nav_star_cat_count = sizeof nav_star_cat / sizeof nav_star_cat[0];'; \
	} > nav_stars_x.c

page: page.html
	./mkpage.sh

clean:
	rm -f navalm libastro.a novas.a $(ASTRO_OBJ) $(NOVAS_OBJ) tools/mkde440 tests/paper_almanac
	rm -f navalm-fltk navalm-book
	rm -rf .build

tests/paper_almanac: tests/paper_almanac.c $(SRC) $(HDR) libastro.a novas.a
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ tests/paper_almanac.c navsight.c \
	      nav_engine_sel.c nav_engine_x.c nav_engine_n.c nav_solsys440.c \
	      nav_eph440.c nav_stars_x.c libastro.a novas.a $(LDLIBS)

check-paper: tests/paper_almanac
	./tests/paper_almanac

# Node.js is needed only for this browser-geometry regression check.
check-plot:
	node tests/plotting_sheet.js

check-list:
	node tests/sight_list.js

check-direct-dr: navalm
	python3 tests/direct_dr.py

# Real HTTP integration with isolated HOME; needs the normal DE440s ephemeris.
check-web-book: navalm
	python3 tests/web_book.py
	node tests/web_book_ui.js

.build/sky-check: tests/sky.c navsky.c navsky.h $(DESKTOP_OBJ) libastro.a novas.a
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ tests/sky.c $(DESKTOP_OBJ) libastro.a novas.a $(LDLIBS)

check-sky: .build/sky-check navalm
	./.build/sky-check
	python3 tests/sky_web.py
	node tests/sky_ui.js

# Removes the generated ephemeris too, but never the downloaded .bsp.
distclean: clean
	rm -f $(EPHEM_NAV)

.PHONY: all nogui classic-only ephem ephem-fetch stars page clean distclean check-paper check-plot check-list
.PHONY: desktop fltk-deps desktop-app check-desktop desktop-checks-app check-book check-web-book check-sky check-direct-dr
