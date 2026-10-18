#!/bin/sh
# Private, pinned FLTK build; does not install into /usr/local or Homebrew.
set -eu
cd "$(dirname "$0")/.."
version=1.4.5
archive="fltk-$version-source.tar.gz"
sha=eede1fb2b8e9c2e581e77082e15252145855c79aad30070ee3b24aabe2f926f1
mkdir -p .deps/downloads
if [ ! -f ".deps/downloads/$archive" ]; then
  curl -fL --retry 2 -o ".deps/downloads/$archive" "https://github.com/fltk/fltk/releases/download/release-$version/$archive"
fi
printf '%s  %s\n' "$sha" ".deps/downloads/$archive" | shasum -a 256 -c -
if [ ! -d ".deps/fltk-$version" ]; then tar -xzf ".deps/downloads/$archive" -C .deps; fi
cmake -S ".deps/fltk-$version" -B .deps/fltk-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/.deps/fltk" \
  -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_FLUID=OFF \
  -DFLTK_BUILD_FLTK_OPTIONS=OFF -DFLTK_BUILD_GL=OFF -DFLTK_BUILD_SHARED_LIBS=OFF
cmake --build .deps/fltk-build --parallel 4
cmake --install .deps/fltk-build
