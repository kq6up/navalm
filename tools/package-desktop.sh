#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin || { echo 'desktop-app creates a macOS bundle. Use make desktop on other platforms.'; exit 1; }
app=NAVALM.app/Contents
mkdir -p "$app/MacOS" "$app/Resources"
cp navalm-fltk "$app/MacOS/navalm-fltk"
cp LICENSE THIRD_PARTY_NOTICES.md SAFETY.md DESKTOP.md "$app/Resources/"
cp .deps/fltk-1.4.5/COPYING "$app/Resources/FLTK-COPYING" 2>/dev/null || true
if [ -f ephem/de440s.nav ]; then cp ephem/de440s.nav "$app/Resources/"; fi
cat > "$app/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>navalm-fltk</string>
<key>CFBundleIdentifier</key><string>org.navalm.desktop.sandbox</string>
<key>CFBundleName</key><string>NAVALM</string>
<key>CFBundleDisplayName</key><string>NAVALM</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>4.2</string>
<key>CFBundleVersion</key><string>1</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --deep --sign - NAVALM.app
echo 'Built NAVALM.app — open NAVALM.app'
