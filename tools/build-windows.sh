#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
: "${LLVM_MINGW_ROOT:?Set LLVM_MINGW_ROOT to an official LLVM-MinGW toolchain}"
out=${1:-.build/windows-package}
mkdir -p "$out"
out=$(cd "$out" && pwd)
if [ ! -f ephem/de440s.bsp ] && [ ! -f ephem/de440s.nav ]; then make ephem-fetch; fi
if [ ! -f ephem/de440s.nav ]; then make ephem; fi
mkdir -p .local/maps
map=.local/maps/ne_10m_land.shp
if [ ! -f "$map" ]; then
    curl --fail --location --proto '=https' --proto-redir '=https' \
        --output "$map.part" \
        https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/10m_physical/ne_10m_land.shp
    mv "$map.part" "$map"
fi
python3 - "$map" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest() == \
 '4cad3a49bc75c1a4c2f3d7efae04f2f8e63151c96764b2658effabf524331fa6', 'Unexpected map checksum'
PY
cmake -S . -B .build/windows-x64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/windows-x64.cmake \
    -DLLVM_MINGW_ROOT="$LLVM_MINGW_ROOT" \
    -DCMAKE_BUILD_TYPE=Release -DNAVALM_EMBED_DATA=ON
cmake --build .build/windows-x64 --target navalm-desktop desktop-core-check --parallel 4
python3 tools/package-windows.py --toolchain "$LLVM_MINGW_ROOT" --output "$out"
