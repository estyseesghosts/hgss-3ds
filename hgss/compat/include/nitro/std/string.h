// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/std/string.h: the varargs print helper, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/std/string.h only declares STD_TVSNPrintf.
 * STD_TSNPrintf below is ntrtwl's (include/nitro/std/string.h: extern int
 * STD_TSNPrintf(char *, size_t, const char *, ...)); the body lives in
 * hgss/compat/src/sdk_standins.c and forwards to STD_TVSNPrintf.
 */

#ifndef HGSS_COMPAT_STD_STRING_H
#define HGSS_COMPAT_STD_STRING_H

#include_next <nitro/std/string.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int STD_TSNPrintf(char *dst, size_t len, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_STD_STRING_H */
