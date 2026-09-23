#!/bin/sh
# Build nav_stars_x.c from an XEphem .edb catalogue.
#
#   make stars   (wraps the generated rows in a complete C source file)
#
# navstars.edb holds one line per navigational star, selected from
# XEphem's SKY2k65.edb (SKY2000 Master Catalog v4, distributed with XEphem
# under the MIT licence), with the navigation name prepended. Each line is
#
#   names|...,f|class|spectrum,RA|pmRA,Dec|pmDec,mag,epoch
#
# with RA and Dec as sexagesimal J2000 and the proper motions in mas/yr.
awk -F, '
function sex(s,   n,a,v,sign) {
  sign = (substr(s,1,1) == "-") ? -1 : 1
  gsub(/^[+-]/,"",s)
  n = split(s,a,":")
  v = a[1] + (n>1 ? a[2]/60 : 0) + (n>2 ? a[3]/3600 : 0)
  return sign*v
}
/^#/ || /^$/ { next }
{
  split($1,names,"|"); name = names[1]
  split($3,ra,"|");    split($4,dec,"|")
  rah  = sex(ra[1]);   pmra  = (ra[2]  == "" ? 0 : ra[2])
  decd = sex(dec[1]);  pmdec = (dec[2] == "" ? 0 : dec[2])
  printf "    {\"%s\", %.9f, %.9f, %.3f, %.3f, %.3f},\n", name, rah*15, decd, pmra, pmdec, $5
}' "$1"
