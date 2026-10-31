// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd.h: the ARM9 sound API with the common types.
 *
 * HGSS's lib/include/nitro/snd.h pulls only the common data/channel/work
 * headers, so SND_Init, SND_StartSeq and friends are invisible through
 * <nitro.h>. This wrapper adds the ARM9 interface declarations (new header
 * nitro/snd/ARM9/interface.h) plus the command-list and tick headers the
 * port's sound code and tests use. Bodies are HGSS's own (or the port's).
 */

#ifndef HGSS_COMPAT_SND_H
#define HGSS_COMPAT_SND_H

#include_next <nitro/snd.h>
#include <nitro/snd/common/command.h>
#include <nitro/snd/common/main.h>
#include <nitro/snd/ARM9/interface.h>

#endif /* HGSS_COMPAT_SND_H */
