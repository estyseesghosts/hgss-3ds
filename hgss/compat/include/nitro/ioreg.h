// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/ioreg.h: I/O register declarations, under the ntrtwl path.
 *
 * HGSS spells the variant files lib/include/nitro/hw/ARM9/io_reg.h and
 * lib/include/nitro/hw/ARM7/io_reg.h (mirroring ntrtwl
 * include/nitro/ioreg.h, which only selects the ARM9/ARM7 variant).
 * Unlike ntrtwl's chain, HGSS's io_reg header expects the memory map
 * separately: every REG_*_ADDR macro is built on HW_REG_BASE, which HGSS
 * defines in lib/include/nitro/hw/mmap_shared.h, so that is pulled in too
 * (via <nitro/hw/mmap.h>, which also applies patch 1301's SDK_PORT_3DS
 * remap of HW_IOREG/HW_MAIN_MEM to the port's arrays).
 */

#ifndef HGSS_COMPAT_NITRO_IOREG_H
#define HGSS_COMPAT_NITRO_IOREG_H

#include <nitro/hw/mmap.h>

#ifdef SDK_ARM9
#include <nitro/hw/ARM9/io_reg.h>
#else
#include <nitro/hw/ARM7/io_reg.h>
#endif

#endif /* HGSS_COMPAT_NITRO_IOREG_H */
