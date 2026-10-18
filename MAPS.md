# Offline land map beneath the Universal Plotting Sheet

The native desktop includes Natural Earth's world land polygons, at 1:10 million
scale. They share the UGP's geographic coordinates and longitude scale, so the
coastline follows the plotting grid when zooming, panning and detaching the sheet.
This is land and coastline context. It has no depths, soundings, navigation aids,
hazards, tides or ENC chart symbols. It does not replace a nautical chart.

In **Charts + setup**:

- **Download world map** downloads approximately 7 MB over HTTPS. You can continue
  using the workbench. **Cancel** stops the download; the previously installed map
  is retained. The new file is installed only after complete download and parsing.
- **Use OpenCPN coastline** reads OpenCPN's chart-directory configuration and looks
  for an installed `OSMSHP/basemap_medium.shp` or `basemap_low.shp`. It reads these
  files in place; it does not modify OpenCPN's setup or charts.
- **Load land map (.shp)** opens a geographic WGS84 Polygon/PolygonZ land shapefile.
  Projected coordinates, other shape types and files over 256 MB are rejected.
  OpenCPN's very large full-resolution coastline is outside this limit.

Calculate a fix in **Saved sights + plotting** to see the underlay. The **Map**
checkbox independently shows/hides it in the docked and detached views. Its path
and visibility persist in `.local/basemap.path`. The Windows executable unpacks
its included map into `.local/maps/` on first launch; the macOS app includes it
in `Contents/Resources/maps/`. `NAVALM_BASEMAP` overrides the selected path.

SVG and GeoJSON exports continue to contain the UGP vectors, without the land map,
so they remain useful as transparent overlays. The cocked-hat inset is magnified
and is deliberately not treated as a geographic map.

## Source and redistribution

Natural Earth: https://www.naturalearthdata.com/downloads/10m-physical-vectors/10m-land/

Pinned download (Natural Earth repository release v5.1.2, land dataset v5.1.1):

https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/10m_physical/ne_10m_land.shp

SHA-256:
`4cad3a49bc75c1a4c2f3d7efae04f2f8e63151c96764b2658effabf524331fa6`

Natural Earth data are public domain:
https://www.naturalearthdata.com/about/terms-of-use/

The OpenCPN option uses the user's existing OSM-derived coastline. Those files
are not bundled or redistributed by NAVALM. OpenStreetMap attribution and terms:
https://www.openstreetmap.org/copyright

## Next chart layer

OpenCPN's S-57 ENC files are not decoded in this version. NOAA also publishes
pre-rendered ENC-derived MBTiles suitable for a future regional chart downloader:
https://distribution.charts.noaa.gov/ncds/index.html

An MBTiles renderer needs its own coverage/zoom index and Mercator projection;
the geographic UGP vectors must be projected into that same viewport. The new
coastline reader and downloader provide the first working offline underlay.
