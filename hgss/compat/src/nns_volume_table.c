/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The NitroSystem sequence player's volume-to-decibel table.
 *
 * NNS_SndPlayerSetInitialVolume / MoveVolume take a linear 0..127 volume.
 * snd_player.c turns it into decibels through _0210D750 before pushing it to
 * the ARM7 player as the external fader. On the DS this symbol is an alias of
 * NitroSDK's SNDi_DecibelTable: the decomp's lib/asm/nitro_sdk_data.s emits it
 * as an empty `.rodata.alias_d750` section, and its link order puts that
 * section immediately before SND_util.o's `.rodata.snd_decibel`, so both names
 * address one table.
 *
 * The port links with the host's default section order, where the empty
 * section landed on unrelated .rodata (a table of function pointers). NNS then
 * read pointer bytes as decibels: the fader became a large positive gain, every
 * channel was clamped to 0 dB (a clipped wall), and the release stop condition
 * in SND_ExChannelMain (`volume <= -723`) could never be reached, so each note
 * sustained until the next key-on reused the channel.
 *
 * tools/hgss_game.py removes the empty section from the SDK data object for the
 * port builds and this object supplies the table. The values are
 * SNDi_DecibelTable's (lib/NitroSDK/src/snd/SND_util.c, 0.1 dB units from
 * -72.3 dB at index 0 to 0 dB at index 127); tests/test_nns_volume_table.py
 * keeps the two copies identical.
 */
#include <nitro/types.h>

const s16 _0210D750[0x80] = {
    -32768,   -421,   -361,   -325,   -300,   -281,   -265,   -252,
      -240,   -230,   -221,   -212,   -205,   -198,   -192,   -186,
      -180,   -175,   -170,   -165,   -161,   -156,   -152,   -148,
      -145,   -141,   -138,   -134,   -131,   -128,   -125,   -122,
      -120,   -117,   -114,   -112,   -110,   -107,   -105,   -103,
      -100,    -98,    -96,    -94,    -92,    -90,    -88,    -86,
       -85,    -83,    -81,    -79,    -78,    -76,    -74,    -73,
       -71,    -70,    -68,    -67,    -65,    -64,    -62,    -61,
       -60,    -58,    -57,    -56,    -54,    -53,    -52,    -51,
       -49,    -48,    -47,    -46,    -45,    -43,    -42,    -41,
       -40,    -39,    -38,    -37,    -36,    -35,    -34,    -33,
       -32,    -31,    -30,    -29,    -28,    -27,    -26,    -25,
       -24,    -23,    -23,    -22,    -21,    -20,    -19,    -18,
       -17,    -17,    -16,    -15,    -14,    -13,    -12,    -12,
       -11,    -10,     -9,     -9,     -8,     -7,     -6,     -6,
        -5,     -4,     -3,     -3,     -2,     -1,     -1,      0,
};
