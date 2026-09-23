#ifndef MOON_DATA_H
#define MOON_DATA_H
#include <stddef.h>
typedef struct { double coeff; int d,m,mp,f; } MoonTerm;
extern const MoonTerm moon_lon[]; extern const size_t moon_lon_n;
extern const MoonTerm moon_lat[]; extern const size_t moon_lat_n;
extern const MoonTerm moon_dist[]; extern const size_t moon_dist_n;
#endif
