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
 * -72.3 dB at index 0 to 0 dB at index 127); tests/test_hgss_game.py
 * (NnsVolumeTableTests) keeps the two copies identical.
 *
 * Indices 128..255 are a tail the DS also has. snd_player.c indexes this table
 * with the fader's high byte (`fader >> 8`) as well as with the volume, and the
 * field code parks the fader at 128 (GF_SndHandleMoveVolume(SND_HANDLE_FIELD,
 * 128, 15) in src/field/field_control.c and src/field_warp_tasks.c), so index
 * 128 is read on every field BGM channel. On the DS those words are the
 * unlabelled .rodata.unified_01 section that follows the empty alias in
 * lib/asm/nitro_sdk_data.s: `.section .rodata.unified_01` is at line 621 and
 * its first .byte at line 622. The first 256 bytes of that section, read as
 * little-endian s16, are the 128 values below (index 128 = 0 dB, 129 = 59,
 * 130 = 118, 131 = 178). The port's link has no such section after the alias,
 * so without this tail index 128 read unrelated rodata as decibels, a large
 * positive gain that pinned the field BGM at full volume.
 */
#include <nitro/types.h>

const s16 _0210D750[0x100] = {
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
           0,      59,     118,     178,     237,     296,     356,     415,
         475,     535,     594,     654,     714,     773,     833,     893,
         953,    1013,    1073,    1134,    1194,    1254,    1314,    1375,
        1435,    1496,    1556,    1617,    1677,    1738,    1799,    1859,
        1920,    1981,    2042,    2103,    2164,    2225,    2287,    2348,
        2409,    2471,    2532,    2593,    2655,    2716,    2778,    2840,
        2902,    2963,    3025,    3087,    3149,    3211,    3273,    3335,
        3397,    3460,    3522,    3584,    3647,    3709,    3772,    3834,
        3897,    3960,    4022,    4085,    4148,    4211,    4274,    4337,
        4400,    4463,    4526,    4590,    4653,    4716,    4780,    4843,
        4907,    4971,    5034,    5098,    5162,    5226,    5289,    5353,
        5417,    5481,    5546,    5610,    5674,    5738,    5803,    5867,
        5932,    5996,    6061,    6125,    6190,    6255,    6320,    6384,
        6449,    6514,    6579,    6645,    6710,    6775,    6840,    6906,
        6971,    7037,    7102,    7168,    7233,    7299,    7365,    7431,
        7496,    7562,    7628,    7694,    7761,    7827,    7893,    7959,
};
