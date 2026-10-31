// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os.h: the arena helpers with the rest of the OS.
 *
 * HGSS's lib/include/nitro/os.h never includes nitro/os/arena.h (ntrtwl's
 * os.h pulls os/common/arena.h), so OS_GetMainArenaLo/Hi and
 * OS_AllocFromMainArenaLo/Hi are invisible through <nitro.h>. This wrapper
 * adds the arena header; the MainArena helpers themselves are static
 * inlines in the nitro/os/arena.h wrapper (ntrtwl's, over HGSS's
 * OS_GetArenaLo/OS_AllocFromArenaLo).
 */

#ifndef HGSS_COMPAT_OS_H
#define HGSS_COMPAT_OS_H

#include_next <nitro/os.h>
#include <nitro/os/arena.h>

#endif /* HGSS_COMPAT_OS_H */
