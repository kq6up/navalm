CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -DNAV_ENABLE_MOON -DNAV_ENABLE_PLANETS -DNAV_ENABLE_STARS
LDLIBS ?= -lm

# The ncurses interface (./navalm --gui) is built by default.
# If curses is unavailable:  make nogui
GUI_CFLAGS = -DNAVALM_GUI
GUI_SRC    = navgui.c
GUI_LIBS   = -lncurses

SRC = navalm.c navsight.c navcmd.c navrc.c navweb.c nav_engine_split.c moon_data.c planet_data.c star_data.c
HDR = nav_engine.h navsight.h navrc.h navweb_page.h moon_data.h planet_data.h star_data.h

all: navalm

navalm: $(SRC) $(GUI_SRC) $(HDR)
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -o $@ $(SRC) $(GUI_SRC) $(LDLIBS) $(GUI_LIBS)

nogui: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o navalm $(SRC) $(LDLIBS)

page: page.html
	./mkpage.sh

clean:
	rm -f navalm
