// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/gx/g3imm.h: the immediate-mode geometry helpers the port's tests
 * use, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/gx/g3imm.h provides the mode/push/pop/translate
 * inlines but no G3_Scale / G3_TexCoord / G3_StoreMtx / G3_RestoreMtx, and no
 * declarations for the G3_LoadMtx44 / G3_LoadMtx43 / G3_MultMtx43 /
 * G3_MultMtx33 functions its own g3imm.c defines. The inlines below are
 * ntrtwl's (include/nitro/gx/g3imm.h), writing the same geometry registers
 * (the port's geometry engine intercepts them); the texture-coordinate
 * packing is ntrtwl's GX_ST (the top 16 bits of each fx32).
 */

#ifndef HGSS_COMPAT_GX_G3IMM_H
#define HGSS_COMPAT_GX_G3IMM_H

#include_next <nitro/gx/g3imm.h>

#ifdef __cplusplus
extern "C" {
#endif

void G3_LoadMtx44(const MtxFx44 *mtx);
void G3_LoadMtx43(const MtxFx43 *mtx);
void G3_MultMtx43(const MtxFx43 *mtx);
void G3_MultMtx33(const MtxFx33 *mtx);

static inline void G3_StoreMtx(int num)
{
#ifdef SDK_PORT_3DS
    NitroPort_GxDirectWrite(G3OP_MTX_STORE, GX_PACK_STOREMTX_PARAM(num));
#else
    reg_G3_MTX_STORE = GX_PACK_STOREMTX_PARAM(num);
#endif
}

static inline void G3_RestoreMtx(int num)
{
#ifdef SDK_PORT_3DS
    NitroPort_GxDirectWrite(G3OP_MTX_RESTORE, GX_PACK_RESTOREMTX_PARAM(num));
#else
    reg_G3_MTX_RESTORE = GX_PACK_RESTOREMTX_PARAM(num);
#endif
}

static inline void G3_Scale(fx32 x, fx32 y, fx32 z)
{
#ifdef SDK_PORT_3DS
    NitroPort_GxDirectWrite(G3OP_MTX_SCALE, (u32)x);
    NitroPort_GxDirectWrite(G3OP_MTX_SCALE, (u32)y);
    NitroPort_GxDirectWrite(G3OP_MTX_SCALE, (u32)z);
#else
    reg_G3_MTX_SCALE = (u32)x;
    reg_G3_MTX_SCALE = (u32)y;
    reg_G3_MTX_SCALE = (u32)z;
#endif
}

static inline void G3_TexCoord(fx32 s, fx32 t)
{
#ifdef SDK_PORT_3DS
    NitroPort_GxDirectWrite(G3OP_TEXCOORD,
                            (u32)(u16)(s >> 8) | ((u32)(u16)(t >> 8) << 16));
#else
    reg_G3_TEXCOORD = (u32)(u16)(s >> 8) | ((u32)(u16)(t >> 8) << 16);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_GX_G3IMM_H */
