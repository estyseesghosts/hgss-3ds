// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/common/command.h: the ARM9/ARM7 sound command list type.
 *
 * HGSS keeps the structure as a bare `struct SNDCommand` in
 * lib/NitroSDK/include-pokediamond/nitro/SND_command_shared.h (reached via
 * the flat SND_command.h); only this compat header provides the ntrtwl-style
 * SNDCommand typedef, the SND_COMMAND_* flags and the SNDCommandID enum
 * (ntrtwl include/nitro/snd/common/command.h values). Only
 * 3ds_port/nitro/src/port_snd.c includes this path.
 */

#ifndef NITRO_SND_COMMON_COMMAND_H_
#define NITRO_SND_COMMON_COMMAND_H_

#include <nitro/types.h>
#include <SND_command.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SNDCommand SNDCommand;

#define SND_COMMAND_NOBLOCK       0
#define SND_COMMAND_BLOCK        (1 << 0)
#define SND_COMMAND_IMMEDIATE    (1 << 1)

typedef enum SNDCommandID {
    SND_COMMAND_START_SEQ,
    SND_COMMAND_STOP_SEQ,
    SND_COMMAND_PREPARE_SEQ,
    SND_COMMAND_START_PREPARED_SEQ,
    SND_COMMAND_PAUSE_SEQ,
    SND_COMMAND_SKIP_SEQ,
    SND_COMMAND_PLAYER_PARAM,
    SND_COMMAND_TRACK_PARAM,
    SND_COMMAND_MUTE_TRACK,
    SND_COMMAND_ALLOCATABLE_CHANNEL,
    SND_COMMAND_PLAYER_LOCAL_VAR,
    SND_COMMAND_PLAYER_GLOBAL_VAR,
    SND_COMMAND_START_TIMER,
    SND_COMMAND_STOP_TIMER,
    SND_COMMAND_SETUP_CHANNEL_PCM,
    SND_COMMAND_SETUP_CHANNEL_PSG,
    SND_COMMAND_SETUP_CHANNEL_NOISE,
    SND_COMMAND_SETUP_CAPTURE,
    SND_COMMAND_SETUP_ALARM,
    SND_COMMAND_CHANNEL_TIMER,
    SND_COMMAND_CHANNEL_VOLUME,
    SND_COMMAND_CHANNEL_PAN,
    SND_COMMAND_SURROUND_DECAY,
    SND_COMMAND_MASTER_VOLUME,
    SND_COMMAND_MASTER_PAN,
    SND_COMMAND_OUTPUT_SELECTOR,
    SND_COMMAND_LOCK_CHANNEL,
    SND_COMMAND_UNLOCK_CHANNEL,
    SND_COMMAND_STOP_UNLOCKED_CHANNEL,
    SND_COMMAND_SHARED_WORK,
    SND_COMMAND_INVALIDATE_SEQ,
    SND_COMMAND_INVALIDATE_BANK,
    SND_COMMAND_INVALIDATE_WAVE,
    SND_COMMAND_READ_DRIVER_INFO
} SNDCommandID;

#ifdef __cplusplus
}
#endif

#endif /* NITRO_SND_COMMON_COMMAND_H_ */
