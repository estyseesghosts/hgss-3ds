// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/mi/compress.h: the MI compression entry points.
 *
 * HGSS has no such header (its SDK ships no compressor; only
 * MI_CompressLZImpl survives in lib/NitroSDK/src/mi/mi_compress.c). The API
 * below is ntrtwl's (include/nitro/mi/compress.h): LZ/LZEx are inlines over
 * HGSS's MI_CompressLZImpl, and RL/Huffman are implemented in
 * hgss/compat/src/sdk_standins.c.
 */

#ifndef HGSS_COMPAT_MI_COMPRESS_H
#define HGSS_COMPAT_MI_COMPRESS_H

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 MI_CompressLZImpl(const u8 *srcp, u32 size, u8 *dstp, int exFormat);

static inline u32 MI_CompressLZ(const u8 *srcp, u32 size, u8 *dstp)
{
    return MI_CompressLZImpl(srcp, size, dstp, FALSE);
}

static inline u32 MI_CompressLZEx(const u8 *srcp, u32 size, u8 *dstp)
{
    return MI_CompressLZImpl(srcp, size, dstp, TRUE);
}

u32 MI_CompressRL(const u8 *srcp, u32 size, u8 *dstp);

#define MI_HUFFMAN_COMPRESS_WORK_SIZE   (12288 + 512 + 1536)

u32 MI_CompressHuffman(const u8 *srcp, u32 size, u8 *dstp, u8 huffBitSize, u8 *work);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MI_COMPRESS_H */
