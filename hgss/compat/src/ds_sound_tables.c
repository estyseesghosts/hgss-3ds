/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The two DS BIOS sound tables and the ARM7 power-control pair the ARM7 sound
 * driver calls, for the port's virtual ARM7.
 *
 * HGSS's ARM7 driver reaches these through the BIOS (SVC_GetPitchTable /
 * SVC_GetVolumeTable are SWIs) and through its own power_manager_driver.c
 * (PMi_SetControl / PMi_ResetControl, which talk to the PM chip over the ARM7
 * SPI bus). Neither transport exists on the port: there is no BIOS and no
 * ARM7 SPI model. The tables are pure functions of their index, so they are
 * computed here with the documented DS formulas; the PM control register is
 * kept as a shadow word because only the driver's sleep path reads it back
 * (SND_BeginSleep / SND_EndSleep), and the port's mixer does not sleep.
 *
 * Compiled by the Makefile's ARM7 rule (tools/hgss_nitro.py lists it in
 * ARM7_SOUND_SRCS), so every global here is renamed to N7_*, matching the
 * renamed references in the driver's own objects.
 *
 * Pitch: BIOS entry i of 768 is round(65536 * (2^(i/768) - 1)); the driver
 * adds 0x10000 back (sound_channel.c:264), giving the 1.0..2.0 rate ratio the
 * octave/exponent handling expects.
 *
 * Volume: BIOS entry i of 724 is the 0..127 channel volume for
 * (i - 723) dB, i.e. round(127 * 10^(dB/20)); 723 is 0 dB (full scale) and
 * 0 is the -723 dB floor (silence), the same clamp SND_CalcChannelVolume
 * applies.
 */

#include <math.h>

u32 SVC_GetPitchTable(int index)
{
    if (index <= 0) {
        return 0;
    }
    if (index >= 768) {
        index = 767;
    }
    return (u32)(65536.0 * (pow(2.0, (double)index / 768.0) - 1.0) + 0.5);
}

u32 SVC_GetVolumeTable(int index)
{
    if (index <= 0) {
        return 0;
    }
    if (index >= 723) {
        return 127;
    }
    return (u32)(127.0 * pow(10.0, ((double)index - 723.0) / 20.0) + 0.5);
}

/* The PM control byte, as the ARM7 driver sees it (power_manager_driver.c
 * bit masks: 1 = sound amp, 2 = ?, 4/8 = backlights, 64 = ?). Only the sound
 * driver's sleep path touches it. */
static u8 sArm7PmControl;

void PMi_SetControl(u32 mask)
{
    sArm7PmControl |= (u8)mask;
}

void PMi_ResetControl(u32 mask)
{
    sArm7PmControl &= (u8)~mask;
}
