// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd.h: the ARM9 sound API with the common types.
 *
 * HGSS's lib/include/nitro/snd.h pulls only the common data/channel/work
 * headers, so SND_Init, SND_StartSeq and friends are invisible through
 * <nitro.h>. This wrapper adds the ARM9 interface declarations (new header
 * nitro/snd/ARM9/interface.h) plus the command-list and tick headers the
 * port's sound code and tests use. Bodies are HGSS's own (or the port's).
 *
 * It also completes NNSSndHandle's declaration: NitroSystem's own
 * nnsys/snd/main.h uses the type without including nnsys/snd/player.h, and
 * the generated nns_all_headers.c includes every nnsys header in path order
 * (snd/main.h before snd/player.h). Including the two headers here, inside
 * <nitro.h>, makes the type visible before any nnsys header regardless of
 * order. heap.h comes first for NNSSndHeapHandle, which player.h uses.
 */

#ifndef HGSS_COMPAT_SND_H
#define HGSS_COMPAT_SND_H

#include_next <nitro/snd.h>
#include <nitro/snd/common/command.h>
#include <nitro/snd/common/main.h>
#include <nitro/snd/ARM9/interface.h>

/* NitroSystem's public sound headers (HGSS's lib/include/nnsys). */
#include <nnsys/snd/heap.h>
#include <nnsys/snd/player.h>

/* The rest of ntrtwl's NitroSystem API that HGSS's partial decompilation
 * dropped (see the file). Relative include: the NNS include directory is
 * searched before hgss/compat/include, so <nnsys/...> would find HGSS's
 * headers first. */
#include "../nnsys/hgss_compat.h"

#endif /* HGSS_COMPAT_SND_H */
