// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/math/dgt.h: the MD5/SHA-1 streaming helpers with the one-shot ones.
 *
 * HGSS's lib/include/nitro/math/dgt.h only declares MATH_CalcMD5 /
 * MATH_CalcSHA1. The streaming types and helpers live in HGSS's own flat
 * lib/NitroSDK/include/MATH_dgt.h (same DGT_Hash calls, ntrtwl names), which
 * this wrapper includes (its guard makes a second inclusion, here or in an
 * SDK translation unit, a no-op). The HMAC declarations use the signatures
 * HGSS's own fs/fs_private.h uses, so SDK translation units see no
 * conflict; the bodies in the port's dgt.c take (void *, int) parameters,
 * which is the same 32-bit ABI.
 */

#ifndef HGSS_COMPAT_MATH_DGT_H
#define HGSS_COMPAT_MATH_DGT_H

#include_next <nitro/math/dgt.h>
#include "MATH_dgt.h"

#ifdef __cplusplus
extern "C" {
#endif

void DGT_Hash1CalcHmac(void *digest, const void *bin_ptr, u32 bin_len, const void *key_ptr,
                       u32 key_len);
void DGT_Hash2CalcHmac(void *digest, const void *bin_ptr, u32 bin_len, const void *key_ptr,
                       u32 key_len);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MATH_DGT_H */
