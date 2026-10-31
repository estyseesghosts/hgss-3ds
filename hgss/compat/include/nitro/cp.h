// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/cp.h: the divider/square-root helpers with the context helpers.
 *
 * HGSS's lib/include/nitro/cp.h pulls only cp/context.h, so the CP_SetDiv,
 * CP_GetDiv, CP_SetSqrt and CP_IsBusy inlines the port (port_cp.c) and its
 * tests use are invisible. ntrtwl's cp.h pulls divider.h, sqrt.h and then
 * context.h; this wrapper does the same (the two already live here under
 * their ntrtwl paths).
 */

#ifndef HGSS_COMPAT_CP_H
#define HGSS_COMPAT_CP_H

#include_next <nitro/cp.h>
#include <nitro/cp/divider.h>
#include <nitro/cp/sqrt.h>

#endif /* HGSS_COMPAT_CP_H */
