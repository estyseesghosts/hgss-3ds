/* Independent DS sound register engine (plain C99, no libctru/NitroSDK).
 * ARM7 SDK sound-register writes target NitroPort_SoundRegisters; the mixer
 * consumes their state at 32,768 samples/s, producing interleaved signed PCM.
 */
#ifndef NITRO_PORT_SOUND_CHIP_H
#define NITRO_PORT_SOUND_CHIP_H
#include <stddef.h>
#include <stdint.h>
#define NITRO_SOUND_RATE 32768u
#define NITRO_SOUND_REG_SIZE 0x600u
#define NITRO_SOUND_CHANNEL_COUNT 16u
extern volatile uint8_t NitroPort_SoundRegisters[NITRO_SOUND_REG_SIZE];

typedef struct NitroSoundVoice {
    uint32_t previousCtrl;
    uint32_t sampleIndex;
    uint64_t phase;
    int32_t predictor;
    int32_t lastSample;
    int32_t stepIndex;
    uint16_t noise;
    uint8_t adpcmByte;
    uint8_t adpcmNibble;
    uint8_t running;
    /* Kept by the mixer so it doesn't redo work every sample: the step for
     * the last timer value, and the ADPCM decoder's state at the loop
     * start (as the DS keeps it), for the loop start and data it was
     * saved for. */
    uint8_t loopSaved;
    uint8_t adpcmClean;    /* the ADPCM state is adpcmSource's, decoded from its start */
    uint32_t adpcmSource;
    uint32_t stepPeriod;
    uint32_t stepRate;
    uint64_t step;
    uint32_t loopSource;
    uint32_t loopAt;
    int32_t loopPredictor;
    int32_t loopStepIndex;
} NitroSoundVoice;

typedef struct NitroSoundCapture {
    uint32_t index;
    uint8_t running;
    /* A capture takes samples at the rate of channel 1's (capture 0) or
     * channel 3's (capture 1) timer, as the DS's does. */
    uint64_t phase;
    uint32_t stepPeriod;
    uint32_t stepRate;
    uint64_t step;
} NitroSoundCapture;

typedef struct NitroSoundChip {
    NitroSoundVoice voice[NITRO_SOUND_CHANNEL_COUNT];
    NitroSoundCapture capture[2];
    uint32_t outputRate;
    /* Sampled sounds (PCM8, PCM16, ADPCM) go from one sample to the next
     * in a straight line, a sample later, instead of holding each sample
     * (the DS's own way, NitroSound_Reset's default): no harsh images of
     * the sound above it, as on the DS's own output and in emulators. */
    uint8_t interpolate;
    /* Right shift applied to the final left/right mix *before* the 16-bit
     * saturation (0 = no shift). The mixer's sum can leave the 16-bit range;
     * the DS's own 20.8-bit mixer attenuates at the output stage and clips
     * only at its final 10-bit DAC, so applying the headroom here keeps loud
     * peaks smooth instead of flat-topping them and then halving the flat
     * top. The port's output stage sets 1 (6 dB), matching the measured DS
     * title level. */
    uint8_t outputShift;
} NitroSoundChip;

void NitroSound_Reset(NitroSoundChip *chip, uint32_t outputRate);
void NitroSound_Mix(NitroSoundChip *chip, int16_t *stereo, size_t frames);
/* The driver is about to set channel ch's enable bit: if it's 0 now (the
 * channel stopped, perhaps just before, and set up again), the channel
 * starts over from its beginning, as on the DS. */
void NitroSound_KeyOn(NitroSoundChip *chip, unsigned channel);

#ifdef NITRO_SOUND_STATS
/* Test-only (3ds_port/nitro/tests/sdat_render.c): the mixer's state for one
 * output frame, handed to the frame hook once the frame is made. Only built
 * with NITRO_SOUND_STATS, so the port's own build has none of it. */
#define NITRO_SOUND_TAP_OFF INT32_MIN   /* the channel made no sample in the frame */
#define NITRO_SOUND_EV_START 1u         /* the channel started (its first frame) */
#define NITRO_SOUND_EV_KILL 2u          /* the driver cleared its enable bit while it played */
#define NITRO_SOUND_EV_END 4u           /* the sound ended (no repeat) */
#define NITRO_SOUND_EV_LOOP 8u          /* the sound wrapped to its loop start after this frame */

typedef struct NitroSoundFrame {
    uint32_t index;                     /* output frames since the hook was set */
    int32_t preL, preR;                 /* after master volume, before outputShift */
    int32_t postL, postR;               /* after outputShift, before saturate() */
    /* Per channel: its sample after volume and shift, before pan (what it
     * adds to the mix), or NITRO_SOUND_TAP_OFF. */
    int32_t tap[NITRO_SOUND_CHANNEL_COUNT];
    uint8_t format[NITRO_SOUND_CHANNEL_COUNT];   /* ctrl bits 29-30 */
    uint8_t volume[NITRO_SOUND_CHANNEL_COUNT];   /* ctrl bits 0-6 */
    uint8_t shift[NITRO_SOUND_CHANNEL_COUNT];    /* the shift applied to the sample: 0, 1, 2 or 4 */
    uint8_t event[NITRO_SOUND_CHANNEL_COUNT];    /* NITRO_SOUND_EV_*, for this frame */
} NitroSoundFrame;

typedef void (*NitroSoundFrameFn)(void *arg, const NitroSoundFrame *frame);
void NitroSound_SetFrameHook(NitroSoundFrameFn fn, void *arg);
#endif
#endif
