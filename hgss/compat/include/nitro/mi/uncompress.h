// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/mi/uncompress.h: decompression entry points, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/mi/uncompress.h declares only MI_UncompressLZ8
 * (plus a bare `inline' MI_GetUncompressedSize). The port's svc.c and
 * mi_uncompress.c also need the compression-type names, MIUnpackBitsParam and
 * the remaining entry points, with ntrtwl values
 * (include/nitro/mi/uncompress.h). Declarations match the port's definitions
 * in 3ds_port/nitro/src/mi_uncompress.c. The guard is compat-specific so the
 * chained HGSS header is still processed.
 */

#ifndef HGSS_COMPAT_MI_UNCOMPRESS_H
#define HGSS_COMPAT_MI_UNCOMPRESS_H

#include_next <nitro/mi/uncompress.h>

/* ntrtwl's <nitro/mi.h> also pulls in <nitro/mi/stream.h>; HGSS's does not,
 * and svc.c relies on the chain for MIReadStreamCallbacks, so include it. */
#include <nitro/mi/stream.h>

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MI_COMPRESSION_LZ              = 0x10,
    MI_COMPRESSION_HUFFMAN         = 0x20,
    MI_COMPRESSION_RL              = 0x30,
    MI_COMPRESSION_DIFF            = 0x80,
    MI_COMPRESSION_TYPE_MASK       = 0xf0,
    MI_COMPRESSION_TYPE_EX_MASK    = 0xff
} MICompressionType;

typedef struct {
    u32 compParam :4;
    u32 compType :4;
    u32 destSize :24;
} MICompressionHeader;

typedef struct {
    u16 srcNum;
    u16 srcBitNum :8;
    u16 destBitNum :8;
    u32 destOffset :31;
    u32 destOffset0_on :1;
} MIUnpackBitsParam;

void MI_UnpackBits(const void *srcp, void *destp, MIUnpackBitsParam *paramp);
void MI_UncompressLZ16(const void *srcp, void *destp);
void MI_UncompressHuffman(const void *srcp, void *destp);
void MI_UncompressRL8(const void *srcp, void *destp);
void MI_UncompressRL16(const void *srcp, void *destp);
void MI_UnfilterDiff8(const void *srcp, void *destp);
void MI_UnfilterDiff16(const void *srcp, void *destp);

static inline MICompressionType MI_GetCompressionType(const void *srcp)
{
    return (MICompressionType)(*(u32 *)srcp & MI_COMPRESSION_TYPE_MASK);
}

void MIi_UncompressBackward(void *bottom);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MI_UNCOMPRESS_H */
