// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/spi/ARM9/tp.h: the synchronous sampling helpers, wrapping HGSS's
 * header.
 *
 * HGSS's lib/include/nitro/spi/ARM9/tp.h declares the async sampling
 * primitives, TP_WaitRawResult, the raw-point auto accessors and the
 * TP_RequestRawSampling inline, but not the calibrated/auto synchronous
 * helpers ntrtwl provides (include/nitro/spi/ARM9/tp.h). Their bodies live
 * in hgss/compat/src/sdk_standins.c, written against HGSS's primitives:
 * calibrated sampling is raw sampling plus TP_GetCalibratedPoint, and the
 * auto start/stop wrappers wait on HGSS's busy bits
 * (TP_COMMAND_AUTO_START = 1, TP_COMMAND_AUTO_STOP = 2 in tp.c).
 */

#ifndef HGSS_COMPAT_SPI_ARM9_TP_H
#define HGSS_COMPAT_SPI_ARM9_TP_H

#include_next <nitro/spi/ARM9/tp.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 TP_RequestCalibratedSampling(TPData *result);
u32 TP_RequestAutoSamplingStart(u16 vcount, u16 frequency, TPData *buffer, u16 count);
u32 TP_RequestAutoSamplingStop(void);
void TP_GetLatestCalibratedPointInAuto(TPData *result);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_SPI_ARM9_TP_H */
