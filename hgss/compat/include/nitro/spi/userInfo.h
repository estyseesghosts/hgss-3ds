// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/spi/userInfo.h: NVRAM firmware-format constants, wrapping HGSS's
 * header.
 *
 * HGSS's lib/include/nitro/spi/userInfo.h defines the NVRAMConfig structure
 * (TS layout: version byte, 10-character nickname, option bits) but no
 * NVRAM_CONFIG_* macros. The values below are ntrtwl NitroSDK's TS200
 * firmware format (include/nitro/spi/common/userInfo_ts_200.h), which pairs
 * DATA_VERSION 5 with a 10-character nickname. Only the VERSION and
 * NICKNAME_LENGTH macros are used today
 * (3ds_port/nitro/src/port_boot.c); the LANG_* set is provided for the
 * NitroPortFirmware language field. Each macro is #ifndef-guarded. The
 * struct-member spelling difference (HGSS favouriteColour vs the port's
 * favoriteColor) is NOT papered over here; that is patch 1202's area.
 */

#ifndef HGSS_COMPAT_SPI_USERINFO_H
#define HGSS_COMPAT_SPI_USERINFO_H

#include_next <nitro/spi/userInfo.h>

#ifndef NVRAM_CONFIG_DATA_VERSION
#define NVRAM_CONFIG_DATA_VERSION           5
#endif
#ifndef NVRAM_CONFIG_NICKNAME_LENGTH
#define NVRAM_CONFIG_NICKNAME_LENGTH        10
#endif

#ifndef NVRAM_CONFIG_LANG_JAPANESE
#define NVRAM_CONFIG_LANG_JAPANESE          0
#endif
#ifndef NVRAM_CONFIG_LANG_ENGLISH
#define NVRAM_CONFIG_LANG_ENGLISH           1
#endif
#ifndef NVRAM_CONFIG_LANG_FRENCH
#define NVRAM_CONFIG_LANG_FRENCH            2
#endif
#ifndef NVRAM_CONFIG_LANG_GERMAN
#define NVRAM_CONFIG_LANG_GERMAN            3
#endif
#ifndef NVRAM_CONFIG_LANG_ITALIAN
#define NVRAM_CONFIG_LANG_ITALIAN           4
#endif
#ifndef NVRAM_CONFIG_LANG_SPANISH
#define NVRAM_CONFIG_LANG_SPANISH           5
#endif
#ifndef NVRAM_CONFIG_LANG_CODE_MAX
#define NVRAM_CONFIG_LANG_CODE_MAX          6
#endif

#endif /* HGSS_COMPAT_SPI_USERINFO_H */
