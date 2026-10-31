#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define NAVALM_VERSION_NUMBER "\([^"]*\)"/\1/p' nav_version.h)
test -n "$version"
test "$(uname -s)" = Darwin || { echo 'desktop-app creates a macOS bundle. Use make desktop on other platforms.'; exit 1; }
app=NAVALM.app/Contents
mkdir -p "$app/MacOS" "$app/Resources"
cp navalm-fltk "$app/MacOS/navalm-fltk"
cp LICENSE THIRD_PARTY_NOTICES.md SAFETY.md DESKTOP.md ANNUAL.md PLANETARIUM.md LINUX.md DEFAULTS.md PROVENANCE.md "$app/Resources/"
mkdir -p "$app/Resources/data/sky"
cp data/sky/README.md "$app/Resources/data/sky/"
cp libastro/LICENSE.XEphem "$app/Resources/"
cp .deps/fltk-1.4.5/COPYING "$app/Resources/FLTK-COPYING" 2>/dev/null || true
mkdir -p "$app/Resources/maps"
if [ -f .local/maps/ne_10m_land.shp ]; then cp .local/maps/ne_10m_land.shp "$app/Resources/maps/"; fi
if [ -f MAPS.md ]; then cp MAPS.md "$app/Resources/"; fi
if [ -f ephem/de440s.nav ]; then cp ephem/de440s.nav "$app/Resources/"; fi
cat > "$app/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>navalm-fltk</string>
<key>CFBundleIdentifier</key><string>org.navalm.desktop.sandbox</string>
<key>CFBundleName</key><string>NAVALM</string>
<key>CFBundleDisplayName</key><string>NAVALM</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$version</string>
<key>CFBundleVersion</key><string>$version</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --deep --sign - NAVALM.app
echo 'Built NAVALM.app — open NAVALM.app'
