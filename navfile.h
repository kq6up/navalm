/* SPDX-License-Identifier: MIT
 * POSIX rename replaces an existing file; Windows CRT rename does not.
 * Keep atomic replacement for saved observations and defaults on Windows.
 */
#ifndef NAVFILE_H
#define NAVFILE_H
#include <stdio.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static int nav_replace_file(const char *source, const char *target) {
    return MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
#else
#define nav_replace_file rename
#endif
#endif
