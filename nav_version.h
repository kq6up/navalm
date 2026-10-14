/* SPDX-License-Identifier: MIT
 * NAVALM contributions: Copyright (c) 2026 Chris Maness.
 * See LICENSE, THIRD_PARTY_NOTICES.md, and SAFETY.md.
 */
/* nav_version.h - the one place NAVALM's version number is written.
 *
 * The CLI, the terminal interface and the web interface all report the
 * version, and before 4.0p each carried its own copy of the string; the
 * web interface was still answering 3.3c after the CLI had moved on.
 * Everything now reads it from here, including mkpage.sh, which emits
 * NAVALM_WEB_VERSION as an alias rather than a second literal.
 */
#ifndef NAV_VERSION_H
#define NAV_VERSION_H

#define NAVALM_VERSION "4.1"

#endif
