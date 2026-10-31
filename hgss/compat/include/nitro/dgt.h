// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/dgt.h: the digest umbrella, missing from HGSS's <nitro.h> chain.
 *
 * HGSS's lib/include/nitro.h never includes a dgt header (only
 * nitro/dgt/common.h exists, with the digest/block sizes). ntrtwl reaches
 * the HMAC entry points through nitro/dgt/dgt.h; this umbrella pulls both
 * so the port's tests see them. Bodies live in the port's dgt.c.
 */

#ifndef HGSS_COMPAT_DGT_H
#define HGSS_COMPAT_DGT_H

#include <nitro/dgt/common.h>
#include <nitro/dgt/dgt.h>

#endif /* HGSS_COMPAT_DGT_H */
