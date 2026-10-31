// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/mi.h: the compression helpers with the rest of MI.
 *
 * HGSS's lib/include/nitro/mi.h pulls dma, dma_card, exMemory, memory,
 * swap and uncompress, but no compression header (HGSS's SDK only compresses
 * in tools). ntrtwl's is nitro/mi/compress.h (MI_CompressLZ/LZEx inlines
 * over MI_CompressLZImpl, MI_CompressRL/Huffman,
 * MI_HUFFMAN_COMPRESS_WORK_SIZE); this wrapper adds the new header below.
 * MI_CompressLZImpl already lives in HGSS's mi_compress.c;
 * MI_CompressRL/Huffman live in hgss/compat/src/sdk_standins.c (ntrtwl's
 * algorithms, format-compatible with HGSS's MI_Uncompress by construction).
 */

#ifndef HGSS_COMPAT_MI_H
#define HGSS_COMPAT_MI_H

#include_next <nitro/mi.h>
#include <nitro/mi/compress.h>

#endif /* HGSS_COMPAT_MI_H */
