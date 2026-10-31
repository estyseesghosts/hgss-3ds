// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * card_common.h: the CARD library's shared command block, under the filename
 * the port's card service uses.
 *
 * FILENAME TRAP, READ BEFORE RENAMING: HGSS's header is the differently-cased
 * CARD_common.h (lib/NitroSDK/include/), and HGSS card sources AND several
 * flat headers (CARD_backup.h, CARD_request.h, CARD_rom.h, CARD_spi.h)
 * include it quoted. On a case-insensitive filesystem those quoted includes
 * resolve to THIS file, so this header must stay safe for those translation
 * units too: when the public SDK world is not present it behaves exactly
 * like the flat header (forwarded via #include_next, exact case, so it also
 * works on case-sensitive filesystems) plus three new command macros. It
 * must NOT pull in the public <nitro/card/common.h> chain there: mixing the
 * public and flat CARD/OS/PXI headers in one translation unit redefines the
 * shared enums. Conversely the port's TU already has the public world (via
 * <nitro.h>), where the flat header would collide the other way, so there it
 * uses the public headers. The branch is picked on NITRO_H_, the public
 * nitro.h guard, which no flat-only translation unit defines; a TU that
 * mixed both worlds would fail loudly either way (as it does today).
 *
 * The three extra command codes the port answers (which HGSS never sends)
 * use ntrtwl NitroSDK's values (include/nitro/card/common.h).
 */

#ifndef HGSS_COMPAT_CARD_COMMON_H
#define HGSS_COMPAT_CARD_COMMON_H

#ifdef NITRO_H_
#include <nitro/card/common.h>
#include <nitro/card/rom.h>
#else
#include_next "CARD_common.h"
#endif

/*
 * HGSS's CARDRequest enum ends at CARD_REQ_ERASE_CHIP_BACKUP (= 12, then
 * CARD_REQ_MAX = 13). The port's switch in 3ds_port/nitro/src/port_card.c
 * also handles the three later ntrtwl commands. Plain macros (not enum
 * entries): HGSS's enum cannot be extended from here.
 */
#define CARD_REQ_READ_STATUS            13
#define CARD_REQ_WRITE_STATUS           14
#define CARD_REQ_ERASE_SUBSECTOR_BACKUP 15

#endif /* HGSS_COMPAT_CARD_COMMON_H */
