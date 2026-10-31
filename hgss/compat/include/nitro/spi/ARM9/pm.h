// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/spi/ARM9/pm.h: the battery and result API, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/spi/ARM9/pm.h declares the backlight/amplifier/
 * sleep API but no PMBattery, no PM_BATTERY enums, no PM_SUCCESS/PM_RESULT
 * enums and no PM_GetBattery/PM_SetBackLightAsync, although HGSS's pm.c
 * defines the async setter. Worse, it declares PM_GetBackLight as returning
 * void while pm.c defines it as returning u32 (the file says so itself,
 * and hides the header declaration with a macro). The additions below are
 * ntrtwl's (include/nitro/spi/ARM9/pm.h and the result block of
 * include/nitro/spi/common/pm_common.h); PM_GetBattery's body lives in
 * hgss/compat/src/sdk_standins.c (ntrtwl's, over HGSS's PMi_ReadRegister).
 *
 * The GetBackLight wrapper calls the real symbol: HGSSCompat_PM_GetBackLight
 * is a static inline, and the raw declaration names the link symbol
 * explicitly, so pm.c's own macro dance (which undefines PM_GetBackLight
 * after including <nitro.h>) is unaffected.
 */

#ifndef HGSS_COMPAT_SPI_ARM9_PM_H
#define HGSS_COMPAT_SPI_ARM9_PM_H

#include_next <nitro/spi/ARM9/pm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_BATTERY_HIGH = 0,
    PM_BATTERY_LOW = 1
} PMBattery;

#ifndef PM_SUCCESS
#define PM_SUCCESS              0
#endif
#ifndef PM_BUSY
#define PM_BUSY                 1
#endif
#ifndef PM_RESULT_SUCCESS
#define PM_RESULT_SUCCESS       0
#endif
#ifndef PM_RESULT_BUSY
#define PM_RESULT_BUSY          1
#endif
#ifndef PM_RESULT_ERROR
#define PM_RESULT_ERROR         2
#endif

typedef void (*PMCallback)(u32 result, void *arg);

u32 PM_GetBattery(PMBattery *batteryBuf);
u32 PM_SetBackLightAsync(PMLCDTarget target, PMBackLightSwitch status, PMCallback callback, void *arg);

extern u32 HGSS_PM_GetBackLightRaw(PMBackLightSwitch *top, PMBackLightSwitch *bottom)
    __asm__("PM_GetBackLight");

static inline u32 HGSSCompat_PM_GetBackLight(PMBackLightSwitch *top, PMBackLightSwitch *bottom)
{
    return HGSS_PM_GetBackLightRaw(top, bottom);
}
#define PM_GetBackLight HGSSCompat_PM_GetBackLight

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_SPI_ARM9_PM_H */
