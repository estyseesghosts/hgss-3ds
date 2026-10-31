// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/hw/ARM9/mmap_global.h: ARM9 memory map, under the ntrtwl path.
 *
 * HGSS has no such file; its map is split across
 * lib/include/nitro/hw/mmap_shared.h, lib/include/nitro/hw/mmap.h and
 * lib/include/nitro/hw/ARM9/mmap.h. This header pulls in HGSS's ARM9 map and
 * adds HW_ROM_HEADER_BUF_END, which HGSS never names but the port needs:
 * the value is ntrtwl's (HW_MAIN_MEM + 0x007fff60, from
 * include/nitro/hw/common/mmap_shared.h), consistent with HGSS's own
 * HW_ROM_HEADER_BUF (HW_MAIN_MEM + 0x007ffe00) plus HW_CARD_ROM_HEADER_SIZE
 * (0x160). Relative to HW_MAIN_MEM, so it follows patch 1301's SDK_PORT_3DS
 * remap automatically.
 */

#ifndef HGSS_COMPAT_HW_ARM9_MMAP_GLOBAL_H
#define HGSS_COMPAT_HW_ARM9_MMAP_GLOBAL_H

#include <nitro/hw/ARM9/mmap.h>

#ifndef HW_ROM_HEADER_BUF_END
#define HW_ROM_HEADER_BUF_END           (HW_MAIN_MEM + 0x007fff60)
#endif

#endif /* HGSS_COMPAT_HW_ARM9_MMAP_GLOBAL_H */
