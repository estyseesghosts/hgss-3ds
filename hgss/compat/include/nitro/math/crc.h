// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/math/crc.h: the table constructors the port's tests use, wrapping
 * HGSS's header.
 *
 * HGSS's lib/include/nitro/math/crc.h and lib/NitroSDK/include/MATH_crc.h
 * only build tables for the pairs HGSS calls: MATH_CRC16InitTable builds a
 * CCITT table (for MATH_CalcCRC16CCITT), and there is no
 * MATH_CRC16CCITTInitTable or MATH_CRC32InitTable at all. The two missing
 * constructors below are ntrtwl's (include/nitro/math/crc.h) and pair with
 * HGSS's MATH_CalcCRC16CCITT / MATH_CalcCRC32 (same algorithms, standard
 * check values 0x29B1 / 0xCBF43926 for "123456789").
 *
 * MATH_CRC16InitTable is redirected to ntrtwl's meaning (a reflected table
 * for MATH_CalcCRC16, check value 0xBB3D): HGSS's inline builds a CCITT
 * table instead, which no in-tree caller pairs with MATH_CalcCRC16. HGSS's
 * own caller (src/math_util.c) pairs MATH_CRC16InitTable with
 * MATH_CalcCRC16CCITT and must keep HGSS's inline; any translation unit
 * that needs HGSS's meaning defines HGSS_MATH_KEEP_HGSS_CRC16_INIT before
 * including <nitro.h>. See docs/progress/agent-notes/p1-standins.md.
 */

#ifndef HGSS_COMPAT_MATH_CRC_H
#define HGSS_COMPAT_MATH_CRC_H

#include_next <nitro/math/crc.h>

#ifdef __cplusplus
extern "C" {
#endif

void MATHi_CRC32InitTableRev(MATHCRC32Table *table, u32 poly);
u32 MATH_CalcCRC32(const MATHCRC32Table *table, const void *data, u32 dataLength);
u16 MATH_CalcCRC16(const MATHCRC16Table *table, const void *data, u32 dataLength);

static inline void MATH_CRC16CCITTInitTable(MATHCRC16Table *table)
{
    MATHi_CRC16InitTable(table, MATH_CRC16_CCITT_POLY);
}

static inline void MATH_CRC32InitTable(MATHCRC32Table *table)
{
    MATHi_CRC32InitTableRev(table, MATH_CRC32_STANDARD_POLY);
}

#ifdef __cplusplus
}
#endif

#ifndef HGSS_MATH_KEEP_HGSS_CRC16_INIT
static inline void HGSSCompat_MATH_CRC16InitTable(MATHCRC16Table *table)
{
    MATHi_CRC16InitTableRev(table, MATH_CRC16_STANDARD_POLY);
}
#define MATH_CRC16InitTable HGSSCompat_MATH_CRC16InitTable
#endif

#endif /* HGSS_COMPAT_MATH_CRC_H */
