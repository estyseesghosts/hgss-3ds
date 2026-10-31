// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/common/bank.h: the sound bank/wave-archive structs.
 *
 * HGSS has no such header: its bank structs live in
 * lib/NitroSDK/include-pokediamond/nitro/SND_bank_shared.h with different
 * member names (waveArcLinks/instOffsets, waveArcLLHead/unk_1C/waveOffsets,
 * rootKey/envAttack and loopEnabled/samplerate/loopStart/loopLength) but
 * identical layouts (SNDBankData 0x40 with instCount at 0x38, SNDWaveArc
 * 0x3C with waveCount at 0x38, SNDInstParam 0xA, SNDWaveParam 0xC). The
 * structs below use ntrtwl's member names (include/nitro/snd/common/bank.h),
 * which the port's tests use; both describe Nintendo's fixed binary format,
 * so the test's synthetic bank parses the same bytes HGSS's driver reads.
 * SNDBinaryFileHeader/BlockHeader come from HGSS's own
 * nitro/snd/common/data.h.
 */

#ifndef HGSS_COMPAT_SND_COMMON_BANK_H
#define HGSS_COMPAT_SND_COMMON_BANK_H

#include <nitro/types.h>
#include <nitro/snd/common/channel.h>
#include <nitro/snd/common/data.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SNDWaveArc;

#define SND_INST_KEYSPLIT_MAX    8
#define SND_BANK_TO_WAVEARC_MAX  4
#define SND_BANK_DISABLE_RELEASE 255

typedef enum SNDInstType {
    SND_INST_INVALID = 0,
    SND_INST_PCM = 0x01,
    SND_INST_PSG,
    SND_INST_NOISE,
    SND_INST_DIRECTPCM,
    SND_INST_NULL,
    SND_INST_DRUM_SET = 0x10,
    SND_INST_KEY_SPLIT
} SNDInstType;

typedef struct SNDWaveArcLink {
    struct SNDWaveArc *waveArc;
    struct SNDWaveArcLink *next;
} SNDWaveArcLink;

typedef struct SNDWaveParam {
    u8 format;
    u8 loopflag;
    u16 rate;
    u16 timer;
    u16 loopstart;
    u32 looplen;
} SNDWaveParam;

typedef struct SNDWaveArc {
    SNDBinaryFileHeader fileHeader;
    SNDBinaryBlockHeader blockHeader;
    SNDWaveArcLink *topLink;
    u32 reserved[7];
    u32 waveCount;
    u32 waveOffset[0];
} SNDWaveArc;

typedef struct SNDWaveData {
    SNDWaveParam param;
    u8 samples[0];
} SNDWaveData;

typedef struct SNDBankData {
    SNDBinaryFileHeader fileHeader;
    SNDBinaryBlockHeader blockHeader;
    SNDWaveArcLink waveArcLink[SND_BANK_TO_WAVEARC_MAX];
    u32 instCount;
    u32 instOffset[0];
} SNDBankData;

typedef struct SNDInstParam {
    u16 wave[2];
    u8 original_key;
    u8 attack;
    u8 decay;
    u8 sustain;
    u8 release;
    u8 pan;
} SNDInstParam;

typedef struct SNDInstData {
    u8 type;
    u8 padding_;
    SNDInstParam param;
} SNDInstData;

typedef struct SNDKeySplit {
    u8 key[SND_INST_KEYSPLIT_MAX];
    SNDInstData instOffset[0];
} SNDKeySplit;

typedef struct SNDDrumSet {
    u8 min;
    u8 max;
    SNDInstData instOffset[0];
} SNDDrumSet;

typedef struct SNDInstPos {
    u32 prgNo;
    u32 index;
} SNDInstPos;

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_SND_COMMON_BANK_H */
