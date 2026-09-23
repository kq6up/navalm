#ifndef PLANET_DATA_H
#define PLANET_DATA_H
#include <stddef.h>
typedef struct { double a,b,c; } NavTerm;
typedef struct { const NavTerm *term; size_t n; } NavSeries;
extern const NavSeries nav_sun[3][6];
extern const NavSeries nav_ear[3][6];
extern const NavSeries nav_ven[3][6];
extern const NavSeries nav_mar[3][6];
extern const NavSeries nav_jup[3][6];
extern const NavSeries nav_sat[3][6];
extern const NavSeries nav_mer[3][6];
#endif
