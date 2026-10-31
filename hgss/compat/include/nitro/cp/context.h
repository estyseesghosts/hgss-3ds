// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/cp/context.h: CP_RestoreContext, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/cp/context.h declares CP_SaveContext and
 * CPi_RestoreContext (patch 0501 defines both in C for the port); ntrtwl
 * names the restore entry point CP_RestoreContext
 * (include/nitro/cp/context.h). The inline below forwards to HGSS's
 * function, so the port's tests can use the ntrtwl name with no SDK change.
 */

#ifndef HGSS_COMPAT_CP_CONTEXT_H
#define HGSS_COMPAT_CP_CONTEXT_H

#include_next <nitro/cp/context.h>

static inline void CP_RestoreContext(CPContext *context)
{
    CPi_RestoreContext(context);
}

#endif /* HGSS_COMPAT_CP_CONTEXT_H */
