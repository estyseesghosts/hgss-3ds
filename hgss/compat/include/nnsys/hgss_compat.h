// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nnsys/hgss_compat.h: the ntrtwl NitroSystem names HGSS's decompilation
 * dropped or renamed, declared over HGSS's own types and symbols.
 *
 * 3ds_port/nitrosystem/tests/test_nitrosystem.c was written against ntrtwl
 * NitroSystem (the one pokeplatinum-3ds builds). HGSS's lib/NitroSystem is a
 * partial decompilation: it implements only the entry points the game uses,
 * its public nnsys headers omit some of the rest, and the C it does provide
 * lives behind names the public headers do not declare. This header closes
 * that gap without touching either tree: missing constants and wrappers are
 * macros or static inlines over HGSS's own public symbols, and the two
 * missing OAM entry points are inlines over the functions HGSS does define.
 *
 * It is included from hgss/compat/include/nitro/snd.h (inside <nitro.h>)
 * because the NNS include directory is searched before hgss/compat/include:
 * a nnsys/... file here would be shadowed by the shim. Only names HGSS lacks
 * are added. NNS_G2dGetNewOamManagerInstance and
 * NNS_G2dApplyAndResetOamManagerBuffer are left to the caller's declaration:
 * ntrtwl types their u16 parameters (default-promoted, so an empty parameter
 * list is not compatible), while the game declares the same symbols with its
 * own OamInstance pointer, so no single prototype here fits both.
 *
 * Everything here is host/GCC-side only (the file is on the port's include
 * path, which the DS build never uses).
 */

#ifndef HGSS_COMPAT_NNSYS_H
#define HGSS_COMPAT_NNSYS_H

#include <nitro/types.h>
/* The public foundation heap types (NNSiFndHeapHead/NNSFndHeapHandle) and
 * NNSFndList; both are layout-identical to ntrtwl's. */
#include <nnsys/fnd/heapcommon.h>
/* NNSG2dCellData/NNSG2dCellDataBank, for NNS_G2dGetCellDataByIdx below. */
#include <nnsys/g2d.h>

/* ------------------------------------------------------------ foundation */

#ifndef NNS_FND_HEAP_INVALID_HANDLE
#define NNS_FND_HEAP_INVALID_HANDLE  NULL
#endif
#ifndef NNS_FND_HEAP_DEFAULT_ALIGNMENT
#define NNS_FND_HEAP_DEFAULT_ALIGNMENT 4
#endif
#ifndef NNS_FND_FRMHEAP_FREE_HEAD
#define NNS_FND_FRMHEAP_FREE_HEAD (1 << 0)
#define NNS_FND_FRMHEAP_FREE_TAIL (1 << 1)
#define NNS_FND_FRMHEAP_FREE_ALL  (NNS_FND_FRMHEAP_FREE_HEAD | NNS_FND_FRMHEAP_FREE_TAIL)
#endif
#ifndef NNSI_UNTHEAP_SIGNATURE
#define NNSI_UNTHEAP_SIGNATURE ('UNTH')
#endif

/* HGSS has these in nnsys/fnd_internal.h only; declare them exactly as its
 * definitions have them (u32 mode, not ntrtwl's int, where they differ). */
NNSFndHeapHandle NNS_FndCreateFrmHeapEx(void *startAddress, u32 size, u16 optFlag);
void NNS_FndDestroyFrmHeap(NNSFndHeapHandle heap);
void *NNS_FndAllocFromFrmHeapEx(NNSFndHeapHandle heap, u32 size, int alignment);
void NNS_FndFreeToFrmHeap(NNSFndHeapHandle heap, u32 mode);
BOOL NNS_FndRecordStateForFrmHeap(NNSFndHeapHandle heap, u32 tag);
BOOL NNS_FndFreeByStateToFrmHeap(NNSFndHeapHandle heap, u32 tag);

#define NNS_FndAllocFromExpHeap(heap, size) \
    NNS_FndAllocFromExpHeapEx(heap, size, NNS_FND_HEAP_DEFAULT_ALIGNMENT)
#define NNS_FndCreateFrmHeap(startAddress, size) \
    NNS_FndCreateFrmHeapEx(startAddress, size, 0)
#define NNS_FndAllocFromFrmHeap(heap, size) \
    NNS_FndAllocFromFrmHeapEx(heap, size, NNS_FND_HEAP_DEFAULT_ALIGNMENT)

/* HGSS's fnd_heapcommon.c keeps the root list and FindContainHeap internal;
 * both are global symbols in libnitrosystem.a (the type is public). */
extern NNSFndList gFndRootHeapList;
NNSiFndHeapHead *FindContainHeap(NNSFndList *list, const void *address);

static inline NNSFndHeapHandle NNS_FndFindContainHeap(const void *memBlock) {
    return (NNSFndHeapHandle)FindContainHeap(&gFndRootHeapList, memBlock);
}

/* Unit heaps: HGSS's lib/NitroSystem/src/fnd_unitheap.c defines only
 * NNS_FndAllocFromUnitHeap and NNS_FndFreeToUnitHeap. Create/destroy/count
 * are the ntrtwl algorithms over HGSS's own heap head and free list
 * (UnitHeap/UnitHeapHead in nnsys/fnd_internal.h: the head, then a pointer to
 * the first free block, then its size). */
typedef struct NNSFndUnitHeapCompat {
    NNSiFndHeapHead head;
    void *freeHead;
    u32 blockSize;
} NNSFndUnitHeapCompat;

void NNSi_FndInitHeapHead(NNSiFndHeapHead *heap, u32 signature, void *heapStart, void *heapEnd, u16 optFlag);
void NNSi_FndFinalizeHeap(NNSiFndHeapHead *heap);

static inline NNSFndHeapHandle NNS_FndCreateUnitHeapEx(void *startAddress, u32 heapSize, u32 memBlockSize,
                                                       int alignment, u16 optFlag) {
    NNSFndUnitHeapCompat *heap;
    u32 start;
    u32 heapStart;
    u32 heapEnd;
    u32 elementNum;
    u32 i;
    u8 *block;

    if (alignment < NNS_FND_HEAP_DEFAULT_ALIGNMENT) {
        alignment = NNS_FND_HEAP_DEFAULT_ALIGNMENT;
    }
    memBlockSize = (memBlockSize + (u32)alignment - 1) & ~((u32)alignment - 1);
    start = ((u32)startAddress + (NNS_FND_HEAP_DEFAULT_ALIGNMENT - 1)) & ~(u32)(NNS_FND_HEAP_DEFAULT_ALIGNMENT - 1);
    heapEnd = ((u32)startAddress + heapSize) & ~(u32)(NNS_FND_HEAP_DEFAULT_ALIGNMENT - 1);
    if (start + sizeof(NNSFndUnitHeapCompat) > heapEnd) {
        return NNS_FND_HEAP_INVALID_HANDLE;
    }
    heapStart = (start + sizeof(NNSFndUnitHeapCompat) + (u32)alignment - 1) & ~((u32)alignment - 1);
    if (heapStart + memBlockSize > heapEnd) {
        return NNS_FND_HEAP_INVALID_HANDLE;
    }
    elementNum = (heapEnd - heapStart) / memBlockSize;
    if (elementNum == 0) {
        return NNS_FND_HEAP_INVALID_HANDLE;
    }
    heapEnd = heapStart + elementNum * memBlockSize;
    heap = (NNSFndUnitHeapCompat *)start;

    NNSi_FndInitHeapHead(&heap->head, NNSI_UNTHEAP_SIGNATURE, (void *)heapStart, (void *)heapEnd, optFlag);
    heap->freeHead = (void *)heapStart;
    heap->blockSize = memBlockSize;
    block = (u8 *)heapStart;
    for (i = 0; i + 1 < elementNum; i++) {
        *(void **)block = block + memBlockSize;
        block += memBlockSize;
    }
    *(void **)block = NULL;
    return &heap->head;
}

static inline void NNS_FndDestroyUnitHeap(NNSFndHeapHandle heap) {
    NNSi_FndFinalizeHeap((NNSiFndHeapHead *)heap);
}

static inline u32 NNS_FndCountFreeBlockForUnitHeap(NNSFndHeapHandle heap) {
    NNSFndUnitHeapCompat *unit = (NNSFndUnitHeapCompat *)heap;
    u32 count = 0;
    void *block;

    for (block = unit->freeHead; block != NULL; block = *(void **)block) {
        count++;
    }
    return count;
}

#define NNS_FndCreateUnitHeap(startAddress, heapSize, memBlockSize) \
    NNS_FndCreateUnitHeapEx(startAddress, heapSize, memBlockSize, NNS_FND_HEAP_DEFAULT_ALIGNMENT, 0)
#define NNS_FndGetMemBlockSizeForUnitHeap(heap) \
    (((NNSFndUnitHeapCompat *)(heap))->blockSize)

/* --------------------------------------------------------------- 2D (GFD) */

#ifndef NNS_GFD_ALLOC_FROM_HIGH
#define NNS_GFD_ALLOC_FROM_HIGH 0
#endif
#ifndef NNS_GFD_ALLOC_FROM_LOW
#define NNS_GFD_ALLOC_FROM_LOW  1
#endif

/* ntrtwl inlines; the key encoding is identical (see the comments in
 * gfd_TexVramMan_Types.h / gfd_PlttVramMan_Types.h). Macros so that the
 * key typedefs need not be visible here. */
#define NNS_GfdGetTexKeySize(memKey) \
    ((u32)(((0x7FFF0000u & (memKey)) >> 16) << 4))
#define NNS_GfdGetPlttKeySize(plttKey) \
    ((u32)(((0xFFFF0000u & (plttKey)) >> 16) << 3))

/* --------------------------------------------------------------- 2D (G2D) */

/* ntrtwl's opaque OAM manager instance is the same storage as HGSS's
 * OamManagerInstance (u32 type, two {u16 first,last,next} ranges, u16 id,
 * u16 pad, BOOL fast transfer). Game code declares its own OamInstance for
 * NNS_G2dGetNewOamManagerInstance/NNS_G2dApplyAndResetOamManagerBuffer, so
 * those two are deliberately left for the caller's own declaration (the
 * NitroSystem test's implicit one); a declaration here with this type, or an
 * empty parameter list (u16 parameters are default-promoted, so it is not
 * compatible), would clash with either g2d_internal.h or the game. */
typedef struct NNSG2dOamManagerInstance {
    u32 type;
    struct { u16 first, last, next; } oam;
    struct { u16 first, last, next; } affine;
    u16 managerId;
    u16 padding;
    BOOL useFastTransfer;
} NNSG2dOamManagerInstance;

#ifndef NNS_G2D_OAMTYPE_MAIN
#define NNS_G2D_OAMTYPE_MAIN 0
#endif

const NNSG2dCellData *NNS_G2dGetCellDataByIdx(const NNSG2dCellDataBank *cellBank, u16 idx);

static inline u16 NNS_G2dGetOamManagerOamCapacity(NNSG2dOamManagerInstance *man) {
    if (man->oam.next <= man->oam.last + 1 && man->oam.first <= man->oam.last) {
        return (u16)(man->oam.last - man->oam.next + 1);
    }
    return 0;
}

/* The OAM shadow buffers, as labelled in HGSS's nnsys_data.s (members of
 * gOamBuffers; declared in nnsys/g2d_internal.h). */
extern GXOamAttr gOamBufferMainOam[];
extern GXOamAttr gOamBufferSubOam[];
extern GXOamAttr gOamBufferSoftOam[];

/* ntrtwl's NNS_G2dEntryOamManagerOam: copy attr0/1/2 into the manager's
 * shadow buffer and advance its cursor. HGSS's equivalent entry point takes
 * an affine index; doing the copy here avoids depending on it (whose
 * prototype uses HGSS's OamManagerInstance). */
static inline BOOL NNS_G2dEntryOamManagerOam(NNSG2dOamManagerInstance *man, const GXOamAttr *oam, u16 num) {
    GXOamAttr *dst;
    u16 i;

    if (NNS_G2dGetOamManagerOamCapacity(man) < num) {
        return FALSE;
    }
    switch (man->type) {
    case 0:
        dst = gOamBufferMainOam;
        break;
    case 1:
        dst = gOamBufferSubOam;
        break;
    case 2:
        dst = gOamBufferSoftOam;
        break;
    default:
        return FALSE;
    }
    dst += man->oam.next;
    for (i = 0; i < num; i++) {
        dst[i].attr0 = oam[i].attr0;
        dst[i].attr1 = oam[i].attr1;
        dst[i].attr2 = oam[i].attr2;
    }
    man->oam.next += num;
    return TRUE;
}

/* --------------------------------------------------------------- 3D (G3D) */

int NNS_G3dGetResDictIdxByName();

/* ntrtwl's NNS_G3dGetMdlByName inline. A statement-expression macro because
 * the model-set types are only defined after <nnsys/g3d.h>, which the caller
 * includes; the index form is HGSS's own lookup. (GCC-only header, like the
 * rest of hgss/compat.) */
#define NNS_G3dGetMdlByName(mdlSet, name) __extension__({                           \
    const NNSG3dResMdlSet *hgssMdlSet_ = (mdlSet);                                  \
    const NNSG3dResName *hgssMdlName_ = (name);                                     \
    int hgssMdlIdx_ = hgssMdlSet_                                                   \
        ? NNS_G3dGetResDictIdxByName(&hgssMdlSet_->dict, hgssMdlName_)              \
        : -1;                                                                       \
    hgssMdlIdx_ < 0                                                                 \
        ? (NNSG3dResMdl *)NULL                                                      \
        : NNS_G3dGetMdlByIdx(hgssMdlSet_, (u32)hgssMdlIdx_);                        \
})

#endif /* HGSS_COMPAT_NNSYS_H */
