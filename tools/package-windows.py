#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect and package the cross-built Windows GUI, without executing it."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--toolchain', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
version = re.search(r'^#define NAVALM_VERSION "([^"]+)"',
                    (root / 'nav_version.h').read_text(), re.M).group(1)
numeric_version = re.search(r'^#define NAVALM_VERSION_NUMBER "([^"]+)"',
                           (root / 'nav_version.h').read_text(), re.M).group(1)
exe = root / '.build/windows-x64/NAVALM.exe'
data = exe.read_bytes()

def u16(at):
    return struct.unpack_from('<H', data, at)[0]

def u32(at):
    return struct.unpack_from('<I', data, at)[0]

pe = u32(0x3c)
assert data[pe:pe + 4] == b'PE\0\0' and u16(pe + 4) == 0x8664, 'Expected x64 PE'
opt = pe + 24
assert u16(opt) == 0x20b and u16(opt + 68) == 2, 'Expected PE32+ Windows GUI'
sections = opt + u16(pe + 20)

def offset(rva):
    for i in range(u16(pe + 6)):
        at = sections + i * 40
        size, va, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
        if va <= rva < va + max(size, raw_size):
            return raw + rva - va
    raise ValueError('RVA outside PE sections')

resource_base = offset(u32(opt + 112 + 2 * 8))

def entries(directory):
    at = resource_base + directory
    count = u16(at + 12) + u16(at + 14)
    return [struct.unpack_from('<II', data, at + 16 + i * 8) for i in range(count)]

def child(directory, ident):
    return next(value for key, value in entries(directory) if key == ident)

def resource(kind, ident):
    folder = child(0, kind)
    assert folder & 0x80000000
    language_folder = child(folder & 0x7fffffff, ident)
    assert language_folder & 0x80000000
    leaf = entries(language_folder & 0x7fffffff)[0][1]
    assert not leaf & 0x80000000
    at = resource_base + leaf
    start, size = offset(u32(at)), u32(at + 4)
    return data[start:start + size]

# Check embedded bytes, not just the presence of a resource section.
assets = [(901, root / 'ephem/de440s.nav'), (902, root / '.local/maps/ne_10m_land.shp')]
asset_notes = []
for ident, source in assets:
    payload = resource(10, ident)
    assert payload == source.read_bytes(), f'Embedded asset mismatch: {source.name}'
    asset_notes.append(f'{source.name}: {len(payload):,} bytes, SHA256 {hashlib.sha256(payload).hexdigest()}')
manifest = resource(24, 1)
assert b'UTF-8' in manifest, 'Missing UTF-8 application manifest'
assert f'version="{numeric_version}.0"'.encode() in manifest, 'Manifest version mismatch'
version_resource = resource(16, 1)
assert (version + '\0').encode('utf-16le') in version_resource, 'PE product version mismatch'
fixed = version_resource.index(struct.pack('<I', 0xfeef04bd))
major, minor, patch = map(int, numeric_version.split('.'))
numeric_pair = (major << 16 | minor, patch << 16)
assert struct.unpack_from('<IIII', version_resource, fixed + 8) == numeric_pair * 2, \
    'PE numeric file/product version mismatch'

readobj = args.toolchain / 'bin/llvm-readobj'
imports = subprocess.check_output([str(readobj), '--coff-imports', str(exe)], text=True)
names = re.findall(r'^  Name: (.+)$', imports, re.M)
allowed = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'shell32.dll',
           'comdlg32.dll', 'comctl32.dll', 'ole32.dll', 'oleaut32.dll', 'uuid.dll',
           'winspool.drv', 'ws2_32.dll', 'winhttp.dll', 'bcrypt.dll', 'msimg32.dll',
           'imm32.dll', 'gdiplus.dll', 'version.dll', 'ucrtbase.dll', 'ntdll.dll', 'shlwapi.dll'}
assert names and all(n.lower() in allowed or n.lower().startswith('api-ms-win-crt-')
                     for n in names), f'Unexpected DLL dependency: {names}'

out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
stage = out / 'NAVALM-Windows-x64'
stage.mkdir(exist_ok=True)
shutil.copy2(exe, stage / 'NAVALM.exe')
for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'PROVENANCE.md', 'SAFETY.md', 'CHANGELOG.md',
             'WINDOWS.md', 'DESKTOP.md', 'MAPS.md', 'ANNUAL.md', 'PLANETARIUM.md']:
    shutil.copy2(root / name, stage / name)
license_dir = stage / 'licenses'
license_dir.mkdir(exist_ok=True)
shutil.copy2(root / 'libastro/LICENSE.XEphem', license_dir / 'LICENSE.XEphem')
fltk_license = root / '.deps/fltk-1.4.5/COPYING'
if not fltk_license.exists():
    fltk_license = root / '.build/windows-x64/_deps/fltk-src/COPYING'
shutil.copy2(fltk_license, license_dir / 'FLTK-COPYING')
shutil.copy2(args.toolchain / 'LICENSE.TXT', license_dir / 'LLVM-LICENSE.TXT')
for source in (args.toolchain / 'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'):
    shutil.copy2(source, license_dir / source.name)
report = (f'NAVALM {version} Windows x64 build inspection\n\n'
          'PE32+ x86-64; Windows GUI subsystem.\n'
          'FLTK/C++ support linked statically; DLL imports are Windows system components.\n'
          'Map and precise ephemeris are embedded byte-for-byte and unpack on first launch.\n'
          'Not executed on Windows or under Wine. macOS shared/native tests passed.\n\n'
          + '\n'.join(asset_notes) + '\n\nDLL imports:\n' + '\n'.join(names)
          + '\n\nNAVALM.exe SHA256: ' + hashlib.sha256(data).hexdigest() + '\n')
(stage / 'BUILD-VALIDATION.txt').write_text(report)
(stage / 'START-HERE.txt').write_text(
    f'NAVALM {version} - Windows 10/11 x64\n\n'
    'Extract this folder into Documents or another writable location.\n'
    'Double-click NAVALM.exe. No installation or runtime package is needed.\n'
    'It includes the offline world land map and the precise JPL ephemeris.\n'
    'First launch shows bundled-data preparation progress and creates .local.\n'
    'Keep .local beside the exe to preserve your sights when updating.\n\n'
    'Charts + setup: download a map, use an OpenCPN coastline, or load a land .shp.\n'
    'Saved sights + plotting: calculate a fix, then toggle Map or Detach sheet.\n'
    'Wheel/+/- zoom; drag pans; Fit or 0 restores the sheet.\n\n'
    'Planetarium: enter UTC and DR, then Update sky or choose morning/evening.\n'
    'Pick a suggested star to start a new sight; enter your actual Hs.\n\n'
    'Annual book (last tab): choose a year and sections, then export PDF\n'
    'or editable LaTeX source. PDF export needs no TeX installation.\n'
    'This Windows package is the native GUI; it does not include the Unix web server.\n\n'
    'This unsigned build was cross-compiled on macOS. It still needs\n'
    'a real Windows launch and save/reopen test. See WINDOWS.md.\n')
archive = out / 'NAVALM-Windows-x64.zip'
# Explicit file list avoids carrying state from any previous staging-directory run.
files = [stage / n for n in ['NAVALM.exe', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
         'PROVENANCE.md', 'SAFETY.md', 'CHANGELOG.md', 'WINDOWS.md', 'DESKTOP.md', 'MAPS.md', 'ANNUAL.md', 'PLANETARIUM.md',
         'START-HERE.txt', 'BUILD-VALIDATION.txt']]
files += list(license_dir.iterdir())
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for path in sorted(files):
        z.write(path, path.relative_to(out))
print(report)
print(f'Created {archive} ({archive.stat().st_size:,} bytes)')
