// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/ctrdg/common/ctrdg_common.h: the cartridge-presence helpers,
 * wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/ctrdg/common/ctrdg_common.h declares only
 * CTRDG_IsPulledOut. CTRDG_IsExisting and CTRDG_IsAgbCartridge below match
 * the port's definitions in 3ds_port/nitro/src/ctrdg.c (no cartridge on
 * the 3DS: both false).
 */

#ifndef HGSS_COMPAT_CTRDG_COMMON_H
#define HGSS_COMPAT_CTRDG_COMMON_H

#include_next <nitro/ctrdg/common/ctrdg_common.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL CTRDG_IsExisting(void);
BOOL CTRDG_IsAgbCartridge(void);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_CTRDG_COMMON_H */
