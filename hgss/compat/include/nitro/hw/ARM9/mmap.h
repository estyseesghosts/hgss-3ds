// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/hw/ARM9/mmap.h: HGSS's ARM9 memory map plus HW_ROM_HEADER_BUF_END.
 *
 * Wrapper around HGSS's lib/include/nitro/hw/ARM9/mmap.h via #include_next:
 * every translation unit that sees HGSS's map (e.g.
 * 3ds_port/nitro/src/port_boot.c through nitro/os/irqTable.h) also sees the
 * end of the ROM-header buffer. The guard is compat-specific on purpose: it
 * must differ from HGSS's NITRO_HW_ARM9_MMAP_H, or the chained header would
 * be skipped.
 */

#ifndef HGSS_COMPAT_HW_ARM9_MMAP_H
#define HGSS_COMPAT_HW_ARM9_MMAP_H

#include_next <nitro/hw/ARM9/mmap.h>

#ifndef HW_ROM_HEADER_BUF_END
#define HW_ROM_HEADER_BUF_END           (HW_MAIN_MEM + 0x007fff60)
#endif

#endif /* HGSS_COMPAT_HW_ARM9_MMAP_H */
