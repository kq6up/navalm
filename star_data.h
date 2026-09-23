#ifndef STAR_DATA_H
#define STAR_DATA_H
#include <stddef.h>
typedef struct { const char *name; double ra_deg, dec_deg, pm_ra_s_per_century, pm_dec_arcsec_per_century, mag; } NavStarData;
extern const NavStarData nav_stars[];
extern const size_t nav_star_count;
#endif
