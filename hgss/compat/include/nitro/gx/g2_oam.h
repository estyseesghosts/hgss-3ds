// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/gx/g2_oam.h: the OBJ attribute helpers, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/gx/g2_oam.h provides GXOamAttr and GXOamMode
 * only: no GXOamAffine, no GX_OAM_SHAPE/COLORMODE/EFFECT enums and no
 * G2_SetOBJAttr/Effect/Affine. Everything below is ntrtwl's
 * (include/nitro/gx/g2_oam.h), as static inlines (no link dependency), minus
 * the SDK_ASSERT checks, which are empty under SDK_FINALROM anyway.
 */

#ifndef HGSS_COMPAT_GX_G2_OAM_H
#define HGSS_COMPAT_GX_G2_OAM_H

#include_next <nitro/gx/g2_oam.h>

#include <nitro/fx/fx.h>
#include <nitro/types.h>

typedef struct {
    u16 _0;
    u16 _1;
    u16 _2;
    s16 PA;
    u16 _4;
    u16 _5;
    u16 _6;
    s16 PB;
    u16 _8;
    u16 _9;
    u16 _10;
    s16 PC;
    u16 _12;
    u16 _13;
    u16 _14;
    s16 PD;
} GXOamAffine;

#define GX_OAM_ATTR01_Y_SHIFT        0
#define GX_OAM_ATTR01_Y_MASK         0x000000ff
#define GX_OAM_ATTR01_RSENABLE_SHIFT 8
#define GX_OAM_ATTR01_RSENABLE_MASK  0x00000300
#define GX_OAM_ATTR01_MODE_SHIFT     10
#define GX_OAM_ATTR01_MODE_MASK      0x00000c00
#define GX_OAM_ATTR01_MOSAIC_SHIFT   12
#define GX_OAM_ATTR01_CM_SHIFT       13
#define GX_OAM_ATTR01_CM_MASK        0x00002000
#define GX_OAM_ATTR01_SHAPE_SHIFT    14
#define GX_OAM_ATTR01_SHAPE_MASK     0x0000c000
#define GX_OAM_ATTR01_X_SHIFT        16
#define GX_OAM_ATTR01_X_MASK         0x01ff0000
#define GX_OAM_ATTR01_RS_SHIFT       25
#define GX_OAM_ATTR01_RS_MASK        0x3e000000
#define GX_OAM_ATTR01_FLIP_MASK      0x30000000
#define GX_OAM_ATTR01_HF_SHIFT       28
#define GX_OAM_ATTR01_VF_SHIFT       29
#define GX_OAM_ATTR01_SIZE_SHIFT     30
#define GX_OAM_ATTR01_SIZE_MASK      0xc0000000
#define GX_OAM_ATTR2_NAME_MASK       0x03ff
#define GX_OAM_ATTR2_PRIORITY_SHIFT  10
#define GX_OAM_ATTR2_PRIORITY_MASK   0x0c00
#define GX_OAM_ATTR2_CPARAM_SHIFT    12
#define GX_OAM_ATTR2_CPARAM_MASK     0xf000

typedef enum {
    GX_OAM_SHAPE_8x8   = (0 << GX_OAM_ATTR01_SHAPE_SHIFT) | (0 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_16x16 = (0 << GX_OAM_ATTR01_SHAPE_SHIFT) | (1 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_32x32 = (0 << GX_OAM_ATTR01_SHAPE_SHIFT) | (2 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_64x64 = (0 << GX_OAM_ATTR01_SHAPE_SHIFT) | (3 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_16x8  = (1 << GX_OAM_ATTR01_SHAPE_SHIFT) | (0 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_32x8  = (1 << GX_OAM_ATTR01_SHAPE_SHIFT) | (1 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_32x16 = (1 << GX_OAM_ATTR01_SHAPE_SHIFT) | (2 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_64x32 = (1 << GX_OAM_ATTR01_SHAPE_SHIFT) | (3 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_8x16  = (2 << GX_OAM_ATTR01_SHAPE_SHIFT) | (0 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_8x32  = (2 << GX_OAM_ATTR01_SHAPE_SHIFT) | (1 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_16x32 = (2 << GX_OAM_ATTR01_SHAPE_SHIFT) | (2 << GX_OAM_ATTR01_SIZE_SHIFT),
    GX_OAM_SHAPE_32x64 = (2 << GX_OAM_ATTR01_SHAPE_SHIFT) | (3 << GX_OAM_ATTR01_SIZE_SHIFT)
} GXOamShape;

typedef enum {
    GX_OAM_COLORMODE_16  = 0,
    GX_OAM_COLORMODE_256 = 1
} GXOamColorMode;

typedef enum {
    GX_OAM_EFFECT_NONE          = (0 << GX_OAM_ATTR01_RSENABLE_SHIFT) | (0 << GX_OAM_ATTR01_HF_SHIFT) | (0 << GX_OAM_ATTR01_VF_SHIFT),
    GX_OAM_EFFECT_FLIP_H        = (0 << GX_OAM_ATTR01_RSENABLE_SHIFT) | (1 << GX_OAM_ATTR01_HF_SHIFT) | (0 << GX_OAM_ATTR01_VF_SHIFT),
    GX_OAM_EFFECT_FLIP_V        = (0 << GX_OAM_ATTR01_RSENABLE_SHIFT) | (0 << GX_OAM_ATTR01_HF_SHIFT) | (1 << GX_OAM_ATTR01_VF_SHIFT),
    GX_OAM_EFFECT_FLIP_HV       = (0 << GX_OAM_ATTR01_RSENABLE_SHIFT) | (1 << GX_OAM_ATTR01_HF_SHIFT) | (1 << GX_OAM_ATTR01_VF_SHIFT),
    GX_OAM_EFFECT_AFFINE        = (1 << GX_OAM_ATTR01_RSENABLE_SHIFT),
    GX_OAM_EFFECT_NODISPLAY     = (2 << GX_OAM_ATTR01_RSENABLE_SHIFT),
    GX_OAM_EFFECT_AFFINE_DOUBLE = (3 << GX_OAM_ATTR01_RSENABLE_SHIFT)
} GXOamEffect;

static inline void G2_SetOBJEffect(GXOamAttr *oam, GXOamEffect effect, int rsParam)
{
    if (GX_OAM_EFFECT_AFFINE != effect && GX_OAM_EFFECT_AFFINE_DOUBLE != effect) {
        oam->attr01 = ((oam->attr01 & ~(GX_OAM_ATTR01_RSENABLE_MASK | GX_OAM_ATTR01_RS_MASK)) |
                       effect);
    } else {
        oam->attr01 = ((oam->attr01 & ~(GX_OAM_ATTR01_RSENABLE_MASK | GX_OAM_ATTR01_RS_MASK)) |
                       effect | (rsParam << GX_OAM_ATTR01_RS_SHIFT));
    }
}

static inline void G2_SetOBJAttr(GXOamAttr *oam, int x, int y, int priority,
                                 GXOamMode mode, BOOL mosaic, GXOamEffect effect,
                                 GXOamShape shape, GXOamColorMode color,
                                 int charName, int cParam, int rsParam)
{
    u32 attr01 = (u32)(((y & 0xff)) |
                       (mode << GX_OAM_ATTR01_MODE_SHIFT) |
                       (mosaic << GX_OAM_ATTR01_MOSAIC_SHIFT) |
                       shape |
                       ((x & 0x1ff) << GX_OAM_ATTR01_X_SHIFT) |
                       effect);

    if (GX_OAM_EFFECT_AFFINE == effect || GX_OAM_EFFECT_AFFINE_DOUBLE == effect) {
        attr01 |= (u32)(rsParam << GX_OAM_ATTR01_RS_SHIFT);
    }
    if (GX_OAM_MODE_BITMAPOBJ != mode) {
        attr01 |= (u32)(color << GX_OAM_ATTR01_CM_SHIFT);
    }
    oam->attr01 = attr01;
    oam->attr2 = (u16)((charName & GX_OAM_ATTR2_NAME_MASK) |
                       (priority << GX_OAM_ATTR2_PRIORITY_SHIFT) |
                       (cParam << GX_OAM_ATTR2_CPARAM_SHIFT));
}

static inline void G2_SetOBJAffine(GXOamAffine *oam, const MtxFx22 *mtx)
{
    oam->PA = (s16)(mtx->_00 >> 4);
    oam->PB = (s16)(mtx->_01 >> 4);
    oam->PC = (s16)(mtx->_10 >> 4);
    oam->PD = (s16)(mtx->_11 >> 4);
}

#endif /* HGSS_COMPAT_GX_G2_OAM_H */
