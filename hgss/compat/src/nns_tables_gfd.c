/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * NNS VRAM-transfer dispatch table for the nitro VRAM-transfer test
 * (3ds_port/nitro/tests/test_vram_transfer.c).
 *
 * gVramTransferFuncTable (lib/asm/nnsys_data.s) maps each NNS_GFD_DST_TYPE to
 * its DoTransfer* routine. test_vram_transfer compiles HGSS's
 * gfd_vram_transfer_manager.c directly and links only libnitro.a, so the
 * table must live there: its 36 function references resolve to the
 * directly-compiled file, and the test dispatches through it exactly as the
 * game does. Kept apart from nns_tables_nns.c so the other tables' broader
 * references (allocators, OAM loaders, animation/sbc callbacks, which only
 * exist in libnitrosystem.a) never burden this link. Translated MWAS -> GAS
 * as in nns_data.c.
 *
 * Compiled into libnitro.a by tools/hgss_nitro.py (hgss/compat/src).
 */

/* GAS spells the section and symbol types with @ on x86 and with % on ARM,
 * where @ starts a comment. The ARM7 target assembles this file with
 * arm-none-eabi-as: `@progbits` is "junk at end of line" and `@object` is
 * "unrecognized symbol type". The x86 strings are byte-identical to before,
 * so the host32 object is unchanged. */
#if defined(__arm__)
#define HGSS_ASM_RODATA "  .section .rodata,\"a\",%progbits\n"
#define HGSS_ASM_TYPE_OBJECT(name) "  .type " name ",%object\n"
#else
#define HGSS_ASM_RODATA "  .section .rodata,\"a\",@progbits\n"
#define HGSS_ASM_TYPE_OBJECT(name) "  .type " name ",@object\n"
#endif

__asm__(
HGSS_ASM_RODATA
"  .balign 4\n"
"	  .weak DoTransfer2dBG0CharMain\n"
"	  .weak DoTransfer2dBG0CharSub\n"
"	  .weak DoTransfer2dBG0ScrMain\n"
"	  .weak DoTransfer2dBG0ScrSub\n"
"	  .weak DoTransfer2dBG1CharMain\n"
"	  .weak DoTransfer2dBG1CharSub\n"
"	  .weak DoTransfer2dBG1ScrMain\n"
"	  .weak DoTransfer2dBG1ScrSub\n"
"	  .weak DoTransfer2dBG2BmpMain\n"
"	  .weak DoTransfer2dBG2BmpSub\n"
"	  .weak DoTransfer2dBG2CharMain\n"
"	  .weak DoTransfer2dBG2CharSub\n"
"	  .weak DoTransfer2dBG2ScrMain\n"
"	  .weak DoTransfer2dBG2ScrSub\n"
"	  .weak DoTransfer2dBG3BmpMain\n"
"	  .weak DoTransfer2dBG3BmpSub\n"
"	  .weak DoTransfer2dBG3CharMain\n"
"	  .weak DoTransfer2dBG3CharSub\n"
"	  .weak DoTransfer2dBG3ScrMain\n"
"	  .weak DoTransfer2dBG3ScrSub\n"
"	  .weak DoTransfer2dBGExtPlttMain\n"
"	  .weak DoTransfer2dBGExtPlttSub\n"
"	  .weak DoTransfer2dBGPlttMain\n"
"	  .weak DoTransfer2dBGPlttSub\n"
"	  .weak DoTransfer2dObjCharMain\n"
"	  .weak DoTransfer2dObjCharSub\n"
"	  .weak DoTransfer2dObjExtPlttMain\n"
"	  .weak DoTransfer2dObjExtPlttSub\n"
"	  .weak DoTransfer2dObjOamMain\n"
"	  .weak DoTransfer2dObjOamSub\n"
"	  .weak DoTransfer2dObjPlttMain\n"
"	  .weak DoTransfer2dObjPlttSub\n"
"	  .weak DoTransfer3dClearImageColor\n"
"	  .weak DoTransfer3dClearImageDepth\n"
"	  .weak DoTransfer3dTex\n"
"	  .weak DoTransfer3dTexPltt\n"
"	  .global gVramTransferFuncTable\n"
"gVramTransferFuncTable:\n"
"	  .long DoTransfer3dTex\n"
"	  .long DoTransfer3dTexPltt\n"
"	  .long DoTransfer3dClearImageColor\n"
"	  .long DoTransfer3dClearImageDepth\n"
"	  .long DoTransfer2dBG0CharMain\n"
"	  .long DoTransfer2dBG1CharMain\n"
"	  .long DoTransfer2dBG2CharMain\n"
"	  .long DoTransfer2dBG3CharMain\n"
"	  .long DoTransfer2dBG0ScrMain\n"
"	  .long DoTransfer2dBG1ScrMain\n"
"	  .long DoTransfer2dBG2ScrMain\n"
"	  .long DoTransfer2dBG3ScrMain\n"
"	  .long DoTransfer2dBG2BmpMain\n"
"	  .long DoTransfer2dBG3BmpMain\n"
"	  .long DoTransfer2dObjPlttMain\n"
"	  .long DoTransfer2dBGPlttMain\n"
"	  .long DoTransfer2dObjExtPlttMain\n"
"	  .long DoTransfer2dBGExtPlttMain\n"
"	  .long DoTransfer2dObjOamMain\n"
"	  .long DoTransfer2dObjCharMain\n"
"	  .long DoTransfer2dBG0CharSub\n"
"	  .long DoTransfer2dBG1CharSub\n"
"	  .long DoTransfer2dBG2CharSub\n"
"	  .long DoTransfer2dBG3CharSub\n"
"	  .long DoTransfer2dBG0ScrSub\n"
"	  .long DoTransfer2dBG1ScrSub\n"
"	  .long DoTransfer2dBG2ScrSub\n"
"	  .long DoTransfer2dBG3ScrSub\n"
"	  .long DoTransfer2dBG2BmpSub\n"
"	  .long DoTransfer2dBG3BmpSub\n"
"	  .long DoTransfer2dObjPlttSub\n"
"	  .long DoTransfer2dBGPlttSub\n"
"	  .long DoTransfer2dObjExtPlttSub\n"
"	  .long DoTransfer2dBGExtPlttSub\n"
"	  .long DoTransfer2dObjOamSub\n"
"	  .long DoTransfer2dObjCharSub\n"
"  .text\n"
);

/*
 * ntrtwl's NNS_GfdGetVramTransferTaskTotalSize (Gfd VramTransferMan): the
 * bytes still queued. HGSS's stripped NitroSystem does not ship it, but
 * test_vram_transfer calls it to observe the queue the manager drains. HGSS
 * keeps one global queue (gfd_vram_transfer_manager.c:234) instead of
 * ntrtwl's manager struct, so read that.
 */
#include <nitro.h>
#include <nnsys/gfd_internal.h>

/* Weak storage: the real definition is in gfd_vram_transfer_manager.c, which
 * test_vram_transfer compiles directly. Links that only need the table (and so
 * pull this member) get this zeroed copy instead of an undefined symbol. */
__attribute__((weak)) VramTransferTaskQueue gVramTransferTaskQueue;

u32 NNS_GfdGetVramTransferTaskTotalSize(void)
{
    return gVramTransferTaskQueue.totalSize;
}
