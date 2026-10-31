// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/rtc/ARM9/api.h: the synchronous date/time getters, wrapping HGSS's
 * header.
 *
 * HGSS's lib/include/nitro/rtc/ARM9/api.h declares only RTC_Init and
 * RTC_GetDateTimeAsync, although its rtc_api.c defines the sync/async
 * getters (RTC_GetDate/Time/DateTime, RTC_GetDateAsync/TimeAsync). The
 * declarations below match those definitions.
 */

#ifndef HGSS_COMPAT_RTC_ARM9_API_H
#define HGSS_COMPAT_RTC_ARM9_API_H

#include_next <nitro/rtc/ARM9/api.h>

#ifdef __cplusplus
extern "C" {
#endif

RTCResult RTC_GetDateAsync(RTCDate *date, RTCCallback callback, void *arg);
RTCResult RTC_GetDate(RTCDate *date);
RTCResult RTC_GetTimeAsync(RTCTime *time, RTCCallback callback, void *arg);
RTCResult RTC_GetTime(RTCTime *time);
RTCResult RTC_GetDateTime(RTCDate *date, RTCTime *time);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_RTC_ARM9_API_H */
