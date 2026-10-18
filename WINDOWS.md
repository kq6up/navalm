# Portable Windows desktop

`NAVALM.exe` targets **Windows 10/11 x64**. FLTK, the C++ library and compiler
support are linked statically. It needs no FLTK DLLs, compiler installation or
Visual C++ redistributable. It uses Windows' own system DLLs, including the UCRT,
WinHTTP and normal graphics/window libraries.

The executable contains the precise JPL ephemeris and Natural Earth offline map.
Put it in a writable folder (for example Documents/NAVALM), then double-click.
First launch creates `.local/` beside it and shows preparation progress while
unpacking the bundled data there. This step does not download anything. It works
offline immediately. Keep `.local/` with the program when moving it to preserve
your saved sights, defaults, selected map and extracted data. Copying the exe
alone starts a fresh workspace. Do not run directly inside the ZIP.

The portable ZIP includes the executable, instructions and third-party notices.
This is an unsigned sandbox build. The existing Unix CLI and `-d` web server
remain in the normal Makefile build; this Windows artifact is the native GUI.

## Fix calculation update

New sight forms leave DR blank until you enter it or load saved defaults.
Latitude 0, longitude 0 remains a valid position. Unsuitable DRs, invalid saved
inputs and parallel LOPs return an explanation; you can correct the sights and
calculate again. Windows uses a native error dialog and records handled errors
and unexpected fault details locally in `.local/desktop-errors.log`.

The coastline fill now handles thousands of contours without FLTK's Windows
complex-polygon routine, which has a fixed 20-contour buffer. Broad views could
overrun that buffer. Islands and holes still use even/odd filling.

To update an existing workspace, close NAVALM and replace `NAVALM.exe`, keeping
your existing `.local/` folder. Preparation progress appears only when bundled
data needs to be extracted.

## Build from macOS or Linux

Install CMake, Ninja, a host C/C++ compiler, Python 3, curl, make, and an official
LLVM-MinGW toolchain for your build host:
https://github.com/mstorsjo/llvm-mingw/releases

This sandbox used `llvm-mingw-20261006-ucrt-macos-universal` (LLVM 23.1.3).
The downloaded archive SHA-256, verified against the official release metadata:
`a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6`.

```sh
export LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw-toolchain
sh tools/build-windows.sh /absolute/path/to/output-folder
```

The script pins FLTK 1.4.5, prepares the JPL ephemeris with the existing host
repacker, verifies the Natural Earth download, cross-compiles static x64 code,
and packages the result. It checks the PE architecture, Windows GUI subsystem,
DLL imports and byte-for-byte embedded data. It never includes your personal
`.local/` navigation files. Build inputs and results are ignored by Git.

For an already prepared source tree, the underlying CMake invocation is:

```sh
cmake -S . -B .build/windows-x64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/windows-x64.cmake \
  -DLLVM_MINGW_ROOT="$LLVM_MINGW_ROOT" \
  -DCMAKE_BUILD_TYPE=Release -DNAVALM_EMBED_DATA=ON
cmake --build .build/windows-x64 --target navalm-desktop desktop-core-check --parallel 4
```

`NAVALM_FLTK_SOURCE` can point at an existing FLTK 1.4.5 source tree to avoid
fetching it again. `NAVALM_EPHEMERIS_FILE` and `NAVALM_MAP_FILE` override the files
embedded in the executable. Defaults match the script's prepared inputs.

## Validation status

The Windows executable was cross-compiled on macOS, and its PE structure,
imports and embedded resources were inspected. The shared desktop/map tests and
native callback/download checks passed on macOS. The previous build was tested
by the user on Windows 10: the almanac and a fix with the intended DR worked;
a fix with DR 0,0 crashed. **This updated build still needs a Windows runtime
retest.** Preparation progress, native dialogs and the new coastline drawing
have not been exercised on a Windows host here. Windows download, file picker
and save/reopen checks are also still needed before a release.

The cross-build also produces `desktop-core-check.exe` for a Windows test host;
run it from the source directory with `ephem/de440s.nav` present. The desktop's
`--self-test` likewise expects the source fixtures and a world map. Setting
`NAVALM_TEST_DOWNLOAD=1` additionally exercises a live background map download
and cancellation. These test modes use private temporary navigation logs.
