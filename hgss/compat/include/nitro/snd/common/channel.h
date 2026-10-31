// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/common/channel.h: the wave-format enum, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/snd/common/channel.h only defines
 * SND_CHANNEL_NUM. The enum below is ntrtwl's
 * (include/nitro/snd/common/channel.h); the port's tests tag synthetic
 * waves with it.
 */

#ifndef HGSS_COMPAT_SND_COMMON_CHANNEL_H
#define HGSS_COMPAT_SND_COMMON_CHANNEL_H

#include_next <nitro/snd/common/channel.h>

typedef enum {
    SND_WAVE_FORMAT_PCM8,
    SND_WAVE_FORMAT_PCM16,
    SND_WAVE_FORMAT_ADPCM,
    SND_WAVE_FORMAT_PSG,
    SND_WAVE_FORMAT_NOISE = SND_WAVE_FORMAT_PSG
} SNDWaveFormat;

#endif /* HGSS_COMPAT_SND_COMMON_CHANNEL_H */
