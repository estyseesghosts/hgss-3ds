// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os/arena.h: the main-arena helpers, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/os/arena.h only declares the OS_ARENA_ arena
 * primitives (OS_GetArenaLo/Hi, OS_AllocFromArenaLo/Hi); the MainArena
 * shorthands below are ntrtwl's (include/nitro/os/common/arena.h), as
 * static inlines over the same primitives, so no SDK change is needed.
 */

#ifndef HGSS_COMPAT_OS_ARENA_H
#define HGSS_COMPAT_OS_ARENA_H

#include_next <nitro/os/arena.h>

static inline void OS_SetMainArenaHi(void *newHi)
{
    OS_SetArenaHi(OS_ARENA_MAIN, newHi);
}

static inline void OS_SetMainArenaLo(void *newLo)
{
    OS_SetArenaLo(OS_ARENA_MAIN, newLo);
}

static inline void *OS_GetMainArenaHi(void)
{
    return OS_GetArenaHi(OS_ARENA_MAIN);
}

static inline void *OS_GetMainArenaLo(void)
{
    return OS_GetArenaLo(OS_ARENA_MAIN);
}

static inline void *OS_AllocFromMainArenaLo(u32 size, u32 align)
{
    return OS_AllocFromArenaLo(OS_ARENA_MAIN, size, align);
}

static inline void *OS_AllocFromMainArenaHi(u32 size, u32 align)
{
    return OS_AllocFromArenaHi(OS_ARENA_MAIN, size, align);
}

#endif /* HGSS_COMPAT_OS_ARENA_H */
