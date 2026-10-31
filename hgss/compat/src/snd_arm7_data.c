/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ARM7 sound data for the vendored port's sound test (test_sound_sequence).
 *
 * HGSS's ARM7 sound driver keeps its globals in sub/asm/sound_card_pad_data.s,
 * which the port's GCC build never assembles. This file embeds the sound-only
 * subset verbatim as assembly (translated MWAS -> GAS like nns_data.c:
 * `.public` -> `.global`, `.word` -> `.long` because x86 GAS `.word` is
 * 16-bit, MWAS `.section .data,4,1,1` -> `.data`, `; address` comments
 * stripped since `;` is not a comment on x86 GAS), so:
 *
 * - SNDi_Work (0x1180), SNDi_SharedWork, the sequence cache window
 *   (sMmlPrintEnable immediately followed by seqCache: sound_seq_player.c
 *   addresses the cache through the sMmlPrintEnable view, so the two must
 *   stay adjacent), the sound thread/alarm/message state, the master
 *   pan/volume words (sMasterPan, u$3681) and the tables (SinTable,
 *   SNDi_DecibelTable, shift/channel/attack) keep the DS layout exactly.
 * - the card, PAD and thread-stack words (cardi_*, CARDi_EnableFlag,
 *   PADi_XYButtonAlarm, sndStack) and the unreferenced MWCC function
 *   statics (arg$3806, arg$3983, buf$3759, ...) are left out: no compiled
 *   ARM7 sound file references them. sLockChannel (no .public in the
 *   original) stays file-local, immediately after sWeakLockChannel, whose
 *   u32[2] view spans both words.
 *
 * Compiled with the Makefile's ARM7 rule (tools/hgss_nitro.py lists it in
 * ARM7_SOUND_SRCS, not SDK_SRCS), so every global here is renamed to N7_*,
 * giving the ARM7 driver its own copies, exactly like the driver's code.
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
"	  .global SinTable\n"
"\n"
"SinTable: \n"
"  .byte 0x00, 0x06, 0x0C, 0x13\n"
"  .byte 0x19, 0x1F, 0x25, 0x2B, 0x31, 0x36, 0x3C, 0x41, 0x47, 0x4C, 0x51, 0x55, 0x5A, 0x5E, 0x62, 0x66\n"
"  .byte 0x6A, 0x6D, 0x70, 0x73, 0x75, 0x78, 0x7A, 0x7B, 0x7D, 0x7E, 0x7E, 0x7F, 0x7F\n"
"\n"
"  .balign 4, 0\n"
"	  .global SNDi_DecibelTable\n"
"\n"
"SNDi_DecibelTable: \n"
"  .byte 0x00, 0x80, 0x2E, 0xFD, 0x2F, 0xFD, 0x75, 0xFD, 0xA7, 0xFD, 0xCE, 0xFD, 0xEE, 0xFD, 0x09, 0xFE\n"
"  .byte 0x20, 0xFE, 0x34, 0xFE, 0x46, 0xFE, 0x57, 0xFE, 0x66, 0xFE, 0x74, 0xFE, 0x81, 0xFE, 0x8D, 0xFE\n"
"  .byte 0x98, 0xFE, 0xA3, 0xFE, 0xAD, 0xFE, 0xB6, 0xFE, 0xBF, 0xFE, 0xC7, 0xFE, 0xCF, 0xFE, 0xD7, 0xFE\n"
"  .byte 0xDF, 0xFE, 0xE6, 0xFE, 0xEC, 0xFE, 0xF3, 0xFE, 0xF9, 0xFE, 0xFF, 0xFE, 0x05, 0xFF, 0x0B, 0xFF\n"
"  .byte 0x11, 0xFF, 0x16, 0xFF, 0x1B, 0xFF, 0x20, 0xFF, 0x25, 0xFF, 0x2A, 0xFF, 0x2E, 0xFF, 0x33, 0xFF\n"
"  .byte 0x37, 0xFF, 0x3C, 0xFF, 0x40, 0xFF, 0x44, 0xFF, 0x48, 0xFF, 0x4C, 0xFF, 0x50, 0xFF, 0x53, 0xFF\n"
"  .byte 0x57, 0xFF, 0x5B, 0xFF, 0x5E, 0xFF, 0x62, 0xFF, 0x65, 0xFF, 0x68, 0xFF, 0x6B, 0xFF, 0x6F, 0xFF\n"
"  .byte 0x72, 0xFF, 0x75, 0xFF, 0x78, 0xFF, 0x7B, 0xFF, 0x7E, 0xFF, 0x81, 0xFF, 0x83, 0xFF, 0x86, 0xFF\n"
"  .byte 0x89, 0xFF, 0x8C, 0xFF, 0x8E, 0xFF, 0x91, 0xFF, 0x93, 0xFF, 0x96, 0xFF, 0x99, 0xFF, 0x9B, 0xFF\n"
"  .byte 0x9D, 0xFF, 0xA0, 0xFF, 0xA2, 0xFF, 0xA5, 0xFF, 0xA7, 0xFF, 0xA9, 0xFF, 0xAB, 0xFF, 0xAE, 0xFF\n"
"  .byte 0xB0, 0xFF, 0xB2, 0xFF, 0xB4, 0xFF, 0xB6, 0xFF, 0xB8, 0xFF, 0xBA, 0xFF, 0xBC, 0xFF, 0xBE, 0xFF\n"
"  .byte 0xC0, 0xFF, 0xC2, 0xFF, 0xC4, 0xFF, 0xC6, 0xFF, 0xC8, 0xFF, 0xCA, 0xFF, 0xCC, 0xFF, 0xCE, 0xFF\n"
"  .byte 0xCF, 0xFF, 0xD1, 0xFF, 0xD3, 0xFF, 0xD5, 0xFF, 0xD6, 0xFF, 0xD8, 0xFF, 0xDA, 0xFF, 0xDC, 0xFF\n"
"  .byte 0xDD, 0xFF, 0xDF, 0xFF, 0xE1, 0xFF, 0xE2, 0xFF, 0xE4, 0xFF, 0xE5, 0xFF, 0xE7, 0xFF, 0xE9, 0xFF\n"
"  .byte 0xEA, 0xFF, 0xEC, 0xFF, 0xED, 0xFF, 0xEF, 0xFF, 0xF0, 0xFF, 0xF2, 0xFF, 0xF3, 0xFF, 0xF5, 0xFF\n"
"  .byte 0xF6, 0xFF, 0xF8, 0xFF, 0xF9, 0xFF, 0xFA, 0xFF, 0xFC, 0xFF, 0xFD, 0xFF, 0xFF, 0xFF, 0x00, 0x00\n"
"	  .global shift$4005\n"
"\n"
"shift$4005: \n"
"  .byte 0x00, 0x01, 0x02, 0x04\n"
"	  .global channel_order$3829\n"
"\n"
"channel_order$3829: \n"
"  .byte 0x04, 0x05, 0x06, 0x07, 0x02, 0x00, 0x03, 0x01, 0x08, 0x09, 0x0A, 0x0B\n"
"  .byte 0x0E, 0x0C, 0x0F, 0x0D\n"
"	  .global attack_table$3790\n"
"\n"
"attack_table$3790: \n"
"  .byte 0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26, 0x33, 0x3F, 0x49, 0x54, 0x5C, 0x64\n"
"  .byte 0x6D, 0x74, 0x7B, 0x7F, 0x84, 0x89, 0x8F\n"
"\n"
"  .balign 4, 0\n"
"  .data\n"
"  .balign 4\n"
"	  .global sMasterPan\n"
"\n"
"sMasterPan: \n"
"	  .long 0xFFFFFFFF\n"
"	  .global u$3681\n"
"\n"
"u$3681: \n"
"	  .long 0x12345678\n"
"  .bss\n"
"  .balign 8\n"
HGSS_ASM_TYPE_OBJECT("sSurroundDecay")
"	  .global sSurroundDecay\n"
"sSurroundDecay: \n"
"  .space 0x4\n"
"  .size sSurroundDecay,.-sSurroundDecay\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sOrgPan")
"	  .global sOrgPan\n"
"sOrgPan: \n"
"  .space 0x10\n"
"  .size sOrgPan,.-sOrgPan\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sOrgVolume")
"	  .global sOrgVolume\n"
"sOrgVolume: \n"
"  .space 0x10\n"
"  .size sOrgVolume,.-sOrgVolume\n"
"\n"
HGSS_ASM_TYPE_OBJECT("initialized$3619")
"	  .global initialized$3619\n"
"initialized$3619: \n"
"  .space 0x4\n"
"  .size initialized$3619,.-initialized$3619\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sndMesgBuffer")
"	  .global sndMesgBuffer\n"
"sndMesgBuffer: \n"
"  .space 0x20\n"
"  .size sndMesgBuffer,.-sndMesgBuffer\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sndMesgQueue")
"	  .global sndMesgQueue\n"
"sndMesgQueue: \n"
"  .space 0x20\n"
"  .size sndMesgQueue,.-sndMesgQueue\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sndAlarm")
"	  .global sndAlarm\n"
"sndAlarm: \n"
"  .space 0x2C\n"
"  .size sndAlarm,.-sndAlarm\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sndThread")
"	  .global sndThread\n"
"sndThread: \n"
"  .space 0xA4\n"
"  .size sndThread,.-sndThread\n"
HGSS_ASM_TYPE_OBJECT("sWeakLockChannel")
"	  .global sWeakLockChannel\n"
"sWeakLockChannel: \n"
"  .space 0x4\n"
"  .size sWeakLockChannel,.-sWeakLockChannel\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sLockChannel")
"sLockChannel: \n"
"  .space 0x4\n"
"  .size sLockChannel,.-sLockChannel\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sMmlPrintEnable")
"	  .global sMmlPrintEnable\n"
"sMmlPrintEnable: \n"
"  .space 0x4\n"
"  .size sMmlPrintEnable,.-sMmlPrintEnable\n"
"\n"
HGSS_ASM_TYPE_OBJECT("seqCache")
"	  .global seqCache\n"
"seqCache: \n"
"  .space 0x18\n"
"  .size seqCache,.-seqCache\n"
"\n"
HGSS_ASM_TYPE_OBJECT("SNDi_SharedWork")
"	  .global SNDi_SharedWork\n"
"SNDi_SharedWork: \n"
"  .space 0x4\n"
"  .size SNDi_SharedWork,.-SNDi_SharedWork\n"
"\n"
HGSS_ASM_TYPE_OBJECT("SNDi_Work")
"	  .global SNDi_Work\n"
"SNDi_Work: \n"
"  .space 0x1180\n"
"  .size SNDi_Work,.-SNDi_Work\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sCommandMesgQueue")
"	  .global sCommandMesgQueue\n"
"sCommandMesgQueue: \n"
"  .space 0x20\n"
"  .size sCommandMesgQueue,.-sCommandMesgQueue\n"
"\n"
HGSS_ASM_TYPE_OBJECT("sCommandMesgBuffer")
"	  .global sCommandMesgBuffer\n"
"sCommandMesgBuffer: \n"
"  .space 0x20\n"
"  .size sCommandMesgBuffer,.-sCommandMesgBuffer\n"
);
