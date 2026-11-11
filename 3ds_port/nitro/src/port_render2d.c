/*
 * The DS's two 2D graphics engines, drawn in software.
 *
 * The game sets up the display through registers, palettes, OAM and VRAM,
 * all of which are ordinary memory in the port (port_ioreg.c,
 * port_memory.c). Once a frame this reads that state and draws what each
 * engine would show: 256x192 pixels in the DS's 15-bit colour (bits 0-4 red,
 * 5-9 green, 10-14 blue).
 *
 * What it draws, per engine:
 *   - the BG modes 0-5: text BGs (4 and 8 bits per pixel, flips, extended
 *     palettes), affine BGs, and the extended affine BGs (16-bit map
 *     entries, 256-colour and direct-colour bitmaps), with scrolling and
 *     affine transforms;
 *   - OBJs (sprites): normal and affine (double size), 4 and 8 bits per
 *     pixel with extended palettes, 1D and 2D character mapping, bitmap
 *     OBJs, semi-transparent OBJs and OBJ windows;
 *   - windows 0, 1 and OBJ, colour effects (alpha blending, brightness up
 *     and down) and the master brightness the game fades the screen with;
 *   - engine A's display modes: off, normal, and VRAM display.
 *   - H-blank effects (NitroPort_LineRegs: the battles' start, a wavy
 *     screen): each line's BG scroll and windows as the game set them for
 *     it, and the 3D's scroll on each line for the compositor
 *     (NitroRender2DSplit.scroll3dLine); the rest of the registers (the
 *     colour effects, the affine BGs' matrices, OAM) as they are at the
 *     frame's end.
 * Not yet: DS 3D on the 3DS GPU (PC shows captured triangles), mosaic, the large
 * bitmap BG of mode 6, the main-memory display mode and display capture
 * (the legendary Pokemon's motion blur).
 *
 * VRAM: each CPU view (engine A BG, A OBJ, B BG, B OBJ, LCDC) is its own
 * block in the port (mmap_global.h), so BG and OBJ graphics are read where
 * the game wrote them. Extended palettes are loaded through the LCDC view;
 * the VRAM bank registers say which bank holds them, and they're read from
 * that bank's LCDC area.
 *
 * Speed: this runs on the 3DS's CPU for both screens every frame, so it
 * works a line at a time and a tile at a time: a text BG reads each map
 * entry and each tile row once for the pixels it covers, each line only
 * looks at the OBJs on it (sorted out once a frame), the windows are worked
 * out once a line, and a pixel's two front-most layers are found walking the
 * BGs in priority order. A layer's pixel is its colour with bit 15 set, or 0
 * where it's transparent. tests/render2d_reference.c is the plain version
 * this was made from; test_render checks that both draw the same.
 *
 * The two engines can be drawn at the same time (from two threads): each
 * has its own working memory. One engine must not be drawn by two threads
 * at once.
 */

#include <nitro.h>
#include <nitro_port.h>
#include <nitro_port_gx3d.h>
#include <nitro_port_render2d.h>
#include <nitro_port_wide.h>
#include <nitro_port_wideart.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 192
#define HMAX NITRO_RENDER2D_MAX_H   /* (a tall picture: NitroRender2DSplit.height) */
/* The widest picture drawn (the 3DS remaster's upper screen: the DS's 256
 * pixels and 32 more each side; NitroRender2DSplit's canvas). */
#define WMAX 400   /* (the field at the DS's pixel size: the 3DS's 400; the wider screen's 320) */
#define OPAQUE 0x8000

/* ---------------------------------------------------------------- memory */

extern unsigned char NitroPort_IoReg[];

/* The blocks the renderer reads are aligned, and everything it reads as 16
 * or 32 bits is at an aligned offset in them (the masks keep it so). */
typedef u16 __attribute__((may_alias)) u16a;
typedef u32 __attribute__((may_alias)) u32a;

/* Slice-A profile ticks: the 3DS's tick counter (port_os.c's pattern: the
 * port never includes libctru's headers, so the one prototype it needs is
 * declared here). Host builds have no counter: phases measure 0 there and
 * only the counts are exact.
 *
 * HGSS_PROFILE_OFF (tools/hgss_game.py, tools/hgss_nitro.py) is the OFF build
 * for timing claims: the per-draw tick reads -- 26 per engine draw, about 52
 * a frame -- disappear, so a PROFILE-vs-OFF A/B measures their cost instead
 * of assuming it. The counters that remain are plain memory increments. */
#ifdef HGSS_PROFILE_OFF
#define R2D_NOW() 0
#elif defined(__3DS__)
extern u64 svcGetSystemTick(void);
#define R2D_NOW() svcGetSystemTick()
#else
#define R2D_NOW() 0
#endif

/* Slice-A profile accumulators, per engine. An engine is drawn by one
 * thread at a time (as sWork assumes), so no lock; the getter's copy can
 * tear mid-draw, which is fine for a diagnostic read once per interval. */
static NitroRender2DProfile sProfile[2];

/* R1: only the previous software picture's identity/dependencies, never cached
 * split output or texture pointers. Metadata is rebuilt by the real draw setup.
 * One writer per engine, like sWork and the existing IfChanged records. */
typedef struct SplitReuse {
    u16 *under, *over;
    u8 *alpha;
    u32 small, vram, mask, scroll[4];
    int valid, flat;
} SplitReuse;
static SplitReuse sSplitReuse[2];
static u32 sCommittedTextOnly[2];
static u32 sSplitPictureSerial[2];
/* P3: the flat picture's change-detection state. A flat picture (every
 * layer a GPU take or proven transparent) is re-rendered on every pass
 * today; the pair of signatures below and the committed BGs' scroll say
 * when it is the same picture, so the pass can be skipped. The full-VRAM
 * hash is not a fallback here: a pass the referenced set cannot model
 * keeps today's path (that hash is the cost this packet removes). */
typedef struct FlatReuse {
    int valid;       /* a flat render stored the pair */
    int vramValid;   /* the referenced-range signature modelled the frame */
    u32 small, vram;
    u32 mask;        /* the committed text BGs (their scroll is checked apart) */
    u32 scroll[4];
} FlatReuse;
static FlatReuse sFlatReuse[2];
/* P3: what the flat passes did, and what the diagnostic found. Read by
 * the shell from another thread: added atomically, read as a delta. */
static unsigned sFlatSkip[2], sFlatRedraw[2], sFlatNotModelled[2], sFlatDisagree[2];
/* Why a flat pass did not skip: the picture changed (small = registers,
 * palettes, OAM, layout; vram = the bytes the frame's layers can read;
 * scroll = a committed BG moved; buffers = the drawer's buffers changed). */
static unsigned sFlatWhySmall[2], sFlatWhyVram[2], sFlatWhyScroll[2], sFlatWhyBuffers[2];
/* Why a flat pass did not skip. */
enum {
    FLAT_WHY_NONE = 0,
    FLAT_WHY_SMALL,      /* the registers, palettes, OAM or the layout */
    FLAT_WHY_VRAM,       /* the bytes the frame's layers can read */
    FLAT_WHY_SCROLL,     /* a committed BG moved */
    FLAT_WHY_BUFFERS     /* the drawer's buffers changed */
};
#define FLAT_COUNT_ADD(counter) __atomic_fetch_add(&(counter), 1u, __ATOMIC_RELAXED)
/* IfChanged may resolve an empty software picture after authoritative GPU
 * commit. Forced Split renders retain the scanline reference implementation. */
#define R2D_TRY_FLAT 0x80000000u
static void Fill(u16 *out, u16 color, int n);
static u32 SplitLayoutSignature(int engine, const NitroRender2DSplit *split);
static void RememberSplit(int engine, u16 *under, const NitroRender2DSplit *split,
                         u32 layout, int snapshot);

static inline u16 Rd16(const u8 *p, u32 off)
{
    return *(const u16a *)(p + off);
}

static inline void Wr16(u8 *p, u32 off, u16 v)
{
    *(u16a *)(p + off) = v;
}

static inline u32 Rd32(const u8 *p, u32 off)
{
    return *(const u32a *)(p + off);
}

#define IO16(off) Rd16(NitroPort_IoReg, (off))

/* LCDC addresses of the VRAM banks, relative to the LCDC view. */
static const u32 sBankLcdc[9] = {
    0x00000, 0x20000, 0x40000, 0x60000, 0x80000, 0x90000, 0x94000, 0x98000, 0xA0000
};

static u8 BankCnt(const u8 *io, int bank)
{
    /* VRAMCNT_A-G at 0x240-0x246, H at 0x248, I at 0x249 */
    return io[bank < 7 ? 0x240 + bank : 0x241 + bank];
}

/* The display state the drawing threads draw from (NitroPort_Render2DTake
 * State): the registers, palettes and OAM as they were when the game
 * handed the frame over, so that what the game does meanwhile (its next
 * frame's sprites, scroll, colours) doesn't get mixed into the picture.
 * The VRAM is read as it is (too big to copy each frame; the game changes
 * it in its V-blank work, and the drawers are waited for before that). */
/* The circle fades' windows (NitroPort_SetWindowCircle): on, middle,
 * radius in the DS's pixels; by engine and window. */
typedef struct WindowCircle {
    int on, cx, cy, r;
} WindowCircle;
static WindowCircle sCircles[2][2];
static u32 sCirclesSerial;

void NitroPort_SetWindowCircle(int engine, int window, int cx, int cy, int r)
{
    WindowCircle *c = &sCircles[engine ? 1 : 0][window ? 1 : 0];

    c->on = r >= 0;
    c->cx = cx;
    c->cy = cy;
    c->r = r < 0 ? 0 : r;
    sCirclesSerial++;
}

typedef struct State2D {
    u8 io[0x2000];
    u8 pltt[0x800];
    u8 oam[0x800];
    /* (an H-blank effect: NitroPort_LineRegs) the engine's registers on
     * each line, when they differ */
    int lines;
    u8 lineRegs[H][NITRO_LINE_REGS];
    /* (the wider screen) the windows moved or widened on each BG
     * (NitroPort_WideWindows), as they were: the game takes a menu's frame
     * off before the menu's tiles leave the VRAM (at its next V-blank), so
     * read live the menu showed a frame where the DS has it, 32 pixels to
     * the left of where it had been (2026-10-23); and the screen's layout
     * (NitroPort_WideSplits, NitroPort_WideParts), which the game also sets
     * before the new screen's tiles are in */
    NitroWideWindow wins[4][NITRO_WIDE_MAX_WINDOWS];
    int nwins[4];
    NitroWideSplit splits[NITRO_WIDE_MAX_SPLITS];
    int nsplits;
    NitroWidePart parts[NITRO_WIDE_MAX_PARTS];
    int nparts;
    uint32_t winSerial;
    WindowCircle circles[2];   /* (NitroPort_SetWindowCircle) */
} State2D;
static State2D sState[2];

/* ----------------------------------------------------------- one engine */

#define OBJ_MOVES_MAX 32   /* sprite moves: the layout's, then the moved windows' */
#define WIDE_PICS_MAX 16   /* pictures a layout draws (NITRO_WIDE_PICTURE) */

typedef struct Engine {
    u32 reusePixels;       /* committed-text mask / TRY_FLAT; revalidated below */
    const u8 *ioAll;       /* all the registers (live, or a State2D's) */
    const u8 *io;          /* the engine's registers (A: +0, B: +0x1000) */
    /* (an H-blank effect) its first NITRO_LINE_REGS bytes on each line, or
     * NULL: io's on every line (NitroPort_LineRegs) */
    const u8 (*lines)[NITRO_LINE_REGS];
    const WindowCircle *circles;   /* windows 0 and 1 (NitroPort_SetWindowCircle) */
    const u8 *bgPltt;      /* 0x200 bytes */
    const u8 *objPltt;
    const u8 *oam;         /* 0x400 bytes */
    const u8 *bgVram;
    u32 bgMask;
    const u8 *objVram;
    u32 objMask;
    const u8 *bgExt[4];    /* BG extended palette slots (8 KB each) or NULL */
    const u8 *objExt;      /* OBJ extended palette (8 KB) or NULL */
    BOOL isA;
    /* The picture: width pixels, the DS's x = 0 at canvas x origin (256 and
     * 0 but for the remaster's wider canvas; see NitroRender2DSplit). */
    int width, origin;
    /* The picture's lines: H, or up to HMAX (NitroRender2DSplit.height);
     * the DS's line 0 at the picture's line top (NitroRender2DSplit.top:
     * the BGs' maps, the OBJs and the windows moved down by it), and the
     * lines with the 3D (h3d: the DS's, or all of a tall field's). */
    int height, top, h3d;
    /* (a tall field, worked out each frame) a clipped BG's lines above and
     * below the DS's: its DS's first (or last) line again where that's one
     * colour across (a fade's black, Flash's dark), else nothing (a BG's
     * map past the DS's lines is never shown: the field's text BG had
     * leftover tiles there) */
    u8 tallEdge[4][2];
    /* (a tall field, worked out each frame: DecideTallWindows) the game's
     * windows on a BG in the DS's lower half go to the picture's bottom,
     * those in its upper half to its top (a text box at the screen's
     * bottom, the place name sign at its top): the line of the BG drawn on
     * each of the picture's lines (or -1: none), for the BGs with windows
     * moved (tallMoved) */
    u8 tallMoved;
    short tallSrc[4][HMAX];
    /* Wider canvas: each BG's pixels only within the DS's 256 (clip), or
     * wrapping round its map; the windows the game anchors moved to the
     * screen's edges (anchorWindows); and whether this is one of the
     * screens made of pictures (NitroPort_WidePictures: the opening, the
     * title...), where a plain edge goes on only where it's plain for a
     * band's width (PlainRun): nothing of a picture is ever stretched (an
     * oval of the opening's strip touching the edge was drawn out into a
     * line, 2026-10-14). Nothing is mirrored anywhere. */
    u8 clip[4];
    u8 anchorWindows;
    /* (a battle's start stretched over the wider screen: NitroRender2DSplit.
     * stretch) each OBJ drawn 4/5 as wide round its middle: stretched 1.5625
     * across and 1.25 down by the compositor, its own shape on the screen */
    u8 squeeze;
    const State2D *taken;  /* (drawn from the display state taken: its windows) */
    u8 pictures;
    /* (wider canvas, worked out each frame) a BG's hand-drawn picture
     * (nitro_port_wideart.h): its pixels, how far the BG has scrolled
     * since it was made, and whether it's the whole picture (else the
     * bands beside the DS's only). */
    const u16 *art[4];
    const u8 *artRows[4];  /* (its rows with anything: NitroPort_WideArtRows; NULL, not known) */
    short artDx[4], artDy[4];
    short artH[4], artV[4];  /* (a text BG's) the scroll its picture was made at */
    u8 artFull[4];
    u8 artBands[4];        /* its bands drawn (else nothing there: the layer is elsewhere) */
    /* (a picture of the bands only of a strip that scrolls round: _loop)
     * where its bands, moved with the layer, bring in its middle, the
     * BG's own pixels go on (they're what the middle was) */
    u8 artLoopBands[4];
    /* A whole picture of a layer that has moved away from where it was made
     * (a Pokemon sliding in): the whole layer from the picture, moved
     * (artDx, artDy: MovedOffset's, the slide made as wide as the screen);
     * artExact: moved as far as the layer (else further), so past the
     * picture's edges the BG's own pixels go on. */
    u8 artMoved[4];
    u8 artExact[4];
    /* (worked out each frame, DecideSplits) a text BG that's a picture with
     * a plain middle and decorated sides: its halves go to the screen's
     * edges, the middle widened with its plain colour. */
    u8 split[4];
    /* (worked out each frame, DecideBandFills: the UI's screens) what goes
     * beside the DS's picture on each line of a text BG, left and right:
     * colour | OPAQUE, or 0 nothing. bandFills[bg]: worked out (else the
     * edges are looked at line by line, as before). */
    u8 bandFills[4];
    u16 bandFill[4][2][HMAX];
    /* (worked out each frame) the windows moved to the screen's edges, on
     * the DS's screen (pixels), and how far: the game's sprites over them
     * go with them (a menu's icons). */
    struct { short x0, y0, x1, y1, shift, stretch, dy; } objMove[OBJ_MOVES_MAX];
    int nObjMove;
    /* (wider canvas, the game's: NitroPort_WideSplitScreen,
     * NitroPort_WideScreenParts) a screen laid out anew in parts: the BGs'
     * MOVE and FILL parts (the splits made into parts first), the BGs they
     * are of (a bit each), those the game moves (they show their own
     * pixels, not a hand-drawn picture), whether sprites are moved (they
     * show themselves, not their pictures), and how far the 3D moves. */
    NitroWidePart parts[NITRO_WIDE_MAX_PARTS + 3 * NITRO_WIDE_MAX_SPLITS];
    int nParts;
    u8 partBgs, movedBgs, objsMoved;
    u8 noArtBgs;           /* (NITRO_WIDE_NO_ART) BGs shown without a hand-drawn picture */
    short shift3d;
    /* (NITRO_WIDE_PICTURE parts) the pictures drawn with the BGs: their
     * pixels, size, top-left on the canvas, turn (cos, sin: 16.16), the
     * canvas rectangle they may cover, and the BG; the BGs that have one. */
    struct {
        const u16 *px;
        short w, h, x, y, x0, y0, x1, y1;
        s32 c, s;
        u8 bg, id;
        short angle;
    } pics[WIDE_PICS_MAX];
    int nPics;
    u8 picBgs;
    /* The palettes as pixels (colour | OPAQUE), 0 for the transparent
     * colour: index 0 of each 16 colours (4 bits per pixel), or of all 256
     * (8 bits per pixel). */
    u16 bgPal4[256];
    /* (NITRO_WIDE_CLEAR parts) the BGs (a bit each) with colours shown
     * clear, which (per palette, a bit each index), and their palettes so */
    u8 clearBgs;
    u16 clearMask[16];
    u16 bgPal4Clear[256];
    u16 bgPal8[256];
    u16 objPal4[256];
    u16 objPal8[256];
} Engine;

static void ShiftDown(const struct Engine *e, u8 *regs);

/* Slice B row cache (defined below, used by DrawTextBgT above its
 * definition): a rendered map row per (engine, BG), keyed scroll-invariant. */
typedef struct {
    u64 key;          /* BgKey without scroll + clear variant (0: empty) */
    u16 ty;           /* map row */
    u16 px[512];      /* the row's pixels (colour | OPAQUE, or 0) */
} TextRowEntry;
static TextRowEntry *sTextRows[2][4];
static u64 sTextKey[2][4];
static u64 TextRowKey(const Engine *e, int bg, u16 bgcnt, u32 dispcnt);
static const u16 *TextRowCached(const Engine *e, int pe, int bg, u64 key, int ty, u16 bgcnt,
                                u32 dispcnt);

/* The windows moved or widened on a BG (NitroPort_WideWindows): as they
 * were when the display state was taken, when drawn from that (as the
 * VRAM's tiles were then too), else as they are. How many. */
static int EngineWindows(const Engine *e, int bg, NitroWideWindow *out)
{
    if (e->taken != NULL) {
        memcpy(out, e->taken->wins[bg], (size_t)e->taken->nwins[bg] * sizeof(*out));
        return e->taken->nwins[bg];
    }
    return NitroPort_WideWindows(e->isA ? 0 : 1, bg, out, NITRO_WIDE_MAX_WINDOWS);
}

/* The windows to read, not copy, on a line. The taken state already holds
 * the frame's copy (NitroPort_Render2DTakeState), so the line path reads
 * that instead of copying it per BG per line; a live draw has no such
 * copy and asks each time (its windows can change between lines unseen by
 * a serial: NitroPort_WideWindows also drops a window whose struct the
 * game reused). */
static const NitroWideWindow *EngineWindowsRef(const Engine *e, int bg, int *n, NitroWideWindow *live)
{
    if (e->taken != NULL) {
        *n = e->taken->nwins[bg];
        return e->taken->wins[bg];
    }
    *n = NitroPort_WideWindows(e->isA ? 0 : 1, bg, live, NITRO_WIDE_MAX_WINDOWS);
    return live;
}

/* The OBJ layer's pixels on the current line. */
typedef struct ObjLine {
    u8 prios;              /* bit p: some pixel has priority p */
    short x0, x1;          /* where the line's OBJs are */
    u16 color[WMAX];       /* colour | OPAQUE, or 0 */
    u8 prio[WMAX];
    u8 alpha[WMAX];        /* bitmap OBJ alpha 1-16 (16 = opaque) */
    u8 semi[WMAX];         /* semi-transparent: blends with what's below */
    u8 window[WMAX];       /* inside the OBJ window */
} ObjLine;

static void FindExtPalettes(Engine *e)
{
    int bank, i;

    for (i = 0; i < 4; i++) {
        e->bgExt[i] = NULL;
    }
    e->objExt = NULL;

    for (bank = 0; bank < 9; bank++) {
        u8 cnt = BankCnt(e->ioAll, bank);
        int mst = cnt & 7;
        int ofs = (cnt >> 3) & 3;
        const u8 *lcdc = NitroPort_LcdcVram + sBankLcdc[bank];

        if (!(cnt & 0x80)) {
            continue;
        }
        if (e->isA) {
            if (mst == 4 && bank == 4) {                    /* E: slots 0-3 */
                for (i = 0; i < 4; i++) {
                    e->bgExt[i] = lcdc + i * 0x2000;
                }
            } else if (mst == 4 && (bank == 5 || bank == 6)) {   /* F/G: 2 slots */
                int first = (ofs & 1) * 2;

                e->bgExt[first] = lcdc;
                e->bgExt[first + 1] = lcdc + 0x2000;
            } else if (mst == 5 && (bank == 5 || bank == 6)) {   /* F/G: OBJ */
                e->objExt = lcdc;
            }
        } else {
            if (mst == 2 && bank == 7) {                    /* H: slots 0-3 */
                for (i = 0; i < 4; i++) {
                    e->bgExt[i] = lcdc + i * 0x2000;
                }
            } else if (mst == 3 && bank == 8) {             /* I: OBJ */
                e->objExt = lcdc;
            }
        }
    }
}

static inline u16 PlttColor(const u8 *pltt, u32 index)
{
    return Rd16(pltt, index * 2) & 0x7FFF;
}

static void MakePalettes(Engine *e)
{
    u32 i;

    for (i = 0; i < 256; i++) {
        u16 bg = PlttColor(e->bgPltt, i) | OPAQUE;
        u16 obj = PlttColor(e->objPltt, i) | OPAQUE;

        e->bgPal4[i] = (i & 15) ? bg : 0;
        e->bgPal8[i] = i ? bg : 0;
        e->objPal4[i] = (i & 15) ? obj : 0;
        e->objPal8[i] = i ? obj : 0;
    }
    if (e->clearBgs) {
        for (i = 0; i < 256; i++) {
            e->bgPal4Clear[i] = ((e->clearMask[i >> 4] >> (i & 15)) & 1) ? 0 : e->bgPal4[i];
        }
    }
}

/* A 16-colour BG's palettes (NITRO_WIDE_CLEAR: some colours clear). */
static inline const u16 *BgPal4(const Engine *e, int bg)
{
    return ((e->clearBgs >> bg) & 1) ? e->bgPal4Clear : e->bgPal4;
}

/* ----------------------------------------------------------------- BGs */

enum { BG_NONE, BG_TEXT, BG_AFFINE, BG_EXTENDED, BG_3D, BG_LARGE };

static int BgKind(const Engine *e, int bg, u32 dispcnt)
{
    int mode = dispcnt & 7;

    if (bg == 0 && e->isA && (dispcnt & 8)) {
        return BG_3D;
    }
    switch (mode) {
    case 0:
        return BG_TEXT;
    case 1:
        return bg < 3 ? BG_TEXT : BG_AFFINE;
    case 2:
        return bg < 2 ? BG_TEXT : BG_AFFINE;
    case 3:
        return bg < 3 ? BG_TEXT : BG_EXTENDED;
    case 4:
        return bg < 2 ? BG_TEXT : (bg == 2 ? BG_AFFINE : BG_EXTENDED);
    case 5:
        return bg < 2 ? BG_TEXT : BG_EXTENDED;
    case 6:
        return bg == 2 && e->isA ? BG_LARGE : BG_NONE;
    default:
        return BG_NONE;
    }
}

static u32 CharBase(const Engine *e, u16 bgcnt, u32 dispcnt)
{
    u32 base = ((bgcnt >> 2) & 0xF) * 0x4000;

    if (e->isA) {
        base += ((dispcnt >> 24) & 7) * 0x10000;
    }
    return base;
}

static u32 ScreenBase(const Engine *e, u16 bgcnt, u32 dispcnt)
{
    u32 base = ((bgcnt >> 8) & 0x1F) * 0x800;

    if (e->isA) {
        base += ((dispcnt >> 27) & 7) * 0x10000;
    }
    return base;
}

/* (the remaster's compositor: NitroRender2DSplit.gpuBgs) Of the BGs it
 * asks for (a bit each), the one it can draw itself now, as the DS would:
 * engine A's text BG, 256 x 256, no mosaic, not 8 bits a pixel with the
 * extended palettes; in front of the 3D with nothing between the two (no
 * BG, no OBJ) and BLDCNT's alpha blending it, alone, onto the 3D (the
 * field's weather: the clouds' shadows, fog, sand); no windows and no
 * H-blank effect. Else -1. From the registers and OAM given (the state
 * taken, or the live one). */
/* Slice S5/S6 bundle: how a layer draws as a quad. 1: alpha-blend onto
 * what's under it (strict: sole first target with the 3D second), 0:
 * opaque (nowhere in the blend at all: mode 0, or brightness it isn't a
 * target of), -1: software (blending with another layer, or brightened).
 * Taken layers can never be blend second targets (conservative, exact);
 * brightness targeting a taken layer rejects it, other layers'
 * brightness is independent. */
static int GpuBgBlendMode(int bg, u16 bldcnt)
{
    int first = (bldcnt >> bg) & 1, second = (bldcnt >> (8 + bg)) & 1;

    if (((bldcnt >> 6) & 3) == 1 && first && (bldcnt & 0x3F) == (1u << bg) && (bldcnt & 0x100)) {
        return 1;
    }
    if (!first && !second) {
        return 0;
    }
    return -1;
}

/* Shared z-order/OBJ validation for quad-taken BGs (S5 bundle). Returns 1
 * and sets *behind (0 in front of the 3D, 1 behind it; behind quads draw
 * before the scene and touch no depth, and every downstream blend is a
 * pure function of identical pixels, so they are exact like the front
 * ones), else 0 with *reason. Behind + blending has no quad path. */
static int GpuBgOrderOk(const u8 *io, int bg, int key, int key3d, u32 dispcnt, const u8 *oam, int takeBlend,
                        int *behind, int *reason)
{
    int k, i;

    if (key >= key3d) {
        /* (behind the 3D) */
        if (takeBlend) {
            *reason = NITRO_GPUBG_REJECT_BEHIND;
            return 0;
        }
        *behind = 1;
    } else {
        *behind = 0;
    }
    for (k = 1; k < 4; k++) {
        int kk;

        if (k == bg || !(dispcnt & (0x100u << k))) {
            continue;
        }
        kk = (Rd16(io, 8 + k * 2) & 3) * 4 + k;
        if (kk > key && kk < key3d) {
            *reason = NITRO_GPUBG_REJECT_BETWEEN;
            return 0;   /* (a BG between it and the 3D) */
        }
    }
    if (dispcnt & 0x1000) {
        int prioBg = key / 4, prio3d = key3d / 4;

        for (i = 0; i < 128; i++) {
            u16 a0 = Rd16(oam, (u32)i * 8), a2 = Rd16(oam, (u32)i * 8 + 4);
            int prio = (a2 >> 10) & 3;

            if (!(a0 & 0x100) && (a0 & 0x200)) {
                continue;   /* (hidden) */
            }
            if (((a0 >> 10) & 3) == 2) {
                continue;   /* (a window OBJ: no windows anyway) */
            }
            /* (an OBJ is in front of the BGs of its priority or lower) */
            if (prio > prioBg && prio <= prio3d) {
                *reason = NITRO_GPUBG_REJECT_OBJ;
                return 0;   /* (an OBJ between it and the 3D) */
            }
            /* (a semi-transparent or bitmap OBJ in front of it: the DS
             * blends it with what's under it, this BG; left out, it would
             * be blended with the 3D instead) */
            if (prio <= prioBg && ((a0 >> 10) & 3) != 0) {
                *reason = NITRO_GPUBG_REJECT_OBJBLEND;
                return 0;
            }
        }
    }
    return 1;
}
/* Per-BG quad validation (S5 bundle): GpuBgOf's checks for one BG.
 * Returns the BG, or -(1 + reason) with a NitroGpuBgReject reason.
 * *blend: 1 alpha-blend onto the 3D, 0 opaque. *behind: 0 in front of the
 * 3D, 1 behind it (flat only: behind + blending has no quad path).
 * Frame checks (H-blank lines, windows, 3D mode) count per evaluated BG. */
static int GpuBgValidBg(const u8 *io, const u8 *oam, int lines, int bg, int *blend, int *behind)
{
    u32 dispcnt = Rd32(io, 0);
    u16 bldcnt = Rd16(io, 0x50), bgcnt;
    int key, key3d, mode = dispcnt & 7, takeBlend, reason = 0;

    if (lines || (dispcnt & 0xE000) || (dispcnt & 0x108) != 0x108 || ((dispcnt >> 16) & 3) != 1) {
        return -(1 + NITRO_GPUBG_REJECT_HBLANK);
    }
    if (!(dispcnt & (0x100u << bg))) {
        return -(1 + NITRO_GPUBG_REJECT_OFF);
    }
    bgcnt = Rd16(io, 8 + bg * 2);
    {
        /* (a text BG in this mode: BgKind's; any map size: the picture and
         * the compositor's quad are map-sized, scrolled by UV) */
        int text = mode == 0 || (mode == 1 && bg < 3) || (mode == 2 && bg < 2) || (mode == 3 && bg < 3) ||
                   (mode >= 4 && mode <= 5 && bg < 2);

        if (!text) {
            return -(1 + NITRO_GPUBG_REJECT_MODE);
        }
        if (bgcnt & 0x40) {
            return -(1 + NITRO_GPUBG_REJECT_MOSAIC);
        }
        if ((bgcnt & 0x80) && (dispcnt & 0x40000000u)) {
            return -(1 + NITRO_GPUBG_REJECT_EXT);
        }
        key = (bgcnt & 3) * 4 + bg;
    }
    key3d = (Rd16(io, 8) & 3) * 4;
    takeBlend = GpuBgBlendMode(bg, bldcnt);
    if (takeBlend < 0) {
        return -(1 + NITRO_GPUBG_REJECT_BLEND);
    }
    if (!GpuBgOrderOk(io, bg, key, key3d, dispcnt, oam, takeBlend, behind, &reason)) {
        return -(1 + reason);
    }
    if (blend != NULL) {
        *blend = takeBlend;
    }
    return bg;
}

/* Slice S5 bundle: affine BG validation. Same z-order/blend rules as
 * text (shared helpers); affine specifics: any power-of-two size up to
 * the quad texture (1024 rejected), wrap flag required (overflow-off
 * clamps to edge texels, the DS shows transparent), no mosaic, no
 * ext-palette mode (the 256-colour palette is unresolved there). */
static int GpuBgAffineBg(const u8 *io, const u8 *oam, int bg, u32 dispcnt, int *blend, int *behind)
{
    u16 bgcnt = Rd16(io, 8 + bg * 2), bldcnt = Rd16(io, 0x50);
    int size = (bgcnt >> 14) & 3;
    int key = (bgcnt & 3) * 4 + bg, key3d = (Rd16(io, 8) & 3) * 4;
    int takeBlend, reason = 0;

    if (bgcnt & 0x40) {
        return -(1 + NITRO_GPUBG_REJECT_MOSAIC);
    }
    if (size == 3) {
        return -(1 + NITRO_GPUBG_REJECT_SIZE);
    }
    if (!(bgcnt & 0x2000)) {
        return -(1 + NITRO_GPUBG_REJECT_NOWRAP);
    }
    if (dispcnt & 0x40000000u) {
        return -(1 + NITRO_GPUBG_REJECT_EXT);
    }
    takeBlend = GpuBgBlendMode(bg, bldcnt);
    if (takeBlend < 0) {
        return -(1 + NITRO_GPUBG_REJECT_BLEND);
    }
    if (!GpuBgOrderOk(io, bg, key, key3d, dispcnt, oam, takeBlend, behind, &reason)) {
        return -(1 + reason);
    }
    if (blend != NULL) {
        *blend = takeBlend;
    }
    return bg;
}

/* First quad-taken BG (S5 bundle, front or flat-behind): what
 * SplitIfChanged hashes. The drawer evaluates each BG itself. */
static int GpuBgOf(const u8 *io, const u8 *oam, int lines, int wanted, int *blend)
{
    u32 dispcnt = Rd32(io, 0);
    int bg, behind = 0;

    if (wanted == 0) {
        return -(1 + NITRO_GPUBG_REJECT_SETUP);
    }
    for (bg = 1; bg < 4 && !((wanted >> bg) & 1); bg++) {
    }
    if (bg > 3 || !(dispcnt & (0x100u << bg))) {
        return -(1 + NITRO_GPUBG_REJECT_OFF);
    }
    return GpuBgValidBg(io, oam, lines, bg, blend, &behind);
}

/* The extended palette slot a BG uses: its own number, or BG0/BG1 can
 * take slot 2/3 (BGxCNT bit 13). */
static const u8 *BgExtSlot(const Engine *e, int bg, u16 bgcnt)
{
    int slot = bg;

    if (bg < 2 && (bgcnt & 0x2000)) {
        slot += 2;
    }
    return e->bgExt[slot];
}

/* ---------------------------------------------------------- the line */

/*
 * Where a layer's pixels go. Two ways:
 *   - T_PLAIN: every pixel is written (colour | OPAQUE, or 0), to a line of
 *     the layer's own;
 *   - otherwise into the screen's line, front layer first: a pixel only
 *     goes where no layer in front has put one (and with T_WINDOW only where
 *     the windows show the layer; with T_LAYER, which layer it was is noted
 *     for the colour effects).
 */
enum { T_PLAIN = 1, T_WINDOW = 2, T_LAYER = 4 };

typedef struct Target {
    u16 *row;
    u8 *layerOf;           /* T_LAYER */
    const u8 *enable;      /* T_WINDOW: the layers each pixel shows */
    u8 layer;
    u8 bit;                /* 1 << layer */
    int filled;            /* pixels of the line that have their layer */
} Target;

/* One pixel into the line. A macro over the drawing function's own copies
 * of the target (row, enable, layerOf, layer, bit, filled), which the
 * compiler can keep in registers: through the Target it would have to
 * reload them after every pixel written. */
#define PUT(x_, c_, flags_)                                                  \
    do {                                                                     \
        int px_ = (x_);                                                      \
        u16 pc_ = (c_);                                                      \
                                                                             \
        if ((flags_) & T_PLAIN) {                                            \
            row[px_] = pc_;                                                  \
        } else if (pc_ && !(row[px_] & OPAQUE) &&                            \
                   (!((flags_) & T_WINDOW) || (enable[px_] & bit))) {        \
            row[px_] = pc_;                                                  \
            if ((flags_) & T_LAYER) {                                        \
                layerOf[px_] = layer;                                        \
            }                                                                \
            filled++;                                                        \
        }                                                                    \
    } while (0)

/* Whether PUT would leave pixel x as it is whatever the colour: a layer in
 * front has it, or the window hides this layer there (not for a layer's
 * own line, T_PLAIN). A pixel worked out per pixel (a hand-drawn picture's)
 * isn't worked out then: on the opening's strips, two of three BGs' picture
 * pixels were behind another's (2026-10-21). */
#define COVERED(x_, flags_)                                                  \
    (!((flags_) & T_PLAIN) &&                                                \
     ((row[(x_)] & OPAQUE) || (((flags_) & T_WINDOW) && !(enable[(x_)] & bit))))

#define TARGET_LOCALS(t)                                                     \
    u16 *row = (t)->row;                                                     \
    u8 *layerOf = (t)->layerOf;                                              \
    const u8 *enable = (t)->enable;                                          \
    u8 layer = (t)->layer;                                                   \
    u8 bit = (t)->bit;                                                       \
    int filled = (t)->filled

/* A run of a BG's line on the canvas: x0..x1, showing map x = x + off
 * (wrapping round the map). */
typedef struct Segment {
    short x0, x1, off;
    short fixed;   /* >= 0: every pixel of the run is the map's pixel at x = fixed */
    u16 color;     /* (SEG_COLOR) */
} Segment;
#define SEG_COLOR (-7)     /* every pixel this colour (Segment.color) */
#define SEG_ART (-3)       /* the BG's hand-drawn picture (Engine.art), in the bands */
#define SEG_ART_MID (-4)   /* the same over the DS's part: the picture where it has it, else the BG's own */
#define SEG_CLEAR (-5)     /* nothing */
#define SEG_ART_MOVED (-6) /* the whole layer from its picture, moved (Engine.artMoved) */
#define MAX_SEGMENTS 24

static const int sTextSizes[4][2] = { { 256, 256 }, { 512, 256 }, { 256, 512 }, { 512, 512 } };
/* Affine BG maps are square powers of two (DrawAffineBg: 128 << size). */
static const int sAffineSizes[4][2] = { { 128, 128 }, { 256, 256 }, { 512, 512 }, { 1024, 1024 } };

/* One pixel of a text BG's map (map x, map row ty): colour | OPAQUE, or 0. */
static u16 TextBgPixel(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int mapx, int ty)
{
    int mapW = sTextSizes[bgcnt >> 14][0];
    const u8 *vram = e->bgVram;
    u32 mask = e->bgMask;
    u32 screenBase = ScreenBase(e, bgcnt, dispcnt);
    u32 charBase = CharBase(e, bgcnt, dispcnt);
    int tx = mapx & (mapW - 1);
    u32 rowAddr = screenBase + (u32)((ty >> 8) * (mapW >> 8)) * 0x800 + (u32)((ty & 255) >> 3) * 64;
    u32 entry = Rd16(vram, (rowAddr + (u32)(tx >> 8) * 0x800 + (u32)((tx & 255) >> 3) * 2) & mask);
    u32 tile = entry & 0x3FF;
    int py = (entry & 0x800) ? 7 - (ty & 7) : (ty & 7);
    int px = (entry & 0x400) ? 7 - (tx & 7) : (tx & 7);

    if (bgcnt & 0x80) {
        u32 index = vram[(charBase + tile * 64 + (u32)py * 8 + (u32)px) & mask];
        const u8 *ext = (dispcnt & 0x40000000) ? BgExtSlot(e, bg, bgcnt) : NULL;

        if (ext != NULL) {
            return index ? (u16)(PlttColor(ext + ((entry >> 12) << 9), index) | OPAQUE) : 0;
        }
        return e->bgPal8[index];
    } else {
        u8 pair = vram[(charBase + tile * 32 + (u32)py * 4 + (u32)(px >> 1)) & mask];

        return BgPal4(e, bg)[((entry >> 12) << 4) | ((px & 1) ? (pair >> 4) : (pair & 15))];
    }
}

/* A pixel of a run of a BG's line as the run draws it (not a hand-drawn
 * picture's): the map's, its colour, or nothing. */
static u16 SegmentPixel(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, const Segment *s, int x, int ty, int mapW)
{
    if (s->fixed >= 0) {
        return TextBgPixel(e, bg, bgcnt, dispcnt, s->fixed, ty);
    }
    if (s->fixed == -1) {
        return TextBgPixel(e, bg, bgcnt, dispcnt, (x + s->off) & (mapW - 1), ty);
    }
    return s->fixed == SEG_COLOR ? s->color : 0;
}

static int TextBgSegments(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int y, int ty, int hofs, int mapW,
                          Segment *segs, int ownOnly);

/* A text BG's own runs on one row of the canvas, without its hand-drawn
 * picture (TextBgSegments, ownOnly), kept from one pixel to the next:
 * y < 0 till a row's are worked out. */
typedef struct OwnRow {
    Segment segs[MAX_SEGMENTS];
    int n, y;
} OwnRow;
static inline void OwnRowStart(OwnRow *own)
{
    own->n = 0;
    own->y = -1;
}

/* A text BG's own pixel at canvas x, y (any: past the canvas too), the BG
 * drawn as it is, without its hand-drawn picture, at the scroll hofs,
 * vofs: its runs' on the canvas (TextBgSegments, ownOnly: the windows
 * moved and the parts as the port lays it out, the map going on past the
 * DS's edges; where a window was, nothing), the map's on the rows past
 * the canvas. The same as a capture's bg<N>_map.png. */
static u16 OwnTextPixel(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int hofs, int vofs, int mapW, int mapH,
                        int x, int y, OwnRow *own)
{
    int ty = (y + vofs) & (mapH - 1), k;

    if (y < 0 || y >= H) {
        return TextBgPixel(e, bg, bgcnt, dispcnt, (x - e->origin + hofs) & (mapW - 1), ty);
    }
    if (own->y != y) {
        own->n = TextBgSegments(e, bg, bgcnt, dispcnt, y, ty, hofs, mapW, own->segs, 1);
        own->y = y;
    }
    for (k = 0; k < own->n; k++) {
        if (x >= own->segs[k].x0 && x < own->segs[k].x1) {
            return SegmentPixel(e, bg, bgcnt, dispcnt, &own->segs[k], x, ty, mapW);
        }
    }
    return 0;
}

/* A text BG's hand-drawn picture's pixel (ax, ay: on the canvas, the
 * picture's row ay): its own, or for NITRO_WIDE_ART_GAME (a picture that
 * leaves the game's art out: nitro_port_wideart.h) the BG's own pixel
 * where the picture had it, as far back as the picture moved it: the BG
 * drawn as it is, without the picture, at the scroll the picture was made
 * at (artH, artV: OwnTextPixel), so a BG scrolled or slid since shows the
 * same art (its key) there. */
static inline __attribute__((always_inline)) u16 ArtTextPixel(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int ax,
                                                              int ay, int mapW, int mapH, OwnRow *own)
{
    u16 c = e->art[bg][ay * NITRO_WIDE_ART_W + ax];

    if (!NitroPort_WideArtIsGame(c)) {
        return c;
    }
    return OwnTextPixel(e, bg, bgcnt, dispcnt, e->artH[bg], e->artV[bg], mapW, mapH,
                        ax - NitroPort_WideArtGameDx(c), ay - NitroPort_WideArtGameDy(c), own);
}

/* Whether a text BG's map has its tile at map x mapx (row ty) flipped
 * sideways: a mirror of another (the battle background's map is its
 * picture, then the same picture mirrored, for its slide-in). */
static BOOL TextBgTileMirrored(const Engine *e, u16 bgcnt, u32 dispcnt, int mapx, int ty)
{
    int mapW = sTextSizes[bgcnt >> 14][0];
    u32 screenBase = ScreenBase(e, bgcnt, dispcnt);
    int tx = mapx & (mapW - 1);
    u32 rowAddr = screenBase + (u32)((ty >> 8) * (mapW >> 8)) * 0x800 + (u32)((ty & 255) >> 3) * 64;
    u32 entry = Rd16(e->bgVram, (rowAddr + (u32)(tx >> 8) * 0x800 + (u32)((tx & 255) >> 3) * 2) & e->bgMask);

    return (entry & 0x400) != 0;
}

/* How much of a line at the DS's edge must be one colour for that colour
 * to go on beside it: a band's width (32) on the screens made of pictures,
 * where anything shorter is part of the picture (the opening strip's ovals
 * and streaks were drawn out into lines), 8 elsewhere (a window's frame, a
 * fade's black). */
static inline int PlainRun(const Engine *e)
{
    return e->pictures ? 32 : 8;
}

/* Whether the DS's pixels x0..x0+n-1 of a text BG's line (map row ty, its
 * scroll hofs) are all one opaque colour. */
static BOOL TextBgPlain(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int hofs, int ty, int x0, int n)
{
    u16 first = TextBgPixel(e, bg, bgcnt, dispcnt, x0 + hofs, ty);
    int i;

    if (!(first & OPAQUE)) {
        return FALSE;
    }
    for (i = 1; i < n; i++) {
        if (TextBgPixel(e, bg, bgcnt, dispcnt, x0 + i + hofs, ty) != first) {
            return FALSE;
        }
    }
    return TRUE;
}

/* (wider canvas) Which text BGs are pictures to split (Engine.split): a
 * plain middle (the DS's x 96-159) on every line, and sides that aren't
 * plain on a quarter of them or more, the intro's bars, say. */
static void DecideSplits(Engine *e, const int *kind, const u16 *bgcnt, u32 dispcnt)
{
    int bg;

    for (bg = 0; bg < 4; bg++) {
        int y, lines = 0, decorated = 0, mapH, hofs, vofs;

        e->split[bg] = 0;
        if (kind[bg] != BG_TEXT || !e->clip[bg] || ((e->partBgs >> bg) & 1)) {
            continue;
        }
        mapH = sTextSizes[bgcnt[bg] >> 14][1];
        hofs = Rd16(e->io, 0x10 + bg * 4) & 0x1FF;
        vofs = Rd16(e->io, 0x12 + bg * 4) & 0x1FF;
        for (y = 4; y < H; y += 8) {
            int ty = (y + vofs) & (mapH - 1);

            if (!TextBgPlain(e, bg, bgcnt[bg], dispcnt, hofs, ty, 96, 64)) {
                break;
            }
            lines++;
            if (!TextBgPlain(e, bg, bgcnt[bg], dispcnt, hofs, ty, 0, 8) ||
                !TextBgPlain(e, bg, bgcnt[bg], dispcnt, hofs, ty, 248, 8)) {
                decorated++;
            }
        }
        e->split[bg] = lines == H / 8 && decorated * 4 >= lines;
    }
}

/* (wider canvas, each frame) Engine.bandFill: on each line of a text BG,
 * beside the DS's picture, the colour of its edge where that's plain
 * (PlainRun); and on the UI's screens, on lines where the BG has something
 * at its edge that isn't plain (a menu's panel reaching it), the plain
 * lines next to them carried on through them: the pattern of those just
 * above (or below), every 1 to 8 lines (a background's stripes go on
 * behind the panel). Beside a party list's panels and a
 * summary's boxes, the bands were black (nothing there: the backdrop),
 * 2026-10-15. */
static void DecideBandFills(Engine *e, const int *kind, const u16 *bgcnt, u32 dispcnt, u32 emptyText)
{
    int bg, side;

    for (bg = 0; bg < 4; bg++) {
        int mapH, hofs, vofs, y, run = PlainRun(e);
        u16 own[2][HMAX];
        u8 edge[2][HMAX];   /* the BG has something at its edge, not plain */

        e->bandFills[bg] = 0;
        if ((emptyText & (1u << bg)) || e->width <= W || kind[bg] != BG_TEXT || !e->clip[bg] || e->art[bg] != NULL || e->split[bg] ||
            ((e->partBgs >> bg) & 1)) {
            continue;
        }
        mapH = sTextSizes[bgcnt[bg] >> 14][1];
        hofs = Rd16(e->io, 0x10 + bg * 4) & 0x1FF;
        vofs = Rd16(e->io, 0x12 + bg * 4) & 0x1FF;
        e->bandFills[bg] = 1;
        for (side = 0; side < 2; side++) {
            int x0 = side ? W - run : 0;

            for (y = 0; y < e->height; y++) {
                int ty = (y + vofs) & (mapH - 1);

                own[side][y] = TextBgPlain(e, bg, bgcnt[bg], dispcnt, hofs, ty, x0, run)
                                   ? TextBgPixel(e, bg, bgcnt[bg], dispcnt, x0 + hofs + (side ? run - 1 : 0), ty)
                                   : 0;
                edge[side][y] = !own[side][y] &&
                                (TextBgPixel(e, bg, bgcnt[bg], dispcnt, (side ? W - 1 : 0) + hofs, ty) & OPAQUE);
                e->bandFill[bg][side][y] = own[side][y];
            }
        }
        if (e->pictures) {
            continue;   /* (the pictures' screens: plain edges only) */
        }
        for (side = 0; side < 2; side++) {
            y = 0;
            while (y < e->height) {
                int a, b, k, p, up, down, src, dir, len;

                if (!edge[side][y]) {
                    y++;
                    continue;
                }
                /* a run of such lines, a .. b - 1 */
                for (a = y, b = y; b < e->height && edge[side][b]; b++) {
                }
                y = b;
                /* the plain lines next to it, above and below (as many as
                 * there are in a row, 16 at most) */
                for (up = 0; up < 16 && a - 1 - up >= 0 && own[side][a - 1 - up]; up++) {
                }
                for (down = 0; down < 16 && b + down < e->height && own[side][b + down]; down++) {
                }
                /* carried on from above (from below if there are fewer
                 * than two plain lines above): the pattern of the plain
                 * lines next to the run (every 1 to 8 lines: the shortest
                 * that repeats over the nearest twice its length, 4 lines
                 * at least), repeated through the run */
                if (up == 0 && down == 0) {
                    continue;
                }
                dir = up >= 2 || down == 0 ? -1 : 1;
                len = dir < 0 ? up : down;
                src = dir < 0 ? a - 1 : b;   /* the plain line next to the run */
                for (p = 1; p < 8 && p < len; p++) {
                    int ok = 1, span = 2 * p > 4 ? 2 * p : 4;

                    if (span > len) {
                        span = len;
                    }
                    for (k = 0; k + p < span && ok; k++) {
                        ok = own[side][src + dir * k] == own[side][src + dir * (k + p)];
                    }
                    if (ok) {
                        break;
                    }
                }
                if (p > len) {
                    p = len;
                }
                for (k = a; k < b; k++) {
                    /* line k: as far into the run as it is, a whole number
                     * of patterns back into the plain lines */
                    int into = dir < 0 ? k - a : b - 1 - k;   /* 0: next to them */
                    int back = (into % p);

                    e->bandFill[bg][side][k] = own[side][src + dir * (p - 1 - back)];
                }
            }
        }
    }
}

/* sin(degrees) in 16.16 (without the maths library: a few terms of its
 * series, on 0-90 degrees). */
static s32 SinFx(int degrees)
{
    int d = degrees % 360, sign = 1;
    float x, x2, v;

    if (d < 0) {
        d += 360;
    }
    if (d >= 180) {
        d -= 180;
        sign = -1;
    }
    if (d > 90) {
        d = 180 - d;
    }
    x = (float)d * 0.017453292f;
    x2 = x * x;
    v = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
    return sign * (s32)(v * 65536.0f + 0.5f);
}

/* (wider canvas) A picture a layout draws (NITRO_WIDE_PICTURE): where, with
 * which BG, turned how far; left out if its file isn't there. */
static void SetUpPicture(Engine *e, const NitroWidePart *p)
{
    int w, h, dx, dy, bg, r;
    const u16 *px;

    if (e->nPics >= WIDE_PICS_MAX || (px = NitroPort_WideArtPicture(p->col, &w, &h, &dx, &dy)) == NULL) {
        return;
    }
    for (bg = 0; bg < 4 && !((p->bgs >> bg) & 1); bg++) {
    }
    if (bg == 4) {
        return;
    }
    e->pics[e->nPics].px = px;
    e->pics[e->nPics].w = (short)w;
    e->pics[e->nPics].h = (short)h;
    e->pics[e->nPics].x = (short)(e->origin + p->x0 + dx);   /* (moved from its place in the editor) */
    e->pics[e->nPics].y = (short)(p->top + dy);
    e->pics[e->nPics].bg = (u8)bg;
    e->pics[e->nPics].id = (u8)p->col;
    e->pics[e->nPics].angle = p->dx;
    e->pics[e->nPics].s = SinFx(p->dx);
    e->pics[e->nPics].c = SinFx(p->dx + 90);
    if (p->dx % 360 == 0) {
        e->pics[e->nPics].c = 0x10000;
        e->pics[e->nPics].s = 0;
        e->pics[e->nPics].x0 = e->pics[e->nPics].x;
        e->pics[e->nPics].y0 = e->pics[e->nPics].y;
        e->pics[e->nPics].x1 = (short)(e->pics[e->nPics].x + w);
        e->pics[e->nPics].y1 = (short)(e->pics[e->nPics].y + h);
    } else {
        /* (turned: within its middle's distance from its corners, at most
         * 3/4 of its longer side) */
        r = (w > h ? w : h) * 3 / 4 + 1;
        e->pics[e->nPics].x0 = (short)(e->pics[e->nPics].x + w / 2 - r);
        e->pics[e->nPics].y0 = (short)(e->pics[e->nPics].y + h / 2 - r);
        e->pics[e->nPics].x1 = (short)(e->pics[e->nPics].x + (w + 1) / 2 + r);
        e->pics[e->nPics].y1 = (short)(e->pics[e->nPics].y + (h + 1) / 2 + r);
    }
    e->picBgs |= (u8)(1 << bg);
    e->nPics++;
}

/* The pictures drawn with BG bg on line y (NITRO_WIDE_PICTURE): over line,
 * where they have a pixel (later ones over earlier ones), the columns they
 * may cover in *x0..*x1 (cleared first when clear). Returns whether any is
 * on the line. */
static BOOL PicturesOnLine(const Engine *e, int bg, int y, u16 *line, BOOL clear, int *x0, int *x1)
{
    int i, x, a = e->width, b = 0;

    for (i = 0; i < e->nPics; i++) {
        if (e->pics[i].bg == bg && y >= e->pics[i].y0 && y < e->pics[i].y1) {
            if (e->pics[i].x0 < a) {
                a = e->pics[i].x0;
            }
            if (e->pics[i].x1 > b) {
                b = e->pics[i].x1;
            }
        }
    }
    if (a < 0) {
        a = 0;
    }
    if (b > e->width) {
        b = e->width;
    }
    if (a >= b) {
        return FALSE;
    }
    if (clear) {
        memset(line + a, 0, (size_t)(b - a) * sizeof(u16));
    }
    for (i = 0; i < e->nPics; i++) {
        const u16 *px = e->pics[i].px;
        int w = e->pics[i].w, h = e->pics[i].h, px0 = e->pics[i].x0, px1 = e->pics[i].x1;

        if (e->pics[i].bg != bg || y < e->pics[i].y0 || y >= e->pics[i].y1) {
            continue;
        }
        if (px0 < 0) {
            px0 = 0;
        }
        if (px1 > e->width) {
            px1 = e->width;
        }
        if (e->pics[i].s == 0 && e->pics[i].c == 0x10000) {
            const u16 *row = px + (y - e->pics[i].y) * w - e->pics[i].x;

            for (x = px0; x < px1; x++) {
                if (row[x]) {
                    line[x] = row[x];
                }
            }
        } else {
            /* Turned about its middle: each pixel from the picture's pixel
             * there turned back (16.16). */
            s32 c = e->pics[i].c, s = e->pics[i].s;
            s32 cx = ((s32)e->pics[i].x << 16) + (w << 15), cy = ((s32)e->pics[i].y << 16) + (h << 15);
            s32 dy = ((s32)y << 16) + 0x8000 - cy;

            for (x = px0; x < px1; x++) {
                s32 dx = ((s32)x << 16) + 0x8000 - cx;
                s32 u = (s32)(((s64)c * dx + (s64)s * dy) >> 16) + (w << 15);
                s32 v = (s32)(((s64)c * dy - (s64)s * dx) >> 16) + (h << 15);
                int sx = u >> 16, sy = v >> 16;

                if (u >= 0 && v >= 0 && sx < w && sy < h && px[sy * w + sx]) {
                    line[x] = px[sy * w + sx];
                }
            }
        }
    }
    *x0 = a;
    *x1 = b;
    return TRUE;
}

/* (wider canvas: a screen laid out anew, NitroPort_WideSplitScreen and
 * NitroPort_WideScreenParts) A run of a line of a BG with parts: canvas
 * x c0..c1-1 showing the BG's column c - origin - shift (+ its scroll), or
 * its column col (col >= 0). */
typedef struct PartRun {
    short c0, c1, shift, col;
} PartRun;
#define PART_RUNS_MAX 24
#define PART_LINE_MAX 48   /* parts on one line of a BG (more: left out) */

/* A part's column x as a column of the BG's picture: the DS screen's
 * column x (NITRO_WIDE_SCREEN_COLUMNS) is the picture's x + hofs. */
static int PartX(const NitroWidePart *p, int x, int hofs)
{
    return (p->bgs & NITRO_WIDE_SCREEN_COLUMNS) && x > -0x7FFF && x < 0x7FFF ? x + hofs : x;
}

/* The runs of line ty of BG bg (its own lines and columns; hofs: a text
 * BG's scroll, 0 for an affine BG's picture), left to right: how many; -1
 * if no part is on the line (where the DS has it, nothing beside). The
 * DS's picture where it is, but for the columns the MOVEs take (only the
 * DS's columns, hofs..hofs + 255); over it each part in turn. */
static int PartRuns(const Engine *e, int bg, int ty, int hofs, PartRun *runs)
{
    struct { short c0, c1, prec, shift, col; } iv[2 * PART_LINE_MAX + 1];
    short cut[2 * (2 * PART_LINE_MAX + 1)];
    int niv = 0, ncut = 0, i, k, n = 0, origin = e->origin, width = e->width;

    for (i = 0; i < e->nParts && niv < PART_LINE_MAX; i++) {
        const NitroWidePart *p = &e->parts[i];
        int c0, c1, px0 = PartX(p, p->x0, hofs), px1 = PartX(p, p->x1, hofs);

        if (!((p->bgs >> bg) & 1) || ty < p->top || ty >= p->bottom) {
            continue;
        }
        if (p->kind == NITRO_WIDE_MOVE) {
            int x0 = px0 > hofs ? px0 : hofs, x1 = px1 < hofs + W ? px1 : hofs + W;

            c0 = origin + x0 - hofs + p->dx;
            c1 = origin + x1 - hofs + p->dx;
            iv[niv].shift = p->dx;
            iv[niv].col = -1;
        } else if (p->kind == NITRO_WIDE_SHOW) {
            /* (the map going on beside the DS's screen: the whole canvas) */
            c0 = 0;
            c1 = width;
            iv[niv].shift = 0;
            iv[niv].col = -1;
        } else {
            c0 = origin + px0 - hofs;
            c1 = origin + px1 - hofs;
            iv[niv].shift = 0;
            iv[niv].col = p->col;
        }
        iv[niv].prec = i;
        if (c0 < 0) {
            c0 = 0;
        }
        if (c1 > width) {
            c1 = width;
        }
        if (c0 >= c1) {
            iv[niv].c0 = iv[niv].c1 = 0;
            niv++;   /* (on the line, but off the canvas) */
            continue;
        }
        iv[niv].c0 = c0;
        iv[niv].c1 = c1;
        cut[ncut++] = c0;
        cut[ncut++] = c1;
        niv++;
    }
    if (niv == 0) {
        return -1;
    }
    /* Under them, the DS's picture where it is, but for what the MOVEs
     * took from it (their columns aren't shown twice). */
    {
        int src[PART_LINE_MAX][2], nsrc = 0, from = hofs, j;

        for (i = 0; i < e->nParts && nsrc < PART_LINE_MAX; i++) {
            const NitroWidePart *p = &e->parts[i];
            int px0 = PartX(p, p->x0, hofs), px1 = PartX(p, p->x1, hofs);
            int x0 = px0 > hofs ? px0 : hofs, x1 = px1 < hofs + W ? px1 : hofs + W;

            if (p->kind != NITRO_WIDE_MOVE || !((p->bgs >> bg) & 1) || ty < p->top || ty >= p->bottom || x0 >= x1) {
                continue;
            }
            /* (in order of their starts) */
            for (j = nsrc; j > 0 && src[j - 1][0] > x0; j--) {
                src[j][0] = src[j - 1][0];
                src[j][1] = src[j - 1][1];
            }
            src[j][0] = x0;
            src[j][1] = x1;
            nsrc++;
        }
        for (j = 0; j <= nsrc; j++) {
            int to = j < nsrc ? src[j][0] : hofs + W;

            if (to > from) {
                int c0 = origin + from - hofs, c1 = origin + to - hofs;

                if (c0 < 0) {
                    c0 = 0;
                }
                if (c1 > width) {
                    c1 = width;
                }
                if (c0 < c1) {
                    iv[niv].c0 = c0;
                    iv[niv].c1 = c1;
                    iv[niv].prec = -1;
                    iv[niv].shift = 0;
                    iv[niv].col = -1;
                    cut[ncut++] = c0;
                    cut[ncut++] = c1;
                    niv++;
                }
            }
            if (j < nsrc && src[j][1] > from) {
                from = src[j][1];
            }
        }
    }
    /* the places where what shows may change, in order */
    for (i = 1; i < ncut; i++) {
        int v = cut[i];

        for (k = i; k > 0 && cut[k - 1] > v; k--) {
            cut[k] = cut[k - 1];
        }
        cut[k] = v;
    }
    for (i = 0; i + 1 < ncut; i++) {
        int c0 = cut[i], c1 = cut[i + 1], best = -1;

        if (c0 == c1) {
            continue;
        }
        for (k = 0; k < niv; k++) {
            if (iv[k].c0 < iv[k].c1 && iv[k].c0 <= c0 && iv[k].c1 >= c1 && (best < 0 || iv[k].prec > iv[best].prec)) {
                best = k;
            }
        }
        if (best < 0) {
            continue;
        }
        if (n > 0 && runs[n - 1].c1 == c0 && runs[n - 1].shift == iv[best].shift &&
            runs[n - 1].col == iv[best].col) {
            runs[n - 1].c1 = (short)c1;
            continue;
        }
        if (n == PART_RUNS_MAX) {
            break;
        }
        runs[n].c0 = (short)c0;
        runs[n].c1 = (short)c1;
        runs[n].shift = (short)iv[best].shift;
        runs[n].col = (short)iv[best].col;
        n++;
    }
    return n;
}

/* A text BG with parts: its line's runs as segments (a line without parts
 * where the DS has it). */
static int PartSegments(const Engine *e, int bg, int ty, int hofs, int mapW, Segment *segs)
{
    PartRun runs[PART_RUNS_MAX];
    int n = PartRuns(e, bg, ty, hofs, runs), i;

    if (n < 0) {
        segs[0].x0 = (short)e->origin;
        segs[0].x1 = (short)(e->origin + W);
        segs[0].off = (short)(hofs - e->origin);
        segs[0].fixed = -1;
        return 1;
    }
    for (i = 0; i < n && i < MAX_SEGMENTS; i++) {
        segs[i].x0 = runs[i].c0;
        segs[i].x1 = runs[i].c1;
        segs[i].off = (short)(hofs - e->origin - runs[i].shift);
        segs[i].fixed = (short)(runs[i].col >= 0 ? (runs[i].col & (mapW - 1)) : -1);
    }
    return i;
}

/* The runs of a text BG's line (y, its map row ty) on a wider canvas: the
 * DS's 256 pixels (or the whole canvas, wrapping round the map, without
 * clip), with the windows the game anchors (NitroPort_WideWindows: the
 * place name sign) moved to the screen's edge. Beside the DS's picture (with
 * clip): its edge colour when the edge is plain on this line (PlainRun),
 * else nothing. A BG with parts: its runs (PartSegments). Returns how
 * many. */
static int TextBgSegments(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int y, int ty, int hofs, int mapW,
                          Segment *segs, int ownOnly)
{
    int origin = e->origin, width = e->width, n = 0, i, k;
    NitroWideWindow winsLive[NITRO_WIDE_MAX_WINDOWS];
    const NitroWideWindow *wins = NULL;
    int nwins = 0;
    struct { int x0, x1, shift, off; } moved[NITRO_WIDE_MAX_WINDOWS];
    int nmoved = 0;

    /* (ownOnly: the BG's own runs, as if it had no hand-drawn picture: for
     * the picture's pixels that are the game's own, ArtTextPixel) */
    if (e->art[bg] != NULL && !ownOnly) {
        /* A hand-drawn picture: the bands (or all of it) from that. */
        if (e->artMoved[bg]) {
            segs[0].x0 = 0;
            segs[0].x1 = (short)width;
            segs[0].off = (short)(hofs - origin);
            segs[0].fixed = SEG_ART_MOVED;
            return 1;
        }
        if (origin > 0) {
            segs[n].x0 = 0;
            segs[n].x1 = (short)origin;
            segs[n].off = 0;
            segs[n].fixed = e->artBands[bg] ? SEG_ART : SEG_CLEAR;
            n++;
        }
        if (!e->artFull[bg] && ((e->partBgs >> bg) & 1)) {
            /* (a BG laid out in parts, its bands drawn: the parts over the
             * DS's part) */
            Segment mid[MAX_SEGMENTS];
            int m = PartSegments(e, bg, ty, hofs, mapW, mid);

            for (i = 0; i < m && n < MAX_SEGMENTS - 1; i++) {
                int a = mid[i].x0 > origin ? mid[i].x0 : origin;
                int b = mid[i].x1 < origin + 256 ? mid[i].x1 : origin + 256;

                if (a < b) {
                    segs[n] = mid[i];
                    segs[n].x0 = (short)a;
                    segs[n].x1 = (short)b;
                    n++;
                }
            }
        } else {
            segs[n].x0 = (short)origin;
            segs[n].x1 = (short)(origin + 256);
            segs[n].off = (short)(hofs - origin);
            segs[n].fixed = e->artFull[bg] ? SEG_ART_MID : -1;
            n++;
        }
        if (origin + 256 < width) {
            segs[n].x0 = (short)(origin + 256);
            segs[n].x1 = (short)width;
            segs[n].off = 0;
            segs[n].fixed = e->artBands[bg] ? SEG_ART : SEG_CLEAR;
            n++;
        }
        return n;
    }
    if ((e->partBgs >> bg) & 1) {
        return PartSegments(e, bg, ty, hofs, mapW, segs);
    }
    if (e->split[bg] && !ownOnly) {
        /* The halves to the edges, the plain middle widened. */
        segs[0].x0 = 0;
        segs[0].x1 = 128;
        segs[0].off = (short)hofs;
        segs[0].fixed = -1;
        segs[1].x0 = (short)(width - 128);
        segs[1].x1 = (short)width;
        segs[1].off = (short)(hofs - (width - 256));
        segs[1].fixed = -1;
        segs[2].x0 = 128;
        segs[2].x1 = (short)(width - 128);
        segs[2].off = 0;
        segs[2].fixed = (short)((128 + hofs) & (mapW - 1));
        return 3;
    }
    if (e->anchorWindows) {
        wins = EngineWindowsRef(e, bg, &nwins, winsLive);
    }
    for (i = 0; i < nwins; i++) {
        int top = wins[i].top * 8, bottom = (wins[i].top + wins[i].height) * 8;
        int a, b, shift;

        if (ty < top || ty >= bottom) {
            continue;
        }
        if (wins[i].widen) {
            /* A text box across the DS's screen: its frame's left part at
             * the screen's left edge, its right part at the right edge, the
             * window (its text) a pixel inside the frame's left part (the
             * room the wider box has is on the right of the text, as drawn
             * for it, 2026-10-16), and between, each line of the frame's
             * inner columns drawn on (its top and bottom borders, the box's
             * inside): on the right, the window's last column (its top and
             * bottom borders and the inside, whatever the frame: not the
             * right part's first column, which is the frame's own drawing
             * in many of the 20 frames, a red or a dark band drawn on). */
            int fl = wins[i].frameLeft * 8, fr = wins[i].frameRight * 8, ind = 0;
            int m0 = wins[i].left * 8, m1 = (wins[i].left + wins[i].width) * 8;
            int in0 = origin + m0 - hofs + fl, in1 = origin + m1 - hofs - fr, move;

            if (in0 < fl || in1 > width - fr || in0 > in1) {
                continue;   /* (not across the DS's screen as it is scrolled) */
            }
            move = fl + 1 - in0;
            if (move > 0) {
                move = 0;
            }
            in0 += move;
            in1 += move;
            /* (the game's screen indicator in the window's last 3 tiles,
             * its first 32 lines: with the frame's right part, the plain
             * inside drawn on from the column before it) */
            if (wins[i].indicator && ty >= top + 8 && ty < top + 8 + 32 && in1 - 24 >= in0) {
                ind = 24;
            }
            n = 0;
            segs[n].x0 = 0;
            segs[n].x1 = (short)fl;
            segs[n].off = (short)m0;
            segs[n].fixed = -1;
            n++;
            if (fl < in0) {
                segs[n].x0 = (short)fl;
                segs[n].x1 = (short)in0;
                segs[n].off = 0;
                segs[n].fixed = (short)((m0 + fl - 1) & (mapW - 1));
                n++;
            }
            segs[n].x0 = (short)in0;
            segs[n].x1 = (short)(in1 - ind);
            segs[n].off = (short)(hofs - origin - move);
            segs[n].fixed = -1;
            n++;
            if (in1 < width - fr) {
                segs[n].x0 = (short)(in1 - ind);
                segs[n].x1 = (short)(width - fr - ind);
                segs[n].off = 0;
                segs[n].fixed = (short)((m1 - fr - ind - 1) & (mapW - 1));
                n++;
            }
            segs[n].x0 = (short)(width - fr - ind);
            segs[n].x1 = (short)width;
            segs[n].off = (short)(m1 - width);
            segs[n].fixed = -1;
            n++;
            return n;
        }
        /* Where it is on the DS's screen (left of its first column). Text maps
         * are 256 or 512 wide (sTextSizes), so the wraparound is a mask, not
         * a divide: ARMv6 has no integer divider (a libcall per line). */
        a = (wins[i].left * 8 - hofs) & (mapW - 1);
        if (a >= 256) {
            a -= mapW;   /* (partly left of the screen) */
        }
        b = a + wins[i].width * 8;
        if (b <= 0 || a >= 256) {
            continue;
        }
        if (wins[i].anchor < 0) {
            shift = -origin;                 /* to the left edge */
        } else {
            shift = width - 256 - origin;    /* to the right edge */
        }
        moved[nmoved].x0 = a + origin;
        moved[nmoved].x1 = b + origin;
        moved[nmoved].shift = shift;
        moved[nmoved].off = hofs - origin - shift;
        nmoved++;
    }
    /* The BG's own pixels, less where the windows were and where they went;
     * beside them, its edge's colour when its edge is plain on this line
     * (a picture's border, say: widened without anything stretched).
     * (ownOnly: as if without clip, its map going on past the DS's edges
     * and no colour beside it, nor split: the BG's own pixels as a capture's
     * bg<N>_map.png has them, what a picture's pixels are told from.) */
    {
        int clip = e->clip[bg] && !ownOnly;
        int bx0 = clip ? origin : 0, bx1 = clip ? origin + 256 : width;
        int cuts[2 * NITRO_WIDE_MAX_WINDOWS][2], ncuts = 0, x, run = PlainRun(e);
        /* (the colour beside it: worked out for the frame, DecideBandFills,
         * or looked at here) */
        u16 bandL = e->bandFills[bg] ? e->bandFill[bg][0][y] : 0, bandR = e->bandFills[bg] ? e->bandFill[bg][1][y] : 0;
        int fillLeft = clip && origin > 0 &&
                       (e->bandFills[bg] ? bandL != 0 : TextBgPlain(e, bg, bgcnt, dispcnt, hofs, ty, 0, run));
        int fillRight = clip && origin + 256 < width &&
                        (e->bandFills[bg] ? bandR != 0 : TextBgPlain(e, bg, bgcnt, dispcnt, hofs, ty, 256 - run, run));

        if (fillLeft) {
            bx0 = 0;
        }
        if (fillRight) {
            bx1 = width;
        }

        for (i = 0; i < nmoved; i++) {
            cuts[ncuts][0] = moved[i].x0;
            cuts[ncuts][1] = moved[i].x1;
            ncuts++;
            cuts[ncuts][0] = moved[i].x0 + moved[i].shift;
            cuts[ncuts][1] = moved[i].x1 + moved[i].shift;
            ncuts++;
        }
        x = bx0;
        while (x < bx1 && n < MAX_SEGMENTS - nmoved) {
            int next = bx1, skip = -1;

            /* the nearest cut at or after x */
            for (k = 0; k < ncuts; k++) {
                if (cuts[k][1] <= x) {
                    continue;
                }
                if (cuts[k][0] <= x) {
                    skip = skip < 0 || cuts[k][1] > skip ? cuts[k][1] : skip;
                } else if (cuts[k][0] < next) {
                    next = cuts[k][0];
                }
            }
            if (skip >= 0) {
                x = skip;
                continue;
            }
            /* (split at the DS's edges: the bands repeat its edge pixel) */
            if (x < origin && next > origin) {
                next = origin;
            } else if (x < origin + 256 && next > origin + 256) {
                next = origin + 256;
            }
            if (!clip && (x < origin || x >= origin + 256)) {
                /* A BG whose map goes on past the DS's edges (the battle's
                 * background): there, tile by tile, the map's own pixels
                 * where it has a picture of its own (the player's
                 * platform's left part, which the game puts there; it was
                 * drawn out from the DS's edge into lines, every terrain's,
                 * 2026-10-16), and where the map only has the picture
                 * mirrored, the map's pixel next to the DS's edge
                 * repeated (nothing mirrored): that's the background's own
                 * edge, as it was before the game drew the platforms into
                 * it (the DS's edge pixel is the platform's, where a
                 * platform reaches the edge: the Elite Four's). */
                short edge = (short)((x < origin ? hofs - 1 : 256 + hofs) & (mapW - 1));

                while (x < next && n < MAX_SEGMENTS - nmoved) {
                    int mx = x + hofs - origin, end = x + 8 - (mx & 7);
                    short fixed = TextBgTileMirrored(e, bgcnt, dispcnt, mx, ty) ? edge : -1;

                    if (end > next) {
                        end = next;
                    }
                    if (n > 0 && segs[n - 1].x1 == x && segs[n - 1].fixed == fixed) {
                        segs[n - 1].x1 = (short)end;
                    } else {
                        segs[n].x0 = (short)x;
                        segs[n].x1 = (short)end;
                        segs[n].off = (short)(hofs - origin);
                        segs[n].fixed = fixed;
                        n++;
                    }
                    x = end;
                }
                continue;
            }
            segs[n].x0 = (short)x;
            segs[n].x1 = (short)next;
            segs[n].off = (short)(hofs - origin);
            if (x < origin && e->bandFills[bg] && clip) {
                segs[n].fixed = SEG_COLOR;
                segs[n].color = bandL;
            } else if (x >= origin + 256 && e->bandFills[bg] && clip) {
                segs[n].fixed = SEG_COLOR;
                segs[n].color = bandR;
            } else if (x < origin) {
                segs[n].fixed = (short)(hofs & (mapW - 1));
            } else if (x >= origin + 256) {
                segs[n].fixed = (short)((255 + hofs) & (mapW - 1));
            } else {
                segs[n].fixed = -1;
            }
            n++;
            x = next;
        }
    }
    for (i = 0; i < nmoved; i++) {
        int x0 = moved[i].x0 + moved[i].shift, x1 = moved[i].x1 + moved[i].shift;

        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > width) {
            x1 = width;
        }
        if (x0 < x1) {
            segs[n].x0 = (short)x0;
            segs[n].x1 = (short)x1;
            segs[n].off = (short)moved[i].off;
            segs[n].fixed = -1;
            n++;
        }
    }
    return n;
}

/* A place in a hand-drawn picture (size pixels across) where the BG's map
 * wraps every period pixels: a place past the picture's edge is the same
 * place of the map a period on, if that's in the picture. -1 if none. */
static inline int ArtWrap(int a, int size, int period)
{
    if (a >= 0 && a < size) {
        return a;
    }
    a %= period;
    if (a < 0) {
        a += period;
    }
    return a < size ? a : -1;
}

/* ArtWrap of a, a + 1, a + 2, ... along a line: the first worked out, the
 * rest stepped. (A division a pixel past a picture's edge: the 3DS's ARM11
 * has no divide instruction, and the opening's pictures did tens of
 * thousands a frame.) */
typedef struct ArtRun {
    int a, m, size, period;
} ArtRun;

static inline void ArtRunStart(ArtRun *r, int a, int size, int period)
{
    r->a = a;
    /* Power-of-two periods (text maps, 256-pixel runs) wrap with a mask, not
     * a divide: ARMv6 has no integer divider (a libcall per segment). Other
     * periods (the 320-wide art) keep the divide. Either way r->m ends in
     * [0, period), as the old modulo-plus-fixup did. */
    r->m = (period > 1 && (period & (period - 1)) == 0) ? a & (period - 1) : a % period;
    if (r->m < 0) {
        r->m += period;
    }
    r->size = size;
    r->period = period;
}

static inline int ArtRunNext(ArtRun *r)
{
    int v = (unsigned)r->a < (unsigned)r->size ? r->a : r->m < r->size ? r->m : -1;

    r->a++;
    if (++r->m == r->period) {
        r->m = 0;
    }
    return v;
}

/* A text BG's line, a tile (or the part of one on screen) at a time. */
static inline __attribute__((always_inline)) void DrawTextBgT(const Engine *e, int bg, u16 bgcnt, u32 dispcnt,
                                                              int y, Target *t, int flags)
{
    static const int sizes[4][2] = { { 256, 256 }, { 512, 256 }, { 256, 512 }, { 512, 512 } };
    int mapW = sizes[bgcnt >> 14][0];
    int mapH = sizes[bgcnt >> 14][1];
    const u8 *io = e->io;
    const u8 *vram = e->bgVram;
    u32 mask = e->bgMask;
    u32 charBase = CharBase(e, bgcnt, dispcnt);
    u32 screenBase = ScreenBase(e, bgcnt, dispcnt);
    BOOL bpp8 = (bgcnt & 0x80) != 0;
    const u8 *ext = (bpp8 && (dispcnt & 0x40000000)) ? BgExtSlot(e, bg, bgcnt) : NULL;
    int hofs = Rd16(io, 0x10 + bg * 4) & 0x1FF;
    int ty = (y + (Rd16(io, 0x12 + bg * 4) & 0x1FF)) & (mapH - 1);
    /* The map row this line is in: its address for the left and the right
     * 256-pixel half of the map. */
    u32 rowAddr = screenBase + (u32)((ty >> 8) * (mapW >> 8)) * 0x800 + (u32)((ty & 255) >> 3) * 64;
    Segment segs[MAX_SEGMENTS];
    OwnRow own;   /* (the BG's runs where its picture was made, for ArtTextPixel) */
    int nsegs, seg;
    TARGET_LOCALS(t);

    OwnRowStart(&own);

    if (e->width == W && e->origin == 0 && !e->anchorWindows) {
        /* the DS's own screen: one run */
        segs[0].x0 = 0;
        segs[0].x1 = W;
        segs[0].off = (short)hofs;
        segs[0].fixed = -1;
        nsegs = 1;
    } else {
        nsegs = TextBgSegments(e, bg, bgcnt, dispcnt, y, ty, hofs, mapW, segs, 0);
        if (flags & T_PLAIN) {
            memset(row, 0, (size_t)e->width * 2);
        }
    }
    for (seg = 0; seg < nsegs; seg++) {
    int x = segs[seg].x0, xe = segs[seg].x1, off = segs[seg].off;

    if (segs[seg].fixed >= 0) {
        /* one pixel of the map, repeated */
        u16 c = TextBgPixel(e, bg, bgcnt, dispcnt, segs[seg].fixed, ty);

        for (; x < xe; x++) {
            PUT(x, c, flags);
        }
        continue;
    }
    if (segs[seg].fixed == SEG_ART) {
        /* the hand-drawn picture, moved by the scroll since it was made;
         * past its edge, round the map as the BG itself goes (a scrolling
         * strip goes on). A strip's picture of its bands only, moved so far
         * that its middle comes in: there the BG's own pixels, what the
         * middle was (the opening's blue strip: else nothing beside the
         * DS's screen but where its bands had come round, 2026-10-23). */
        const u16 *art = e->art[bg];
        int ay = ArtWrap(y + e->artDy[bg], H, mapH);
        int loop = e->artLoopBands[bg];
        ArtRun run;

        if (ay < 0 || (e->artRows[bg] != NULL && !e->artRows[bg][ay])) {
            continue;   /* (nothing on this row of it: nothing to put) */
        }
        ArtRunStart(&run, x + e->artDx[bg], NITRO_WIDE_ART_W, mapW);
        (void)art;
        for (; x < xe; x++) {
            int ax = ArtRunNext(&run);

            if (COVERED(x, flags)) {
                continue;
            }
            if (loop && ax >= e->origin && ax < e->origin + W) {
                PUT(x, TextBgPixel(e, bg, bgcnt, dispcnt, (x + hofs - e->origin) & (mapW - 1), ty), flags);
                continue;
            }
            PUT(x,
                (ay >= 0 && ax >= 0)
                    ? ArtTextPixel(e, bg, bgcnt, dispcnt, ax, ay, mapW, mapH, &own)
                    : 0,
                flags);
        }
        continue;
    }
    if (segs[seg].fixed == SEG_CLEAR) {
        /* Nothing to put: PUT with 0 writes nothing unless the layer's own
         * line is being drawn (T_PLAIN), and that line was zeroed above. */
        continue;
    }
    if (segs[seg].fixed == SEG_COLOR) {
        u16 c = segs[seg].color;

        if (c == 0 && !(flags & T_PLAIN)) {
            continue;   /* (as SEG_CLEAR: nothing written, nothing counted) */
        }
        if (flags == 0 && (((uintptr_t)row | (uintptr_t)(row + x)) & 3) == 0) {
            /* (a plain merge: the same colour over the pixels not yet
             * drawn, two at a time, as MergeT does) */
            u32 c2 = (u32)c * 0x10001u;

            for (; x + 1 < xe; x += 2) {
                u32 have = *(u32a *)(row + x);

                if (!(have & 0x80008000u)) {
                    *(u32a *)(row + x) = c2;
                    filled += 2;
                } else {
                    if (!(have & OPAQUE)) {
                        row[x] = c;
                        filled++;
                    }
                    if (!(have & 0x80000000u)) {
                        row[x + 1] = c;
                        filled++;
                    }
                }
            }
        }
        for (; x < xe; x++) {
            PUT(x, c, flags);
        }
        continue;
    }
    if (segs[seg].fixed == SEG_ART_MID) {
        /* Over the DS's part, only what the picture has of the DS's part
         * (its middle when it was made): what was drawn in its bands is
         * made up, and scrolled in here it would show where the DS shows
         * the map (the opening's trees, drawn on in the bands, came in
         * when their layer scrolled away). Elsewhere the BG's own. */
        const u16 *art = e->art[bg];
        int ay = ArtWrap(y + e->artDy[bg], H, mapH);
        ArtRun run;

        int lo = ay >= 0 ? e->origin : 0, hi = ay >= 0 ? e->origin + W : 0;
        /* (nothing on this row of it: nothing where it's the picture's) */
        BOOL empty = ay >= 0 && e->artRows[bg] != NULL && !e->artRows[bg][ay];
        int dx = e->artDx[bg];

        if (empty && x + dx >= 0 && xe - 1 + dx < NITRO_WIDE_ART_W) {
            /* (an empty row of it, not going round: the picture's part,
             * x lo - dx .. hi - dx, has nothing; the rest is the BG's own) */
            int m0 = lo - dx, m1 = hi - dx;

            for (; x < xe; x++) {
                if (x >= m0 && x < m1) {
                    x = m1 - 1;
                    continue;
                }
                if (!COVERED(x, flags)) {
                    PUT(x, TextBgPixel(e, bg, bgcnt, dispcnt, (x + off) & (mapW - 1), ty), flags);
                }
            }
            continue;
        }
        ArtRunStart(&run, x + dx, NITRO_WIDE_ART_W, mapW);
        (void)art;
        for (; x < xe; x++) {
            int ax = ArtRunNext(&run);

            if (COVERED(x, flags)) {
                continue;
            }
            if (ax >= lo && ax < hi) {
                if (!empty) {
                    PUT(x, ArtTextPixel(e, bg, bgcnt, dispcnt, ax, ay, mapW, mapH, &own), flags);
                }
            } else {
                PUT(x, TextBgPixel(e, bg, bgcnt, dispcnt, (x + off) & (mapW - 1), ty), flags);
            }
        }
        continue;
    }
    if (segs[seg].fixed == SEG_ART_MOVED) {
        /* the whole layer from its picture, moved; past the picture's
         * edges the BG's own pixels (when moved as far as the BG: the
         * picture is of the map there), else nothing */
        const u16 *art = e->art[bg];
        int ay = ArtWrap(y + e->artDy[bg], H, mapH);

        BOOL empty = ay >= 0 && e->artRows[bg] != NULL && !e->artRows[bg][ay];

        for (; x < xe; x++) {
            int ax = x + e->artDx[bg];

            (void)art;
            if (COVERED(x, flags)) {
                continue;
            }
            if (ay >= 0 && ax >= 0 && ax < NITRO_WIDE_ART_W) {
                if (!empty) {
                    PUT(x, ArtTextPixel(e, bg, bgcnt, dispcnt, ax, ay, mapW, mapH, &own), flags);
                }
            } else if (e->artExact[bg]) {
                PUT(x, TextBgPixel(e, bg, bgcnt, dispcnt, (x + off) & (mapW - 1), ty), flags);
            } else {
                PUT(x, 0, flags);
            }
        }
        continue;
    }
    /* Slice B: scroll-shifted row reuse. The tile loop below computed
     * exactly TextBgPixel per pixel (same map entry, flip, palette and
     * transparency in every path), so a cached map row holds those values;
     * indexing by map coordinate makes any scroll offset, window shift or
     * segment layout hit. PUT still applies the front-layer and window
     * rules, so merging is unchanged. A NULL row (malloc failed) draws
     * direct, as before. */
    {
        const u16 *cachedRow =
            TextRowCached(e, e->isA ? 0 : 1, bg, sTextKey[e->isA ? 0 : 1][bg], ty, bgcnt, dispcnt);

        if (cachedRow != NULL) {
            /* Slice S2b: the hit path copies whole words like MergeT's
             * opaque-pair fast path (same proof: flags 0/T_LAYER only, so
             * no window check; the row test decides). The general PUT loop
             * below stays for windows and odd alignments. */
            int writeLayer = (flags & T_LAYER) != 0;
            int wideOk = flags == 0 || flags == T_LAYER;

            while (x < xe) {
                int tx = (x + off) & (mapW - 1);
                int n = mapW - tx;
                int i = 0;

                if (n > xe - x) {
                    n = xe - x;
                }
                if (wideOk && ((((uintptr_t)(row + x)) | ((uintptr_t)(cachedRow + tx))) & 3) == 0) {
                    /* Slice S2d: four transparent cached pixels write nothing
                     * under the PUT this path allows (its pc_ test), so skip
                     * them as one word-pair. Empty text lines (the field's
                     * log-14: 100% rows hit, still ~40% text) cost 64 loads
                     * instead of 256 PUTs. */
                    for (; i + 3 < n; i += 4) {
                        u32 tz0 = *(const u32a *)(cachedRow + tx + i);
                        u32 tz1 = *(const u32a *)(cachedRow + tx + i + 2);

                        if ((tz0 | tz1) != 0) {
                            break;
                        }
                    }
                    /* Two opaque cached pixels over two empty screen pixels
                     * copy as one word; anything else falls to the pixel
                     * path, which decides each. */
                    for (; i + 1 < n; i += 2) {
                        u32 pair = *(const u32a *)(cachedRow + tx + i);

                        if ((pair & 0x80008000u) == 0x80008000u) {
                            u32 have = *(u32a *)(row + x + i);

                            if (!(have & 0x80008000u)) {
                                *(u32a *)(row + x + i) = pair;
                                if (writeLayer) {
                                    layerOf[x + i] = layer;
                                    layerOf[x + i + 1] = layer;
                                }
                                filled += 2;
                                continue;
                            }
                        }
                        if ((cachedRow[tx + i] & OPAQUE) && !(row[x + i] & OPAQUE)) {
                            row[x + i] = cachedRow[tx + i];
                            if (writeLayer) {
                                layerOf[x + i] = layer;
                            }
                            filled++;
                        }
                        if ((cachedRow[tx + i + 1] & OPAQUE) && !(row[x + i + 1] & OPAQUE)) {
                            row[x + i + 1] = cachedRow[tx + i + 1];
                            if (writeLayer) {
                                layerOf[x + i + 1] = layer;
                            }
                            filled++;
                        }
                    }
                }
                for (; i < n; i++) {
                    PUT(x + i, cachedRow[tx + i], flags);
                }
                x += n;
            }
        } else while (x < xe) {
        int tx = (x + off) & (mapW - 1);
        int px0 = tx & 7;
        int n = 8 - px0;
        u32 entryAddr = rowAddr + (u32)(tx >> 8) * 0x800 + (u32)((tx & 255) >> 3) * 2;
        u32 entry = Rd16(vram, entryAddr & mask);
        u32 tile = entry & 0x3FF;
        int py = (entry & 0x800) ? 7 - (ty & 7) : (ty & 7);
        int flip = (entry & 0x400) ? 7 : 0;   /* pixel px of the tile is at px ^ flip */
        int i;

        if (n > xe - x) {
            n = xe - x;
        }
        if (bpp8) {
            const u8 *chars = vram + ((charBase + tile * 64 + (u32)py * 8) & mask);

            if (ext != NULL) {
                const u8 *pltt = ext + ((entry >> 12) << 9);

                for (i = 0; i < n; i++) {
                    u32 index = chars[(px0 + i) ^ flip];

                    PUT(x + i, index ? (u16)(PlttColor(pltt, index) | OPAQUE) : 0, flags);
                }
            } else if (n == 8) {
                /* a whole tile's row: its 8 bytes as two words (the
                 * battles' backgrounds) */
                u32 lo = Rd32(chars, 0), hi = Rd32(chars, 4);
                const u16 *pal8 = e->bgPal8;

                if ((lo | hi) == 0) {
                    if (flags & T_PLAIN) {
                        memset(row + x, 0, 16);
                    }
                } else if (!flip) {
                    PUT(x + 0, pal8[lo & 255], flags);
                    PUT(x + 1, pal8[(lo >> 8) & 255], flags);
                    PUT(x + 2, pal8[(lo >> 16) & 255], flags);
                    PUT(x + 3, pal8[lo >> 24], flags);
                    PUT(x + 4, pal8[hi & 255], flags);
                    PUT(x + 5, pal8[(hi >> 8) & 255], flags);
                    PUT(x + 6, pal8[(hi >> 16) & 255], flags);
                    PUT(x + 7, pal8[hi >> 24], flags);
                } else {
                    PUT(x + 0, pal8[hi >> 24], flags);
                    PUT(x + 1, pal8[(hi >> 16) & 255], flags);
                    PUT(x + 2, pal8[(hi >> 8) & 255], flags);
                    PUT(x + 3, pal8[hi & 255], flags);
                    PUT(x + 4, pal8[lo >> 24], flags);
                    PUT(x + 5, pal8[(lo >> 16) & 255], flags);
                    PUT(x + 6, pal8[(lo >> 8) & 255], flags);
                    PUT(x + 7, pal8[lo & 255], flags);
                }
            } else {
                for (i = 0; i < n; i++) {
                    PUT(x + i, e->bgPal8[chars[(px0 + i) ^ flip]], flags);
                }
            }
        } else {
            u32 bits = Rd32(vram, (charBase + tile * 32 + (u32)py * 4) & mask);
            const u16 *pal = BgPal4(e, bg) + ((entry >> 12) << 4);

            if (bits == 0) {
                /* all transparent */
                if (flags & T_PLAIN) {
                    memset(row + x, 0, (size_t)n * 2);
                }
            } else if (n == 8) {
                if (flip) {
                    /* the pixels right to left: the nibbles high to low */
                    bits = ((bits >> 4) & 0x0F0F0F0Fu) | ((bits & 0x0F0F0F0Fu) << 4);
                    bits = (bits >> 24) | ((bits >> 8) & 0xFF00u) | ((bits << 8) & 0xFF0000u) | (bits << 24);
                }
                PUT(x + 0, pal[bits & 15], flags);
                PUT(x + 1, pal[(bits >> 4) & 15], flags);
                PUT(x + 2, pal[(bits >> 8) & 15], flags);
                PUT(x + 3, pal[(bits >> 12) & 15], flags);
                PUT(x + 4, pal[(bits >> 16) & 15], flags);
                PUT(x + 5, pal[(bits >> 20) & 15], flags);
                PUT(x + 6, pal[(bits >> 24) & 15], flags);
                PUT(x + 7, pal[bits >> 28], flags);
            } else {
                for (i = 0; i < n; i++) {
                    PUT(x + i, pal[(bits >> (((px0 + i) ^ flip) * 4)) & 0xF], flags);
                }
            }
        }
        x += n;
    }
    }
    }
    t->filled = filled;
}

/* (a tall field) The line of a BG drawn on the picture's line y: y, or
 * for a clipped BG past the DS's lines, its DS's first or last (tallEdge),
 * or -1: nothing. */
static inline int TallLineOf(const Engine *e, int bg, int y)
{
    if (e->top != 0 && ((e->tallMoved >> bg) & 1)) {
        y = e->tallSrc[bg][y];   /* (its windows moved to the picture's top or bottom) */
        if (y < 0) {
            return -1;
        }
    }
    if (e->top == 0 || !e->clip[bg] || (y >= e->top && y < e->top + H)) {
        return y;
    }
    return y < e->top ? (e->tallEdge[bg][0] ? e->top : -1) : (e->tallEdge[bg][1] ? e->top + H - 1 : -1);
}

/* (a tall field) Which clipped text BGs' first and last DS lines go on
 * past them: one colour across the DS's 256 columns. */
static void DecideTallEdges(Engine *e, const int *kind, const u16 *bgcnt, u32 dispcnt)
{
    int bg, side;

    memset(e->tallEdge, 0, sizeof(e->tallEdge));
    if (e->top == 0) {
        return;
    }
    for (bg = 0; bg < 4; bg++) {
        int mapH, hofs, vofs;

        if (kind[bg] != BG_TEXT || !e->clip[bg]) {
            continue;
        }
        mapH = sTextSizes[bgcnt[bg] >> 14][1];
        hofs = Rd16(e->io, 0x10 + bg * 4) & 0x1FF;
        vofs = Rd16(e->io, 0x12 + bg * 4) & 0x1FF;
        for (side = 0; side < 2; side++) {
            int ty = ((side ? e->top + H - 1 : e->top) + vofs) & (mapH - 1);

            e->tallEdge[bg][side] = (u8)TextBgPlain(e, bg, bgcnt[bg], dispcnt, hofs, ty, 0, W);
        }
    }
}

/* (a tall field) The game's windows on each text BG (NitroPort_WideWindows)
 * to the picture's top or bottom: those wholly in the DS's lower half down
 * by top (to the bottom: a text box, YES/NO above it), wholly in its upper
 * half up by top (the place name sign); the BG's lines they leave drawn
 * from nowhere (the rest of such a line is the BG's own, usually clear).
 * The OBJs over them go with them (objMove's dy). */
static void DecideTallWindows(Engine *e, const int *kind)
{
    int bg;

    e->tallMoved = 0;
    if (e->top == 0 || !e->anchorWindows) {
        return;
    }
    for (bg = 0; bg < 4; bg++) {
        NitroWideWindow wins[NITRO_WIDE_MAX_WINDOWS];
        int n, i, y, moved = 0, vofs;

        if (kind[bg] != BG_TEXT) {
            continue;
        }
        n = EngineWindows(e, bg, wins);
        vofs = Rd16(e->io, 0x12 + bg * 4) & 0x1FF;   /* (the picture's lines: the DS's less top) */
        for (y = 0; y < e->height; y++) {
            e->tallSrc[bg][y] = (short)y;
        }
        /* where they were: none of them there now */
        for (i = 0; i < n; i++) {
            int y0 = ((wins[i].top * 8 - vofs) & 0x1FF) - (((wins[i].top * 8 - vofs) & 0x100) ? 512 : 0);
            int y1 = y0 + wins[i].height * 8, ds0 = y0 - e->top, ds1 = y1 - e->top;
            int dy = ds0 >= H / 2 ? e->top : ds1 <= H / 2 ? -e->top : 0;

            if (dy == 0) {
                continue;
            }
            for (y = y0 < 0 ? 0 : y0; y < y1 && y < e->height; y++) {
                e->tallSrc[bg][y] = -1;
            }
            moved = 1;
        }
        /* where they go */
        for (i = 0; i < n; i++) {
            int y0 = ((wins[i].top * 8 - vofs) & 0x1FF) - (((wins[i].top * 8 - vofs) & 0x100) ? 512 : 0);
            int y1 = y0 + wins[i].height * 8, ds0 = y0 - e->top, ds1 = y1 - e->top;
            int dy = ds0 >= H / 2 ? e->top : ds1 <= H / 2 ? -e->top : 0;

            if (dy == 0) {
                continue;
            }
            for (y = y0; y < y1; y++) {
                if (y + dy >= 0 && y + dy < e->height) {
                    e->tallSrc[bg][y + dy] = (short)y;
                }
            }
            if (e->nObjMove < OBJ_MOVES_MAX) {
                int hofs = Rd16(e->io, 0x10 + bg * 4) & 0x1FF;

                e->objMove[e->nObjMove].x0 = (short)(wins[i].left * 8 - hofs);
                e->objMove[e->nObjMove].x1 = (short)((wins[i].left + wins[i].width) * 8 - hofs);
                e->objMove[e->nObjMove].y0 = (short)y0;
                e->objMove[e->nObjMove].y1 = (short)y1;
                e->objMove[e->nObjMove].shift = 0;
                e->objMove[e->nObjMove].stretch = 0;
                e->objMove[e->nObjMove].dy = (short)dy;
                e->nObjMove++;
            }
        }
        if (moved) {
            e->tallMoved |= (u8)(1 << bg);
        }
    }
}

static void DrawTextBg(const Engine *e, int bg, u16 bgcnt, u32 dispcnt, int y, Target *t, int flags)
{
    y = TallLineOf(e, bg, y);
    if (y < 0) {
        return;
    }
    switch (flags) {
    case T_PLAIN:
        DrawTextBgT(e, bg, bgcnt, dispcnt, y, t, T_PLAIN);
        break;
    case 0:
        DrawTextBgT(e, bg, bgcnt, dispcnt, y, t, 0);
        break;
    case T_WINDOW:
        DrawTextBgT(e, bg, bgcnt, dispcnt, y, t, T_WINDOW);
        break;
    case T_LAYER:
        DrawTextBgT(e, bg, bgcnt, dispcnt, y, t, T_LAYER);
        break;
    default:
        DrawTextBgT(e, bg, bgcnt, dispcnt, y, t, T_WINDOW | T_LAYER);
        break;
    }
}

/* A layer's own line (T_PLAIN) into the screen's line, from x0 to x1; for
 * the OBJ line, only the pixels of priority prio. */
static inline __attribute__((always_inline)) void MergeT(Target *t, const u16 *line, const u8 *prioOf,
                                                         int prio, int x0, int x1, int flags)
{
    int x;
    TARGET_LOCALS(t);

    /* (a plain merge, no windows or layers: the common BG row) Two opaque
     * pixels of the layer over two of the screen can be copied as one
     * word; where either pair isn't wholly opaque, the pixel path below
     * decides each. The source and the screen must both be word-aligned
     * for the wide loads and stores (a pixel-path x0 may not be).
     * Slice S3: T_LAYER as well as 0. Proved flag-independent: with no
     * T_WINDOW bit the general path below is PUT without the window or
     * priority checks, i.e. exactly what the word and pixel paths do —
     * plus layerOf, which the word path was missing (it only ran at
     * flags == 0). T_PLAIN and T_WINDOW still take the general path. */
    if ((flags == 0 || flags == T_LAYER) && prioOf == NULL &&
        (((uintptr_t)(row + x0) | (uintptr_t)(line + x0)) & 3) == 0) {
        int writeLayer = (flags & T_LAYER) != 0;

        for (x = x0; x + 1 < x1; x += 2) {
            u32 pair = *(const u32a *)(line + x);

            if ((pair & 0x80008000u) == 0x80008000u) {
                u32 have = *(u32a *)(row + x);

                if (!(have & 0x80008000u)) {
                    *(u32a *)(row + x) = pair;
                    if (writeLayer) {
                        layerOf[x] = layer;
                        layerOf[x + 1] = layer;
                    }
                    filled += 2;
                    continue;
                }
            }
            /* (one or both of the pair needs the general path) */
            if ((line[x] & OPAQUE) && !(row[x] & OPAQUE)) {
                row[x] = line[x];
                if (writeLayer) {
                    layerOf[x] = layer;
                }
                filled++;
            }
            if ((line[x + 1] & OPAQUE) && !(row[x + 1] & OPAQUE)) {
                row[x + 1] = line[x + 1];
                if (writeLayer) {
                    layerOf[x + 1] = layer;
                }
                filled++;
            }
        }
        for (; x < x1; x++) {
            if ((line[x] & OPAQUE) && !(row[x] & OPAQUE)) {
                row[x] = line[x];
                if (writeLayer) {
                    layerOf[x] = layer;
                }
                filled++;
            }
        }
        t->filled = filled;
        return;
    }
    for (x = x0; x < x1; x++) {
        /* (a 3D pixel of alpha 0 can have a colour, but no OPAQUE) */
        if ((line[x] & OPAQUE) && (prioOf == NULL || prioOf[x] == prio)) {
            PUT(x, line[x], flags);
        }
    }
    t->filled = filled;
}

static void Merge(Target *t, const u16 *line, const u8 *prioOf, int prio, int x0, int x1, int flags)
{
    switch (flags) {
    case 0:
        MergeT(t, line, prioOf, prio, x0, x1, 0);
        break;
    case T_WINDOW:
        MergeT(t, line, prioOf, prio, x0, x1, T_WINDOW);
        break;
    case T_LAYER:
        MergeT(t, line, prioOf, prio, x0, x1, T_LAYER);
        break;
    default:
        MergeT(t, line, prioOf, prio, x0, x1, T_WINDOW | T_LAYER);
        break;
    }
}

/* Affine and extended BGs: the texture coordinate of each pixel comes from
 * the reference point and the 2x2 matrix (8 fractional bits). */
/* An affine (or extended, or bitmap) BG's pixel at its map's (tx, ty), as
 * its kind (mode: 0 affine, 1 extended tiles, 2 256-colour bitmap, 3
 * direct colour) has it: colour | OPAQUE, or 0 (off its map, unwrapped). */
static inline u16 AffineSample(const Engine *e, int mode, const u8 *ext, u32 charBase, u32 screenBase, int mapW,
                               int mapH, BOOL wrap, s32 tx, s32 ty)
{
    const u8 *vram = e->bgVram;
    u32 mask = e->bgMask;

    if (wrap) {
        tx &= mapW - 1;
        ty &= mapH - 1;
    } else if (tx < 0 || ty < 0 || tx >= mapW || ty >= mapH) {
        return 0;
    }
    switch (mode) {
    case 3: {
        u16 c = Rd16(vram, (screenBase + ((u32)ty * (u32)mapW + (u32)tx) * 2) & mask);

        return (c & 0x8000) ? c : 0;   /* colour | OPAQUE */
    }
    case 2:
        return e->bgPal8[vram[(screenBase + (u32)ty * (u32)mapW + (u32)tx) & mask]];
    case 0: {
        u32 tile = vram[(screenBase + (u32)(ty >> 3) * (u32)(mapW >> 3) + (u32)(tx >> 3)) & mask];

        return e->bgPal8[vram[(charBase + tile * 64 + (u32)(ty & 7) * 8 + (u32)(tx & 7)) & mask]];
    }
    default: {
        u32 entry = Rd16(vram, (screenBase + ((u32)(ty >> 3) * (u32)(mapW >> 3) + (u32)(tx >> 3)) * 2) & mask);
        int px = tx & 7;
        int py = ty & 7;
        u32 index;

        if (entry & 0x400) {
            px = 7 - px;
        }
        if (entry & 0x800) {
            py = 7 - py;
        }
        index = vram[(charBase + (entry & 0x3FF) * 64 + (u32)py * 8 + (u32)px) & mask];
        if (index == 0) {
            return 0;
        }
        return (u16)((ext != NULL ? PlttColor(ext, ((entry >> 12) << 8) | index) : PlttColor(e->bgPltt, index)) |
                     OPAQUE);
    }
    }
}

static void DrawAffineBg(const Engine *e, int bg, int kind, u16 bgcnt, u32 dispcnt, int y, u16 *out)
{
    const u8 *io = e->io;

    if (TallLineOf(e, bg, y) < 0) {
        memset(out, 0, (size_t)e->width * sizeof(u16));   /* (a tall field: nothing past the DS's lines) */
        return;
    }
    u32 base = bg == 2 ? 0x20 : 0x30;
    s32 pa = (s16)Rd16(io, base + 0);
    s32 pb = (s16)Rd16(io, base + 2);
    s32 pc = (s16)Rd16(io, base + 4);
    s32 pd = (s16)Rd16(io, base + 6);
    s32 refX = (s32)(Rd32(io, base + 8) << 4) >> 4;    /* 28-bit signed */
    s32 refY = (s32)(Rd32(io, base + 12) << 4) >> 4;
    /* (a wider canvas: the DS's x = 0 at e->origin; without clip, the BG
     * goes on past the DS's edges as its matrix takes it; split, across
     * the canvas: its parts, moved, wherever they are) */
    int splitHere = (e->partBgs >> bg) & 1;
    int xs = e->clip[bg] && !splitHere ? e->origin : 0, xe = e->clip[bg] && !splitHere ? e->origin + W : e->width;
    PartRun runs[PART_RUNS_MAX];
    int nruns = -1, run = 0;
    s32 splitTy = -0x7FFFFFFF;
    const u16 *art = splitHere ? e->art[bg] : NULL;
    s32 fx = refX + pb * (y - e->top) + pa * (xs - e->origin);
    s32 fy = refY + pd * (y - e->top) + pc * (xs - e->origin);
    BOOL wrap = (bgcnt & 0x2000) != 0;
    int size = bgcnt >> 14;
    u32 charBase = CharBase(e, bgcnt, dispcnt);
    u32 screenBase = ScreenBase(e, bgcnt, dispcnt);
    int mode;   /* 0: affine (8-bit map), 1: extended tiles, 2: 256-colour bitmap, 3: direct colour */
    int mapW, mapH, x;
    const u8 *ext = NULL;

    if (kind == BG_AFFINE) {
        mode = 0;
        mapW = mapH = 128 << size;
    } else if (!(bgcnt & 0x80)) {
        mode = 1;
        mapW = mapH = 128 << size;
        if (dispcnt & 0x40000000) {
            ext = BgExtSlot(e, bg, bgcnt);
        }
    } else {
        static const int bmp[4][2] = { { 128, 128 }, { 256, 256 }, { 512, 256 }, { 512, 512 } };

        mode = (bgcnt & 0x4) ? 3 : 2;
        mapW = bmp[size][0];
        mapH = bmp[size][1];
        screenBase = ((bgcnt >> 8) & 0x1F) * 0x4000;   /* bitmaps: 16 KB units */
    }

    if (xs > 0 || xe < e->width) {
        memset(out, 0, (size_t)e->width * 2);
    }
    for (x = xs; x < xe; x++, fx += pa, fy += pc) {
        s32 tx = fx >> 8;
        s32 ty = fy >> 8;

        out[x] = 0;
        if (splitHere) {
            /* Where the matrix takes this pixel is a place on the split
             * picture (laid out across the canvas: x -origin.. at rest);
             * from that, the BG's own pixel it shows (or its hand-drawn
             * picture's, made at rest). */
            if (art != NULL && (e->artFull[bg] || tx < 0 || tx >= W)) {
                s32 ax = tx + e->origin;
                u16 c = 0;

                if (ax >= 0 && ax < NITRO_WIDE_ART_W && ty >= 0 && ty < H) {
                    c = art[ty * NITRO_WIDE_ART_W + ax];
                }
                if (!NitroPort_WideArtIsGame(c)) {
                    out[x] = c;
                    continue;
                }
                /* (the game's own there, as far back as the picture moved
                 * it: as the split draws it) */
                tx -= NitroPort_WideArtGameDx(c);
                ty -= NitroPort_WideArtGameDy(c);
            }
            if (ty != splitTy) {
                nruns = PartRuns(e, bg, ty, 0, runs);
                splitTy = ty;
                run = 0;
            }
            if (nruns < 0) {
                if (tx < 0 || tx >= W) {
                    continue;   /* (a line without parts: the DS's part only) */
                }
            } else {
                /* the run this place is in (the place goes on from the last
                 * pixel's, mostly: look from that run) */
                s32 c = tx + e->origin;

                if (run >= nruns || c < runs[run].c0 || c >= runs[run].c1) {
                    for (run = 0; run < nruns && !(c >= runs[run].c0 && c < runs[run].c1); run++) {
                    }
                    if (run == nruns) {
                        continue;
                    }
                }
                tx = runs[run].col >= 0 ? runs[run].col : tx - runs[run].shift;
                if (tx < 0 || tx >= W) {
                    continue;
                }
            }
        }
        out[x] = AffineSample(e, mode, ext, charBase, screenBase, mapW, mapH, wrap, tx, ty);
    }
    /* (wider canvas) beside the DS's screen, the edge's colour when the
     * edge is plain on this line (PlainRun) */
    if (xs > 0 && xe - xs >= PlainRun(e)) {
        int i, run = PlainRun(e);
        BOOL plain = (out[xs] & OPAQUE) != 0;

        for (i = 1; i < run && plain; i++) {
            plain = out[xs + i] == out[xs];
        }
        if (plain) {
            for (i = 0; i < xs; i++) {
                out[i] = out[xs];
            }
        }
    }
    if (xe < e->width && xe - xs >= PlainRun(e)) {
        int i, run = PlainRun(e);
        BOOL plain = (out[xe - 1] & OPAQUE) != 0;

        for (i = 2; i <= run && plain; i++) {
            plain = out[xe - i] == out[xe - 1];
        }
        if (plain) {
            for (i = xe; i < e->width; i++) {
                out[i] = out[xe - 1];
            }
        }
    }
    /* a hand-drawn picture: the bands (or all of it) from that (a split
     * BG's, above: where its matrix takes it) */
    if (e->art[bg] != NULL && !splitHere) {
        const u16 *line = e->art[bg] + y * NITRO_WIDE_ART_W;
        int i;

        for (i = 0; i < e->width && i < NITRO_WIDE_ART_W; i++) {
            if (e->artFull[bg] || i < e->origin || i >= e->origin + W) {
                u16 c = line[i];

                if (NitroPort_WideArtIsGame(c)) {
                    /* the game's own, as far back as the picture moved it:
                     * the BG's pixel there (drawn above over the DS's part
                     * when it's there; beside it, where the matrix takes
                     * it past the DS's edge) */
                    int gx = i - NitroPort_WideArtGameDx(c), gy = y - NitroPort_WideArtGameDy(c);

                    if (gx == i && gy == y && i >= xs && i < xe) {
                        continue;
                    }
                    c = AffineSample(e, mode, ext, charBase, screenBase, mapW, mapH, wrap,
                                     (refX + pb * gy + pa * (gx - e->origin)) >> 8,
                                     (refY + pd * gy + pc * (gx - e->origin)) >> 8);
                }
                out[i] = c;
            }
        }
    }
}

/* ------------------------------------------------------- hand-drawn art */

/* FNV-1a, 64 bits. */
#define KEY_START 0xcbf29ce484222325ull
static inline u64 KeyAdd(u64 h, u32 v)
{
    int i;

    for (i = 0; i < 4; i++) {
        h = (h ^ (v & 0xFF)) * 0x100000001b3ull;
        v >>= 8;
    }
    return h;
}

/* A quicker hash of the same words (two 32-bit lanes, a word at a time):
 * BgKeyCached's check that nothing BgKey reads changed. */
static inline u64 QuickAdd(u64 h, u32 v)
{
    u32 a = (u32)h, b = (u32)(h >> 32);

    a = (a ^ v) * 0x9E3779B1u;
    a = (a << 15) | (a >> 17);
    b = (b + v) * 0x85EBCA77u;
    b = ((b << 13) | (b >> 19)) ^ a;
    return ((u64)b << 32) | a;
}

/* A BG's key (nitro_port_wideart.h): its kind and size, its whole map, the
 * tiles the map uses (each once, by number) and the palettes they're in.
 * Not where its map and tiles are in VRAM, nor its scroll: the same art is
 * the same key wherever the game put it. 0 for a BG with none (a bitmap).
 * quick: QuickAdd's hash of the same words instead (not a key). */
static inline __attribute__((always_inline)) u64 BgKeyT(const Engine *e, int bg, int kind, u16 bgcnt,
                                                        u32 dispcnt, int quick)
{
    const u8 *vram = e->bgVram;
    u32 mask = e->bgMask, charBase = CharBase(e, bgcnt, dispcnt), screenBase = ScreenBase(e, bgcnt, dispcnt);
    u32 used[1024 / 32];
    u32 banks = 0, entries, i;
    int bpp8, wide16;   /* 8-bit tiles; 16-bit map entries */
    u64 h = KEY_START;
#ifndef HGSS_PROFILE_OFF
    u32 tileBytes = 0;  /* P1: tile bytes actually walked below */
#endif
#define ADD(h, v) (quick ? QuickAdd(h, v) : KeyAdd(h, v))

    if (kind == BG_TEXT) {
        bpp8 = (bgcnt & 0x80) != 0;
        wide16 = 1;
        entries = (u32)(sTextSizes[bgcnt >> 14][0] / 8) * (u32)(sTextSizes[bgcnt >> 14][1] / 8);
    } else if (kind == BG_AFFINE || (kind == BG_EXTENDED && !(bgcnt & 0x80))) {
        bpp8 = 1;
        wide16 = kind == BG_EXTENDED;
        entries = (u32)((128 << (bgcnt >> 14)) / 8) * (u32)((128 << (bgcnt >> 14)) / 8);
    } else {
        return 0;   /* a bitmap, the 3D */
    }
    h = ADD(h, (u32)kind | ((u32)(bgcnt & 0xC080) << 8));
    memset(used, 0, sizeof(used));
    /* the map (a text BG's in its 2 KB blocks, in order: the same bytes) */
    for (i = 0; i < entries; i++) {
        u32 entry = wide16 ? Rd16(vram, (screenBase + i * 2) & mask) : vram[(screenBase + i) & mask];
        u32 tile = wide16 ? entry & 0x3FF : entry;

        h = ADD(h, entry);
        used[tile >> 5] |= 1u << (tile & 31);
        if (wide16) {
            banks |= 1u << (entry >> 12);
        }
    }
    /* the tiles used */
    for (i = 0; i < 1024; i++) {
        u32 size = bpp8 ? 64 : 32, k;

        if (!(used[i >> 5] & (1u << (i & 31)))) {
            continue;
        }
#ifndef HGSS_PROFILE_OFF
        tileBytes += size;
#endif
        for (k = 0; k < size; k += 4) {
            h = ADD(h, Rd32(vram, (charBase + i * size + k) & mask));
        }
    }
    /* their palettes */
    if (!bpp8) {
        for (i = 0; i < 16; i++) {
            if (banks & (1u << i)) {
                u32 k;

                for (k = 0; k < 16; k++) {
                    h = ADD(h, PlttColor(e->bgPltt, i * 16 + k));
                }
            }
        }
    } else {
        const u8 *ext = (dispcnt & 0x40000000) && wide16 ? BgExtSlot(e, bg, bgcnt) : NULL;

        if (ext != NULL) {
            for (i = 0; i < 16; i++) {
                if (banks & (1u << i)) {
                    u32 k;

                    for (k = 0; k < 256; k++) {
                        h = ADD(h, PlttColor(ext + (i << 9), k));
                    }
                }
            }
        } else {
            for (i = 0; i < 256; i++) {
                h = ADD(h, PlttColor(e->bgPltt, i));
            }
        }
    }
#undef ADD
#ifndef HGSS_PROFILE_OFF
    /* P1: bytes actually walked (map entries + used tiles; palettes excluded). */
    sProfile[e->isA ? 0 : 1].keyMapBytes += (u64)entries * (wide16 ? 2u : 1u);
    sProfile[e->isA ? 0 : 1].keyTileBytes += tileBytes;
#endif
    return h ? h : 1;
}


static u64 BgKey(const Engine *e, int bg, int kind, u16 bgcnt, u32 dispcnt)
{
    return BgKeyT(e, bg, kind, bgcnt, dispcnt, 0);
}

/* Prove a plain text map transparent from CURRENT bytes, not a dirty flag or
 * a previously empty picture. Index zero is transparent in both tile formats,
 * regardless of palette/flip/scroll. Visit each referenced tile once; do not
 * assume that an empty map entry means its tile zero is empty too. The caller
 * excludes artwork/parts/windows that could add pixels outside these tiles. */
static int TextBgTransparent(const Engine *e, u16 bgcnt, u32 dispcnt)
{
    u32 seen[32] = {0};
    u32 screen = ScreenBase(e, bgcnt, dispcnt), chars = CharBase(e, bgcnt, dispcnt);
    u32 entries = (u32)(sTextSizes[bgcnt >> 14][0] / 8) * (u32)(sTextSizes[bgcnt >> 14][1] / 8);
    u32 bytes = (bgcnt & 0x80) ? 64 : 32;
#ifndef HGSS_PROFILE_OFF
    u32 tileBytes = 0;  /* P1: tile bytes actually walked below */
#endif

    for (u32 i = 0; i < entries; i++) {
        u32 tile = Rd16(e->bgVram, (screen + i * 2) & e->bgMask) & 0x3FFu;
        u32 bit = 1u << (tile & 31);
        if (seen[tile >> 5] & bit) continue;
        seen[tile >> 5] |= bit;
#ifndef HGSS_PROFILE_OFF
        tileBytes += bytes;
#endif
        for (u32 k = 0; k < bytes; k += 4) {
            if (Rd32(e->bgVram, (chars + tile * bytes + k) & e->bgMask)) {
#ifndef HGSS_PROFILE_OFF
                /* P1: count the walk even on the early (nontransparent) exit. */
                sProfile[e->isA ? 0 : 1].keyMapBytes += (u64)entries * 2u;
                sProfile[e->isA ? 0 : 1].keyTileBytes += tileBytes;
#endif
                return 0;
            }
        }
    }
#ifndef HGSS_PROFILE_OFF
    sProfile[e->isA ? 0 : 1].keyMapBytes += (u64)entries * 2u;
    sProfile[e->isA ? 0 : 1].keyTileBytes += tileBytes;
#endif
    return 1;
}

/* BgKey for drawing, worked out again only when what it reads changed
 * (QuickAdd's hash of it differs): FNV-1a's 64-bit multiply a byte took a
 * sixth of drawing the opening's screens with pictures, every frame. Each
 * engine's BGs, under a lock (an engine is drawn by one thread at a time,
 * but the PC's tools may draw one besides). */
static struct {
    u64 quick, key;
    u8 kind, valid;
    u16 bgcnt;
} sKeyCache[2][4];
static volatile int sKeyCacheLock[2];

static u64 BgKeyCached(const Engine *e, int bg, int kind, u16 bgcnt, u32 dispcnt)
{
    int eng = e->isA ? 0 : 1;
    u64 quick = BgKeyT(e, bg, kind, bgcnt, dispcnt, 1), key;

    while (__atomic_test_and_set(&sKeyCacheLock[eng], __ATOMIC_ACQUIRE)) {
    }
    if (sKeyCache[eng][bg].valid && sKeyCache[eng][bg].quick == quick && sKeyCache[eng][bg].kind == (u8)kind &&
        sKeyCache[eng][bg].bgcnt == bgcnt) {
        key = sKeyCache[eng][bg].key;
        __atomic_clear(&sKeyCacheLock[eng], __ATOMIC_RELEASE);
        return key;
    }
    __atomic_clear(&sKeyCacheLock[eng], __ATOMIC_RELEASE);
    key = BgKey(e, bg, kind, bgcnt, dispcnt);
    while (__atomic_test_and_set(&sKeyCacheLock[eng], __ATOMIC_ACQUIRE)) {
    }
    sKeyCache[eng][bg].quick = quick;
    sKeyCache[eng][bg].key = key;
    sKeyCache[eng][bg].kind = (u8)kind;
    sKeyCache[eng][bg].bgcnt = bgcnt;
    sKeyCache[eng][bg].valid = 1;
    __atomic_clear(&sKeyCacheLock[eng], __ATOMIC_RELEASE);
    return key;
}

/* Slice B: scroll-shifted text-BG row reuse. A text BG's rendered pixel is
 * a pure function of its map coordinate (TextBgPixel): the scroll only
 * selects which map pixels show. So a map row rendered once can serve every
 * screen line showing it, at any horizontal scroll, until the map, tiles
 * or palettes change. Keyed by BgKey (which covers exactly those, and not
 * the scroll) plus the wide-clear palette variant BgKey doesn't cover, and
 * tagged by map row. Direct-mapped by row (256 slots cover any frame's
 * window: e->height tops out at NITRO_RENDER2D_MAX_H): no within-frame
 * collisions, no LRU bookkeeping, and a miss costs one row render.
 * Per (engine, BG), malloc'd on first text draw (NULL: the cache is off and
 * drawing is exactly as before). An engine is drawn by one thread at a time,
 * as sWork assumes. */
#define TEXTROW_SLOTS 256

/* This BG's cache key: its map, tiles and colours, but not its scroll --
 * plus the wide-clear palette variant, which BgKey doesn't cover (BgPal4
 * reads bgPal4Clear for it instead of bgPal4). */
static u64 FoldClearVariant(const Engine *e, int bg, u64 key)
{
    if (key != 0 && ((e->clearBgs >> bg) & 1)) {
        u32 i;

        for (i = 0; i < 16; i++) {
            key = KeyAdd(key, e->clearMask[i]);
        }
    }
    return key;
}

static u64 TextRowKey(const Engine *e, int bg, u16 bgcnt, u32 dispcnt)
{
    return FoldClearVariant(e, bg, BgKeyCached(e, bg, BG_TEXT, bgcnt, dispcnt));
}

/* The BG's map row ty rendered (colour | OPAQUE, or 0, per pixel), from the
 * row cache, rendering and keeping it on a miss. NULL when the cache is off
 * (its malloc failed): the caller then draws direct, as before. */
static const u16 *TextRowCached(const Engine *e, int pe, int bg, u64 key, int ty, u16 bgcnt,
                                u32 dispcnt)
{
    TextRowEntry *table;
    TextRowEntry *slot;
    int i;

    /* No key (captures drawn without one): draw direct, as before. */
    if (key == 0) {
        return NULL;
    }
    table = sTextRows[pe][bg];
    if (table == NULL) {
        table = (TextRowEntry *)malloc(TEXTROW_SLOTS * sizeof(TextRowEntry));
        if (table == NULL) {
            return NULL;
        }
        memset(table, 0, TEXTROW_SLOTS * sizeof(TextRowEntry));
        sTextRows[pe][bg] = table;
    }
    slot = &table[ty & (TEXTROW_SLOTS - 1)];
    if (slot->key == key && slot->ty == (u16)ty) {
        sProfile[pe].rowHits++;
        return slot->px;
    }
    int mapW = sTextSizes[bgcnt >> 14][0];
    for (i = 0; i < mapW; i++) {
        slot->px[i] = TextBgPixel(e, bg, bgcnt, dispcnt, i, ty);
    }
    /* TextBgPixel wraps mapx by mapW. For a 256-wide map its second
     * 256 pixels are exactly the first half, including flips and palette. */
    if (mapW == 256) {
        memcpy(slot->px + 256, slot->px, 256 * sizeof(slot->px[0]));
    }
    slot->key = key;
    slot->ty = (u16)ty;
    sProfile[pe].rowMisses++;
    return slot->px;
}

/* (wider canvas) each BG's hand-drawn picture, if there's one made where
 * the BG is now: of a text BG's pictures, the one made at the scroll
 * nearest its scroll now, less than a screen away (a picture of what's on
 * screen, not of another part of the map). */
#define ART_REST 8   /* pixels from where a picture was made that are still there */
static void LookUpArt(Engine *e, const int *kind, const u16 *bgcnt, u32 dispcnt,
                        const u64 *frameKey)
{
    int bg;

    if (e->width != NITRO_WIDE_ART_W || NitroPort_WideArtCount() == 0) {
        return;
    }
    for (bg = 0; bg < 4; bg++) {
        u64 key;
        int hofs, vofs, full, moved = (e->movedBgs >> bg) & 1;
        const u16 *art;

        if (kind[bg] == BG_NONE || kind[bg] == BG_3D || !e->clip[bg] || (moved && kind[bg] != BG_TEXT) ||
            ((e->noArtBgs >> bg) & 1)) {
            continue;   /* (a BG the screen's parts move: its own pixels; NO_ART: no picture) */
        }
        /* Slice S2a: the frame's key, computed once in DrawEngineT's setup
         * (NULL: compute here, as before — captures). */
        key = frameKey != NULL ? frameKey[bg] : BgKeyCached(e, bg, kind[bg], bgcnt[bg], dispcnt);
        if (key == 0) {
            continue;
        }
        if (kind[bg] == BG_TEXT) {
            int mapW = sTextSizes[bgcnt[bg] >> 14][0], mapH = sTextSizes[bgcnt[bg] >> 14][1];
            int curH = Rd16(e->io, 0x10 + bg * 4) & 0x1FF, curV = Rd16(e->io, 0x12 + bg * 4) & 0x1FF;
            int dx, dy;

            if (moved) {
                /* A BG the screen's parts move: a picture made right where
                 * it is (a capture of the screen as the parts lay it out,
                 * drawn on: the boxes' panel's edges, 2026-10-16), else its
                 * own pixels as the parts move them (scrolled elsewhere, the
                 * parts' columns are another picture's). */
                art = NitroPort_WideArtFindNear(key, curH, curV, mapW, mapH, 1, 1, &hofs, &vofs, &full);
                if (art == NULL) {
                    continue;
                }
                e->artDx[bg] = e->artDy[bg] = 0;
                e->artH[bg] = (short)hofs;
                e->artV[bg] = (short)vofs;
                e->artBands[bg] = 1;
                e->art[bg] = art;
                e->artRows[bg] = NitroPort_WideArtRows(art);
                e->artFull[bg] = (u8)(full & NITRO_WIDE_ART_FULL);
                e->artLoopBands[bg] = 0;
                continue;
            }
            art = NitroPort_WideArtFindNear(key, curH, curV, mapW, mapH, W, H, &hofs, &vofs, &full);
            if (art == NULL) {
                continue;
            }
            e->artH[bg] = (short)hofs;
            e->artV[bg] = (short)vofs;
            /* how far it has scrolled since, the shorter way round its map
             * (masks, not divides: text maps are 256 or 512 wide). */
            dx = ((curH - hofs) & (mapW - 1));
            dx = dx >= mapW / 2 ? dx - mapW : dx;
            dy = ((curV - vofs) & (mapH - 1));
            dy = dy >= mapH / 2 ? dy - mapH : dy;
            e->artDx[bg] = (short)dx;
            e->artDy[bg] = (short)dy;
            /* Its bands while it's across where the picture was made (a
             * few pixels either way), or always for a strip that scrolls
             * round (_loop). Up and down doesn't matter (the running
             * scene's city bobs 13-22 pixels from its picture's place).
             * Elsewhere sideways, a whole picture is the whole layer, moved
             * with it: a Pokemon sliding in shows whole, not cut at the
             * DS's edge (2026-10-14), its slide widened so that it comes
             * in from the wide screen's edge: what the DS hides beyond its
             * edge, waiting to come in, stays hidden (Chimchar's hand
             * showed beside the DS's screen before he came in). A picture
             * of the bands only shows nothing there. */
            e->artBands[bg] = (u8)((full & NITRO_WIDE_ART_LOOP) ||
                                   (e->artDx[bg] >= -ART_REST && e->artDx[bg] <= ART_REST));
            if (!e->artBands[bg] && (full & NITRO_WIDE_ART_FULL)) {
                int left, right, moved = -e->artDx[bg], widened = moved;

                if (NitroPort_WideArtExtent(art, &left, &right)) {
                    widened = NitroPort_WideArtMovedOffset(moved, left, right);
                }
                e->artMoved[bg] = 1;
                e->artExact[bg] = (u8)(widened == moved);
                e->artDx[bg] = (short)-widened;
            }
        } else {
            art = NitroPort_WideArtFindNear(key, 0, 0, 512, 512, 0, 0, &hofs, &vofs, &full);
            if (art == NULL) {
                continue;
            }
        }
        e->art[bg] = art;
        e->artRows[bg] = NitroPort_WideArtRows(art);
        e->artFull[bg] = (u8)(full & NITRO_WIDE_ART_FULL);
        e->artLoopBands[bg] = (u8)(kind[bg] == BG_TEXT && (full & NITRO_WIDE_ART_LOOP) && !(full & NITRO_WIDE_ART_FULL));
        if (kind[bg] != BG_TEXT) {
            e->artBands[bg] = 1;
        }
    }
}

/* ---------------------------------------------------------------- OBJs */

static const u8 sObjSize[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },     /* square */
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },     /* wide */
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },     /* tall */
};

/* An OBJ that shows, as OAM describes it, worked out once a frame. */
typedef struct Obj {
    s16 ox;                /* left edge, -256..255 */
    s16 stretch;           /* (moved by a screen's part) its edge column drawn on over this many pixels back
                            * to where it was: its first column left of it (> 0), its last right of it (< 0) */
    u8 oy;
    short oyTall;          /* (a tall field: Engine.top) its top line on the picture, signed: no wrapping */
    u8 w, h;
    u16 boxW, boxH;        /* doubled for double-size affine OBJs */
    u8 affine, hflip, vflip, bpp8;
    u8 mode;               /* 0 normal, 1 semi-transparent, 2 window, 3 bitmap */
    u8 prio;
    u8 pal;
    u16 tile;
    s32 pa, pb, pc, pd;
    s16 mid2;              /* (Engine.squeeze) twice the x it's drawn narrower round: its sprite's middle */
} Obj;

/* (wider canvas) a group of OBJs' hand-drawn picture (see ObjGroups):
 * drawn into the OBJ layer where no OBJ is, at the group's priority. */
typedef struct ObjArt {
    const u16 *art;
    const u8 *rows;        /* (its rows with anything: NitroPort_WideArtRows) */
    short dx, dy;          /* how far the group has moved since its capture */
    u8 prio, full;
} ObjArt;
#define OBJ_ART_MAX 4

/* This frame's OBJs, and which of them are on each line (in OAM order). */
typedef struct ObjFrame {
    Obj obj[128];
    u8 count;
    u8 blends;             /* some are semi-transparent or bitmap OBJs */
    u8 artCount;
    ObjArt art[OBJ_ART_MAX];
    u8 onLine[HMAX][128];
    u8 lineCount[HMAX];
    u8 taken[128];         /* S6: drawn as compositor quads, not here */
    u8 nTaken;
} ObjFrame;

/* OAM entry i as an Obj (its left edge on the canvas): FALSE if it's
 * hidden (or of the shape that doesn't exist). */
static BOOL ReadObj(const Engine *e, int i, Obj *obj)
{
    const u8 *o = e->oam + i * 8;
    u16 a0 = Rd16(o, 0), a1 = Rd16(o, 2), a2 = Rd16(o, 4);
    int shape = (a0 >> 14) & 3;

    if (!(a0 & 0x100) && (a0 & 0x200)) {
        return FALSE;                       /* hidden */
    }
    if (shape == 3) {
        return FALSE;
    }
    obj->affine = (a0 & 0x100) != 0;
    obj->stretch = 0;
    obj->w = sObjSize[shape][(a1 >> 14) & 3][0];
    obj->h = sObjSize[shape][(a1 >> 14) & 3][1];
    obj->boxW = obj->w;
    obj->boxH = obj->h;
    obj->pa = 256;
    obj->pb = 0;
    obj->pc = 0;
    obj->pd = 256;
    if (obj->affine) {
        int p = (a1 >> 9) & 0x1F;

        obj->pa = (s16)Rd16(e->oam, (u32)p * 32 + 6);
        obj->pb = (s16)Rd16(e->oam, (u32)p * 32 + 14);
        obj->pc = (s16)Rd16(e->oam, (u32)p * 32 + 22);
        obj->pd = (s16)Rd16(e->oam, (u32)p * 32 + 30);
        if (a0 & 0x200) {               /* double size */
            obj->boxW *= 2;
            obj->boxH *= 2;
        }
    }
    obj->oy = (u8)(a0 & 0xFF);
    if (e->top) {
        /* (a tall field's lines above the DS's: moved down by them. On
         * the DS one from y 192 comes round to the top only where it goes
         * past 256; one above 192 never does, however tall: its lines past
         * the DS's are below it here, not at the picture's top) */
        int y = a0 & 0xFF;

        obj->oyTall = (short)((y >= H && (y + obj->boxH > 256 || y >= 256 - e->top) ? y - 256 : y) + e->top);
        obj->oy = (u8)obj->oyTall;
    }
    obj->ox = (s16)(a1 & 0x1FF);
    /* (x 256-511 is left of the screen; on a wider canvas the first of
     * those are right of the DS's screen, where the canvas goes on) */
    if (obj->ox >= 256 + (e->width - W - e->origin)) {
        obj->ox -= 512;
    }
    if (e->nObjMove) {
        /* (over a window moved to an edge: with it) */
        int cx = obj->ox + obj->boxW / 2, cy = e->top ? obj->oyTall + obj->boxH / 2 : (obj->oy + obj->boxH / 2) & 255, k;

        for (k = 0; k < e->nObjMove; k++) {
            if (e->objMove[k].dy == 0 && cx >= e->objMove[k].x0 && cx < e->objMove[k].x1 &&
                cy >= e->objMove[k].y0 && cy < e->objMove[k].y1) {
                obj->ox += e->objMove[k].shift;
                if (e->objMove[k].stretch && !obj->affine && obj->mode != 2) {
                    obj->stretch = e->objMove[k].shift;
                }
                break;
            }
        }
        /* (a tall field: over a window moved to the picture's top or
         * bottom, with it; the window's x as the DS has it) */
        for (k = 0; k < e->nObjMove; k++) {
            if (e->objMove[k].dy != 0 && cx >= e->objMove[k].x0 && cx < e->objMove[k].x1 &&
                cy >= e->objMove[k].y0 && cy < e->objMove[k].y1) {
                obj->oyTall = (short)(obj->oyTall + e->objMove[k].dy);
                obj->oy = (u8)obj->oyTall;
                break;
            }
        }
    }
    obj->ox += e->origin;
    obj->hflip = !obj->affine && (a1 & 0x1000) != 0;
    obj->vflip = !obj->affine && (a1 & 0x2000) != 0;
    obj->bpp8 = (a0 & 0x2000) != 0;
    obj->mode = (a0 >> 10) & 3;
    obj->tile = a2 & 0x3FF;
    obj->prio = (a2 >> 10) & 3;
    obj->pal = a2 >> 12;
    return TRUE;
}

static void SortObjs(const Engine *e, ObjFrame *f)
{
    int i, y;

    f->count = 0;
    f->blends = 0;
    f->artCount = 0;
    memset(f->lineCount, 0, sizeof(f->lineCount));
    for (i = 0; i < 128; i++) {
        Obj *obj = &f->obj[f->count];

        if (!ReadObj(e, i, obj)) {
            continue;
        }
        if (obj->ox >= e->width || obj->ox + obj->boxW <= 0) {
            continue;                       /* off the sides */
        }

        /* The lines it's on: (y - oy) & 255 < boxH. */
        for (y = 0; y < e->height; y++) {
            if (e->top ? y - obj->oyTall >= 0 && y - obj->oyTall < obj->boxH : ((y - obj->oy) & 0xFF) < obj->boxH) {
                f->onLine[y][f->lineCount[y]++] = f->count;
            }
        }
        if (obj->mode == 1 || obj->mode == 3) {
            f->blends = 1;
        }
        f->count++;
    }
    if (e->squeeze) {
        /* (Engine.squeeze) each sprite drawn narrower round its middle: the
         * OBJs touching or overlapping one another one sprite (a big
         * picture made of several), so its pieces stay together */
        u8 root[128];
        int j, k;

        for (i = 0; i < f->count; i++) {
            root[i] = (u8)i;
        }
        for (i = 0; i < f->count; i++) {
            const Obj *a = &f->obj[i];

            for (j = i + 1; j < f->count; j++) {
                const Obj *b = &f->obj[j];
                int ra, rb;

                if (a->mode == 2 || b->mode == 2 || a->ox > b->ox + b->boxW || b->ox > a->ox + a->boxW ||
                    a->oy > b->oy + b->boxH || b->oy > a->oy + a->boxH) {
                    continue;
                }
                for (ra = i; root[ra] != ra; ra = root[ra]) { }
                for (rb = j; root[rb] != rb; rb = root[rb]) { }
                if (ra != rb) {
                    root[ra > rb ? ra : rb] = (u8)(ra < rb ? ra : rb);
                }
            }
        }
        for (i = 0; i < f->count; i++) {
            int r, lo = 32767, hi = -32768;

            for (r = i; root[r] != r; r = root[r]) { }
            for (k = 0; k < f->count; k++) {
                int rk;

                for (rk = k; root[rk] != rk; rk = root[rk]) { }
                if (rk == r) {
                    if (f->obj[k].ox < lo) {
                        lo = f->obj[k].ox;
                    }
                    if (f->obj[k].ox + f->obj[k].boxW > hi) {
                        hi = f->obj[k].ox + f->obj[k].boxW;
                    }
                }
            }
            f->obj[i].mid2 = (s16)(lo + hi);
        }
    }
}

/* The address of an OBJ's pixel row in its graphics: a tile's row of 8
 * pixels (tx a multiple of 8), or for bitmap OBJs the pixel itself. */
static u32 ObjCharAddr(const Obj *obj, u32 dispcnt, int tx, int ty)
{
    if (obj->mode == 3) {
        if (dispcnt & 0x40) {       /* 1D */
            return (u32)obj->tile * ((dispcnt & 0x400000) ? 256u : 128u) + ((u32)ty * obj->w + (u32)tx) * 2;
        } else if (dispcnt & 0x20) {    /* 2D, 256 dots wide */
            return (u32)(obj->tile & 0x1F) * 0x10 + (u32)(obj->tile & ~0x1F) * 0x80 + ((u32)ty * 256 + (u32)tx) * 2;
        } else {                    /* 2D, 128 dots wide */
            return (u32)(obj->tile & 0x0F) * 0x10 + (u32)(obj->tile & ~0x0F) * 0x80 + ((u32)ty * 128 + (u32)tx) * 2;
        }
    } else {
        u32 addr;
        int tileX = tx >> 3, tileY = ty >> 3;

        if (dispcnt & 0x10) {       /* 1D character mapping */
            u32 unit = 32u << ((dispcnt >> 20) & 3);
            u32 n = (u32)(tileY * (obj->w >> 3) + tileX);

            addr = (u32)obj->tile * unit + n * (obj->bpp8 ? 64u : 32u);
        } else {                    /* 2D: 32 characters per row */
            addr = (u32)obj->tile * 32 + (u32)tileY * 1024 + (u32)tileX * (obj->bpp8 ? 64u : 32u);
        }
        return addr + (u32)(ty & 7) * (obj->bpp8 ? 8u : 4u);
    }
}

/* One pixel of an OBJ's graphics, at (tx, ty) in it: its colour | OPAQUE
 * and *alpha, or 0 where it's transparent. */
static u16 ObjPixel(const Engine *e, const Obj *obj, u32 dispcnt, int tx, int ty, int *alpha)
{
    u32 addr;

    *alpha = 16;
    if (obj->mode == 3) {                /* bitmap OBJ: 15-bit colour */
        u16 rgb = Rd16(e->objVram, ObjCharAddr(obj, dispcnt, tx, ty) & e->objMask);

        if (!(rgb & 0x8000) || obj->pal == 0) {
            return 0;
        }
        *alpha = obj->pal + 1;
        return rgb;
    }
    addr = ObjCharAddr(obj, dispcnt, tx & ~7, ty);
    if (obj->bpp8) {
        u32 index = e->objVram[(addr + (u32)(tx & 7)) & e->objMask];

        if (index == 0) {
            return 0;
        }
        return ((e->objExt != NULL && (dispcnt & 0x80000000))
                    ? PlttColor(e->objExt, ((u32)obj->pal << 8) | index)
                    : PlttColor(e->objPltt, index)) | OPAQUE;
    } else {
        u8 pair = e->objVram[(addr + (u32)((tx & 7) >> 1)) & e->objMask];
        u32 index = (tx & 1) ? (pair >> 4) : (pair & 0xF);

        if (index == 0) {
            return 0;
        }
        return PlttColor(e->objPltt, ((u32)obj->pal << 4) | index) | OPAQUE;
    }
}

/* S6 bundle: taken OBJs' atlas (512 x 512 RGBA5551, 2 texels a pixel with
 * transparent gutters; 3 x 3 cells of 64 x 64). Decoded once per content
 * change with the same ObjPixel the software uses, so identical by
 * construction; upload is a straight tile. */
static uint16_t sGpuObjAtlas[512 * 512];
static u64 sGpuObjAtlasKey;
/* Slice S5: the frame gate's DISPCNT value, logged once (it decides the
 * next fix: windows, BG0 display, or display mode keep takes off). */
static int sGpuGateDspLogged = 0;
static int sGpuGateKindLogged = 0;

static void GpuObjAtlasFill(const Engine *e, u32 dispcnt, const ObjFrame *f, const int *takeObj, const int *takeCell,
                            int nTake)
{
    int t, tx, ty;

    memset(sGpuObjAtlas, 0, sizeof(sGpuObjAtlas));
    for (t = 0; t < nTake; t++) {
        const Obj *o = &f->obj[takeObj[t]];
        int cx = (takeCell[t] % 3) * NITRO_GPU_ATLAS_PITCH + NITRO_GPU_ATLAS_GUTTER;
        int cy = (takeCell[t] / 3) * NITRO_GPU_ATLAS_PITCH + NITRO_GPU_ATLAS_GUTTER;

        for (ty = 0; ty < o->h; ty++) {
            for (tx = 0; tx < o->w; tx++) {
                int alpha;
                u16 c = ObjPixel(e, o, dispcnt, tx, ty, &alpha);
                u16 packed;

                if (c == 0) {
                    continue;
                }
                packed = (uint16_t)(((c & 31) << 11) | (((c >> 5) & 31) << 6) | (((c >> 10) & 31) << 1) | 1);
                sGpuObjAtlas[(size_t)(cy + ty * 2) * 512 + cx + tx * 2] = packed;
                sGpuObjAtlas[(size_t)(cy + ty * 2) * 512 + cx + tx * 2 + 1] = packed;
                sGpuObjAtlas[(size_t)(cy + ty * 2 + 1) * 512 + cx + tx * 2] = packed;
                sGpuObjAtlas[(size_t)(cy + ty * 2 + 1) * 512 + cx + tx * 2 + 1] = packed;
            }
        }
    }
}

/* ---------------------------------------------- OBJ groups' hand-drawn art */

/* The OBJs are put in groups for wide art (nitro_port_wideart.h): those of
 * one priority and one palette (the 8-bit ones of a priority together); a
 * scene's sprite background (the intro's strip behind the Pokemon) is
 * usually one, the sprites in front of it others. The window and bitmap
 * OBJs aren't in any. */
static inline int ObjGroupOf(const Obj *obj)
{
    return (obj->prio << 5) | (obj->bpp8 ? 16 : obj->pal);
}

/* A fingerprint of an OBJ's first tile (its pixels): which of a group's
 * OBJs its position is taken from. */
static u64 ObjFirstTile(const Engine *e, const Obj *obj, u32 dispcnt)
{
    u32 addr = ObjCharAddr(obj, dispcnt, 0, 0) & e->objMask & ~3u, k;
    u64 h = KEY_START;

    for (k = 0; k < (obj->bpp8 ? 64u : 32u); k += 4) {
        h = (h ^ Rd32(e->objVram, (addr + k) & e->objMask)) * 0x100000001b3ull;
    }
    return h;
}

/* The OBJ groups shown (all of OAM's, on screen or not): each one's key and
 * where it is. The key is a fingerprint of the tiles its OBJs use (each
 * once, in VRAM's order: their pixels, not where they are) and its
 * palette: so not of how many OBJs show them, nor their shapes or places
 * (a strip of sprites scrolling round, its sprites cut differently as they
 * go, keeps its key). Where it is: the position (OAM's x 0-511, y 0-255)
 * of the OBJ whose first tile has the smallest fingerprint, the first of
 * those. */
static int ObjGroups(const Engine *e, u32 dispcnt, NitroObjGroup *out, int max)
{
    struct {
        u64 anchor;
        u16 count, x, y;
    } g[128];
    s8 groupOf[128];
    u32 used[0x40000 / 32 / 32];   /* the 32-byte tile units used, of OBJ VRAM */
    int i, k, n = 0;

    memset(g, 0, sizeof(g));
    for (i = 0; i < 128; i++) {
        Obj obj;
        u64 h;

        groupOf[i] = -1;
        if (!ReadObj(e, i, &obj) || obj.mode >= 2) {
            continue;
        }
        k = ObjGroupOf(&obj);
        groupOf[i] = (s8)k;
        h = ObjFirstTile(e, &obj, dispcnt);
        if (g[k].count == 0 || h < g[k].anchor) {
            g[k].anchor = h;
            g[k].x = Rd16(e->oam, (u32)i * 8 + 2) & 0x1FF;
            g[k].y = Rd16(e->oam, (u32)i * 8) & 0xFF;
        }
        g[k].count++;
    }
    for (k = 0; k < 128 && n < max; k++) {
        u64 key;
        u32 units = (e->objMask + 1) / 32, u, c;

        if (g[k].count == 0) {
            continue;
        }
        /* the tiles its OBJs use */
        memset(used, 0, sizeof(used));
        for (i = 0; i < 128; i++) {
            Obj obj;
            int tx, ty;

            if (groupOf[i] != k || !ReadObj(e, i, &obj)) {
                continue;
            }
            for (ty = 0; ty < obj.h; ty += 8) {
                for (tx = 0; tx < obj.w; tx += 8) {
                    u = (ObjCharAddr(&obj, dispcnt, tx, ty) & e->objMask) >> 5;
                    used[u >> 5] |= 1u << (u & 31);
                    if (obj.bpp8 && u + 1 < units) {
                        used[(u + 1) >> 5] |= 1u << ((u + 1) & 31);
                    }
                }
            }
        }
        key = KeyAdd(KEY_START, 0x4F420000u | (u32)(k >> 5));   /* "OB", the priority */
        for (u = 0; u < units; u++) {
            if (used[u >> 5] == 0) {
                u += 31;
                continue;
            }
            if (used[u >> 5] & (1u << (u & 31))) {
                for (c = 0; c < 32; c += 4) {
                    key = (key ^ Rd32(e->objVram, u * 32 + c)) * 0x100000001b3ull;
                }
            }
        }
        /* its colours */
        if (!(k & 16)) {
            for (c = 0; c < 16; c++) {
                key = (key ^ PlttColor(e->objPltt, ((u32)(k & 15) << 4) | c)) * 0x100000001b3ull;
            }
        } else {
            for (c = 0; c < 256; c++) {
                key = (key ^ PlttColor(e->objPltt, c)) * 0x100000001b3ull;
            }
        }
        out[n].key = key ? key : 1;
        out[n].group = k;
        out[n].prio = k >> 5;
        out[n].x = g[k].x;
        out[n].y = g[k].y;
        out[n].count = g[k].count;
        n++;
    }
    return n;
}

/* (wider canvas) the OBJ groups' hand-drawn pictures, if there are any (all
 * groups', or only's: -1 for all). */
static void LookUpObjArt(const Engine *e, ObjFrame *f, u32 dispcnt, int only)
{
    NitroObjGroup groups[32];
    int n, i;

    f->artCount = 0;
    if (e->width != NITRO_WIDE_ART_W || NitroPort_WideArtCount() == 0 || !(dispcnt & 0x1000) || e->objsMoved) {
        return;   /* (sprites the screen's parts move: themselves) */
    }
    n = ObjGroups(e, dispcnt, groups, 32);
    for (i = 0; i < n && f->artCount < OBJ_ART_MAX; i++) {
        int x, y, full, dx, dy;
        const u16 *art;
        ObjArt *a;

        if (only >= 0 && groups[i].group != only) {
            continue;
        }
        art = NitroPort_WideArtFindNear(groups[i].key, groups[i].x, groups[i].y, 512, 256, 0, 0, &x, &y, &full);
        if (art == NULL) {
            continue;
        }
        dx = (groups[i].x - x + 512) & 511;
        dy = (groups[i].y - y + 256) & 255;
        a = &f->art[f->artCount++];
        a->art = art;
        a->rows = NitroPort_WideArtRows(art);
        a->dx = (short)(dx >= 256 ? dx - 512 : dx);
        a->dy = (short)(dy >= 128 ? dy - 256 : dy);
        a->prio = (u8)groups[i].prio;
        a->full = (u8)(full & NITRO_WIDE_ART_FULL);
    }
}

/* Put one OBJ pixel on the line (lower-numbered OBJs, drawn first, stay in
 * front; the window OBJs only mark the OBJ window). */
static inline void PutObjPixel(const Obj *obj, ObjLine *out, int x, u16 rgb, int alpha)
{
    if (obj->mode == 2) {
        out->window[x] = 1;
        return;
    }
    out->color[x] = rgb | OPAQUE;
    out->prio[x] = obj->prio;
    out->prios |= (u8)(1 << obj->prio);
    out->semi[x] = obj->mode == 1;
    out->alpha[x] = (u8)alpha;
}

/* (NitroPort_Render2DSmoothObjs) */
static volatile int sSmoothObjs[2];

void NitroPort_Render2DSmoothObjs(int engine, int on)
{
    __atomic_store_n(&sSmoothObjs[engine ? 1 : 0], on ? 1 : 0, __ATOMIC_RELEASE);
}

/* An affine OBJ's pixel drawn smoothly: (u, v) where its middle falls in
 * the OBJ's texels, in 256ths, from the top-left texel's middle; the four
 * texels round it blended by how near each is, the clear ones left out (a
 * pixel mostly clear: clear). 0: clear. */
static u16 SmoothObjPixel(const Engine *e, const Obj *obj, u32 dispcnt, s32 u, s32 v, int *alpha)
{
    int tx = u >> 8, ty = v >> 8, ax = u & 255, ay = v & 255, k, weights = 0, a = 16, best = 0;
    u32 r = 0, g = 0, b = 0;

    for (k = 0; k < 4; k++) {
        int px = tx + (k & 1), py = ty + (k >> 1);
        int weight = ((k & 1) ? ax : 256 - ax) * ((k >> 1) ? ay : 256 - ay);
        int texelAlpha;
        u16 c;

        if (weight == 0 || px < 0 || py < 0 || px >= obj->w || py >= obj->h) {
            continue;
        }
        c = ObjPixel(e, obj, dispcnt, px, py, &texelAlpha);
        if (c == 0) {
            continue;
        }
        if (weight > best) {
            best = weight;   /* (a bitmap OBJ's alpha: the nearest shown texel's) */
            a = texelAlpha;
        }
        weights += weight;
        r += (u32)(c & 31) * (u32)weight;
        g += (u32)((c >> 5) & 31) * (u32)weight;
        b += (u32)((c >> 10) & 31) * (u32)weight;
    }
    *alpha = a;
    if (weights * 2 < 256 * 256) {
        return 0;
    }
    r = (r + (u32)weights / 2) / (u32)weights;
    g = (g + (u32)weights / 2) / (u32)weights;
    b = (b + (u32)weights / 2) / (u32)weights;
    return (u16)(r | (g << 5) | (b << 10) | OPAQUE);
}

/* The OBJ layer's line; returns whether any OBJ is on it. */
static BOOL DrawObjs(const Engine *e, const ObjFrame *f, u32 dispcnt, int y, ObjLine *out)
{
    int smooth = __atomic_load_n(&sSmoothObjs[e->isA ? 0 : 1], __ATOMIC_ACQUIRE);

    u16 sprites[WMAX];
    int k;

    if (!(dispcnt & 0x1000) || (f->lineCount[y] == 0 && f->artCount == 0)) {
        return FALSE;
    }
    /* Only the span the line's OBJs can write is cleared: nothing outside
     * it is read while they're drawn (the sprite pictures cover the whole
     * row, and the squeeze maps a cluster's sprites round the cluster,
     * so both keep the full clear). */
    {
        int lo = e->width, hi = 0;

        if (f->artCount > 0 || e->squeeze) {
            lo = 0;
            hi = e->width;
        } else {
            for (k = 0; k < f->lineCount[y]; k++) {
                const Obj *obj = &f->obj[f->onLine[y][k]];
                int a = obj->ox, b = obj->ox + obj->boxW;

                if (obj->stretch > 0) {
                    a -= obj->stretch;
                } else if (obj->stretch < 0) {
                    b -= obj->stretch;
                }
                if (a < 0) {
                    a = 0;
                }
                if (b > e->width) {
                    b = e->width;
                }
                if (a < lo) {
                    lo = a;
                }
                if (b > hi) {
                    hi = b;
                }
            }
        }
        if (lo < hi) {
            memset(out->color + lo, 0, (size_t)(hi - lo) * sizeof(out->color[0]));
            memset(out->window + lo, 0, (size_t)(hi - lo));
        }
    }
    out->prios = 0;
    out->x0 = (short)e->width;
    out->x1 = 0;

    for (k = 0; k < f->lineCount[y]; k++) {
        const Obj *obj = &f->obj[f->onLine[y][k]];

        if (f->taken[f->onLine[y][k]]) {
            continue;   /* (S6: a compositor quad draws it) */
        }
        int ly = e->top ? y - obj->oyTall : (y - obj->oy) & 0xFF;
        int sx = obj->ox < 0 ? -obj->ox : 0;
        int end = obj->ox + obj->boxW > e->width ? e->width - obj->ox : obj->boxW;

        if (obj->ox + sx < out->x0) {
            out->x0 = (short)(obj->ox + sx);
        }
        if (obj->ox + end > out->x1) {
            out->x1 = (short)(obj->ox + end);
        }

        if (e->squeeze && obj->mode != 2) {
            /* (Engine.squeeze) its sprite 4/5 as wide round the sprite's
             * middle (window OBJs stretch with the windows): each pixel's
             * middle 5/4 as far from it, in 1/256 pixels; through its
             * matrix if it has one */
            s32 dy = ly - obj->boxH / 2;
            s32 mid = obj->mid2 * 128;
            int x0 = (int)((mid + (obj->ox * 256 - mid) * 4 / 5) >> 8);
            int x1 = (int)((mid + ((obj->ox + obj->boxW) * 256 - mid) * 4 / 5 + 255) >> 8);
            int x;

            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > e->width) {
                x1 = e->width;
            }
            if (x0 < out->x0) {
                out->x0 = (short)x0;
            }
            if (x1 > out->x1) {
                out->x1 = (short)x1;
            }
            for (x = x0; x < x1; x++) {
                s32 rel = mid + (x * 256 + 128 - mid) * 5 / 4 - 128 - obj->ox * 256;
                int tx, ty, alpha;
                u16 rgb;

                if (out->color[x] & OPAQUE) {
                    continue;
                }
                if (obj->affine) {
                    s32 dx = rel - (obj->boxW / 2) * 256;
                    s32 fx = ((obj->pa * dx) >> 8) + obj->pb * dy;
                    s32 fy = ((obj->pc * dx) >> 8) + obj->pd * dy;

                    tx = (fx >> 8) + obj->w / 2;
                    ty = (fy >> 8) + obj->h / 2;
                } else {
                    tx = rel >> 8;
                    ty = ly;
                    if (obj->hflip) {
                        tx = obj->w - 1 - tx;
                    }
                    if (obj->vflip) {
                        ty = obj->h - 1 - ty;
                    }
                }
                if (tx < 0 || ty < 0 || tx >= obj->w || ty >= obj->h) {
                    continue;
                }
                rgb = ObjPixel(e, obj, dispcnt, tx, ty, &alpha);
                if (rgb) {
                    PutObjPixel(obj, out, x, rgb, alpha);
                }
            }
        } else if (obj->affine && smooth && obj->mode != 2) {
            /* (NitroPort_Render2DSmoothObjs: the pixel's middle mapped,
             * the texels round it blended) */
            s32 dy = ly - obj->boxH / 2;
            s32 dx = sx - obj->boxW / 2;
            s32 fx = obj->pa * dx + obj->pb * dy + (obj->pa + obj->pb) / 2 + (obj->w << 7) - 128;
            s32 fy = obj->pc * dx + obj->pd * dy + (obj->pc + obj->pd) / 2 + (obj->h << 7) - 128;

            for (; sx < end; sx++, fx += obj->pa, fy += obj->pc) {
                int x = obj->ox + sx;
                int alpha;
                u16 rgb;

                if ((out->color[x] & OPAQUE) && obj->mode != 2) {
                    continue;
                }
                if (fx < -256 || fy < -256 || fx >= obj->w << 8 || fy >= obj->h << 8) {
                    continue;
                }
                rgb = SmoothObjPixel(e, obj, dispcnt, fx, fy, &alpha);
                if (rgb) {
                    PutObjPixel(obj, out, x, rgb, alpha);
                }
            }
        } else if (obj->affine) {
            s32 dy = ly - obj->boxH / 2;
            s32 dx = sx - obj->boxW / 2;
            s32 fx = obj->pa * dx + obj->pb * dy;
            s32 fy = obj->pc * dx + obj->pd * dy;

            for (; sx < end; sx++, fx += obj->pa, fy += obj->pc) {
                int x = obj->ox + sx;
                int tx = (fx >> 8) + obj->w / 2;
                int ty = (fy >> 8) + obj->h / 2;
                int alpha;
                u16 rgb;

                if ((out->color[x] & OPAQUE) && obj->mode != 2) {
                    continue;
                }
                if (tx < 0 || ty < 0 || tx >= obj->w || ty >= obj->h) {
                    continue;
                }
                rgb = ObjPixel(e, obj, dispcnt, tx, ty, &alpha);
                if (rgb) {
                    PutObjPixel(obj, out, x, rgb, alpha);
                }
            }
        } else if (obj->mode == 3) {
            int ty = obj->vflip ? obj->h - 1 - ly : ly;

            for (; sx < end; sx++) {
                int x = obj->ox + sx;
                int tx = obj->hflip ? obj->w - 1 - sx : sx;
                int alpha;
                u16 rgb;

                if (out->color[x] & OPAQUE) {
                    continue;
                }
                rgb = ObjPixel(e, obj, dispcnt, tx, ty, &alpha);
                if (rgb) {
                    PutObjPixel(obj, out, x, rgb, alpha);
                }
            }
        } else {
            /* A row of tiles: each tile's row of 8 pixels read once. */
            int ty = obj->vflip ? obj->h - 1 - ly : ly;
            const u8 *vram = e->objVram;
            u32 mask = e->objMask;
            const u8 *ext = obj->bpp8 && e->objExt != NULL && (dispcnt & 0x80000000)
                                ? e->objExt + ((u32)obj->pal << 9) : NULL;
            const u16 *pal = obj->bpp8 ? e->objPal8 : e->objPal4 + ((u32)obj->pal << 4);
            while (sx < end) {
                int tx = obj->hflip ? obj->w - 1 - sx : sx;
                u32 addr = ObjCharAddr(obj, dispcnt, tx & ~7, ty);
                /* the pixels of this tile on the line, left to right */
                int n = obj->hflip ? (tx & 7) + 1 : 8 - (tx & 7);
                int step = obj->hflip ? -1 : 1;
                int i;

                if (n > end - sx) {
                    n = end - sx;
                }
                for (i = 0; i < n; i++, tx += step) {
                    int x = obj->ox + sx + i;
                    u32 index;

                    if ((out->color[x] & OPAQUE) && obj->mode != 2) {
                        continue;
                    }
                    if (obj->bpp8) {
                        index = vram[(addr + (u32)(tx & 7)) & mask];
                    } else {
                        u8 pair = vram[(addr + (u32)((tx & 7) >> 1)) & mask];

                        index = (tx & 1) ? (pair >> 4) : (pair & 0xF);
                    }
                    if (index != 0) {
                        PutObjPixel(obj, out, x, ext != NULL ? PlttColor(ext, index) : pal[index], 16);
                    }
                }
                sx += n;
            }
        }
        if (obj->stretch) {
            /* moved by a screen's part, stretched: its edge column drawn
             * on back to where it was (a cursor made of two halves,
             * widened with its row) */
            int ty = obj->vflip ? obj->h - 1 - ly : ly;
            int edge = obj->stretch > 0 ? 0 : obj->w - 1;
            int tx = obj->hflip ? obj->w - 1 - edge : edge;
            int x0 = obj->stretch > 0 ? obj->ox - obj->stretch : obj->ox + obj->w;
            int x1 = obj->stretch > 0 ? obj->ox : obj->ox + obj->w - obj->stretch;
            int alpha, x;
            u16 rgb = ObjPixel(e, obj, dispcnt, tx, ty, &alpha);

            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > e->width) {
                x1 = e->width;
            }
            for (x = x0; rgb && x < x1; x++) {
                if (!(out->color[x] & OPAQUE)) {
                    PutObjPixel(obj, out, x, rgb, alpha);
                }
            }
            if (x0 < out->x0) {
                out->x0 = (short)x0;
            }
            if (x1 > out->x1) {
                out->x1 = (short)x1;
            }
        }
    }
    if (f->artCount > 0) {
        /* Slice S3 art-line fast path: the sprite-row copy below feeds
         * only the WideArtIsGame lookups in the loop, which all start with
         * this same ay/rows test. When no art group covers the line the
         * loop writes nothing, so skip both the copy and the walk. */
        int artHere = 0, ak;

        for (ak = 0; ak < f->artCount; ak++) {
            int aay = ArtWrap(y - f->art[ak].dy, H, 256);

            if (aay >= 0 && (f->art[ak].rows == NULL || f->art[ak].rows[aay])) {
                artHere = 1;
                break;
            }
        }
        if (!artHere) {
            return out->x0 < out->x1 || f->lineCount[y] > 0;
        }
        /* (the sprites' own pixels, before any picture goes on: a picture's
         * pixel that's a sprite's moved along is drawn from them) */
        memcpy(sprites, out->color, (size_t)e->width * sizeof(u16));
    }
    for (k = 0; k < f->artCount; k++) {
        /* a group's picture where no OBJ is (in the bands beside the DS's
         * screen, or all over) */
        const ObjArt *a = &f->art[k];
        int ay = ArtWrap(y - a->dy, H, 256), x;
        const u16 *row;
        ArtRun run;

        if (ay < 0 || (a->rows != NULL && !a->rows[ay])) {
            continue;
        }
        row = a->art + ay * NITRO_WIDE_ART_W;
        ArtRunStart(&run, -a->dx, NITRO_WIDE_ART_W, 256);
        for (x = 0; x < e->width; x++) {
            /* (past the picture's edge: a screen's width on, as a strip of
             * sprites that scrolls round goes on) */
            int ax = ArtRunNext(&run);
            u16 c;

            if (ax < 0 || (out->color[x] & OPAQUE) ||
                (!a->full && x >= e->origin && x < e->origin + W)) {
                continue;
            }
            c = row[ax];
            if (NitroPort_WideArtIsGame(c)) {
                /* the sprites' own pixel that far back on the row (the
                 * picture had it there, moved) */
                int gx = x - NitroPort_WideArtGameDx(c);

                c = (gx >= 0 && gx < e->width && gx != x) ? sprites[gx] : 0;
            }
            if (!(c & 0x8000)) {
                continue;
            }
            out->color[x] = c | OPAQUE;
            out->prio[x] = a->prio;
            out->prios |= (u8)(1 << a->prio);
            out->semi[x] = 0;
            out->alpha[x] = 16;
            if (x < out->x0) {
                out->x0 = (short)x;
            }
            if (x + 1 > out->x1) {
                out->x1 = (short)(x + 1);
            }
        }
    }
    return out->x0 < out->x1 || f->lineCount[y] > 0;
}

/* ------------------------------------------------------------ the frame */

static u16 Blend(u16 a, u16 b, int eva, int evb)
{
    /* The three parts side by side in one word (red at bit 0, blue at 10,
     * green at 21), so two multiplications do all six; each sum is under
     * 1024 and can't run into the next part. */
    u32 pa = (a & 0x7C1Fu) | ((u32)(a & 0x03E0u) << 16);
    u32 pb = (b & 0x7C1Fu) | ((u32)(b & 0x03E0u) << 16);
    u32 sum = pa * (u32)eva + pb * (u32)evb;
    int r = (sum & 0x3FF) >> 4;
    int bl = ((sum >> 10) & 0x3FF) >> 4;
    int g = ((sum >> 21) & 0x3FF) >> 4;

    if (r > 31) {
        r = 31;
    }
    if (g > 31) {
        g = 31;
    }
    if (bl > 31) {
        bl = 31;
    }
    return (u16)(r | (g << 5) | (bl << 10));
}

/* Blend() when eva + evb <= 16 (always for a 3D or bitmap OBJ pixel's own
 * alpha, a and 16 - a): no part can go over 31, so nothing to clamp, and
 * the parts come out of the sum with a shift and two masks. */
static inline u16 BlendIn16(u16 a, u16 b, u32 eva, u32 evb)
{
    u32 pa = (a & 0x7C1Fu) | ((u32)(a & 0x03E0u) << 16);
    u32 pb = (b & 0x7C1Fu) | ((u32)(b & 0x03E0u) << 16);
    u32 sum = (pa * eva + pb * evb) >> 4;

    return (u16)((sum & 0x7C1Fu) | ((sum >> 16) & 0x03E0u));
}

static u16 Brighten(u16 c, int evy)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    r += ((31 - r) * evy) >> 4;
    g += ((31 - g) * evy) >> 4;
    b += ((31 - b) * evy) >> 4;
    return (u16)(r | (g << 5) | (b << 10));
}

static u16 Darken(u16 c, int evy)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    r -= (r * evy) >> 4;
    g -= (g * evy) >> 4;
    b -= (b * evy) >> 4;
    return (u16)(r | (g << 5) | (b << 10));
}

static BOOL InRange(int v, int lo, int hi)
{
    /* hi is exclusive; lo > hi wraps around the screen edge */
    return lo <= hi ? (v >= lo && v < hi) : (v >= lo || v < hi);
}

/* Set enable[x] = value for the x in a window's horizontal range (the DS's
 * x, at origin on the canvas; beyond the DS's screen is outside). */
/* The DS's windows are rectangles of its screen, x lo to hi (hi = 0: its
 * right edge). On a wider canvas (origin > 0, width > 256) a window at
 * the DS's left or right edge goes on to the canvas's (the intro's
 * letterbox, a window over the whole screen: the bands beside the DS's
 * picture are in it too). */
static void FillRange(u8 *enable, int lo, int hi, u8 value, int origin, int width, BOOL toEdge)
{
    /* An empty window (lo = hi: the circle wipes' lines past the circle are
     * 0-0) covers nothing, the bands beside the DS's picture included: a
     * window from x 0 reaches the left edge only when it covers x 0. (The
     * Town Map's circle closing left its blue side bars showing in the
     * bands, 2026-10-24.) */
    if (lo == hi) {
        return;
    }
    /* (the wider canvas, or toEdge: NitroRender2DSplit.windowsToEdge) A
     * window a pixel short of an edge is at it: games cover the screen
     * with 0-255 (the right end is exclusive, and 256 doesn't fit in the
     * register), leaving the DS's last column out. On the wider screen
     * that column and the band beyond it went black (the intro's city); on
     * the remaster's lower screen, drawn 1.25 times bigger, a black strip
     * down its right edge (the intro's girl running, 2026-10-21). The DS's
     * own picture keeps the DS's column. */
    if ((toEdge || origin > 0) && lo < hi) {
        if (hi == 255) {
            hi = W;
        }
        if (lo == 1) {
            lo = 0;
        }
    }
    if (origin > 0 && lo < hi) {
        memset(enable + origin + lo, value, (size_t)(hi - lo));
        if (lo == 0) {
            memset(enable, value, (size_t)origin);
        }
        if (hi == W && origin + W < width) {
            memset(enable + origin + W, value, (size_t)(width - origin - W));
        }
        return;
    }
    if (lo <= hi) {
        if (hi > lo) {
            memset(enable + origin + lo, value, (size_t)(hi - lo));
        }
        if (lo == 0 && origin > 0) {
            memset(enable, value, (size_t)origin);
        }
        if (hi == 0 && origin + W < width) {
            memset(enable + origin + W, value, (size_t)(width - origin - W));
        }
    } else {
        memset(enable + origin + lo, value, (size_t)(W - lo));   /* lo <= 255 */
        memset(enable + origin, value, (size_t)hi);
        if (origin + W < width) {
            memset(enable + origin + W, value, (size_t)(width - origin - W));
        }
        if (hi == 0 && origin > 0) {
            memset(enable, value, (size_t)origin);
        }
    }
}

/* A circle fade's window on one line (dsY: the DS's line; the lines of a
 * tall field above and below the DS's count on), across the whole canvas:
 * the half chord as the game works it out (the square root of r^2 less
 * the line's distance squared, rounded down), from x - half to x + half.
 * (The game's own windows per line stop at the DS's columns and lines:
 * walking into Eterna Forest the circle was cut flat at the DS's sides,
 * a band of the lines it covered whole reached the screen's edges, and
 * the lines above and below the DS's had its first and last line's.) */
static void CircleRange(u8 *enable, const WindowCircle *c, int dsY, u8 value, int origin, int width)
{
    int dy = dsY - c->cy, lo, hi;
    long rr = (long)c->r * c->r, q;
    int half;

    if (dy < 0) {
        dy = -dy;
    }
    if (dy >= c->r) {
        return;
    }
    q = rr - (long)dy * dy;
    {
        /* (the integer square root, rounded down) */
        int lo2 = 0, hi2 = c->r;

        while (lo2 < hi2) {
            int mid = (lo2 + hi2 + 1) / 2;

            if ((long)mid * mid <= q) {
                lo2 = mid;
            } else {
                hi2 = mid - 1;
            }
        }
        half = lo2;
    }
    lo = origin + c->cx - half;
    hi = origin + c->cx + half;
    if (lo < 0) {
        lo = 0;
    }
    if (hi > width) {
        hi = width;
    }
    if (lo < hi) {
        memset(enable + lo, value, (size_t)(hi - lo));
    }
}

/* The layers of a line, front to back: the OBJs of priority 0, the BGs of
 * priority 0 (by number), the OBJs of priority 1, and so on. */
#define SLOT_OBJ 4

/* What drawing one engine works with, line by line. */
typedef struct EngineWork {
    u16 lineOf[4][WMAX];  /* each BG's own line, when it isn't drawn into the screen's */
    ObjLine objLine;
    ObjFrame objFrame;
    u8 enableLine[WMAX];
    u8 layer1[WMAX];      /* each pixel's front layer (0-3 BG, 4 OBJ), for the effects */
    u8 alpha3d[WMAX];     /* BG0's 3D alpha (0-31) on this line, when scrolled */
    u16 picLine[WMAX];    /* the pictures drawn with a BG on this line (PicturesOnLine) */
    u8 lineIo[0x2000];    /* (an H-blank effect) the registers as they are on the line being drawn */
} EngineWork;

static EngineWork sWork[2];

/* A layer of the line being drawn, in the order the layers are in front of
 * each other: its own line (NULL: not drawn yet) and, for the OBJ layer,
 * the priority its pixels must have to be in this place. */
typedef struct LineLayer {
    const u16 *px;
    const u8 *prioOf;
    short x0, x1;
    u8 layer, p;
} LineLayer;

/* A BG's line in its own buffer (the 3D: the image's own line, unless
 * scrolled), and for the 3D its alpha. */
static const u16 *LayerLine(const Engine *e, EngineWork *work, int bg, int kind, u16 bgcnt,
                            u32 dispcnt, int y, const u16 *gx3dPixels, const u8 *gx3dAlpha,
                            const u8 **alpha3d)
{
    u16 *line = work->lineOf[bg];
    int x;

    if (kind != BG_3D && TallLineOf(e, bg, y) < 0) {
        memset(line, 0, (size_t)e->width * sizeof(u16));   /* (a tall field: nothing past the DS's lines) */
        return line;
    }
    if (kind == BG_TEXT) {
        Target t;

        t.row = line;
        t.layerOf = NULL;
        t.enable = NULL;
        t.layer = (u8)bg;
        t.bit = (u8)(1 << bg);
        t.filled = 0;
        DrawTextBg(e, bg, bgcnt, dispcnt, y, &t, T_PLAIN);
    } else if (kind == BG_3D) {
        int scroll = Rd16(e->io, 0x10) & 0x1FF;

        if (scroll == 0 && gx3dPixels != NULL && gx3dAlpha != NULL) {
            /* The image's own line, no copy. */
            *alpha3d = gx3dAlpha + y * W;
            return gx3dPixels + y * W;
        }
        for (x = 0; x < W; ++x) {
            int sx = (x + scroll) & 0x1FF;

            line[x] = (sx < W && gx3dPixels != NULL) ? gx3dPixels[y * W + sx] : 0;
            work->alpha3d[x] = (sx < W && gx3dAlpha != NULL) ? gx3dAlpha[y * W + sx] : 31;
        }
        *alpha3d = work->alpha3d;
    } else {
        DrawAffineBg(e, bg, kind, bgcnt, dispcnt, y, line);
    }
    if ((e->picBgs >> bg) & 1) {
        int x0, x1;

        (void)PicturesOnLine(e, bg, y, line, FALSE, &x0, &x1);
    }
    return line;
}

/* What a line that may blend two layers needs (DrawEngine's, for
 * BlendLine). */
typedef struct BlendLineState {
    const Engine *e;
    EngineWork *work;
    const int *kind;
    const u16 *bgcnt;
    u32 dispcnt;
    int y;
    const u16 *gx3dPixels;
    const u8 *gx3dAlpha;
    const u8 **alpha3d;
    LineLayer *list;
    int nlist;
    const s8 *entryOf;
    const ObjLine *objLine;
    u32 special;
    u32 bldcnt;
    u16 backdrop;
    int effect, eva, evb, evy;
    /* (split) each pixel's alpha over the 3D, 0-16: see DrawEngine. */
    u8 *alphaOut;
    int width;
} BlendLineState;

/* Each pixel of a line whose front layers are drawn (row, layer1): one that
 * blends with what's behind it (a first target of alpha blending, a partly
 * transparent 3D pixel, a semi-transparent OBJ) takes the next layer that
 * covers it, or the backdrop. windows: some window is on (enableLine says
 * which layers and effects each pixel shows); without, all of them. */
static inline __attribute__((always_inline)) void BlendLineT(const BlendLineState *b, u16 *row,
                                                             const u8 *enableLine, const u8 *layer1,
                                                             int windows, int split)
{
    u8 *alphaOut = b->alphaOut;
    const ObjLine *objLine = b->objLine;
    const int nlist = b->nlist;
    const u32 special = b->special;
    const u32 bldcnt = b->bldcnt;
    const u32 seconds = (bldcnt >> 8) & 0x3F;   /* the second targets, by layer */
    /* The first targets of alpha blending (when the windows let them). */
    const u32 firsts = b->effect == 1 ? bldcnt & 0x3F : 0;
    const u16 backdrop = b->backdrop;
    const int effect = b->effect;
    const u32 eva = (u32)b->eva, evb = (u32)b->evb;
    const int evy = b->evy;
    const BOOL in16 = eva + evb <= 16;   /* alpha blending can't go over 31 */
    const BOOL has3d = b->kind[0] == BG_3D;
    const u8 *alpha3dLine = *b->alpha3d;
    /* The line's layers, copied where the compiler can keep them: the
     * pixels written don't change them (as far as it can tell). */
    const u16 *px[8];
    const u8 *prioOf[8];
    int x0[8], x1[8];
    u8 layerOf[8], prio[8], entryOf[8];
    int x, k;

    for (k = 0; k < nlist; k++) {
        px[k] = b->list[k].px;
        prioOf[k] = b->list[k].prioOf;
        x0[k] = b->list[k].x0;
        x1[k] = b->list[k].x1;
        layerOf[k] = b->list[k].layer;
        prio[k] = b->list[k].p;
    }
    memcpy(entryOf, b->entryOf, sizeof(entryOf));
    for (x = 0; x < b->width; x++) {
        u32 c = row[x];
        u32 enable = windows ? enableLine[x] : 0x3F;
        int top = 5, second = 5;          /* 5: the backdrop */
        u16 topColor = backdrop, secondColor = backdrop;
        BOOL alpha3d = FALSE, objAlpha = FALSE;

        if (split) {
            if (!(c & OPAQUE)) {
                continue;   /* (the 3D, or what's behind it) */
            }
            alphaOut[x] = 16;
        }
        if (c & OPAQUE) {
            top = layer1[x];
            topColor = (u16)(c & 0x7FFF);
            if (!((special >> top) & 1)) {
                /* (most pixels) shown as they are */
                row[x] = split ? (u16)c : topColor;
                continue;
            }
            alpha3d = top == 0 && has3d && alpha3dLine[x] < 31;
            objAlpha = top == SLOT_OBJ && (objLine->semi[x] || objLine->alpha[x] < 16);
            if (alpha3d || objAlpha || (((firsts >> top) & 1) && (enable & 0x20))) {
                /* The layers behind the front one, in order. */
                for (k = entryOf[top == SLOT_OBJ ? 4 + objLine->prio[x] : top] + 1; k < nlist; k++) {
                    u16 lc;

                    if (x < x0[k] || x >= x1[k] || (windows && !((enable >> layerOf[k]) & 1))) {
                        continue;
                    }
                    if (px[k] == NULL) {
                        px[k] = b->list[k].px =
                            LayerLine(b->e, b->work, layerOf[k], b->kind[layerOf[k]],
                                      b->bgcnt[layerOf[k]], b->dispcnt, b->y, b->gx3dPixels,
                                      b->gx3dAlpha, b->alpha3d);
                    }
                    lc = px[k][x];
                    if (!(lc & OPAQUE) || (prioOf[k] != NULL && prioOf[k][x] != prio[k])) {
                        continue;
                    }
                    second = layerOf[k];
                    secondColor = lc & 0x7FFF;
                    break;
                }
            }
        }
        if (split && second == 5) {
            /* (the layers in front of the 3D) nothing in front of the 3D
             * behind this pixel: the 3D is, and BLDCNT's second-target
             * bit for BG0 says whether the pixel blends with it. Blended
             * by the compositor, by the alpha noted here. */
            if (objAlpha && (seconds & 1)) {
                u32 a = objLine->alpha[x];

                alphaOut[x] = (u8)(a < 16 ? a : eva);
                c = topColor;
            } else if ((enable & 0x20) && (bldcnt & (1u << top))) {
                if (effect == 1 && (seconds & 1)) {
                    alphaOut[x] = (u8)eva;
                    c = topColor;
                } else if (effect == 2) {
                    c = Brighten(topColor, evy);
                } else if (effect == 3) {
                    c = Darken(topColor, evy);
                } else {
                    c = topColor;
                }
            } else {
                c = topColor;
            }
            row[x] = (u16)(c | OPAQUE);
            continue;
        }
        if (alpha3d && ((seconds >> second) & 1)) {
            /* A partly transparent 3D pixel blends with what's below by
             * its own alpha, whatever the effect setting. */
            u32 a = ((u32)alpha3dLine[x] * 16 + 15) / 31;

            c = BlendIn16(topColor, secondColor, a, 16 - a);
        } else if (objAlpha && ((seconds >> second) & 1)) {
            /* Semi-transparent and bitmap OBJs blend with what's below,
             * whatever the effect setting. */
            u32 a = objLine->alpha[x];

            if (a < 16) {
                c = BlendIn16(topColor, secondColor, a, 16 - a);
            } else {
                c = in16 ? BlendIn16(topColor, secondColor, eva, evb)
                         : Blend(topColor, secondColor, (int)eva, (int)evb);
            }
        } else if ((enable & 0x20) && (bldcnt & (1u << top))) {
            if (effect == 1 && ((seconds >> second) & 1)) {
                c = in16 ? BlendIn16(topColor, secondColor, eva, evb)
                         : Blend(topColor, secondColor, (int)eva, (int)evb);
            } else if (effect == 2) {
                c = Brighten(topColor, evy);
            } else if (effect == 3) {
                c = Darken(topColor, evy);
            } else {
                c = topColor;
            }
        } else {
            c = topColor;
        }
        row[x] = (u16)(split ? c | OPAQUE : c);
    }
}

static void BlendLine(const BlendLineState *b, u16 *row, const u8 *enableLine, const u8 *layer1,
                      BOOL windows)
{
    if (windows) {
        BlendLineT(b, row, enableLine, layer1, 1, 0);
    } else {
        BlendLineT(b, row, enableLine, layer1, 0, 0);
    }
}

/* (split) The layers in front of the 3D: each drawn pixel keeps its OPAQUE
 * bit, the others stay 0, and b->alphaOut gets each drawn pixel's alpha
 * over the 3D (16: opaque). The line's layers (b->list) are those in
 * front of the 3D only. */
static void BlendLineOver(const BlendLineState *b, u16 *row, const u8 *enableLine, const u8 *layer1,
                          BOOL windows)
{
    if (windows) {
        BlendLineT(b, row, enableLine, layer1, 1, 1);
    } else {
        BlendLineT(b, row, enableLine, layer1, 0, 1);
    }
}

/* BG0 acquires the newest finished image on both platforms. PC fills it
 * with the Stage 1 software rasterizer, 3DS with the PICA off-screen worker.
 * A held image is never overwritten by its producer during DrawEngine. */

/* (split) The layers in front of the 3D, from the line as drawn so far
 * (row, with each pixel's layer in layer1): into split->over and
 * split->overAlpha, blended among themselves or marked to blend with the
 * 3D (BlendLineOver), or brightened or darkened. */
static void SnapshotOver(NitroRender2DSplit *split, BlendLineState *blendState, const u16 *row,
                         int y, int nlist, BOOL blend2, int flags, int effect, int evy,
                         u16 bldcnt, const u8 *enableLine, const u8 *layer1, int drawn)
{
    int w = blendState->width;
    u16 *over = split->over + y * w;
    u8 *alpha = split->overAlpha + y * w;
    int x;

    /* Nothing of the line is in front of the 3D: the whole over row is
     * empty, so it needn't be copied (nor its alpha filled in: a pixel's
     * alpha is only read where its over pixel is OPAQUE). nlist alone
     * doesn't say this, as the layers aren't noted when they don't
     * blend 2. */
    if (drawn == 0) {
        memset(over, 0, (size_t)w * sizeof(u16));
        return;
    }
    memcpy(over, row, (size_t)w * sizeof(u16));
    if (blend2) {
        blendState->y = y;
        blendState->nlist = nlist;
        blendState->alphaOut = alpha;
        BlendLineOver(blendState, over, enableLine, layer1, (flags & T_WINDOW) != 0);
        return;
    }
    /* Slice S3: without an effect that can fire on this row there is
     * nothing to blend or brighten — the alpha is uniformly 16. The loop
     * below needs effect >= 2 AND a first-target bit set; the two spellings
     * agree exactly, so this is provably the same pixels. */
    if (effect < 2 || (bldcnt & 0x1Fu) == 0) {
        memset(alpha, 16, (size_t)w);
        return;
    }
    /* Slice S3: without an effect that can fire on this row there is
     * nothing to blend or brighten — the alpha is uniformly 16. The loop
     * below is kept for effect >= 2 with first-target bits set; both spell
     * it `effect >= 2`, so the two agree exactly. */
    if (effect < 2 || (bldcnt & 0x1Fu) == 0) {
        memset(alpha, 16, (size_t)w);
        return;
    }
    for (x = 0; x < w; x++) {
        u16 c = over[x];

        alpha[x] = 16;
        if ((c & OPAQUE) && effect >= 2 && (enableLine[x] & 0x20) && (bldcnt & (1 << layer1[x]))) {
            over[x] = (u16)((effect == 2 ? Brighten(c & 0x7FFF, evy) : Darken(c & 0x7FFF, evy)) | OPAQUE);
        }
    }
}

/* Drawn in two layers for a compositor that puts the 3D in between
 * (split != NULL): out gets what's behind the 3D (the picture without BG0),
 * split->over the layers in front of it (OPAQUE where they cover it, 0
 * elsewhere) with each pixel's alpha over the 3D in split->overAlpha. Where
 * a window hides the 3D, over shows what's behind it. Brightness effects
 * on the 3D itself and the master brightness are left to the compositor
 * (split->tint3d, split->masterBright). */
static inline __attribute__((always_inline)) void DrawEngineT(Engine *e, u16 *out, NitroRender2DSplit *split,
                                                              int fixedWidth, int y0, int y1)
{
    /* Each engine has its own (they may be drawn at the same time, on two
     * CPU cores). */
    EngineWork *work = &sWork[e->isA ? 0 : 1];
    ObjLine *objLine = &work->objLine;
    ObjFrame *objFrame = &work->objFrame;
    u8 *enableLine = work->enableLine;
    u8 *layer1 = work->layer1;
    LineLayer list[8];   /* the line's layers, front to back */
    int nlist;
    s8 entryOf[5 + 3];   /* (blend2) each BG's place in the list; the OBJs' at 4 + priority */
    const u8 *io = e->io;
    u32 dispcnt = Rd32(io, 0);
    u16 bgcnt[4];
    int kind[4];
    const u16 *gx3dPixels = NULL;
    const u8 *gx3dAlpha = NULL;
    const u8 *alpha3d = work->alpha3d;   /* BG0's 3D alpha on the line being drawn */
    int gx3dTranslucent = 0;   /* the 3D image has pixels of alpha 1-30 */
    u16 bldcnt = Rd16(io, 0x50);
    u16 bldalpha = Rd16(io, 0x52);
    int eva = bldalpha & 31, evb = (bldalpha >> 8) & 31;
    int evy = Rd16(io, 0x54) & 31;
    int effect = (bldcnt >> 6) & 3;
    u16 win0h = Rd16(io, 0x40), win1h = Rd16(io, 0x42);
    u16 win0v = Rd16(io, 0x44), win1v = Rd16(io, 0x46);
    u16 winin = Rd16(io, 0x48), winout = Rd16(io, 0x4A);
    BOOL anyWindow = (dispcnt & 0xE000) != 0;
    u16 backdrop = PlttColor(e->bgPltt, 0);
    BOOL blend2;   /* some pixels may blend two layers */
    u32 special;   /* (blend2) the layers whose pixels may not just show as they are */
    int flags;     /* how layers go into the line, when only the front one matters */
    BlendLineState blendState;
    /* (the DS's own width known to the compiler: its loops as before) */
    const int width = fixedWidth ? fixedWidth : e->width;
    BOOL snapped;  /* (split) this line's layers in front of the 3D are noted */
    int y, x, bg, p;
    /* Slice A: this engine's profile accumulators. */
    int pe = e->isA ? 0 : 1;
    NitroRender2DProfile *prof = &sProfile[pe];
    u64 frameKey[4];
    u32 emptyText = 0;
    u64 tSetup = R2D_NOW(), tPart;
    u64 setupPal = 0, setupKey = 0;
#ifndef HGSS_PROFILE_OFF
    u64 setupTransparent = 0, setupDecide = 0;  /* P1: finer setup split */
#endif

    if (eva > 16) {
        eva = 16;
    }
    if (evb > 16) {
        evb = 16;
    }
    if (evy > 16) {
        evy = 16;
    }
    tPart = R2D_NOW();
    FindExtPalettes(e);
    MakePalettes(e);
    setupPal = R2D_NOW() - tPart;
    tPart = R2D_NOW();
    for (bg = 0; bg < 4; bg++) {
        bgcnt[bg] = Rd16(io, 8 + bg * 2);
        kind[bg] = (dispcnt & (0x100u << bg)) ? BgKind(e, bg, dispcnt) : BG_NONE;
        if (kind[bg] == BG_LARGE) {
            kind[bg] = BG_NONE;   /* large-bitmap mode still unsupported */
        }
        /* Slice S2a: this frame's scroll-invariant key per BG, computed
         * once and shared by LookUpArt, the gpuBg path and the row cache
         * (one BgKey walk instead of three). */
        frameKey[bg] = (kind[bg] == BG_NONE || kind[bg] == BG_3D)
                           ? 0
                           : BgKeyCached(e, bg, kind[bg], bgcnt[bg], dispcnt);
        /* Slice B: this frame's row key per text BG (the frame key plus
         * the wide-clear palette variant). */
        sTextKey[pe][bg] = kind[bg] == BG_TEXT ? FoldClearVariant(e, bg, frameKey[bg]) : 0;
    }
    prof->setupTicks += R2D_NOW() - tSetup;
    prof->setupPalTicks += setupPal;
    prof->setupKeyTicks += R2D_NOW() - tPart;
    if (e->isA && kind[0] == BG_3D && split == NULL) {
        e->height = H;   /* (a tall picture with the 3D: only in two layers, the 3D on the DS's lines) */
    }
    if (fixedWidth == 0) {
        if (e->top == 0) {
            /* (a tall field: no pictures split, no hand-drawn ones: those
             * are the DS's lines' at the wider screen's 320) */
            tPart = R2D_NOW();
            DecideSplits(e, kind, bgcnt, dispcnt);
            LookUpArt(e, kind, bgcnt, dispcnt, frameKey);
#ifndef HGSS_PROFILE_OFF
            setupDecide += R2D_NOW() - tPart;
#endif
        }
        if (e->reusePixels && split != NULL && split->gpuBgs && e->isA &&
            (dispcnt & 0x108) == 0x108 && !(dispcnt & 0xE000) && e->lines == NULL &&
            e->height == H && e->top == 0 && !e->circles[0].on && !e->circles[1].on) {
            tPart = R2D_NOW();
            for (bg = 1; bg < 4; bg++) {
                if (kind[bg] == BG_TEXT && e->art[bg] == NULL && !e->split[bg] &&
                    !((e->partBgs | e->picBgs) & (1u << bg)) &&
                    TextBgTransparent(e, bgcnt[bg], dispcnt)) {
                    emptyText |= 1u << bg;
                }
            }
#ifndef HGSS_PROFILE_OFF
            setupTransparent += R2D_NOW() - tPart;
#endif
        }
        tPart = R2D_NOW();
        DecideBandFills(e, kind, bgcnt, dispcnt, emptyText);
#ifndef HGSS_PROFILE_OFF
        setupDecide += R2D_NOW() - tPart;
#endif
        DecideTallEdges(e, kind, bgcnt, dispcnt);
        DecideTallWindows(e, kind);
#ifndef HGSS_PROFILE_OFF
        /* P1: alongside the pal/key split (TallEdges/Windows stay in the
         * setup residual). */
        prof->setupTransparentTicks += setupTransparent;
        prof->setupDecideTicks += setupDecide;
#endif
    }
    /* S5/S6 bundle take stash (function scope: takes are evaluated in the
     * split block, committed after SortObjs). Per taken BG: slot index =
     * position here; blend/behind/scroll ride into the emitted quads.
     * frameOk gates both BG and OBJ takes (H-blank windows/scrolls and
     * no-3D-mode reject everything, counted once). */
    int nBgTake = 0;
    u32 committedText = 0;
    int takeBlend[NITRO_GPU_BG_SLOTS], takeBehind[NITRO_GPU_BG_SLOTS], takeBands[NITRO_GPU_BG_SLOTS];
    int takeHofs[NITRO_GPU_BG_SLOTS], takeVofs[NITRO_GPU_BG_SLOTS];
#ifndef HGSS_PROFILE_OFF
    u64 tTake = 0;  /* P1: take/commit block start (assigned on the split path) */
#endif
    if (split != NULL) {
        /* The 3D header before frameOk reads it: Render zeroes the split
         * ahead of `over` on every call, so frameOk must see this call's
         * values, not the previous call's (log-20's all-dsp was a zeroed
         * has3d failing the gate while dispcnt itself passed). */
        split->has3d = e->isA && kind[0] == BG_3D;
        split->prio3d = bgcnt[0] & 3;
        split->scroll3d = split->has3d ? (Rd16(io, 0x10) - e->shift3d) & 0x1FF : 0;
        split->tint3d = (split->has3d && effect >= 1 && (bldcnt & 1)) ? effect : 0;
        split->evy = evy;
        split->eva = eva;
        split->evb = evb;
        split->blend3d = (bldcnt >> 8) & 0x3F;
        split->backdrop = backdrop;
    }
    int frameOk = split != NULL && split->has3d && split->gpuBgs && e->lines == NULL && !split->stretch &&
                  !(dispcnt & 0xE000) && (dispcnt & 0x108) == 0x108 && ((dispcnt >> 16) & 3) == 1;
    if (split != NULL) {
        /* S5/S6 bundle: quads and slots start empty every frame; takes
         * below fill them. (A stale quad drawn over a menu would be a
         * wrong picture, not just a slow one.) */
#ifndef HGSS_PROFILE_OFF
        tTake = R2D_NOW();
#endif
        split->nGpuQuads = 0;
        split->gpuQuadsBehind = 0;
        for (int s = 0; s < NITRO_GPU_BG_SLOTS; s++) {
            split->gpuBgSlotBg[s] = -1;
        }
        split->gpuObjAtlasKey = 0;
        if (split->has3d) {
            /* Drawn without BG0: the layers in front of it are noted as
             * the line reaches it. */
            kind[0] = BG_NONE;
        }
        /* A BG the compositor draws itself (NitroRender2DSplit.gpuBgs):
         * left out, with what it needs to draw it. */
        /* S5/S6 bundle: take every eligible BG (up to the slot pool) for
         * compositor quads, front or flat-behind. Slot arrays carry what
         * the shell needs (picture per slot, made again when its key
         * changes); blend/behind/scroll ride the function-scope take
         * stash into the emitted quads at commit (after SortObjs and the
         * region rule). Untaken BGs draw in software exactly as before. */
        if (split->has3d && split->gpuBgs) {
            int b;

            if (!frameOk) {
                /* Slice S5: the frame gate failed with 3D + nominations live:
                 * exactly one of stretch, per-line H-blank regs, windows,
                 * BG0 undisplayed, or DISPCNT display mode. Split so the log
                 * says which (in frameOk's own check order). The dispcnt
                 * value is logged once: it decides the next fix. */
                int reason = split->stretch ? NITRO_GPUBG_REJECT_STRETCH
                             : e->lines != NULL ? NITRO_GPUBG_REJECT_HBLANK
                             : (dispcnt & 0xE000) ? NITRO_GPUBG_REJECT_WIN
                             : ((dispcnt & 0x108) != 0x108) ? NITRO_GPUBG_REJECT_BG0
                                                            : NITRO_GPUBG_REJECT_DISP;

                prof->gpuBgReject[reason]++;
                if (reason == NITRO_GPUBG_REJECT_DISP && !sGpuGateDspLogged) {
                    sGpuGateDspLogged = 1;
                    NitroPort_Log("[2d] gate dsp: dispcnt=%08x powcnt=%04x isA=%d snapshot=%d gpuBgs=%x",
                                  dispcnt, (unsigned)Rd16(e->ioAll, 0x304),
                                  e->isA ? 1 : 0, e->taken != NULL ? 1 : 0,
                                  (unsigned)split->gpuBgs);
                }
            } else for (b = 1; b < 4 && nBgTake < NITRO_GPU_BG_SLOTS; b++) {
                int bgBlend = 1, behind = 0, gb;
                u64 key;
                u32 i;

                if (!((split->gpuBgs >> b) & 1) || !(dispcnt & (0x100u << b))) {
                    continue;
                }
                if (kind[b] != BG_TEXT && kind[b] != BG_AFFINE) {
                    prof->gpuBgReject[NITRO_GPUBG_REJECT_MODE]++;
                    if (!sGpuGateKindLogged) {
                        sGpuGateKindLogged = 1;
                        NitroPort_Log("[2d] gate kind: bg=%d kind=%d bgcnt=%04x dispcnt=%08x",
                                      b, kind[b], (unsigned)Rd16(io, 8 + b * 2), dispcnt);
                    }
                    continue;
                }
                if (kind[b] == BG_TEXT && (e->art[b] != NULL || ((e->partBgs >> b) & 1) || e->split[b])) {
                    /* The software wouldn't decode plain tiles here (wide
                     * art, splits, part-shifted runs): a full-map quad can't
                     * reproduce those. clip only widens the canvas with the
                     * same scroll (DecideBandFills) and anchorWindows only
                     * shifts enabled windows (frameOk already vetoed those,
                     * so EngineWindows yields none): both are quad-exact. */
                    prof->gpuBgReject[NITRO_GPUBG_REJECT_MODE]++;
                    continue;
                }
                if (kind[b] == BG_AFFINE) {
                    gb = GpuBgAffineBg(io, e->oam, b, dispcnt, &bgBlend, &behind);
                } else {
                    gb = GpuBgValidBg(io, e->oam, 0, b, &bgBlend, &behind);
                }
                if (gb < 0) {
                    /* Slice S5: why it stays on software (-(1 + reason)). */
                    prof->gpuBgReject[-1 - gb]++;
                    continue;
                }
                if (kind[gb] != BG_TEXT && kind[gb] != BG_AFFINE) {
                    /* The validator's mode test passed but BgKind
                     * disagrees: same bucket as MODE, never silent. */
                    prof->gpuBgReject[NITRO_GPUBG_REJECT_MODE]++;
                    continue;
                }
                int bands = kind[gb] == BG_TEXT && !behind && e->clip[gb] && e->width > W;
                if (bands) {
                    int runs = 0;
                    /* Bound extra draw calls as well as the split array:
                     * the compositor has one finite vertex budget shared
                     * by both eyes and the rest of the screen. */
                    if (bgBlend || e->top != 0 || e->height != H || !e->bandFills[gb] ||
                        e->origin < 0 || e->origin + W > e->width) {
                        prof->gpuBgReject[NITRO_GPUBG_REJECT_MODE]++;
                        continue;
                    }
                    for (int side = 0; side < 2; side++) {
                        u16 prev = 0;
                        for (int line = 0; line < H; line++) {
                            u16 c = e->bandFill[gb][side][line];
                            if (c && c != prev) runs++;
                            prev = c;
                        }
                    }
                    if (runs > 16) {
                        prof->gpuBgReject[NITRO_GPUBG_REJECT_MODE]++;
                        continue;
                    }
                }
                /* Slice S2a: the frame's key (computed once in setup). */
                key = frameKey[gb] != 0 ? frameKey[gb] : BgKeyCached(e, gb, kind[gb], bgcnt[gb], dispcnt);
                for (i = 0; i < 0x200; i += 4) {
                    key = (key ^ Rd32(e->bgPltt, i)) * 0x100000001B3ull;
                }
                split->gpuBgSlotBg[nBgTake] = gb;
                split->gpuBgKey[nBgTake] = key;
                if (kind[gb] == BG_AFFINE) {
                    split->gpuBgW[nBgTake] = sAffineSizes[(bgcnt[gb] >> 14) & 3][0];
                    split->gpuBgH[nBgTake] = sAffineSizes[(bgcnt[gb] >> 14) & 3][1];
                    /* Affine overflow off clamps to the map's edge
                     * (transparent outside); on wraps like text. */
                    split->gpuBgWrap[nBgTake] = (bgcnt[gb] & 0x2000) ?
                        NITRO_GPU_WRAP_REPEAT : NITRO_GPU_WRAP_CLAMP;
                } else {
                    split->gpuBgW[nBgTake] = sTextSizes[(bgcnt[gb] >> 14) & 3][0];
                    split->gpuBgH[nBgTake] = sTextSizes[(bgcnt[gb] >> 14) & 3][1];
                    split->gpuBgWrap[nBgTake] = NITRO_GPU_WRAP_REPEAT;
                }
                takeBlend[nBgTake] = bgBlend;
                takeBehind[nBgTake] = behind;
                takeBands[nBgTake] = bands;
                takeHofs[nBgTake] = Rd16(io, 0x10 + gb * 4) & 0x1FF;
                takeVofs[nBgTake] = Rd16(io, 0x12 + gb * 4) & 0x1FF;
                nBgTake++;
            }
        }
    } else if (e->isA && kind[0] == BG_3D) {
        gx3dPixels = NitroPort_Gx3dAcquireImage();
        gx3dAlpha = gx3dPixels != NULL ? NitroPort_Gx3dImageAlpha(gx3dPixels) : NULL;
        gx3dTranslucent = gx3dAlpha != NULL && NitroPort_Gx3dImageTranslucent(gx3dPixels);
    }
    objFrame->blends = 0;
    objFrame->artCount = 0;
    memset(objFrame->taken, 0, sizeof(objFrame->taken));
    objFrame->nTaken = 0;
    if (dispcnt & 0x1000) {
        u64 t0 = R2D_NOW();

        SortObjs(e, objFrame);
        prof->objTicks += R2D_NOW() - t0;
        if (fixedWidth == 0) {
            LookUpObjArt(e, objFrame, dispcnt, -1);
        }
    } else {
        /* ObjFrame persists between scenes. Without OBJ display SortObjs
         * doesn't run: discard its old entries before GPU region/atlas
         * selection and empty-picture admission consult the current count.
         * OAM need not be cleared when the game disables OBJ rendering. */
        objFrame->count = 0;
    }
    /* S5/S6 bundle commit: OBJ takes, the region rule, kind masking and
     * quad emission. Runs when takes are possible; the region rule decides
     * what actually draws as quads, so a frame with nothing taken draws in
     * software exactly as before. */
    if (frameOk && e->isA) {
        /* (taken OBJs: OAM index order; cells assigned in that order) */
        int takeObj[128], takeCell[128], nObjTake = 0;
        int frontClean = 1, behindClean = 1;
        int p, s, k;

        if (e->squeeze) {
            /* (the whole sprite field drawn narrower: software) */
            for (k = 0; k < objFrame->count; k++) {
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_SQUEEZE]++;
            }
            frontClean = behindClean = 0;
        } else for (k = 0; k < objFrame->count; k++) {
            const Obj *o = &objFrame->obj[k];

            if (o->mode == 2) {
                continue;   /* (window OBJs draw no pixels) */
            }
            if (o->mode == 1) {
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_SEMI]++;
                if (o->prio <= split->prio3d) frontClean = 0; else behindClean = 0;
                continue;
            }
            if (o->mode == 3) {
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_BITMAP]++;
                if (o->prio <= split->prio3d) frontClean = 0; else behindClean = 0;
                continue;
            }
            if (o->affine) {
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_AFFINE]++;
                if (o->prio <= split->prio3d) frontClean = 0; else behindClean = 0;
                continue;
            }
            if (e->objsMoved) {
                /* (the wide layout moved some OBJs: quads use OAM positions) */
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_MOVED]++;
                if (o->prio <= split->prio3d) frontClean = 0; else behindClean = 0;
                continue;
            }
            if (((bldcnt >> 6) & 3) != 0 && (bldcnt & 0x10)) {
                /* (BLDCNT touches OBJs: brightness, or alpha blending
                 * with them as first targets) */
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_BLEND]++;
                if (o->prio <= split->prio3d) frontClean = 0; else behindClean = 0;
                continue;
            }
            if (nObjTake >= NITRO_GPU_ATLAS_CELLS) {
                prof->gpuObjReject[NITRO_GPUOBJ_REJECT_CAP]++;
                if (o->prio <= split->prio3d) frontClean = 0; else behindClean = 0;
                continue;
            }
            takeObj[nObjTake] = k;
            takeCell[nObjTake] = nObjTake;
            nObjTake++;
        }
        /* The region rule: every drawn layer of a region must be taken,
         * else the whole region stays software (mixed regions can't
         * interleave: software blends read taken layers wrong). Untaken
         * text/affine BGs and exotic OBJs veto their region. */
        {
            int b;

            for (b = 1; b < 4; b++) {
                if (!(dispcnt & (0x100u << b)) || kind[b] == BG_NONE || kind[b] == BG_3D) {
                    continue;
                }
                if (kind[b] != BG_TEXT && kind[b] != BG_AFFINE) {
                    if ((bgcnt[b] & 3) < split->prio3d) {
                        frontClean = 0;
                    } else {
                        behindClean = 0;
                    }
                    continue;
                }
                /* (a text/affine BG: taken above, or software below) */
                {
                    int taken = 0, s;

                    for (s = 0; s < nBgTake; s++) {
                        if (split->gpuBgSlotBg[s] == b) {
                            taken = 1;
                        }
                    }
                    if (!taken) {
                        if ((bgcnt[b] & 3) < split->prio3d) {
                            frontClean = 0;
                        } else {
                            behindClean = 0;
                        }
                    }
                }
            }
        }
        /* Commit or revert, per region: an all-quad region draws quads,
         * a mixed region draws everything in software. Behind commits
         * first (drawn first); front second. Untaken regions keep their
         * software layers (kind masks and taken marks only where a
         * region commits). */
        if ((frontClean || behindClean) && (nBgTake > 0 || nObjTake > 0)) {
            int s, k;
            u64 atlasKey = KEY_START;
            int c, u;
            /* Committed takes: taken layers in clean regions. */
            int commitBg[NITRO_GPU_BG_SLOTS], nCommitBg = 0;
            int commitObj[128], commitCell[128], nCommitObj = 0;

            for (s = 0; s < nBgTake; s++) {
                if ((takeBehind[s] && behindClean) || (!takeBehind[s] && frontClean)) {
                    commitBg[nCommitBg++] = s;
                }
            }
            for (k = 0; k < nObjTake; k++) {
                if ((objFrame->obj[takeObj[k]].prio > split->prio3d && behindClean) ||
                    (objFrame->obj[takeObj[k]].prio <= split->prio3d && frontClean)) {
                    commitObj[nCommitObj] = takeObj[k];
                    commitCell[nCommitObj] = takeCell[k];
                    nCommitObj++;
                }
            }
            /* The atlas key covers the committed OBJs in commit order,
             * their tile bytes and their palette colours: identical
             * content reuses the upload, anything else redraws it. */
            for (k = 0; k < nCommitObj; k++) {
                const Obj *o = &objFrame->obj[commitObj[k]];
                u32 used[8192 / 32];
                u32 size = o->bpp8 ? 64u : 32u;

                atlasKey = KeyAdd(KeyAdd(KeyAdd(KeyAdd(KeyAdd(atlasKey, (u32)o->tile),
                    (u32)o->pal), (u32)o->bpp8), (u32)o->w), (u32)o->h);
                memset(used, 0, sizeof(used));
                for (c = 0; c < (int)(o->w * o->h / 64); c++) {
                    int tx = (c * 8) % o->w, ty = (c * 8 / o->w) * 8;

                    u = (ObjCharAddr(o, dispcnt, tx, ty) & e->objMask) >> 5;
                    used[u >> 5] |= 1u << (u & 31);
                }
                for (u = 0; u < 8192u; u++) {
                    if (used[u >> 5] & (1u << (u & 31))) {
                        for (c = 0; c < (int)size; c += 4) {
                            atlasKey = (atlasKey ^ Rd32(e->objVram, (u * 32 + (u32)c) & e->objMask)) *
                                0x100000001b3ull;
                        }
                    }
                }
                if (o->bpp8) {
                    for (c = 0; c < 256; c++) {
                        atlasKey = (atlasKey ^ PlttColor(e->objPltt, (u32)c)) * 0x100000001b3ull;
                    }
                } else {
                    for (c = 0; c < 16; c++) {
                        atlasKey = (atlasKey ^ PlttColor(e->objPltt, ((u32)o->pal << 4) | (u32)c)) * 0x100000001b3ull;
                    }
                }
            }
            if (nCommitBg + nCommitObj > 0) {
                if (nCommitObj > 0) {
                atlasKey = atlasKey ? atlasKey : 1;
                if (atlasKey != sGpuObjAtlasKey) {
                    GpuObjAtlasFill(e, dispcnt, objFrame, commitObj, commitCell, nCommitObj);
                    sGpuObjAtlasKey = atlasKey;
                }
                }
            }
            split->gpuObjAtlasPicture = sGpuObjAtlas;
            split->gpuObjAtlasKey = nCommitObj > 0 ? atlasKey : 0;
            /* Mask committed BGs out of the software picture. */
            for (s = 0; s < nCommitBg; s++) {
                int b = split->gpuBgSlotBg[commitBg[s]];

                if (kind[b] == BG_TEXT) {
                    committedText |= 1u << b;
                }
                kind[split->gpuBgSlotBg[commitBg[s]]] = BG_NONE;
                prof->gpuBgTaken++;
            }
            /* Mark committed OBJs out of the software lines. */
            for (s = 0; s < nCommitObj; s++) {
                objFrame->taken[commitObj[s]] = 1;
                prof->gpuObjTaken++;
            }
            objFrame->nTaken = (u8)(nCommitObj > 0);
            /* Emit back-to-front within each region (painter's order):
             * behind region first, then front. Inside a pass: prio passes
             * high to low, BGs (slot order reversed = bg descending) then
             * OBJs (commit order reversed = OAM descending). */
            {
                int p, prio, si, k2;
                int nQuads = 0, nBehind = 0;

                for (p = 1; p >= 0; p--) {
                    int wantBehind = (p == 0);

                    for (prio = 3; prio >= 0; prio--) {
                        for (si = nCommitBg - 1; si >= 0; si--) {
                            int slot = commitBg[si];
                            int gb = (int)split->gpuBgSlotBg[slot];
                            NitroGpuQuad *q;

                            if (takeBehind[slot] != wantBehind) {
                                continue;
                            }
                            if ((int)(bgcnt[gb] & 3) != prio) {
                                continue;
                            }
                            q = &split->gpuQuads[nQuads++];
                            q->kind = 0;
                            q->blend = (u8)takeBlend[slot];
                            q->slot = (u8)slot;
                            q->eva = (u8)eva;
                            q->evb = (u8)evb;
                            q->wrap = (u8)split->gpuBgWrap[slot];
                            q->pad[0] = q->pad[1] = 0;
                            q->x0 = 0;
                            q->y0 = 0;
                            q->x1 = (u16)e->width;
                            q->y1 = (u16)(e->top > 0 && e->height > 192 ? e->height : 192);
                            if (takeBands[slot]) {
                                q->x0 = (u16)e->origin;
                                q->x1 = (u16)(e->origin + W);
                            }
                            /* UVs normalized to the texture below (map pixels
                             * times texels-per-pixel over 512). */
                            float texel = (split->gpuBgW[slot] == 256 && split->gpuBgH[slot] == 256) ? 2.0f : 1.0f;

                            if (kind[gb] == BG_AFFINE) {
                                u32 base = gb == 2 ? 0x20u : 0x30u;
                                s32 pa = (s16)Rd16(io, base + 0), pb = (s16)Rd16(io, base + 2);
                                s32 pc = (s16)Rd16(io, base + 4), pd = (s16)Rd16(io, base + 6);
                                s32 refX = (s32)(Rd32(io, base + 8) << 4) >> 4;
                                s32 refY = (s32)(Rd32(io, base + 12) << 4) >> 4;
                                float x0 = (float)e->origin, y0 = (float)e->top;
                                float x1 = x0 + (float)e->width, y1 = y0 + q->y1 - q->y0;

                                q->u0 = (float)(refX + pa * (s32)(x0 - e->origin) + pb * (s32)(y0 - e->top)) * texel / 512.0f;
                                q->v0 = (float)(refY + pc * (x0 - e->origin) + pd * (y0 - e->top)) * texel / 512.0f;
                                q->u1 = (float)(refX + pa * (s32)(x1 - e->origin) + pb * (s32)(y1 - e->top)) * texel / 512.0f;
                                q->v1 = (float)(refY + pc * (x1 - e->origin) + pd * (y1 - e->top)) * texel / 512.0f;
                                q->u2 = (float)(refX + pa * (s32)(x1 - e->origin) + pb * (s32)(y0 - e->top)) * texel / 512.0f;
                                q->v2 = (float)(refY + pc * (x1 - e->origin) + pd * (y0 - e->top)) * texel / 512.0f;
                            } else {
                                q->u0 = ((float)takeHofs[slot] - (takeBands[slot] ? 0.0f : (float)e->origin)) * texel / 512.0f;
                                q->v0 = ((float)takeVofs[slot] - (float)e->top) * texel / 512.0f;
                                q->u1 = q->u0 + (float)(q->x1 - q->x0) * texel / 512.0f;
                                q->v1 = q->v0 + (float)(q->y1 - q->y0) * texel / 512.0f;
                                q->u2 = q->u1;
                                q->v2 = q->v0;
                            }
                            if (wantBehind) {
                                nBehind++;
                            }
                            if (takeBands[slot]) {
                                for (int side = 0; side < 2; side++) {
                                    int y = 0;
                                    while (y < H) {
                                        int end = y + 1;
                                        u16 colour = e->bandFill[gb][side][y];
                                        while (end < H && e->bandFill[gb][side][end] == colour) end++;
                                        if (colour != 0) {
                                            NitroGpuQuad *band = &split->gpuQuads[nQuads++];
                                            memset(band, 0, sizeof(*band));
                                            band->kind = 2;
                                            band->pad[0] = (u8)colour;
                                            band->pad[1] = (u8)(colour >> 8);
                                            band->x0 = side ? (u16)(e->origin + W) : 0;
                                            band->x1 = side ? (u16)e->width : (u16)e->origin;
                                            band->y0 = (u16)y;
                                            band->y1 = (u16)end;
                                        }
                                        y = end;
                                    }
                                }
                            }
                        }
                        for (k2 = nCommitObj - 1; k2 >= 0; k2--) {
                            const Obj *o = &objFrame->obj[commitObj[k2]];
                            NitroGpuQuad *q;
                            int cell = commitCell[k2];
                            int cx = (cell % 3) * 132 + 2, cy = (cell / 3) * 132 + 2;
                            float uu0 = (float)cx / 512.0f, uu1 = (float)(cx + o->w * 2) / 512.0f;
                            float vv0 = 1.0f - (float)cy / 512.0f, vv1 = vv0 - (float)(o->h * 2) / 512.0f;

                            if (o->prio != prio) {
                                continue;
                            }
                            if ((o->prio > split->prio3d) != wantBehind) {
                                continue;
                            }
                            q = &split->gpuQuads[nQuads++];
                            q->kind = 1;
                            q->blend = 0;
                            q->slot = (u8)cell;
                            q->eva = 16;
                            q->evb = 0;
                            q->wrap = (u8)NITRO_GPU_WRAP_CLAMP;
                            q->pad[0] = q->pad[1] = 0;
                            q->x0 = (u16)o->ox;
                            q->y0 = (u16)(o->oy + e->top);
                            q->x1 = (u16)(o->ox + o->w);
                            q->y1 = (u16)(o->oy + e->top + o->h);
                            if (o->hflip) {
                                float t = uu0;

                                uu0 = uu1;
                                uu1 = t;
                            }
                            if (o->vflip) {
                                float t = vv0;

                                vv0 = vv1;
                                vv1 = t;
                            }
                            q->u0 = uu0;
                            q->v0 = vv0;
                            q->u1 = uu1;
                            q->v1 = vv1;
                            q->u2 = uu1;
                            q->v2 = vv0;
                            if (wantBehind) {
                                nBehind++;
                            }
                        }
                    }
                }
                split->nGpuQuads = nQuads;
                split->gpuQuadsBehind = nBehind;
            }
            if (frontClean) {
                prof->gpuRegionClean[0]++;
            }
            if (behindClean) {
                prof->gpuRegionClean[1]++;
            }
        }
    }
    /* The reuse slice is deliberately field-only: every displayed 2D BG must
     * have committed as text or been proven transparent, with no sprites, windows or
     * per-line effects. Mixed regions and affine takes keep the full renderer.
     * Re-run all setup/validation/quad emission above even on reuse: scroll can
     * affect layout eligibility, and fresh metadata must reach the compositor. */
    if (frameOk && committedText && (committedText | emptyText) == ((dispcnt >> 8) & 0xEu) &&
        objFrame->count == 0 && !e->circles[0].on && !e->circles[1].on) {
        sCommittedTextOnly[pe] = committedText;
        if ((e->reusePixels & R2D_TRY_FLAT) && e->height == H && e->top == 0 &&
            !(bldcnt & 0x20) && split->over != NULL) {
            /* Every nontransparent 2D layer is an actual GPU take, not a
             * nomination. Each CPU
             * row therefore has t.filled == 0 (untaken text maps have just
             * been proven transparent): SnapshotOver clears over and
             * leaves its unused alpha alone; resolve writes the backdrop.
             * No backdrop first-target bit means even a colour effect cannot
             * change that result. Do two whole-plane operations instead of
             * 192 row setups, OBJ/priority walks, clears and timed finishes.
             * Refill on EVERY call: content changes and new buffers are safe;
             * there is no new unchanged-memory or writer-coverage assumption. */
             Fill(out, backdrop, width * H);
#ifndef HGSS_PROFILE_OFF
             { u64 tFlat = R2D_NOW(); memset(split->over, 0, (size_t)width * H * sizeof(u16));
               prof->flatFillTicks += R2D_NOW() - tFlat;
               prof->takeTicks += R2D_NOW() - tTake; }
#else
              memset(split->over, 0, (size_t)width * H * sizeof(u16));
#endif
              split->flatCpuPlanes = 1;
             split->scroll3dLines = 0;
             sSplitReuse[pe].flat = 1;
             return;
        }
        if ((e->reusePixels & ~R2D_TRY_FLAT) == committedText) {
            split->scroll3dLines = 0;
#ifndef HGSS_PROFILE_OFF
            prof->takeTicks += R2D_NOW() - tTake;
#endif
            return;
        }
    }
#ifndef HGSS_PROFILE_OFF
    /* P1: the take/commit block ends here on the full-render path. */
    if (split != NULL) prof->takeTicks += R2D_NOW() - tTake;
#endif
    if (!anyWindow) {
        memset(enableLine, 0x3F, (size_t)width);
    }
    /* Blending two layers needs second targets, and alpha blending of some
     * first target that's on screen, or OBJs or 3D pixels that blend by
     * themselves; otherwise each pixel shows its front-most layer, perhaps
     * brightened or darkened. (Battles set alpha blending with no first
     * target on the upper screen, and with BG1 on the touch screen while
     * BG1 is off: no pixel blends.) */
    {
        u32 shown = 0x20;   /* the backdrop */

        for (bg = 0; bg < 4; bg++) {
            if (kind[bg] != BG_NONE) {
                shown |= 1u << bg;
            }
        }
        if (dispcnt & 0x1000) {
            shown |= 0x10;
        }
        blend2 = (bldcnt & 0x3F00) &&
                 ((effect == 1 && (bldcnt & shown)) || objFrame->blends || gx3dTranslucent);
    }
    special = (effect != 0 ? (bldcnt & 0x1Fu) : 0) | (gx3dTranslucent ? 0x01u : 0) |
              (objFrame->blends ? 0x10u : 0);
    flags = (anyWindow ? T_WINDOW : 0) | (effect >= 2 ? T_LAYER : 0);
    if (split != NULL && split->has3d) {
        flags |= T_LAYER;   /* (the layers in front of the 3D: which is which) */
    }
    blendState.alphaOut = NULL;
    blendState.width = width;   /* (split: each line's, in SnapshotOver) */
    blendState.e = e;
    blendState.work = work;
    blendState.kind = kind;
    blendState.bgcnt = bgcnt;
    blendState.dispcnt = dispcnt;
    blendState.gx3dPixels = gx3dPixels;
    blendState.gx3dAlpha = gx3dAlpha;
    blendState.alpha3d = &alpha3d;
    blendState.list = list;
    blendState.entryOf = entryOf;
    blendState.objLine = objLine;
    blendState.special = special;
    blendState.bldcnt = bldcnt;
    blendState.backdrop = backdrop;
    blendState.effect = effect;
    blendState.eva = eva;
    blendState.evb = evb;
    blendState.evy = evy;

    /* (an H-blank effect) Each line's registers: the BGs' scroll, the
     * windows (where they are, which ones show, what's in and out of them)
     * and the 3D's scroll, as the game set them for it. The colour effects
     * and the rest are the frame's. */
    if (e->lines != NULL) {
        u8 *lineIo = work->lineIo;
        int off = e->isA ? 0 : 0x1000;

        memcpy(lineIo, e->ioAll, sizeof(work->lineIo));
        e->ioAll = lineIo;
        e->io = lineIo + off;
    }
    if (split != NULL) {
        split->scroll3dLines = 0;
    }

    for (y = y0 < 0 ? 0 : y0; y < (y1 < 0 ? e->height : y1); y++) {
        u16 *row = out + y * width;
        BOOL objs;
        Target t;

        if (e->lines != NULL) {
            u8 *lineRegs = (u8 *)e->io;
            u32 lineDispcnt;

            /* (a tall picture's lines past the DS's: its last line's; a
             * tall field's above them: its first's) */
            {
                int ds = y - e->top;

                memcpy(lineRegs, e->lines[ds < 0 ? 0 : ds < H ? ds : H - 1], NITRO_LINE_REGS);
                if (e->top) {
                    ShiftDown(e, lineRegs);
                }
            }
            lineDispcnt = Rd32(lineRegs, 0);
            dispcnt = (dispcnt & ~0xE000u) | (lineDispcnt & 0xE000u);
            win0h = Rd16(lineRegs, 0x40);
            win1h = Rd16(lineRegs, 0x42);
            win0v = Rd16(lineRegs, 0x44);
            win1v = Rd16(lineRegs, 0x46);
            winin = Rd16(lineRegs, 0x48);
            winout = Rd16(lineRegs, 0x4A);
            anyWindow = (dispcnt & 0xE000) != 0;
            flags = (flags & ~T_WINDOW) | (anyWindow ? T_WINDOW : 0);
            if (!anyWindow) {
                memset(enableLine, 0x3F, (size_t)width);
            }
            if (split != NULL && split->has3d && y - e->top >= 0 && y - e->top < H) {
                int scroll = (Rd16(lineRegs, 0x10) - e->shift3d) & 0x1FF;

                split->scroll3dLine[y - e->top] = (short)scroll;
                split->scroll3dLines |= scroll != split->scroll3d;
            }
        }
        objs = FALSE;
        {
            /* Slice A: the OBJ layer's per-line cost. */
            u64 t0 = R2D_NOW();

            objs = DrawObjs(e, objFrame, dispcnt, y, objLine);
            prof->objTicks += R2D_NOW() - t0;
        }
        prof->lines++;

        /* Which layers and effects each pixel shows (the windows): window
         * 0 over window 1 over the OBJ window over the outside. */
        if (anyWindow) {
            u64 t0 = R2D_NOW();
            memset(enableLine, winout & 0x3F, (size_t)width);
            if ((dispcnt & 0x8000) && objs) {
                for (x = objLine->x0; x < objLine->x1; x++) {
                    if (objLine->window[x]) {
                        enableLine[x] = (winout >> 8) & 0x3F;
                    }
                }
            }
            if ((dispcnt & 0x4000) && e->circles[1].on) {
                CircleRange(enableLine, &e->circles[1], y - e->top, (winin >> 8) & 0x3F, e->origin, width);
            } else if ((dispcnt & 0x4000) && InRange(y, win1v >> 8, win1v & 0xFF)) {
                FillRange(enableLine, win1h >> 8, win1h & 0xFF, (winin >> 8) & 0x3F, e->origin, width, split != NULL && split->windowsToEdge);
            }
            if ((dispcnt & 0x2000) && e->circles[0].on) {
                CircleRange(enableLine, &e->circles[0], y - e->top, winin & 0x3F, e->origin, width);
            } else if ((dispcnt & 0x2000) && InRange(y, win0v >> 8, win0v & 0xFF)) {
                FillRange(enableLine, win0h >> 8, win0h & 0xFF, winin & 0x3F, e->origin, width, split != NULL && split->windowsToEdge);
            }
            prof->windowTicks += R2D_NOW() - t0;
        }

        t.row = row;
        t.layerOf = layer1;
        t.enable = enableLine;
        t.filled = 0;
        memset(row, 0, (size_t)width * sizeof(u16));
        nlist = 0;
        snapped = FALSE;

        /* The layers front to back (the OBJs of priority 0, the BGs of
         * priority 0 by number, the OBJs of priority 1, ...), until every
         * pixel has its front layer: the layers behind aren't even drawn
         * (when two layers blend, only if a pixel needs the one behind). */
        for (p = 0; p < 4; p++) {
            int slot;

            for (slot = -1; slot < 4; slot++) {
                const u16 *px = NULL;
                const u8 *prioOf = NULL;
                int layer, x0 = 0, x1 = width;

                if (slot == 0 && split != NULL && split->has3d && p == split->prio3d && y < e->h3d) {
                    /* The 3D's place among the layers: what's drawn so far
                     * is in front of it. */
                    {
                        u64 t0 = R2D_NOW();

                        SnapshotOver(split, &blendState, row, y, nlist, blend2, flags, effect,
                                     evy, bldcnt, enableLine, layer1, t.filled);
                        prof->finishTicks += R2D_NOW() - t0;
                    }
                    snapped = TRUE;
                    /* What's behind the 3D is drawn without them (nothing
                     * was drawn when t.filled is 0: the line is already
                     * zeroed). */
                    if (t.filled != 0) {
                        memset(row, 0, (size_t)width * sizeof(u16));
                    }
                    t.filled = 0;
                    nlist = 0;
                }
                if (slot < 0) {
                    if (!objs || !(objLine->prios & (1 << p))) {
                        continue;
                    }
                    layer = SLOT_OBJ;
                    prioOf = objLine->prio;
                    x0 = objLine->x0;
                    x1 = objLine->x1;
                } else {
                    bg = slot;
                    if (kind[bg] == BG_NONE || (bgcnt[bg] & 3) != p) {
                        continue;
                    }
                    layer = bg;
                }
                if (blend2) {
                    /* Noted, for the pixels that need the layer behind
                     * their front one: its own line (px) is drawn then. */
                    entryOf[layer == SLOT_OBJ ? 4 + p : layer] = (s8)nlist;
                    list[nlist].layer = (u8)layer;
                    list[nlist].p = (u8)p;
                    list[nlist].px = layer == SLOT_OBJ ? objLine->color : NULL;
                    list[nlist].prioOf = prioOf;
                    list[nlist].x0 = (short)x0;
                    list[nlist].x1 = (short)x1;
                    nlist++;
                }
                if (t.filled >= width) {
                    if (!blend2) {
                        break;
                    }
                    continue;
                }
                t.layer = (u8)layer;
                t.bit = (u8)(1 << layer);
                if (layer == SLOT_OBJ) {
                    px = objLine->color;
                } else if (kind[bg] == BG_TEXT) {
                    /* straight into the line (the pictures drawn with it
                     * first: in front of it) */
                    int px0, px1;

                    if (((e->picBgs >> bg) & 1) &&
                        PicturesOnLine(e, bg, y, work->picLine, TRUE, &px0, &px1)) {
                        u64 t0 = R2D_NOW();

                        Merge(&t, work->picLine, NULL, p, px0, px1, blend2 ? flags | T_LAYER : flags);
                        prof->mergeTicks += R2D_NOW() - t0;
                    }
                    {
                        u64 t0 = R2D_NOW();

                        DrawTextBg(e, bg, bgcnt[bg], dispcnt, y, &t, blend2 ? flags | T_LAYER : flags);
                        prof->textTicks += R2D_NOW() - t0;
                        prof->textDraws++;
                    }
                    continue;
                } else {
                    u64 t0 = R2D_NOW();

                    px = LayerLine(e, work, bg, kind[bg], bgcnt[bg], dispcnt, y,
                                   gx3dPixels, gx3dAlpha, &alpha3d);
                    prof->layerTicks += R2D_NOW() - t0;
                    prof->layerDraws++;
                    if (blend2) {
                        list[nlist - 1].px = px;
                    }
                }
                {
                    u64 t0 = R2D_NOW();

                    Merge(&t, px, prioOf, p, x0, x1, blend2 ? flags | T_LAYER : flags);
                    prof->mergeTicks += R2D_NOW() - t0;
                }
            }
        }

        if (split != NULL && split->has3d && !snapped && y < e->h3d) {
            /* (the line was full before the 3D's place: everything drawn
             * is in front of it, and nothing behind it was drawn; a tall
             * picture's lines past the DS's have no 3D: all of it behind) */
            SnapshotOver(split, &blendState, row, y, nlist, blend2, flags, effect, evy, bldcnt,
                         enableLine, layer1, t.filled);
            if (t.filled != 0) {
                memset(row, 0, (size_t)width * sizeof(u16));
            }
            nlist = 0;
        }
        if (!blend2) {
            u64 t1 = R2D_NOW();

            /* Slice S3: the brighten loop below darkens even with effect 0/1
             * when a first-target bit is set (existing behavior, kept), so
             * the plain resolve is only identical when no first-target bit
             * can match: bldcnt bits 0-5 clear (layer1 holds 0-4, backdrop
             * is 5). Then every pixel resolves to drawn-or-backdrop exactly
             * as the pair loop writes it. */
            if (!(flags & T_LAYER) || (bldcnt & 0x3Fu) == 0) {
                if (t.filled == 0) {
                    /* Slice A cut: nothing drawn on the line, so the loop
                     * below would write the backdrop into every pixel. One
                     * fill instead of a resolve pass (the line is already
                     * zeroed above, and filled == 0 means no Merge or PUT
                     * wrote into it: provably the same pixels). Counted for
                     * the Slice B cache design. */
                    prof->emptyLines++;
                    u32 back2 = (u32)backdrop * 0x10001u;

                    if ((((uintptr_t)row) & 3) == 0) {
                        u32a *pair = (u32a *)row;

                        for (x = 0; x < width / 2; x++) {
                            pair[x] = back2;
                        }
                    } else {
                        for (x = 0; x < width; x++) {
                            row[x] = backdrop;
                        }
                    }
                    if (width & 1) {
                        row[width - 1] = backdrop;
                    }
                } else if (((uintptr_t)row & 3) == 0) {
                    /* Two pixels at a time: a pixel keeps its colour if
                     * drawn, else takes the backdrop. */
                    u32a *pair = (u32a *)row;
                    u32 back2 = (u32)backdrop * 0x10001u;

                    for (x = 0; x < width / 2; x++) {
                        u32 c = pair[x];
                        u32 drawn = (c >> 15) & 0x10001u;

                        pair[x] = (c & (drawn * 0x7FFFu)) | (back2 & ((drawn ^ 0x10001u) * 0xFFFFu));
                    }
                } else {
                    for (x = 0; x < width; x++) {
                        u16 c = row[x];

                        row[x] = (c & OPAQUE) ? (u16)(c & 0x7FFF) : backdrop;
                    }
                }
            } else {
                /* brighten or darken the layers chosen */
                for (x = 0; x < width; x++) {
                    u16 c = row[x];
                    int top = (c & OPAQUE) ? layer1[x] : 5;

                    c = (c & OPAQUE) ? (u16)(c & 0x7FFF) : backdrop;
                    if ((enableLine[x] & 0x20) && (bldcnt & (1 << top))) {
                        c = effect == 2 ? Brighten(c, evy) : Darken(c, evy);
                    }
                    row[x] = c;
                }
            }
            prof->finishTicks += R2D_NOW() - t1;
        } else {
            /* Each pixel has its front layer; some blend with the one behind. */
            blendState.y = y;
            blendState.nlist = nlist;
            {
                u64 t1 = R2D_NOW();

                BlendLine(&blendState, row, enableLine, layer1, anyWindow);
                prof->blendTicks += R2D_NOW() - t1;
                prof->blendLines++;
            }
        }
        if (split != NULL && split->has3d && anyWindow && y < e->h3d) {
            /* Where a window hides the 3D, what's behind it shows. */
            u16 *over = split->over + y * width;
            u8 *alpha = split->overAlpha + y * width;
            u64 t1 = R2D_NOW();

            for (x = 0; x < width; x++) {
                if (enableLine[x] & 1) {
                    continue;
                }
                if (!(over[x] & OPAQUE)) {
                    over[x] = (u16)(row[x] | OPAQUE);
                } else if (alpha[x] < 16) {
                    /* (a pixel that would blend with the 3D blends with
                     * what's behind it instead) */
                    over[x] = (u16)(BlendIn16(over[x] & 0x7FFF, row[x], alpha[x], 16 - alpha[x]) | OPAQUE);
                }
                alpha[x] = 16;
            }
            prof->finishTicks += R2D_NOW() - t1;
        }
    }
    /* Release only after every scanline has been composited. */
    if(gx3dPixels)NitroPort_Gx3dReleaseImage(gx3dPixels);
}

static void DrawEngine(Engine *e, u16 *out, NitroRender2DSplit *split, int y0, int y1)
{
    if (e->width == W) {
        DrawEngineT(e, out, split, W, y0, y1);
    } else {
        DrawEngineT(e, out, split, 0, y0, y1);
    }
}

static void MasterBrightness(u16 *out, u16 reg, int n)
{
    int mode = reg >> 14;
    int factor = reg & 31;
    int i;

    if (factor > 16) {
        factor = 16;
    }
    if (factor == 0 || (mode != 1 && mode != 2)) {
        return;
    }
    if (factor == 16) {
        /* all the way: white or black */
        for (i = 0; i < n; i++) {
            out[i] = mode == 1 ? 0x7FFF : 0;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        out[i] = mode == 1 ? Brighten(out[i], factor) : Darken(out[i], factor);
    }
}

static void Fill(u16 *out, u16 color, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        out[i] = color;
    }
}

/* (wider canvas) The screen's parts (port_wide.c): the splits made into
 * parts (the part left of the split, the part right of it, the column
 * drawn on between), then the game's parts; its sprite moves first in
 * line, and the 3D's. */
static void SetUpParts(Engine *e)
{
    NitroWideSplit splits[NITRO_WIDE_MAX_SPLITS];
    NitroWidePart game[NITRO_WIDE_MAX_PARTS];
    int eng = e->isA ? 0 : 1, ns, ng, i;

    if (e->taken != NULL) {
        /* (as they were when the display state was taken) */
        ns = e->taken->nsplits;
        ng = e->taken->nparts;
        memcpy(splits, e->taken->splits, (size_t)ns * sizeof(*splits));
        memcpy(game, e->taken->parts, (size_t)ng * sizeof(*game));
    } else {
        ns = NitroPort_WideSplits(eng, splits);
        ng = NitroPort_WideParts(eng, game);
    }

    for (i = 0; i < ns; i++) {
        const NitroWideSplit *sp = &splits[i];
        NitroWidePart *p = &e->parts[e->nParts];

        /* (the gap's column first: the parts drawn over it, as before
         * when they overlap) */
        memset(p, 0, 3 * sizeof(*p));
        p[0].kind = p[1].kind = NITRO_WIDE_MOVE;
        p[0].bgs = p[1].bgs = p[2].bgs = sp->bgs;
        p[0].top = p[1].top = p[2].top = (s16)sp->top;
        p[0].bottom = p[1].bottom = p[2].bottom = (s16)sp->bottom;
        p[0].x0 = -0x7FFF;   /* (from the DS's first column, and to its last: PartRuns) */
        p[0].x1 = p[1].x0 = sp->x;
        p[1].x1 = 0x7FFF;
        p[0].dx = sp->left;
        p[1].dx = sp->right;
        e->nParts += 2;
        if (sp->fill && sp->left < sp->right) {
            /* the gap's column under the two parts */
            p[2] = p[1];
            p[1] = p[0];
            p[0].kind = NITRO_WIDE_FILL;
            p[0].x0 = (s16)(sp->x + sp->left);
            p[0].x1 = (s16)(sp->x + sp->right);
            p[0].dx = 0;
            p[0].col = sp->x;
            e->nParts++;
        }
        e->partBgs |= sp->bgs;
    }
    for (i = 0; i < ng; i++) {
        const NitroWidePart *p = &game[i];

        switch (p->kind) {
        case NITRO_WIDE_MOVE:
        case NITRO_WIDE_FILL:
        case NITRO_WIDE_SHOW:
            e->parts[e->nParts++] = *p;
            e->partBgs |= p->bgs & 0x0F;
            if (p->kind == NITRO_WIDE_MOVE) {
                e->movedBgs |= p->bgs & 0x0F;
            }
            break;
        case NITRO_WIDE_OBJS:
            if (e->nObjMove < OBJ_MOVES_MAX) {
                e->objMove[e->nObjMove].x0 = p->x0;
                e->objMove[e->nObjMove].x1 = p->x1;
                e->objMove[e->nObjMove].y0 = p->top;
                e->objMove[e->nObjMove].y1 = p->bottom;
                e->objMove[e->nObjMove].shift = p->dx;
                e->objMove[e->nObjMove].stretch = (short)(p->col != 0);
                e->nObjMove++;
                e->objsMoved = 1;
            }
            break;
        case NITRO_WIDE_SHIFT3D:
            e->shift3d = (short)(e->shift3d + p->dx);
            break;
        case NITRO_WIDE_PICTURE:
            SetUpPicture(e, p);
            break;
        case NITRO_WIDE_CLEAR:
            e->clearBgs |= (u8)(p->bgs & 0x0F);
            e->clearMask[p->dx & 15] |= (u16)p->col;
            break;
        case NITRO_WIDE_NO_ART:
            e->noArtBgs |= (u8)(p->bgs & 0x0F);
            break;
        }
    }
}

/* (a tall field: NitroRender2DSplit.top) An engine's registers as the
 * picture's lines see them, the DS's line 0 at line top: the BGs' maps
 * moved down (VOFS less top) and the windows (WIN0V, WIN1V; one from the
 * DS's top or to its bottom from the picture's). The OBJs: ReadObj; an
 * affine BG: DrawAffineBg. */
static void ShiftDown(const struct Engine *e, u8 *regs)
{
    int bg, k;

    for (bg = 0; bg < 4; bg++) {
        u16 v = Rd16(regs, 0x12 + bg * 4);

        Wr16(regs, 0x12 + bg * 4, (u16)((v & ~0x1FFu) | ((v - e->top) & 0x1FFu)));
    }
    for (k = 0; k < 2; k++) {
        u16 v = Rd16(regs, 0x44 + k * 2);
        int v0 = v >> 8, v1 = v & 0xFF;

        v0 = v0 == 0 ? 0 : v0 + e->top;
        v1 = v1 >= H || v1 == 0 ? e->height : v1 + e->top;
        Wr16(regs, 0x44 + k * 2, (u16)(((v0 > 255 ? 255 : v0) << 8) | (v1 > 255 ? 255 : v1)));
    }
}

/* An engine's registers and memory, and the canvas (split's: width,
 * origin, clip; NULL: the DS's). */
static void SetUpEngine(Engine *e, int engine, const NitroRender2DSplit *split)
{
    BOOL taken = split != NULL && split->snapshot;
    const State2D *st = &sState[engine ? 1 : 0];

    memset(e, 0, sizeof(*e));
    e->isA = engine == 0;
    e->taken = taken ? st : NULL;
    e->ioAll = taken ? st->io : NitroPort_IoReg;
    e->io = e->ioAll + (e->isA ? 0 : 0x1000);
    e->lines = taken ? (st->lines ? (const u8(*)[NITRO_LINE_REGS])st->lineRegs : NULL)
                     : (NitroPort_LineRegs.active ? (const u8(*)[NITRO_LINE_REGS])NitroPort_LineRegs.regs[e->isA ? 0 : 1]
                                                  : NULL);
    e->circles = taken ? st->circles : sCircles[e->isA ? 0 : 1];
    e->bgPltt = (taken ? st->pltt : NitroPort_Pltt) + (e->isA ? 0 : 0x400);
    e->objPltt = e->bgPltt + 0x200;
    e->oam = (taken ? st->oam : NitroPort_Oam) + (e->isA ? 0 : 0x400);
    e->bgVram = e->isA ? NitroPort_BgVram : NitroPort_DbBgVram;
    e->bgMask = e->isA ? 0x7FFFF : 0x1FFFF;
    e->objVram = e->isA ? NitroPort_ObjVram : NitroPort_DbObjVram;
    e->objMask = e->isA ? 0x3FFFF : 0x1FFFF;
    e->width = W;
    e->height = H;
    e->h3d = H;
    e->squeeze = (u8)(split != NULL && e->isA && split->stretch);
    if (split != NULL && split->height > H && split->height <= HMAX && split->width <= W) {
        e->height = split->height;   /* (a tall picture: the DS's width only) */
    }
    if (split != NULL && e->isA && split->top > 0 && split->height > H && split->height <= HMAX &&
        split->top + H <= split->height) {
        /* (the field at the DS's pixel size on the 3DS's whole screen: lines
         * above and below the DS's, the 3D on all of them; the registers
         * as those lines see them, in a copy) */
        static u8 shiftedOf[2][0x2000];   /* (each engine's drawer its own) */
        u8 *shifted = shiftedOf[engine ? 1 : 0];

        e->height = split->height;
        e->h3d = split->height;
        e->top = split->top;
        memcpy(shifted, e->ioAll, sizeof(shiftedOf[0]));
        e->ioAll = shifted;
        e->io = shifted;
        ShiftDown(e, shifted);
    }
    if (split != NULL && split->width > W && split->width <= WMAX) {
        int bg;

        e->width = split->width;
        e->origin = split->origin;
        for (bg = 0; bg < 4; bg++) {
            e->clip[bg] = split->clip[bg];
        }
        e->anchorWindows = (u8)split->anchorWindows;
        e->pictures = (u8)(NitroPort_WidePictures() != 0);
        SetUpParts(e);
        if (e->anchorWindows) {
            /* The windows moved to an edge, on the DS's screen: the
             * sprites over them go with them. */
            for (bg = 0; bg < 4 && e->nObjMove < OBJ_MOVES_MAX; bg++) {
                NitroWideWindow wins[NITRO_WIDE_MAX_WINDOWS];
                int n = EngineWindows(e, bg, wins), i;
                int hofs = Rd16(e->io, 0x10 + bg * 4) & 0x1FF, vofs = Rd16(e->io, 0x12 + bg * 4) & 0x1FF;

                for (i = 0; i < n && e->nObjMove < OBJ_MOVES_MAX; i++) {
                    if (wins[i].anchor == 0) {
                        continue;
                    }
                    e->objMove[e->nObjMove].x0 = (short)(wins[i].left * 8 - hofs);
                    e->objMove[e->nObjMove].x1 = (short)((wins[i].left + wins[i].width) * 8 - hofs);
                    e->objMove[e->nObjMove].y0 = (short)(wins[i].top * 8 - vofs);
                    e->objMove[e->nObjMove].y1 = (short)((wins[i].top + wins[i].height) * 8 - vofs);
                    e->objMove[e->nObjMove].shift =
                        (short)(wins[i].anchor < 0 ? -e->origin : e->width - W - e->origin);
                    e->objMove[e->nObjMove].stretch = 0;
                    e->nObjMove++;
                }
            }
        }
    }
}

/* Half-A L4 retained state, defined below. */
static void PartialBRefresh(const Engine *e, NitroRender2DSplit *split, const u16 *under);
static void PartialBInvalidate(void);
static int sPartialB;
static u64 sPartialEpilogueInvalidates;
static u64 sPartialEpilogueEntries;
static u64 sA0TryPostRender;
static u64 sA0RangeEntryB;

static void RenderRange(int engine, u16 *out, NitroRender2DSplit *split, u32 reusePixels, int y0, int y1, int band)
{
#ifdef HGSS_A0_DIAG
    if (engine) sA0RangeEntryB++;
#endif
    Engine e;
    u32 dispcnt;
    u16 powcnt;
    u32 layout = split ? SplitLayoutSignature(engine, split) : 0;
    int snapshot = split ? split->snapshot : 0;
    int graphics = 0, drew = 0;

    /* Invalidate before all early returns, including display-off/forced blank.
     * Forced draws also establish a new record, never inherit a stale one. */
    sSplitReuse[engine ? 1 : 0].valid = 0;
    sSplitReuse[engine ? 1 : 0].flat = 0;
    sCommittedTextOnly[engine ? 1 : 0] = 0;
    if (!band) {
        sSplitPictureSerial[engine ? 1 : 0]++;
    }
    SetUpEngine(&e, engine, split);
    e.reusePixels = reusePixels;
    if (split != NULL) {
        memset(split, 0, offsetof(NitroRender2DSplit, over));
        split->masterBright = Rd16(e.io, 0x6C);
    }
    powcnt = Rd16(e.ioAll, 0x304);
    dispcnt = Rd32(e.io, 0);

    /* The engine powered off: nothing on that screen. */
    if (!(powcnt & (e.isA ? 0x0002 : 0x0200))) {
        Fill(out, 0, e.width * e.height);
    } else if (dispcnt & 0x80) {                  /* forced blank: white */
        Fill(out, 0x7FFF, e.width * e.height);
    } else switch ((dispcnt >> 16) & (e.isA ? 3 : 1)) {
    case 0:                                /* display off: white */
        Fill(out, 0x7FFF, e.width * e.height);
        break;
    case 1: {
        int lines = e.height;

        DrawEngine(&e, out, split, band ? y0 : 0, band ? y1 : -1);
        if (!band && e.height < lines) {
            /* (a tall picture asked for with the 3D on: the DS's lines
             * drawn, the rest black) */
            Fill(out + e.width * e.height, 0, e.width * (lines - e.height));
        }
        graphics = 1;
        drew = 1;
        break;
    }
    case 2: {                              /* VRAM display: a bank in LCDC */
        const u8 *bank = NitroPort_LcdcVram + ((dispcnt >> 18) & 3) * 0x20000;
        int i;

        Fill(out, 0, e.width * e.height);
        for (i = 0; i < W * H; i++) {
            /* W is 256: shifts, not a divide and modulo per pixel. */
            out[(i >> 8) * e.width + e.origin + (i & 255)] = Rd16(bank, (u32)i * 2) & 0x7FFF;
        }
        drew = 1;
        break;
    }
    default:
        NitroPort_Unimplemented("display mode: main memory display");
        Fill(out, 0, e.width * e.height);
        break;
    }
    /* The fills above return without this in the original Render: only a
     * drawn graphics or VRAM-display picture resolves brightness or
     * establishes a split record. */
    if (drew) {
        if (split == NULL) {
            if (!band) {
                MasterBrightness(out, Rd16(e.io, 0x6C), e.width * e.height);
            }
        } else {
            RememberSplit(engine, out, split, layout, snapshot);
        }
    }
    /* Half-A L4: a whole engine-B render establishes (or drops, for fills)
     * the retained state the next band decision compares against. */
    if (!band && (engine ? 1 : 0) == 1 && sPartialB) {
        sPartialEpilogueEntries++;
        if (graphics && split != NULL) {
            PartialBRefresh(&e, split, out);
        } else {
            sPartialEpilogueInvalidates++;
            PartialBInvalidate();
        }
    }
}

static void Render(int engine, u16 *out, NitroRender2DSplit *split, u32 reusePixels)
{
    RenderRange(engine, out, split, reusePixels, 0, -1, 0);
}

void NitroPort_Render2DBgOffsets(uint16_t out[8])
{
    int i;

    for (i = 0; i < 8; i++) {
        out[i] = (uint16_t)(Rd16(NitroPort_IoReg, 0x10 + 2 * i) & 0x1FF);
    }
}

int NitroPort_Render2DBgPicture(int bg, int snapshot, uint16_t *out)
{
    NitroRender2DSplit layout;
    Engine e;
    u32 dispcnt, charBase, screenBase;
    u16 bgcnt;
    int bpp8, tx, ty, x, y;

    if (bg < 0 || bg > 3) {
        return 0;
    }
    memset(&layout, 0, sizeof(layout));
    layout.snapshot = snapshot;
    SetUpEngine(&e, 0, &layout);
    dispcnt = Rd32(e.io, 0);
    bgcnt = Rd16(e.io, 8 + bg * 2);
    if (BgKind(&e, bg, dispcnt) == BG_AFFINE && ((bgcnt >> 14) & 3) != 3) {
        /* S5/S6 bundle: affine maps decode with AffineSample (same as the
         * software path, so identical by construction). */
        int mapW = 128 << ((bgcnt >> 14) & 3);
        int mapH = mapW;

        charBase = CharBase(&e, bgcnt, dispcnt);
        screenBase = ScreenBase(&e, bgcnt, dispcnt);
        for (y = 0; y < mapH; y++) {
            uint16_t *row = out + (size_t)y * (size_t)mapW;

            for (x = 0; x < mapW; x++) {
                u16 c = AffineSample(&e, 0, NULL, charBase, screenBase, mapW, mapH, TRUE, x, y);

                if (c == 0) {
                    row[x] = 0;
                    continue;
                }
                row[x] = (uint16_t)(((c & 31) << 11) | (((c >> 5) & 31) << 6) | (((c >> 10) & 31) << 1) | 1);
            }
        }
        return 1;
    }
    if (BgKind(&e, bg, dispcnt) != BG_TEXT) {
        return 0;
    }
    bpp8 = (bgcnt & 0x80) != 0;
    charBase = CharBase(&e, bgcnt, dispcnt);
    screenBase = ScreenBase(&e, bgcnt, dispcnt);
    /* S5b: any text map size (sTextSizes order: 256, 512x256, 256x512,
     * 512x512). Tile (tx, ty) lives in the screen block its top-left 256
     * pixels are in (each block 0x800 apart); within it, row-major. For
     * 256x256 this reduces exactly to the old linear form. */
    {
        int mapW = sTextSizes[(bgcnt >> 14) & 3][0], mapH = sTextSizes[(bgcnt >> 14) & 3][1];
        int tilesX = mapW >> 3, tilesY = mapH >> 3;

        for (ty = 0; ty < tilesY; ty++) {
            for (tx = 0; tx < tilesX; tx++) {
                u32 entry = Rd16(e.bgVram, (screenBase +
                    (u32)((tx >> 5) + (ty >> 5) * (tilesX >> 5)) * 0x800 +
                    (u32)(((ty & 31) * 32) + (tx & 31)) * 2) & e.bgMask);
                u32 tile = entry & 0x3FF, pal = (u32)(entry >> 12) * 16;
                int hflip = (entry >> 10) & 1, vflip = (entry >> 11) & 1;

                for (y = 0; y < 8; y++) {
                    int sy = vflip ? 7 - y : y;
                    uint16_t *row = out + (size_t)(ty * 8 + y) * (size_t)mapW + tx * 8;

                    for (x = 0; x < 8; x++) {
                        int sx = hflip ? 7 - x : x;
                        u32 index;
                        u16 c;

                        if (bpp8) {
                            index = e.bgVram[(charBase + tile * 64 + (u32)(sy * 8 + sx)) & e.bgMask];
                        } else {
                            u8 b = e.bgVram[(charBase + tile * 32 + (u32)(sy * 4 + sx / 2)) & e.bgMask];

                            index = sx & 1 ? b >> 4 : b & 15;
                            if (index != 0) {
                                index += pal;
                            }
                        }
                        if (index == 0) {
                            row[x] = 0;
                            continue;
                        }
                        c = Rd16(e.bgPltt, index * 2);
                        row[x] = (uint16_t)(((c & 31) << 11) | (((c >> 5) & 31) << 6) | (((c >> 10) & 31) << 1) | 1);
                    }
                }
            }
        }
    }
    return 1;
}

/* An affine BG's whole map (any size up to 512 x 512; 0 otherwise, nothing
 * written) into out, a line of mapW after another, as RGBA5551 (red in the
 * top bits; 0 where the BG is clear): what the compositor draws an affine
 * BG left out of the picture from (gpuBgLeftOut). Same decode as the
 * software path (AffineSample), so identical by construction. From the
 * state taken (snapshot: 1) or the live one; the VRAM as it is. */
int NitroPort_Render2DLayer(int engine, int bg, const NitroRender2DSplit *layout, u16 *out, uint64_t *key, int *hofs,
                            int *vofs, int *prio)
{
    Engine e;
    u32 dispcnt;
    u16 bgcnt[4], line[WMAX];
    int kind[4], b, y;

    SetUpEngine(&e, engine, layout);
    dispcnt = Rd32(e.io, 0);
    if (((dispcnt >> 16) & 3) != 1 || (dispcnt & 0x80) || bg < 0 || bg > 3) {
        return 0;
    }
    FindExtPalettes(&e);
    MakePalettes(&e);
    for (b = 0; b < 4; b++) {
        bgcnt[b] = Rd16(e.io, 8 + b * 2);
        kind[b] = (dispcnt & (0x100u << b)) ? BgKind(&e, b, dispcnt) : BG_NONE;
        if (kind[b] == BG_LARGE) {
            kind[b] = BG_NONE;
        }
    }
    if (kind[bg] == BG_NONE || kind[bg] == BG_3D) {
        return 0;
    }
    if (e.width > W) {
        DecideSplits(&e, kind, bgcnt, dispcnt);
        LookUpArt(&e, kind, bgcnt, dispcnt, NULL);
        DecideBandFills(&e, kind, bgcnt, dispcnt, 0);
    }
    *key = BgKey(&e, bg, kind[bg], bgcnt[bg], dispcnt);
    *hofs = kind[bg] == BG_TEXT ? Rd16(e.io, 0x10 + bg * 4) & 0x1FF : 0;
    *vofs = kind[bg] == BG_TEXT ? Rd16(e.io, 0x12 + bg * 4) & 0x1FF : 0;
    *prio = bgcnt[bg] & 3;
    /* Slice B: this capture's own row key (captures run outside any
     * DrawEngineT, whose per-frame keys they must not reuse). */
    sTextKey[engine ? 1 : 0][bg] = kind[bg] == BG_TEXT ? TextRowKey(&e, bg, bgcnt[bg], dispcnt) : 0;
    for (y = 0; y < H; y++) {
        if (kind[bg] == BG_TEXT) {
            Target t;

            t.row = line;
            t.layerOf = NULL;
            t.enable = NULL;
            t.layer = (u8)bg;
            t.bit = (u8)(1 << bg);
            t.filled = 0;
            DrawTextBg(&e, bg, bgcnt[bg], dispcnt, y, &t, T_PLAIN);
        } else {
            DrawAffineBg(&e, bg, kind[bg], bgcnt[bg], dispcnt, y, line);
        }
        memcpy(out + y * e.width, line, (size_t)e.width * sizeof(u16));
    }
    return 1;
}

/* (capturing: the game's thread, between frames) the OBJ frame and line
 * of one group alone; the drawing threads have their own. */
static ObjFrame sCaptureObjFrame;
static ObjLine sCaptureObjLine;

/* SetUpEngine for one of the capture's single layers: 0 if the engine
 * shows no normal picture. */
static BOOL SetUpLayer(Engine *e, int engine, const NitroRender2DSplit *layout, u32 *dispcnt)
{
    SetUpEngine(e, engine, layout);
    *dispcnt = Rd32(e->io, 0);
    if (((*dispcnt >> 16) & 3) != 1 || (*dispcnt & 0x80)) {
        return FALSE;
    }
    FindExtPalettes(e);
    MakePalettes(e);
    return TRUE;
}

int NitroPort_Render2DObjGroups(int engine, const NitroRender2DSplit *layout, NitroObjGroup *groups, int max)
{
    Engine e;
    u32 dispcnt;

    if (!SetUpLayer(&e, engine, layout, &dispcnt) || !(dispcnt & 0x1000)) {
        return 0;
    }
    return ObjGroups(&e, dispcnt, groups, max);
}

int NitroPort_Render2DPictures(int engine, const NitroRender2DSplit *layout, NitroWidePictureShown *out, int max)
{
    Engine e;
    u32 dispcnt;
    int i, n = 0;

    if (!SetUpLayer(&e, engine, layout, &dispcnt)) {
        return 0;
    }
    for (i = 0; i < e.nPics && n < max; i++) {
        out[n].picture = e.pics[i].id;
        out[n].bg = e.pics[i].bg;
        out[n].prio = Rd16(e.io, 8 + e.pics[i].bg * 2) & 3;
        out[n].x = e.pics[i].x;
        out[n].y = e.pics[i].y;
        out[n].w = e.pics[i].w;
        out[n].h = e.pics[i].h;
        out[n].angle = e.pics[i].angle;
        n++;
    }
    return n;
}

int NitroPort_Render2DObjLayer(int engine, int group, const NitroRender2DSplit *layout, u16 *out)
{
    Engine e;
    u32 dispcnt;
    ObjFrame *f = &sCaptureObjFrame;
    ObjLine *line = &sCaptureObjLine;
    int y, x, k, any = 0;

    if (!SetUpLayer(&e, engine, layout, &dispcnt) || !(dispcnt & 0x1000)) {
        return 0;
    }
    SortObjs(&e, f);
    /* only the group's OBJs on each line */
    for (y = 0; y < H; y++) {
        int n = 0;

        for (k = 0; k < f->lineCount[y]; k++) {
            const Obj *obj = &f->obj[f->onLine[y][k]];

            if (obj->mode < 2 && ObjGroupOf(obj) == group) {
                f->onLine[y][n++] = f->onLine[y][k];
            }
        }
        f->lineCount[y] = (u8)n;
    }
    LookUpObjArt(&e, f, dispcnt, group);
    for (y = 0; y < H; y++) {
        u16 *row = out + y * e.width;

        if (!DrawObjs(&e, f, dispcnt, y, line)) {
            memset(row, 0, (size_t)e.width * sizeof(u16));
            continue;
        }
        for (x = 0; x < e.width; x++) {
            row[x] = (line->color[x] & OPAQUE) ? line->color[x] : 0;
            any |= row[x] != 0;
        }
    }
    return any;   /* (a group all off the screen: nothing to draw) */
}

/* ------------------------------------------------- sharing wide art */

/* The BG's own pixels over the canvas and a margin past its edges
 * (SHARE_MX, SHARE_MY), at the scroll a picture was made at (OwnTextPixel;
 * an affine BG's where its matrix takes it now): what the picture's
 * pixels are compared with, where they are and where they were moved
 * from (NITRO_WIDE_ART_MOVE_MAX either way, and a little up or down;
 * 832 x 224 pixels, 364 KB). */
#define SHARE_MX (NITRO_WIDE_ART_MOVE_MAX + 1)
#define SHARE_MY NITRO_WIDE_ART_MOVE_UP_MAX
#define SHARE_RW (NITRO_WIDE_ART_W + 2 * SHARE_MX)
#define SHARE_RH (H + 2 * SHARE_MY)
#define SHARE_SAMPLES 512
#define SHARE_VOTES_LEAST 16
static u16 *sShareRef;   /* (got the first time something is shared: the 3DS program never does) */
static u16 (*sShareVotes)[2 * SHARE_MX];
#define SHARE_REF(x, y) sShareRef[((y) + SHARE_MY) * SHARE_RW + (x) + SHARE_MX]

/* A picture's pixels that are the BG's own dx right and dy down of where
 * the BG has them: made NITRO_WIDE_ART_GAME, moved that far. How many. */
static int ShareMark(u16 *px, int dx, int dy)
{
    int x, y, n = 0;

    for (y = 0; y < H; y++) {
        int ry = y - dy;

        if (ry < -SHARE_MY || ry >= H + SHARE_MY) {
            continue;
        }
        for (x = 0; x < NITRO_WIDE_ART_W; x++) {
            u16 *c = &px[y * NITRO_WIDE_ART_W + x], g;
            int rx = x - dx;

            if (!(*c & OPAQUE) || rx < -SHARE_MX || rx >= NITRO_WIDE_ART_W + SHARE_MX) {
                continue;
            }
            g = SHARE_REF(rx, ry);
            if ((g & OPAQUE) && (g & 0x7FFF) == (*c & 0x7FFF)) {
                *c = NitroPort_WideArtGameAt(dx, dy);
                n++;
            }
        }
    }
    return n;
}

/* Whether a picture's pixel is at an edge (in the picture as made, orig:
 * not as its pixels are left out): opaque, and not the same as the one
 * left of it or the one above (the pixels a move can be told from: in a
 * plain stretch every pixel is the same as the BG's over a whole plain
 * stretch of it, wherever that is). */
static BOOL ShareEdgePixel(const u16 *px, int x, int y)
{
    u16 c = px[y * NITRO_WIDE_ART_W + x];

    return (c & OPAQUE) && ((x > 0 && px[y * NITRO_WIDE_ART_W + x - 1] != c) ||
                            (y > 0 && px[(y - 1) * NITRO_WIDE_ART_W + x] != c));
}

/* The move most of a picture's pixels (those left: not yet the game's
 * own) match the BG's own at, from a sample of those at edges (sideways
 * only, for a group of sprites: its picture's row is drawn from the
 * sprites on that row): how many votes it got, 0 when fewer than a
 * quarter of the sample agree (the sample's own art, matching the BG's
 * here and there by chance). */
static int ShareBestMove(const u16 *px, const u16 *orig, int sideways, int *dx, int *dy)
{
    u16 (*votes)[2 * SHARE_MX] = sShareVotes;
    int x, y, edges = 0, samples = 0, step, i = 0, best = 0, ox, oy;
    int oy0 = sideways ? 0 : -SHARE_MY, oy1 = sideways ? 1 : SHARE_MY;

    for (y = 0; y < H; y++) {
        for (x = 0; x < NITRO_WIDE_ART_W; x++) {
            edges += (px[y * NITRO_WIDE_ART_W + x] & OPAQUE) && ShareEdgePixel(orig, x, y);
        }
    }
    step = edges / SHARE_SAMPLES + 1;
    memset(votes, 0, sizeof(u16) * 2 * SHARE_MY * 2 * SHARE_MX);
    for (y = 0; y < H; y++) {
        for (x = 0; x < NITRO_WIDE_ART_W; x++) {
            u16 c = px[y * NITRO_WIDE_ART_W + x];

            if (!(c & OPAQUE) || !ShareEdgePixel(orig, x, y) || i++ % step != 0) {
                continue;
            }
            samples++;
            for (oy = oy0; oy < oy1; oy++) {
                int ry = y - oy;

                if (ry < -SHARE_MY || ry >= H + SHARE_MY) {
                    continue;
                }
                for (ox = -NITRO_WIDE_ART_MOVE_MAX; ox <= NITRO_WIDE_ART_MOVE_MAX; ox++) {
                    int rx = x - ox;

                    if (rx >= -SHARE_MX && rx < NITRO_WIDE_ART_W + SHARE_MX && SHARE_REF(rx, ry) == c) {
                        votes[oy + SHARE_MY][ox + SHARE_MX]++;
                    }
                }
            }
        }
    }
    /* (the shortest of the moves most agree on: a tile pattern that
     * repeats agrees on every move by the pattern's size) */
    *dx = *dy = 0;
    for (oy = oy0; oy < oy1; oy++) {
        for (ox = -NITRO_WIDE_ART_MOVE_MAX; ox <= NITRO_WIDE_ART_MOVE_MAX; ox++) {
            int v = votes[oy + SHARE_MY][ox + SHARE_MX];

            if ((ox != 0 || oy != 0) && (v > best || (v == best && (ox < 0 ? -ox : ox) + (oy < 0 ? -oy : oy) < (*dx < 0 ? -*dx : *dx) + (*dy < 0 ? -*dy : *dy)))) {
                best = v;
                *dx = ox;
                *dy = oy;
            }
        }
    }
    return best >= SHARE_VOTES_LEAST && best * 4 >= samples ? best : 0;
}

/* A picture of a BG with the BG's own pixels left out: those the same as
 * the BG's, where the picture was made, made NITRO_WIDE_ART_GAME (the
 * renderer puts the BG's own back there: ArtTextPixel, AffineSample),
 * where they are or moved from where the BG has them (the move most of
 * them match at, up to three times over: a Pokemon of the opening's moved
 * along, the map's beyond the DS's edge copied into a band); a picture of
 * the bands only has nothing in the DS's part (it isn't shown there). 0 if
 * the BG isn't where the picture can be told from (a BG laid out in parts,
 * or split, away from where its picture was made). */
static BOOL StripBgArt(const Engine *e, int bg, int kind, u16 bgcnt, u32 dispcnt, int hofs, int vofs, int full,
                       const u16 *orig, u16 *px)
{
    int x, y, curH = Rd16(e->io, 0x10 + bg * 4) & 0x1FF, curV = Rd16(e->io, 0x12 + bg * 4) & 0x1FF, round;
    BOOL parts = (e->partBgs >> bg) & 1;

    if (kind == BG_TEXT) {
        int mapW = sTextSizes[bgcnt >> 14][0], mapH = sTextSizes[bgcnt >> 14][1];
        OwnRow own;

        OwnRowStart(&own);
        /* (the BG as it's drawn where the picture was made: a BG laid out
         * in parts only when that's where it is) */
        if (parts && (curH != hofs || curV != vofs)) {
            return FALSE;
        }
        for (y = -SHARE_MY; y < H + SHARE_MY; y++) {
            for (x = -SHARE_MX; x < NITRO_WIDE_ART_W + SHARE_MX; x++) {
                SHARE_REF(x, y) = OwnTextPixel(e, bg, bgcnt, dispcnt, hofs, vofs, mapW, mapH, x, y, &own);
            }
        }
    } else {
        const u8 *io = e->io;
        u32 base = bg == 2 ? 0x20 : 0x30;
        s32 pa = (s16)Rd16(io, base + 0), pb = (s16)Rd16(io, base + 2);
        s32 pc = (s16)Rd16(io, base + 4), pd = (s16)Rd16(io, base + 6);
        s32 refX = (s32)(Rd32(io, base + 8) << 4) >> 4, refY = (s32)(Rd32(io, base + 12) << 4) >> 4;
        BOOL wrap = (bgcnt & 0x2000) != 0;
        int size = bgcnt >> 14, mode, mapW, mapH;
        u32 charBase = CharBase(e, bgcnt, dispcnt), screenBase = ScreenBase(e, bgcnt, dispcnt);
        const u8 *ext = NULL;

        if (parts) {
            return FALSE;   /* (split: where its matrix takes it, its parts) */
        }
        if (kind == BG_AFFINE) {
            mode = 0;
            mapW = mapH = 128 << size;
        } else if (!(bgcnt & 0x80)) {
            mode = 1;
            mapW = mapH = 128 << size;
            if (dispcnt & 0x40000000) {
                ext = BgExtSlot(e, bg, bgcnt);
            }
        } else {
            static const int bmp[4][2] = { { 128, 128 }, { 256, 256 }, { 512, 256 }, { 512, 512 } };

            mode = (bgcnt & 0x4) ? 3 : 2;
            mapW = bmp[size][0];
            mapH = bmp[size][1];
            screenBase = ((bgcnt >> 8) & 0x1F) * 0x4000;
        }
        /* (the matrix as it is now: an affine BG's picture is used
         * wherever it's scrolled) */
        for (y = -SHARE_MY; y < H + SHARE_MY; y++) {
            for (x = -SHARE_MX; x < NITRO_WIDE_ART_W + SHARE_MX; x++) {
                SHARE_REF(x, y) = AffineSample(e, mode, ext, charBase, screenBase, mapW, mapH, wrap,
                                               (refX + pb * y + pa * (x - e->origin)) >> 8,
                                               (refY + pd * y + pc * (x - e->origin)) >> 8);
            }
        }
    }
    ShareMark(px, 0, 0);
    for (round = 0; round < 3; round++) {
        int dx, dy;

        if (ShareBestMove(px, orig, 0, &dx, &dy) == 0 || ShareMark(px, dx, dy) == 0) {
            break;
        }
    }
    if (!(full & NITRO_WIDE_ART_FULL)) {
        for (y = 0; y < H; y++) {
            memset(px + y * NITRO_WIDE_ART_W + e->origin, 0, W * sizeof(u16));
        }
    }
    return TRUE;
}

int NitroPort_Render2DShareArt(int engine)
{
    static u16 px[NITRO_WIDE_ART_W * H], layer[NITRO_WIDE_ART_W * H];
    static u8 oam[0x400];
    NitroRender2DSplit split;
    Engine e;
    u32 dispcnt;
    u16 bgcnt[4];
    int kind[4], b, shared = 0;
    char name[64];

    if (NitroPort_WideArtUnshared() == 0) {
        return 0;
    }
    if (sShareRef == NULL) {
        sShareRef = malloc(sizeof(u16) * SHARE_RW * SHARE_RH);
        sShareVotes = malloc(sizeof(u16) * 2 * SHARE_MY * 2 * SHARE_MX);
        if (sShareRef == NULL || sShareVotes == NULL) {
            NitroPort_Log("[wide art] no memory to share the pictures with");
            free(sShareRef);
            free(sShareVotes);
            sShareRef = NULL;
            sShareVotes = NULL;
            return 0;
        }
    }
    memset(&split, 0, sizeof(split));
    NitroPort_WideLayout(engine, &split);
    if (split.width != NITRO_WIDE_ART_W || !SetUpLayer(&e, engine, &split, &dispcnt)) {
        return 0;
    }
    for (b = 0; b < 4; b++) {
        bgcnt[b] = Rd16(e.io, 8 + b * 2);
        kind[b] = (dispcnt & (0x100u << b)) ? BgKind(&e, b, dispcnt) : BG_NONE;
    }
    for (b = 0; b < 4; b++) {
        const u16 *pixels;
        uint64_t key;
        int hofs, vofs, full, i = 0;

        if (kind[b] == BG_NONE || kind[b] == BG_3D || kind[b] == BG_LARGE) {
            continue;
        }
        key = BgKey(&e, b, kind[b], bgcnt[b], dispcnt);
        while (NitroPort_WideArtToShare(key, i, &pixels, &hofs, &vofs, &full, name, sizeof(name))) {
            memcpy(px, pixels, sizeof(px));
            if (StripBgArt(&e, b, kind[b], bgcnt[b], dispcnt, hofs, vofs, full, pixels, px) &&
                NitroPort_WideArtShareWrite(name, px) == 0) {
                shared++;
            } else {
                i++;   /* (not now: another time, where the BG can be told from) */
            }
        }
    }
    if (dispcnt & 0x1000) {
        /* The OBJ groups': their sprites' own pixels left out (a group's
         * picture only goes where none of its OBJs is anyway), and those
         * moved along made NITRO_WIDE_ART_GAME (drawn from the sprites on
         * the row: DrawObjs). */
        NitroObjGroup groups[32];
        int count = ObjGroups(&e, dispcnt, groups, 32), g;

        for (g = 0; g < count; g++) {
            const u16 *pixels;
            int hofs, vofs, full, i = 0, x, y, drawn = -1, drawnDx = 0, drawnDy = 0;

            while (NitroPort_WideArtToShare(groups[g].key, i, &pixels, &hofs, &vofs, &full, name, sizeof(name))) {
                /* (how far the group has moved since the picture was
                 * made, the shorter way round OAM's 512 x 256: as
                 * LookUpObjArt has it) */
                int dx = (groups[g].x - hofs + 512) & 511, dy = (groups[g].y - vofs + 256) & 255;

                dx = dx >= 256 ? dx - 512 : dx;
                dy = dy >= 128 ? dy - 256 : dy;

                memcpy(px, pixels, sizeof(px));
                if (drawn < 0 || dx != drawnDx || dy != drawnDy) {
                    /* the group alone, without its picture, back where
                     * the picture was made (its OBJs moved back by as far
                     * as the group has moved since: a group that slides
                     * is mostly off the canvas where it is now) */
                    ObjFrame *f = &sCaptureObjFrame;
                    ObjLine *line = &sCaptureObjLine;
                    Engine there = e;
                    int k;

                    memcpy(oam, e.oam, sizeof(oam));
                    for (k = 0; k < 128; k++) {
                        u8 *o = oam + k * 8;
                        u16 a0 = Rd16(o, 0), a1 = Rd16(o, 2), a2 = Rd16(o, 4);
                        int mode = (a0 >> 10) & 3, prio = (a2 >> 10) & 3, pal = (a2 >> 12) & 15;
                        int group = (prio << 5) | ((a0 & 0x2000) ? 16 : pal);

                        if (mode >= 2 || group != groups[g].group) {
                            continue;
                        }
                        a0 = (u16)((a0 & ~0xFFu) | ((a0 - dy) & 0xFF));
                        a1 = (u16)((a1 & ~0x1FFu) | ((a1 - dx) & 0x1FF));
                        o[0] = (u8)a0;
                        o[1] = (u8)(a0 >> 8);
                        o[2] = (u8)a1;
                        o[3] = (u8)(a1 >> 8);
                    }
                    there.oam = oam;
                    SortObjs(&there, f);
                    for (y = 0; y < H; y++) {
                        int n = 0;

                        for (k = 0; k < f->lineCount[y]; k++) {
                            const Obj *obj = &f->obj[f->onLine[y][k]];

                            if (obj->mode < 2 && ObjGroupOf(obj) == groups[g].group) {
                                f->onLine[y][n++] = f->onLine[y][k];
                            }
                        }
                        f->lineCount[y] = (u8)n;
                    }
                    f->artCount = 0;
                    drawn = 0;
                    drawnDx = dx;
                    drawnDy = dy;
                    for (y = 0; y < H; y++) {
                        u16 *row = layer + y * NITRO_WIDE_ART_W;

                        if (!DrawObjs(&there, f, dispcnt, y, line)) {
                            memset(row, 0, NITRO_WIDE_ART_W * sizeof(u16));
                            continue;
                        }
                        for (x = 0; x < NITRO_WIDE_ART_W; x++) {
                            row[x] = (line->color[x] & OPAQUE) ? line->color[x] : 0;
                            drawn |= row[x] != 0;
                        }
                    }
                }
                if (drawn > 0) {
                    /* The sprites' own pixels moved along (a strip of
                     * sprites that goes round, copied on into a band):
                     * the game's own, drawn from the sprites that far
                     * back on the row, wherever the strip is (first: a
                     * pixel the sprites cover now they may not cover
                     * elsewhere). */
                    int round, dx2, dy2;

                    memset(sShareRef, 0, sizeof(u16) * SHARE_RW * SHARE_RH);
                    for (y = 0; y < H; y++) {
                        for (x = 0; x < NITRO_WIDE_ART_W; x++) {
                            SHARE_REF(x, y) = layer[y * NITRO_WIDE_ART_W + x];
                        }
                    }
                    for (round = 0; round < 3; round++) {
                        if (ShareBestMove(px, pixels, 1, &dx2, &dy2) == 0 || ShareMark(px, dx2, dy2) == 0) {
                            break;
                        }
                    }
                }
                for (y = 0; y < H; y++) {
                    for (x = 0; x < NITRO_WIDE_ART_W; x++) {
                        u16 *c = &px[y * NITRO_WIDE_ART_W + x], g2 = layer[y * NITRO_WIDE_ART_W + x];

                        if (!(full & NITRO_WIDE_ART_FULL) && x >= e.origin && x < e.origin + W) {
                            *c = 0;
                        } else if (drawn > 0 && (*c & OPAQUE) && (g2 & OPAQUE) && (g2 & 0x7FFF) == (*c & 0x7FFF)) {
                            *c = 0;   /* (the sprites' own, where they are: nothing to add) */
                        }
                    }
                }
                if (NitroPort_WideArtShareWrite(name, px) == 0) {
                    shared++;
                } else {
                    i++;
                }
            }
        }
    }
    return shared;
}

void NitroPort_Render2D(int engine, u16 *out)
{
    Render(engine, out, NULL, 0);
}

void NitroPort_Render2DSplit(int engine, u16 *under, NitroRender2DSplit *split)
{
    Render(engine, under, split, 0);
}

BOOL NitroPort_EngineAOnTop(void)
{
    /* POWCNT1 bit 15: engine A's picture goes to the upper screen. */
    return (IO16(0x304) & 0x8000) != 0;
}

void NitroPort_RenderScreens(u16 *top, u16 *bottom)
{
    BOOL aOnTop = NitroPort_EngineAOnTop();

    NitroPort_Render2D(0, aOnTop ? top : bottom);
    NitroPort_Render2D(1, aOnTop ? bottom : top);
}

/* ------------------------------------------------- unchanged screens */

/* A 32-bit FNV-1a hash of words, two lanes at a time. */
static u32 HashWords(const u8 *p, u32 bytes, u32 h)
{
    const u32a *w = (const u32a *)p;
    /* Rotate the incoming state for the second lane. With h2 = h ^ C, a
     * difference in bit 31 of an earlier region flips bit 31 in BOTH lanes;
     * odd multiplies preserve that single-bit difference and the final XOR
     * cancels it. A later region could therefore erase a real VRAM change. */
    u32 n = bytes / 8, h2 = ((h << 1) | (h >> 31)) ^ 0x9E3779B9u, i;

    for (i = 0; i < n; i++) {
        h = (h ^ w[2 * i]) * 16777619u;
        h2 = (h2 ^ w[2 * i + 1]) * 16777619u;
    }
    return (h ^ (h2 * 31u)) * 16777619u;
}

/* Everything an engine's picture comes from: its registers, palettes, OAM,
 * VRAM (as the CPU sees it, and the LCDC banks it may use as extended
 * palettes or for VRAM display), the VRAM bank settings and POWCNT1. */
void NitroPort_Render2DTakeState(int engine)
{
    State2D *st = &sState[engine ? 1 : 0];

    memcpy(st->io, NitroPort_IoReg, sizeof(st->io));
    memcpy(st->pltt, NitroPort_Pltt, sizeof(st->pltt));
    memcpy(st->oam, NitroPort_Oam, sizeof(st->oam));
    st->lines = NitroPort_LineRegs.active;
    if (st->lines) {
        memcpy(st->lineRegs, NitroPort_LineRegs.regs[engine ? 1 : 0], sizeof(st->lineRegs));
    }
    memcpy(st->circles, sCircles[engine ? 1 : 0], sizeof(st->circles));
    {
        int bg;

        st->winSerial = NitroPort_WideWindowsSerial();
        for (bg = 0; bg < 4; bg++) {
            st->nwins[bg] = NitroPort_WideWindows(engine ? 1 : 0, bg, st->wins[bg], NITRO_WIDE_MAX_WINDOWS);
        }
        st->nsplits = NitroPort_WideSplits(engine ? 1 : 0, st->splits);
        st->nparts = NitroPort_WideParts(engine ? 1 : 0, st->parts);
    }
}

/* What an engine's picture is drawn from, in two parts: the small ones
 * (registers, palettes, OAM) and its VRAM (the most part: 864 KB for
 * engine A, read in full to work it out). Half-A L4 splits the OAM out
 * (skipOam) so engine B's change can be attributed to registers, OAM or
 * tiles separately; skipping only omits that one hash from the chain. */
static u32 EngineSmallSignature(int engine, int snapshot, u32 gpuBgMask, int skipOam, int skipSwap)
{
    BOOL isA = engine == 0;
    const State2D *st = &sState[isA ? 0 : 1];
    const u8 *ioAll = snapshot ? st->io : NitroPort_IoReg;
    const u8 *io = ioAll + (isA ? 0 : 0x1000);
    u32 regs[0x70 / 4];
    u32 h = 2166136261u;

    /* Not DISP3DCNT's two acknowledge bits (12, 13: the 3D's colour
     * buffer underflow and polygon RAM overflow): the game writes them
     * every frame, alternating, and they change nothing drawn. Counted,
     * engine A's picture was drawn and upscaled again every other frame
     * of the field with nothing on it changed (about 15 times a second, 9
     * ms each on the 3D's core: 2026-10-15). */
    memcpy(regs, io, sizeof(regs));
    if (isA) {
        regs[0x60 / 4] &= ~0x3000u;
    }
    /* MASTER_BRIGHT (0x6C) does not change the split picture: with a split
     * the compositor applies it on the GPU. Hashing it made every fade
     * redraw and republish the whole 320x192 screen for nothing. */
    regs[0x6C / 4] = 0;
    /* (a BG the compositor draws itself: its scroll isn't in the picture,
     * which needn't be drawn again as it scrolls) */
    if (isA) {
        for (int bg = 1; bg < 4; bg++) {
            if (gpuBgMask & (1u << bg)) {
                regs[(0x10 + bg * 4) / 4] = 0;
            }
        }
    }
    h = HashWords((const u8 *)regs, 0x70, h);
    h = HashWords(ioAll + 0x240, 0x10, h);              /* VRAMCNT */
    /* POWCNT1 bit 15 is the display swap-buffers request: raised every
     * scene and cleared by VBlank, it flaps continuously while nothing
     * drawn changes. The L4 retained signature (skipSwap) ignores it; the
     * legacy path keeps the exact bit, where a swap can accompany a new
     * 3D image that engine A must draw. */
    h = (h ^ (Rd16(ioAll, 0x304) & (u32)(skipSwap ? ~0x8000u : ~0u))) * 16777619u;
    h = HashWords((snapshot ? st->pltt : NitroPort_Pltt) + (isA ? 0 : 0x400), skipOam ? 0x200 : 0x400, h);
    if (!skipOam) {
        h = HashWords((snapshot ? st->oam : NitroPort_Oam) + (isA ? 0 : 0x400), 0x400, h);
    }
    /* (an H-blank effect: each line's registers, but those two bits) */
    if (snapshot ? st->lines : NitroPort_LineRegs.active) {
        const u8(*lines)[NITRO_LINE_REGS] = snapshot ? st->lineRegs : NitroPort_LineRegs.regs[isA ? 0 : 1];
        int y;

        h ^= 0x5A5A5A5Au;
        for (y = 0; y < H; y++) {
            memcpy(regs, lines[y], sizeof(regs));
            if (isA) {
                regs[0x60 / 4] &= ~0x3000u;
            }
            regs[0x6C / 4] = 0;   /* (MASTER_BRIGHT: the compositor's) */
            h = HashWords((const u8 *)regs, 0x70, h);
        }
    }
    return h;
}

static int sRefHashB, sRefHashA, sRefHashDiagnostic;
static u64 sRefHashBytes, sFullHashBytes;
static unsigned sRefHashDisagreements;
/* Half-A L4: engine B's signature split into per-part pieces. rest (the
 * caller) covers registers, BG palettes and layout; these cover one BG's
 * registers, map and referenced tiles each, the OBJ tiles, and the OBJ and
 * extended palettes. parts == NULL keeps the legacy combined hash below
 * byte-identical; otherwise only the pieces are filled (the combined value
 * is then meaningless and not returned). */
typedef struct PartialBParts {
    u32 bgReg[4];
    u32 bgMap[4];
    u32 bgChar[4];
    u32 objTiles;
    u32 objPal;
    u32 extPal;
} PartialBParts;
static u32 EngineBReferencedSignature(const u8 *io, const u8 *oam, const u8 *pltt, int *modelled, PartialBParts *parts);
static u32 EngineReferencedSignature(int engine, const u8 *io, const u8 *oam,
                                     const u8 *pltt, int *modelled);
static u32 EngineFullVramSignature(int engine, const u8 *io);

static u32 EngineVramSignature(int engine, const u8 *io, const u8 *oam)
{
    const u8 *pltt = oam == sState[engine].oam ? sState[engine].pltt : NitroPort_Pltt;

    /* The bytes the frame's layers can read, when that is modelled here;
     * every byte of VRAM otherwise. */
    if (engine == 0 ? sRefHashA : sRefHashB) {
        int modelled = 1;
        u32 ref = EngineReferencedSignature(engine, io, oam, pltt, &modelled);

        if (modelled) {
            return ref;
        }
    }
    return EngineFullVramSignature(engine, io);
}

/* P3: the referenced-range signature only. Unlike EngineVramSignature this
 * never falls back to the full-VRAM hash: a frame the referenced set does
 * not model is reported as "not modelled" and the caller keeps today's
 * path (hashing 864 KB to prove "nothing changed" costs as much as the
 * pass it would replace). */
static int EngineReferencedOnly(int engine, const u8 *io, const u8 *oam, u32 *out)
{
    const u8 *pltt = oam == sState[engine].oam ? sState[engine].pltt : NitroPort_Pltt;
    int modelled = 1;
    u32 ref = EngineReferencedSignature(engine, io, oam, pltt, &modelled);

    if (!modelled) {
        return 0;
    }
    *out = ref;
    return 1;
}

/* L3: engine B's reference set.  This deliberately follows the same address
 * calculations as the rasterizer: map extents, the tiles named by those
 * maps, enabled extended palettes, and graphics for visible OAM entries.
 * OAM/register changes remain in EngineSmallSignature, so affine matrices and
 * the attr0 bit-9 affine-vs-hidden distinction are not lost here. */
static u32 RefHashRange(u32 h, const u8 *p, u32 off, u32 bytes, u32 mask)
{
    while (bytes != 0) {
        u32 at = off & mask;
        u32 n = bytes;
        u32 until = (mask + 1u) - at;

        if (n > until) n = until;
        sRefHashBytes += n;
        h = HashWords(p + at, n, h);
        off += n;
        bytes -= n;
    }
    return h;
}

static u32 EngineBReferencedSignature(const u8 *io, const u8 *oam, const u8 *pltt, int *modelled, PartialBParts *parts)
{
    const u8 *bg = NitroPort_DbBgVram, *obj = NitroPort_DbObjVram;
    u32 h = 2166136261u, dispcnt = Rd32(io, 0), bgMask = 0x1FFFF, objMask = 0x1FFFF;
    int b;
    static const u8 size[3][4][2] = {
        {{8,8},{16,16},{32,32},{64,64}},
        {{16,8},{32,8},{32,16},{64,32}},
        {{8,16},{8,32},{16,32},{32,64}}
    };

    /* Only the graphics-display mode with text/affine/extended BGs is
     * modelled. Anything else keeps the full-hash path above. */
    if (((dispcnt >> 16) & 3) != 1 || (dispcnt & 7) > 5) {
        *modelled = 0;
        return 0;
    }
    if (parts != NULL) {
        parts->objTiles = 2166136261u;
        parts->extPal = 2166136261u;
        for (b = 0; b < 4; b++) {
            parts->bgReg[b] = parts->bgMap[b] = parts->bgChar[b] = 0;
        }
    }

    for (b = 0; b < 4; b++) {
        u16 cnt;
        int kind, bpp8, mapW, mapH;
        u32 map, chars, mapBytes, maxTile = 0, i;

        if (!(dispcnt & (0x100u << b))) continue;
        cnt = Rd16(io, 0x08 + b * 2);
        /* B has no 3D BG, and these are the only memory-backed kinds. */
        kind = (dispcnt & 7) == 1 ? (b < 3 ? BG_TEXT : BG_AFFINE) :
               (dispcnt & 7) == 2 ? (b < 2 ? BG_TEXT : BG_AFFINE) :
               (dispcnt & 7) == 3 ? (b < 3 ? BG_TEXT : BG_EXTENDED) :
               (dispcnt & 7) == 4 ? (b < 2 ? BG_TEXT : (b == 2 ? BG_AFFINE : BG_EXTENDED)) :
               (dispcnt & 7) == 5 ? (b < 2 ? BG_TEXT : BG_EXTENDED) : BG_TEXT;
        if (kind == BG_NONE || kind == BG_LARGE) {
            *modelled = 0;
            return 0;
        }
        /* Extended bitmap BGs use 16 KiB bitmap bases and can span more
         * than the tiled character/map ranges below. Keep the full hash. */
        if (kind == BG_EXTENDED && (cnt & 0x80)) {
            *modelled = 0;
            return 0;
        }
        bpp8 = kind == BG_TEXT ? ((cnt & 0x80) != 0) : 1;
        if (kind == BG_TEXT) {
            mapW = sTextSizes[cnt >> 14][0]; mapH = sTextSizes[cnt >> 14][1];
        } else {
            mapW = mapH = 128 << (cnt >> 14);
        }
        map = ScreenBase(&(Engine){0}, cnt, dispcnt);
        chars = CharBase(&(Engine){0}, cnt, dispcnt);
        mapBytes = (u32)(mapW / 8) * (u32)(mapH / 8) * (kind == BG_AFFINE ? 1u : 2u);
        if (parts != NULL) {
            /* One BG's own registers (its BGCNT and scroll; affine
             * parameters and everything else stay in the caller's rest). */
            u32 reg[2];
            reg[0] = (u32)cnt | ((u32)Rd16(io, 0x10 + b * 2) << 16);
            reg[1] = (u32)Rd16(io, 0x12 + b * 2);
            parts->bgReg[b] = HashWords((const u8 *)reg, sizeof(reg), 2166136261u);
            parts->bgMap[b] = RefHashRange(2166136261u, bg, map, mapBytes, bgMask);
            for (i = 0; i < mapBytes / (kind == BG_AFFINE ? 1u : 2u); i++) {
                u32 tile = kind == BG_AFFINE ? bg[(map + i) & bgMask] :
                           Rd16(bg, (map + i * 2) & bgMask) & 0x3ffu;
                if (tile > maxTile) maxTile = tile;
            }
            parts->bgChar[b] = RefHashRange(2166136261u, bg, chars, (maxTile + 1) * (bpp8 ? 64u : 32u), bgMask);
            continue;
        }
        h = RefHashRange(h, bg, map, mapBytes, bgMask);
        for (i = 0; i < mapBytes / (kind == BG_AFFINE ? 1u : 2u); i++) {
            u32 tile = kind == BG_AFFINE ? bg[(map + i) & bgMask] :
                       Rd16(bg, (map + i * 2) & bgMask) & 0x3ffu;
            if (tile > maxTile) maxTile = tile;
        }
        h = RefHashRange(h, bg, chars, (maxTile + 1) * (bpp8 ? 64u : 32u), bgMask);
        /* Palette contents are also referenced ranges. Engine B's BG and
         * OBJ palettes both live in the second KiB; hashing all of it is
         * conservative but never misses a pixel (OBJ palette changes alone
         * must flip the signature). */
        h = RefHashRange(h, pltt + 0x400, 0, 0x400, 0x7FF);
    }
    if (parts != NULL) {
        /* The BG palette (the second KiB's first half) is the caller's
         * rest through the small signature; the OBJ half and the enabled
         * extended banks are per-part here. */
        parts->objPal = HashWords(pltt + 0x600, 0x200, 2166136261u);
    }

    /* B extended palettes live in H/I LCDC.  A slot is selected by VRAMCNT;
     * hashing the enabled banks is cheaper than the old 0xC000 sweep and is
     * conservative for palette-index changes. The wrap mask must keep the
     * H bank's bit 16 (0x98000 & 0xAFFFF would alias it onto 0x88000,
     * missing its changes): 0xBFFFF holds both banks' bytes. */
    for (b = 7; b < 9; b++) {
        u8 cnt = BankCnt(io - 0x1000, b);
        if ((cnt & 0x80) && ((cnt & 7) == 2 || (cnt & 7) == 3)) {
            if (parts != NULL) {
                parts->extPal = RefHashRange(parts->extPal, NitroPort_LcdcVram, sBankLcdc[b], 0x4000, 0xBFFFF);
            } else {
                h = RefHashRange(h, NitroPort_LcdcVram, sBankLcdc[b], 0x4000, 0xBFFFF);
            }
        }
    }

    /* Visible OBJ graphics only.  attr0 bit 9 means hidden only when bit 8
     * (affine mode) is clear; in affine mode it is the double-size bit. */
    for (b = 0; b < 128; b++) {
        const u8 *o = oam + 0x400 + b * 8;
        u16 a0 = Rd16(o, 0), a1 = Rd16(o, 2), a2 = Rd16(o, 4);
        int shape = (a0 >> 14) & 3, affine = (a0 & 0x100) != 0;
        int w, hh, x, y, boxW, boxH, bpp8;
        u32 addr, bytes, unit;
        if ((!affine && (a0 & 0x200)) || shape == 3) continue;
        w = size[shape][(a1 >> 14) & 3][0]; hh = size[shape][(a1 >> 14) & 3][1];
        boxW = affine && (a0 & 0x200) ? w * 2 : w;
        boxH = affine && (a0 & 0x200) ? hh * 2 : hh;
        x = a1 & 0x1FF; if (x >= 256) x -= 512; y = a0 & 0xFF;
        if (x >= 256 || x + boxW <= 0 || (y >= 192 && y + boxH <= 192)) continue;
        bpp8 = (a0 & 0x2000) != 0;
        if (a0 & 0x1000) { /* not possible in attr0; retained for clarity */ }
        if (dispcnt & 0x10) {
            unit = 32u << ((dispcnt >> 20) & 3);
            addr = (u32)(a2 & 0x3FF) * unit;
            bytes = (u32)(w / 8) * (u32)(hh / 8) * (bpp8 ? 64u : 32u);
        } else {
            addr = (u32)(a2 & 0x3FF) * 32u;
            bytes = (u32)(hh / 8) * 1024u + (u32)(w / 8) * (bpp8 ? 64u : 32u);
        }
        if (parts != NULL) {
            parts->objTiles = RefHashRange(parts->objTiles, obj, addr, bytes, objMask);
            continue;
        }
        h = RefHashRange(h, obj, addr, bytes, objMask);
    }
    return h;
}

void NitroPort_Render2DSetReferenceHash(int enabled, int diagnostic)
{
    sRefHashB = enabled != 0;
    sRefHashDiagnostic = diagnostic != 0;
    if (sRefHashDiagnostic)
        NitroPort_Log("[2d] refHashDiag=on (reference/full change cross-check)");
}

void NitroPort_Render2DSetReferenceHashA(int enabled)
{
    sRefHashA = enabled != 0;
    if (sRefHashA)
        NitroPort_Log("[2d] refHashA=on (referenced engine-A ranges)");
}

/* L4a: engine A's reference set -- the same idea as engine B's above, on
 * engine A's banks. The change detector used to hash 864 KB of BG/OBJ VRAM
 * and LCDC on every drawer pass to prove "nothing changed"; that is the
 * drawer's whole cost on an Old 3DS (the rasterizer draws no lines in a
 * still picture, yet the pass takes 50 ms). Only the bytes the display can
 * actually read are hashed: each enabled BG's map and the characters that
 * map names, the extended palette banks the display has mapped, and the
 * tiles of the sprites OAM shows.
 *
 * Exactness: a byte outside this set cannot be read by this frame's layers,
 * so it cannot change the picture. Anything not modelled here (bitmap
 * modes, the large BG, bitmap OBJ, a VRAM/main-memory display) sets
 * *modelled = 0 and the caller keeps the full hash. */
static u32 EngineReferencedSignature(int engine, const u8 *io, const u8 *oam,
                                     const u8 *pltt, int *modelled)
{
    Engine eng;
    const u8 *bg = engine == 0 ? NitroPort_BgVram : NitroPort_DbBgVram;
    const u8 *obj = engine == 0 ? NitroPort_ObjVram : NitroPort_DbObjVram;
    u32 h = 2166136261u, dispcnt = Rd32(io, 0);
    u32 bgMask = engine == 0 ? 0x7FFFFu : 0x1FFFFu;
    u32 objMask = engine == 0 ? 0x3FFFFu : 0x1FFFFu;
    int b;

    *modelled = 1;
    memset(&eng, 0, sizeof(eng));
    eng.isA = engine == 0;
    eng.io = io;
    eng.ioAll = io - (engine ? 0x1000 : 0);
    eng.oam = oam + (engine ? 0x400 : 0);

    /* The tiled BGs of the graphics display only. */
    if (((dispcnt >> 16) & 3) != 1 || (dispcnt & 7) > 5) {
        *modelled = 0;
        return 0;
    }
    for (b = 0; b < 4; b++) {
        u16 cnt;
        int kind, bpp8, mapW, mapH;
        u32 map, chars, mapBytes, maxTile = 0, i;

        if (!(dispcnt & (0x100u << b))) {
            continue;
        }
        cnt = Rd16(io, 0x08 + b * 2);
        kind = BgKind(&eng, b, dispcnt);
        if (kind == BG_NONE || kind == BG_3D) {
            continue;                       /* (the 3D reads no VRAM) */
        }
        if (kind == BG_LARGE || (kind == BG_EXTENDED && (cnt & 0x80))) {
            *modelled = 0;                  /* (bitmaps keep the full hash) */
            return 0;
        }
        bpp8 = kind == BG_TEXT ? ((cnt & 0x80) != 0) : 1;
        if (kind == BG_TEXT) {
            mapW = sTextSizes[cnt >> 14][0];
            mapH = sTextSizes[cnt >> 14][1];
        } else {
            mapW = mapH = 128 << (cnt >> 14);
        }
        map = ScreenBase(&eng, cnt, dispcnt);
        chars = CharBase(&eng, cnt, dispcnt);
        mapBytes = (u32)(mapW / 8) * (u32)(mapH / 8) * (kind == BG_AFFINE ? 1u : 2u);
        h = RefHashRange(h, bg, map, mapBytes, bgMask);
        for (i = 0; i < mapBytes / (kind == BG_AFFINE ? 1u : 2u); i++) {
            u32 tile = kind == BG_AFFINE ? bg[(map + i) & bgMask]
                                         : Rd16(bg, (map + i * 2) & bgMask) & 0x3FFu;
            if (tile > maxTile) {
                maxTile = tile;
            }
        }
        h = RefHashRange(h, bg, chars, (maxTile + 1) * (bpp8 ? 64u : 32u), bgMask);
    }
    /* Engine A's palettes (both halves) are already in the small signature. */
    if (engine != 0) {
        h = RefHashRange(h, pltt + 0x400, 0, 0x400, 0x7FF);
    }
    /* The extended palette banks the display has mapped: A's E-G hold BG
     * slots and the OBJ palette; B's H/I. One bank each, hashed whole. */
    {
        int first = engine == 0 ? 4 : 7, end = engine == 0 ? 7 : 9;

        for (b = first; b < end; b++) {
            u8 cnt = BankCnt(eng.ioAll, b);
            int mst = cnt & 7;
            int used = engine == 0 ? (mst == 4 || mst == 5) : (mst == 2 || mst == 3);

            if ((cnt & 0x80) && used) {
                h = RefHashRange(h, NitroPort_LcdcVram, sBankLcdc[b], 0x4000, 0xBFFFF);
            }
        }
    }
    /* The tiles of every sprite OAM shows, addressed exactly as the
     * rasterizer addresses them. */
    for (b = 0; b < 128; b++) {
        Obj o;
        int tx, ty;

        if (!ReadObj(&eng, b, &o)) {
            continue;
        }
        if (o.mode == 3) {
            *modelled = 0;                  /* (bitmap OBJ: the full hash) */
            return 0;
        }
        for (ty = 0; ty < o.boxH; ty += 8) {
            for (tx = 0; tx < o.boxW; tx += 8) {
                u32 at = ObjCharAddr(&o, dispcnt, tx, ty) & objMask;

                h = RefHashRange(h, obj, at, o.bpp8 ? 64u : 32u, objMask);
            }
        }
    }
    return h;
}

/* Every byte of VRAM the engine owns: the old change detector, kept for the
 * states the reference set does not model and as the diagnostic's reference. */
static u32 EngineFullVramSignature(int engine, const u8 *io)
{
    u32 h = 2166136261u;

    if (engine == 0) {
        h = HashWords(NitroPort_BgVram, 0x80000, h);
        h = HashWords(NitroPort_ObjVram, 0x40000, h);
        h = HashWords(NitroPort_LcdcVram + 0x80000, 0x18000, h);   /* E, F, G */
        if (((Rd32(io, 0) >> 16) & 3) == 2) {                       /* VRAM display */
            h = HashWords(NitroPort_LcdcVram + ((Rd32(io, 0) >> 18) & 3) * 0x20000, 0x20000, h);
        }
        sFullHashBytes += 0xD8000u;
    } else {
        h = HashWords(NitroPort_DbBgVram, 0x20000, h);
        h = HashWords(NitroPort_DbObjVram, 0x20000, h);
        h = HashWords(NitroPort_LcdcVram + 0x98000, 0xC000, h);    /* H, I */
        sFullHashBytes += 0x4C000u;
    }
    return h;
}

/* The band renderer's range form, defined with Render below. band = 0 is
 * a whole picture (with the L4 retained-state note); band = 1 renders
 * lines [y0, y1) only (y1 < 0: whole) with no retained-state note. */
static void RenderRange(int engine, u16 *out, NitroRender2DSplit *split, u32 reusePixels, int y0, int y1, int band);

/* ------------------------------------------------- Half-A L4: redraw
 * only what changed (engine B).
 *
 * The engine-B signature is split into per-part pieces (registers and
 * layout as rest, one register/map/tile piece per BG, OBJ tiles and
 * palettes). A frame whose parts all match the last rendered frame draws
 * nothing; a frame with only OAM/OBJ-tile changes re-renders the scanline
 * band covering the union of old and new sprite bounds; a frame with one
 * text BG's map changed re-renders the band covering those tiles;
 * anything else (registers, windows, blending, brightness-affecting
 * state, unmodelled modes) renders whole. Untouched lines keep the
 * drawer's buffer, so a band render through the unmodified pipeline is
 * bit-exact by construction: re-rendered lines see current inputs, kept
 * lines' inputs are unchanged. Off by default (port.ini partialB). */
static int sPartialB = 0, sPartialBDiag = 0, sPartialBChar = 0;

/* Bands this tall (of the 192 lines) render whole instead: the band setup
 * costs more than it saves. */
#define PARTIAL_B_MAX_BAND 160

enum { PARTIAL_BAND_NONE = 0, PARTIAL_BAND_FULL = 1, PARTIAL_BAND_PART = 2 };

#ifdef HGSS_A0_DIAG
typedef struct PartialBRestTerms {
    u32 layout, regs, vramCnt, power, palette, lines;
    u32 regWords[28], lineWords[28];
} PartialBRestTerms;
static NitroPartialBReasons sPartialReasons;
#define PARTIAL_REASON(field) (sPartialReasons.field++)
#else
#define PARTIAL_REASON(field) ((void)0)
#endif

static struct {
    int valid;
#ifdef HGSS_A0_DIAG
    PartialBRestTerms restTerms;
#endif
    u32 rest, oamRegs, objTiles, objPal, extPal;
    u32 bgReg[4], bgMap[4], bgChar[4];
    u8 oam[0x400];
    u16 *mapFlat[4];
    u32 mapEntries[4];
    u8 *charData[4];
    u32 charBytes[4], charBase[4];
    const u16 *underPtr;
    int bandKind, bandY0, bandY1;
    u64 frames, objTakes, bgTakes, unchangedZero, fallbacks, partialLines;
    u64 charTakes;
    u64 tryUnmodelled, tryUnderMismatch, refreshAdopts, refreshInvalidates;
    unsigned diagMismatch;
    u16 *diag;   /* 256 x 192 dual-path scratch, diagnostic only */
} sPartial;

static void PartialBFree(void)
{
    int b;

    for (b = 0; b < 4; b++) {
        free(sPartial.mapFlat[b]);
        sPartial.mapFlat[b] = NULL;
        sPartial.mapEntries[b] = 0;
        free(sPartial.charData[b]);
        sPartial.charData[b] = NULL;
        sPartial.charBytes[b] = 0;
    }
    free(sPartial.diag);
    sPartial.diag = NULL;
}

static void PartialBReset(void)
{
    PartialBFree();
    memset(&sPartial, 0, sizeof(sPartial));
#ifdef HGSS_A0_DIAG
    memset(&sPartialReasons, 0, sizeof(sPartialReasons));
#endif
}

void NitroPort_Render2DSetPartialBChar(int enabled)
{
    int on = enabled != 0;
    if (on != sPartialBChar) {
        sPartialBChar = on;
        PartialBReset();
    }
    NitroPort_Log("[2d] partialBChar=%s (text tile dirty bands; default off)", on ? "on" : "off");
}

void NitroPort_Render2DPartialBCharStats(u64 *takes)
{
    if (takes) *takes = sPartial.charTakes;
}

void NitroPort_Render2DSetPartialB(int enabled, int diagnostic)
{
    int on = enabled != 0, diag = diagnostic != 0;

    if (on == sPartialB && diag == sPartialBDiag) {
        return;
    }
    sPartialB = on;
    sPartialBDiag = diag;
    PartialBReset();
    NitroPort_Log("[2d] partialB=%s (engine-B band redraw; default off)", on ? "on" : "off");
    NitroPort_Log("[2d] partialBDiag=%s (dual-path compare; engine B renders twice)", diag ? "on" : "off");
}

void NitroPort_Render2DPartialBStats(u64 *frames, u64 *objTakes, u64 *bgTakes,
                                     u64 *unchangedZero, u64 *fallbacks,
                                     u64 *partialLines, unsigned *diagMismatch)
{
    if (frames) *frames = sPartial.frames;
    if (objTakes) *objTakes = sPartial.objTakes;
    if (bgTakes) *bgTakes = sPartial.bgTakes;
    if (unchangedZero) *unchangedZero = sPartial.unchangedZero;
    if (fallbacks) *fallbacks = sPartial.fallbacks;
    if (partialLines) *partialLines = sPartial.partialLines;
    if (diagMismatch) *diagMismatch = sPartial.diagMismatch;
}

void NitroPort_Render2DPartialBDebug(u64 *tryUnmodelled, u64 *tryUnderMismatch,
                                     u64 *refreshAdopts, u64 *refreshInvalidates,
                                     u64 *epilogueInvalidates, u64 *epilogueEntries,
                                     u64 *tryPostRender, u64 *rangeEntryB)
{
    if (tryUnmodelled) *tryUnmodelled = sPartial.tryUnmodelled;
    if (tryUnderMismatch) *tryUnderMismatch = sPartial.tryUnderMismatch;
    if (refreshAdopts) *refreshAdopts = sPartial.refreshAdopts;
    if (refreshInvalidates) *refreshInvalidates = sPartial.refreshInvalidates;
    if (epilogueInvalidates) *epilogueInvalidates = sPartialEpilogueInvalidates;
    if (epilogueEntries) *epilogueEntries = sPartialEpilogueEntries;
    if (tryPostRender) *tryPostRender = sA0TryPostRender;
    if (rangeEntryB) *rangeEntryB = sA0RangeEntryB;
}

static void PartialBInvalidate(void)
{
    sPartial.valid = 0;
    sPartial.bandKind = PARTIAL_BAND_NONE;
}

/* Whether an OAM entry can draw, and the screen lines it can draw on.
 * Mirrors ReadObj/SortObjs line membership ((y - oy) & 0xFF < boxH; the
 * tall-picture oyTall form never fast-paths): x is ignored, so an
 * x-culled sprite still contributes its lines. A superset is safe (extra
 * lines only cost a re-render); a missing line would be a wrong pixel.
 * FALSE for hidden entries and shape 3. */
static int PartialBObjLines(const u8 *o, int *oy, int *boxH)
{
    u16 a0 = Rd16(o, 0), a1 = Rd16(o, 2);
    int shape = (a0 >> 14) & 3, affine = (a0 & 0x100) != 0;
    int hh;

    if ((!affine && (a0 & 0x200)) || shape == 3) {
        return 0;
    }
    hh = sObjSize[shape][(a1 >> 14) & 3][1];
    *oy = a0 & 0xFF;
    *boxH = affine && (a0 & 0x200) ? hh * 2 : hh;
    return 1;
}

/* Union an entry's lines into [y0, y1). */
static void PartialBAddLines(int oy, int boxH, int *y0, int *y1)
{
    int y;

    for (y = 0; y < H; y++) {
        if (((y - oy) & 0xFF) < boxH) {
            if (y < *y0) {
                *y0 = y;
            }
            if (y + 1 > *y1) {
                *y1 = y + 1;
            }
        }
    }
}

/* Read a text BG's flat entries through the same blocked addressing the
 * software path decodes (each 0x800 block holds the tiles whose top-left
 * 256 pixels are in it). */
static u16 PartialBMapEntry(const u8 *bg, u32 screenBase, int tilesX, int tx, int ty)
{
    return Rd16(bg, (screenBase + (u32)((tx >> 5) + (ty >> 5) * (tilesX >> 5)) * 0x800 +
                        (u32)(((ty & 31) * 32) + (tx & 31)) * 2) & 0x1FFFF);
}

/* The screen lines showing a text BG's changed tiles: tile row ty's map
 * rows, through the frame scroll (H-blank scrolling declines the caller:
 * per-line scrolls would need per-line bands). */
static void PartialBBgBand(int mapH, int vofs, const u16 *retained, const u8 *bg,
                           u32 screenBase, int tilesX, int tilesY, int *y0, int *y1)
{
    int mask = mapH - 1, tx, ty;

    for (ty = 0; ty < tilesY; ty++) {
        for (tx = 0; tx < tilesX; tx++) {
            if (PartialBMapEntry(bg, screenBase, tilesX, tx, ty) != retained[(size_t)ty * tilesX + tx]) {
                int r;

                for (r = ty * 8; r < ty * 8 + 8; r++) {
                    int y = (r - vofs) & mask;

                    if (y < H) {
                        if (y < *y0) {
                            *y0 = y;
                        }
                        if (y + 1 > *y1) {
                            *y1 = y + 1;
                        }
                    }
                }
            }
        }
    }
}

#ifdef HGSS_A0_DIAG
/* Mirror the retained signature's inputs, including all H-blank lines.
 * These hashes are diagnostic only and may identify overlapping causes. */
static PartialBRestTerms PartialBGetRestTerms(int snapshot, u32 layout)
{
    const State2D *st = &sState[1];
    const u8 *ioAll = snapshot ? st->io : NitroPort_IoReg;
    const u8 *pltt = snapshot ? st->pltt : NitroPort_Pltt;
    const u8 (*lines)[NITRO_LINE_REGS] = snapshot ? st->lineRegs : NitroPort_LineRegs.regs[1];
    u32 regs[0x70 / 4], hash = 2166136261u;
    int y, i, active = snapshot ? st->lines : NitroPort_LineRegs.active;
    PartialBRestTerms out;

    memcpy(regs, ioAll + 0x1000, sizeof(regs));
    regs[0x6c / 4] = 0;
    out.layout = layout;
    out.regs = HashWords((const u8 *)regs, sizeof(regs), hash);
    for (i = 0; i < 28; i++) {
        out.regWords[i] = regs[i];
        out.lineWords[i] = active ? 2166136261u : 0;
    }
    out.vramCnt = HashWords(ioAll + 0x240, 0x10, hash);
    out.power = Rd16(ioAll, 0x304) & ~0x8000u;
    out.palette = HashWords(pltt + 0x400, 0x200, hash);
    out.lines = active ? 0x5a5a5a5au : 0;
    if (active) {
        for (y = 0; y < H; y++) {
            memcpy(regs, lines[y], sizeof(regs));
            regs[0x6c / 4] = 0;
            out.lines = HashWords((const u8 *)regs, sizeof(regs), out.lines);
            for (i = 0; i < 28; i++)
                out.lineWords[i] = (out.lineWords[i] ^ regs[i]) * 16777619u;
        }
    }
    return out;
}

void NitroPort_Render2DPartialBReasons(NitroPartialBReasons *out)
{
    if (out != NULL) *out = sPartialReasons;
}
#endif

static void PartialBAdopt(const PartialBParts *parts, u32 rest, u32 oamRegs, const u8 *oamCur,
                          const u16 *under, int bandKind, int bandY0, int bandY1)
{
    int b;

#ifdef HGSS_A0_DIAG
    {
        int snapshot = oamCur == sState[1].oam + 0x400;
        /* The layout is supplied by the same split used for rest. Its
         * component is filled by the caller immediately before adoption. */
        u32 layout = sPartial.restTerms.layout;
        sPartial.restTerms = PartialBGetRestTerms(snapshot, layout);
    }
#endif
    sPartial.rest = rest;
    sPartial.oamRegs = oamRegs;
    sPartial.objTiles = parts->objTiles;
    sPartial.objPal = parts->objPal;
    sPartial.extPal = parts->extPal;
    for (b = 0; b < 4; b++) {
        sPartial.bgReg[b] = parts->bgReg[b];
        sPartial.bgMap[b] = parts->bgMap[b];
        sPartial.bgChar[b] = parts->bgChar[b];
    }
    memcpy(sPartial.oam, oamCur, 0x400);
    sPartial.underPtr = under;
    sPartial.valid = 1;
    sPartial.bandKind = bandKind;
    sPartial.bandY0 = bandY0;
    sPartial.bandY1 = bandY1;
}

/* Snapshot the flat entries of an enabled text BG (for the next content
 * diff), or drop them when the BG is gone. */
static void PartialBKeepMap(int bg, int enabled, int text, u32 screenBase, int tilesX, int tilesY)
{
    u32 entries = enabled && text ? (u32)tilesX * (u32)tilesY : 0;
    int tx, ty;

    if (entries != sPartial.mapEntries[bg]) {
        free(sPartial.mapFlat[bg]);
        sPartial.mapFlat[bg] = entries != 0 ? (u16 *)malloc((size_t)entries * sizeof(u16)) : NULL;
        sPartial.mapEntries[bg] = (sPartial.mapFlat[bg] != NULL || entries == 0) ? entries : 0;
    }
    if (sPartial.mapEntries[bg] == 0) {
        return;
    }
    for (ty = 0; ty < tilesY; ty++) {
        for (tx = 0; tx < tilesX; tx++) {
            sPartial.mapFlat[bg][(size_t)ty * tilesX + tx] =
                PartialBMapEntry(NitroPort_DbBgVram, screenBase, tilesX, tx, ty);
        }
    }
}

/* Retain the exact text character bytes, not hashes: a changed tile can
 * then be mapped to every screen row that references it. Allocation failure
 * leaves the whole-render path in charge. */
static void PartialBKeepChars(int bg, int text, u32 base, u32 bytes)
{
    u32 first;
    if (!sPartialBChar || !text) bytes = 0;
    if (bytes != sPartial.charBytes[bg]) {
        free(sPartial.charData[bg]);
        sPartial.charData[bg] = bytes ? (u8 *)malloc(bytes) : NULL;
        sPartial.charBytes[bg] = sPartial.charData[bg] ? bytes : 0;
    }
    if (!sPartial.charData[bg]) return;
    base &= 0x1ffffu;
    first = 0x20000u - base;
    if (first > bytes) first = bytes;
    memcpy(sPartial.charData[bg], NitroPort_DbBgVram + base, first);
    if (first < bytes) memcpy(sPartial.charData[bg] + first, NitroPort_DbBgVram, bytes - first);
    sPartial.charBase[bg] = base;
}

/* Returns -1 if the retained map does not match, otherwise the union of
 * visible rows whose entries reference changed character bytes. */
static int PartialBCharBand(int bg, u32 base, u32 tileBytes, int mapH, int vofs,
                            u32 screenBase, int tilesX, int tilesY, int *y0, int *y1)
{
    u8 changed[1024];
    int tx, ty;
    u32 tile;
    const u8 *vram = NitroPort_DbBgVram;
    const u16 *map = sPartial.mapFlat[bg];
    if (!map || !sPartial.charData[bg] || sPartial.charBytes[bg] != tileBytes * 1024u ||
        sPartial.charBase[bg] != (base & 0x1ffffu)) return -1;
    for (tile = 0; tile < 1024; tile++) {
        u32 off = (base + tile * tileBytes) & 0x1ffffu;
        /* Text character bases and 32/64-byte tiles align to the VRAM wrap. */
        changed[tile] = memcmp(vram + off, sPartial.charData[bg] + tile * tileBytes, tileBytes) != 0;
    }
    for (ty = 0; ty < tilesY; ty++) {
        for (tx = 0; tx < tilesX; tx++) {
            u16 entry = PartialBMapEntry(vram, screenBase, tilesX, tx, ty);
            if (entry != map[(size_t)ty * tilesX + tx]) return -1;
            if (changed[entry & 0x3ffu]) {
                for (int r = ty * 8; r < ty * 8 + 8; r++) {
                    int y = (r - vofs) & (mapH - 1);
                    if (y < H) {
                        if (y < *y0) *y0 = y;
                        if (y + 1 > *y1) *y1 = y + 1;
                    }
                }
            }
        }
    }
    return 0;
}

/* Dual-render every band kind, including BG bands, when requested. */
static int PartialBDiagBand(u16 *under, NitroRender2DSplit *split)
{
    if (!sPartialBDiag) return 1;
    if (!sPartial.diag) sPartial.diag = (u16 *)malloc((size_t)W * H * sizeof(u16));
    if (!sPartial.diag) return 0;
    RenderRange(1, sPartial.diag, split, R2D_TRY_FLAT, 0, -1, 1);
    if (memcmp(under, sPartial.diag, (size_t)W * H * sizeof(u16)) == 0) return 1;
    sPartial.diagMismatch++;
    NitroPort_Log("[2d] partialBDiag mismatch #%u", sPartial.diagMismatch);
    return 0;
}

/* Note a whole engine-B render in the retained state (called from the
 * range renderer, which knows the Engine the picture was drawn with). */
static void PartialBRefresh(const Engine *e, NitroRender2DSplit *split, const u16 *under)
{
    int snapshot = e->taken != NULL ? 1 : 0;
    const State2D *st = &sState[1];
    const u8 *oamBase = snapshot ? st->oam : NitroPort_Oam;
    u32 layout = SplitLayoutSignature(1, split);
    PartialBParts parts;
    int modelled = 1, b;
    u32 dispcnt = Rd32(e->io, 0);

    (void)EngineBReferencedSignature(e->io, oamBase, snapshot ? st->pltt : NitroPort_Pltt, &modelled, &parts);
    if (!modelled) {
        sPartial.refreshInvalidates++;
        PartialBInvalidate();
        return;
    }
    sPartial.refreshAdopts++;
#ifdef HGSS_A0_DIAG
    sPartial.restTerms.layout = layout;
#endif
    PartialBAdopt(&parts, EngineSmallSignature(1, snapshot, 0, 1, 1) * 31u ^ layout,
                  HashWords(oamBase + 0x400, 0x400, 2166136261u), oamBase + 0x400, under,
                  PARTIAL_BAND_FULL, 0, 0);
    for (b = 0; b < 4; b++) {
        u16 cnt = Rd16(e->io, 0x08 + b * 2);
        int enabled = (dispcnt & (0x100u << b)) != 0;
        int kind = enabled ? BgKind(&(Engine){0}, b, dispcnt) : BG_NONE;
        int text = kind == BG_TEXT;
        u32 screenBase = enabled && text ? ScreenBase(&(Engine){0}, cnt, dispcnt) : 0;
        int mapW = enabled && text ? sTextSizes[(cnt >> 14) & 3][0] : 0;
        int mapH = enabled && text ? sTextSizes[(cnt >> 14) & 3][1] : 0;

        PartialBKeepMap(b, enabled, text, screenBase, mapW >> 3, mapH >> 3);
        PartialBKeepChars(b, text, CharBase(&(Engine){0}, cnt, dispcnt),
                          (cnt & 0x80) ? 65536u : 32768u);
    }
}

/* The L5 handoff: the engine-B band the last L4 decision drew (PART), the
 * whole picture (FULL), or nothing claimable (NONE: legacy or fill path).
 * Set on the drawer thread and read by the same thread's publish; the
 * publish band diff stays authoritative. */
void NitroPort_Render2DPartialBand(int engine, int *kind, int *y0, int *y1)
{
    int isB = (engine ? 1 : 0) == 1;

    if (kind) {
        *kind = isB ? sPartial.bandKind : PARTIAL_BAND_NONE;
    }
    if (y0) {
        *y0 = isB ? sPartial.bandY0 : 0;
    }
    if (y1) {
        *y1 = isB ? sPartial.bandY1 : 0;
    }
}

/* Engine B's change decision with the split signature. 1: drawn (band or
 * whole); 0: unchanged; -1: not L4's case (the legacy logic draws whole). */
static int PartialBTry(u16 *under, NitroRender2DSplit *split)
{
    const State2D *st = &sState[1];
    int snapshot = split->snapshot ? 1 : 0;
    const u8 *ioBase = snapshot ? st->io : NitroPort_IoReg;
    const u8 *oamBase = snapshot ? st->oam : NitroPort_Oam;
    const u8 *io = ioBase + 0x1000, *oam = oamBase + 0x400;
    u32 dispcnt = Rd32(io, 0);
    u32 layout = SplitLayoutSignature(1, split);
    u32 rest = EngineSmallSignature(1, snapshot, 0, 1, 1) * 31u ^ layout;
    u32 oamRegs = HashWords(oam, 0x400, 2166136261u);
    PartialBParts parts;
    int modelled = 1, b, changedBg = -1, oamSame;

    (void)EngineBReferencedSignature(io, oamBase, snapshot ? st->pltt : NitroPort_Pltt, &modelled, &parts);
    /* A fill, not a picture: power off, forced blank. The small signature
     * already changes with these, but a band render cannot reproduce a
     * fill, so they never fast-path. */
    if (!(Rd16(ioBase, 0x304) & 0x0200) || (dispcnt & 0x80)) {
        modelled = 0;
    }
    /* The band renderer draws the DS's 256 x 192 picture only. A wider or
     * taller engine-B picture (the top screen's layout) renders whole. */
    if (split->width > W || split->height > H) {
        modelled = 0;
    }
    if (!modelled) {
        sPartial.tryUnmodelled++;
        return -1;
    }
    if (sPartial.underPtr != under) {
        sPartial.tryUnderMismatch++;
        return -1;
    }
    sPartial.frames++;
    if (!sPartial.valid) {
        Render(1, under, split, R2D_TRY_FLAT);
#ifdef HGSS_A0_DIAG
        sA0TryPostRender++;
#endif
        return 1;
    }
    if (rest != sPartial.rest || parts.extPal != sPartial.extPal) {
        /* Registers, windows, blending, palettes, layout: whole. */
#ifdef HGSS_A0_DIAG
        {
            PartialBRestTerms now = PartialBGetRestTerms(snapshot, layout);
            const PartialBRestTerms *old = &sPartial.restTerms;
            int changed = 0;
#define REST_TERM(field, counter) do { if (now.field != old->field) { PARTIAL_REASON(counter); changed = 1; } } while (0)
            PARTIAL_REASON(shared);
            if (rest != sPartial.rest) {
                PARTIAL_REASON(rest);
                REST_TERM(layout, restLayout);
                REST_TERM(regs, restRegs);
                REST_TERM(vramCnt, restVramCnt);
                REST_TERM(power, restPower);
                REST_TERM(palette, restPalette);
                REST_TERM(lines, restLines);
                for (int i = 0; i < 28; i++) {
                    if (now.regWords[i] != old->regWords[i]) {
                        sPartialReasons.regWords[i]++;
                        sPartialReasons.regChangedBits[i] |= now.regWords[i] ^ old->regWords[i];
                        sPartialReasons.regBefore[i] = old->regWords[i];
                        sPartialReasons.regAfter[i] = now.regWords[i];
                    }
                    if (now.lineWords[i] != old->lineWords[i]) sPartialReasons.lineWords[i]++;
                }
                if (!changed) PARTIAL_REASON(restOther);
            }
            if (parts.extPal != sPartial.extPal) PARTIAL_REASON(extPal);
#undef REST_TERM
        }
#endif
        sPartial.fallbacks++;
        Render(1, under, split, R2D_TRY_FLAT);
        return 1;
    }
    oamSame = oamRegs == sPartial.oamRegs && parts.objTiles == sPartial.objTiles &&
              parts.objPal == sPartial.objPal;
    for (b = 0; b < 4; b++) {
        if (parts.bgReg[b] != sPartial.bgReg[b] || parts.bgMap[b] != sPartial.bgMap[b] ||
            parts.bgChar[b] != sPartial.bgChar[b]) {
            changedBg = changedBg == -1 ? b : -2;
        }
    }
    if (oamSame && changedBg == -1) {
        /* Nothing changed: the legacy path's unchanged return, without
         * hashing the VRAM again. */
        sPartial.bandKind = PARTIAL_BAND_NONE;
        sPartial.unchangedZero++;
        return 0;
    }
    if (!oamSame && changedBg == -1) {
        /* OBJ-only change: the icon-animation case. */
        int y0 = H, y1 = 0, i;

        for (i = 0; i < 128; i++) {
            int oy, boxH;

            if (memcmp(oam + (size_t)i * 8, sPartial.oam + (size_t)i * 8, 8) == 0) {
                continue;
            }
            if (PartialBObjLines(oam + (size_t)i * 8, &oy, &boxH)) {
                PartialBAddLines(oy, boxH, &y0, &y1);
            }
            if (PartialBObjLines(sPartial.oam + (size_t)i * 8, &oy, &boxH)) {
                PartialBAddLines(oy, boxH, &y0, &y1);
            }
        }
        if (parts.objTiles != sPartial.objTiles || parts.objPal != sPartial.objPal) {
            /* Tiles or palette changed under the sprites too: an unchanged
             * sprite may still show new pixels. Cover every visible
             * sprite's lines as well (unioned with the changed entries'
             * lines above, never replacing them); with none visible the
             * picture is unchanged. */
            for (i = 0; i < 128; i++) {
                int oy, boxH;

                if (PartialBObjLines(oam + (size_t)i * 8, &oy, &boxH)) {
                    PartialBAddLines(oy, boxH, &y0, &y1);
                }
            }
        }
        if (y0 >= y1) {
#ifdef HGSS_A0_DIAG
            sPartial.restTerms.layout = layout;
#endif
            PartialBAdopt(&parts, rest, oamRegs, oam, under, PARTIAL_BAND_NONE, 0, 0);
            sPartial.unchangedZero++;
            return 0;
        }
        if (y1 - y0 >= PARTIAL_B_MAX_BAND) {
            PARTIAL_REASON(objWide);
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        RenderRange(1, under, split, R2D_TRY_FLAT, y0, y1, 1);
        if (!PartialBDiagBand(under, split)) {
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
#ifdef HGSS_A0_DIAG
        sPartial.restTerms.layout = layout;
#endif
        PartialBAdopt(&parts, rest, oamRegs, oam, under, PARTIAL_BAND_PART, y0, y1);
        sPartial.objTakes++;
        sPartial.partialLines += (u64)(y1 - y0);
        return 1;
    }
    if (oamSame && changedBg >= 0) {
        /* One BG's content changed: localize a text BG's tiles. */
        u16 cnt = Rd16(io, 0x08 + changedBg * 2);
        int kind = BgKind(&(Engine){0}, changedBg, dispcnt);
        int mapW, mapH, tilesX, tilesY, vofs, y0 = H, y1 = 0;
        u32 screenBase;

        if (kind != BG_TEXT) {
            PARTIAL_REASON(bgKind);
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        if (parts.bgReg[changedBg] != sPartial.bgReg[changedBg]) {
            PARTIAL_REASON(bgRegs);
            /* Its scroll or control moved: whole. */
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        if (snapshot ? st->lines : NitroPort_LineRegs.active) {
            /* Per-line scrolls: whole. */
            PARTIAL_REASON(bgLines);
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        if (parts.bgChar[changedBg] != sPartial.bgChar[changedBg]) {
            u32 charBase = CharBase(&(Engine){0}, cnt, dispcnt);
            u32 tileBytes = (cnt & 0x80) ? 64u : 32u;
            int charY0 = H, charY1 = 0;

            mapW = sTextSizes[(cnt >> 14) & 3][0];
            mapH = sTextSizes[(cnt >> 14) & 3][1];
            tilesX = mapW >> 3;
            tilesY = mapH >> 3;
            vofs = Rd16(io, 0x12 + changedBg * 4) & 0x1ff;
            screenBase = ScreenBase(&(Engine){0}, cnt, dispcnt);
            /* Mosaic spreads source rows into adjacent display rows. A map
             * change or missing snapshot also declines the localization. */
            if (sPartialBChar && !(cnt & 0x40) &&
                parts.bgMap[changedBg] == sPartial.bgMap[changedBg] &&
                PartialBCharBand(changedBg, charBase, tileBytes, mapH, vofs,
                                 screenBase, tilesX, tilesY, &charY0, &charY1) == 0 &&
                charY1 - charY0 < PARTIAL_B_MAX_BAND) {
                if (charY0 < charY1) {
                    RenderRange(1, under, split, R2D_TRY_FLAT, charY0, charY1, 1);
                    if (!PartialBDiagBand(under, split)) {
                        Render(1, under, split, R2D_TRY_FLAT);
                        return 1;
                    }
                }
#ifdef HGSS_A0_DIAG
                sPartial.restTerms.layout = layout;
#endif
                PartialBAdopt(&parts, rest, oamRegs, oam, under,
                              charY0 < charY1 ? PARTIAL_BAND_PART : PARTIAL_BAND_NONE,
                              charY0 < charY1 ? charY0 : 0, charY0 < charY1 ? charY1 : 0);
                PartialBKeepChars(changedBg, 1, charBase, tileBytes * 1024u);
                if (charY0 >= charY1) {
                    sPartial.unchangedZero++;
                    return 0;
                }
                sPartial.charTakes++;
                sPartial.bgTakes++;
                sPartial.partialLines += (u64)(charY1 - charY0);
                return 1;
            }
            PARTIAL_REASON(bgChar);
#ifdef HGSS_A0_DIAG
            sPartialReasons.bgCharLayer[changedBg]++;
#endif
            /* Tile pixels changed without the map: not localized yet. */
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        if (sPartial.mapFlat[changedBg] == NULL) {
            PARTIAL_REASON(bgNoMap);
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        mapW = sTextSizes[(cnt >> 14) & 3][0];
        mapH = sTextSizes[(cnt >> 14) & 3][1];
        tilesX = mapW >> 3;
        tilesY = mapH >> 3;
        vofs = Rd16(io, 0x12 + changedBg * 4) & 0x1FF;
        screenBase = ScreenBase(&(Engine){0}, cnt, dispcnt);
        PartialBBgBand(mapH, vofs, sPartial.mapFlat[changedBg], NitroPort_DbBgVram,
                       screenBase, tilesX, tilesY, &y0, &y1);
        if (y0 >= y1) {
            /* The map hash moved but no entry did: whole (safe). */
            PARTIAL_REASON(bgNoBand);
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        if (y1 - y0 >= PARTIAL_B_MAX_BAND) {
            PARTIAL_REASON(bgWide);
            sPartial.fallbacks++;
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
        RenderRange(1, under, split, R2D_TRY_FLAT, y0, y1, 1);
        if (!PartialBDiagBand(under, split)) {
            Render(1, under, split, R2D_TRY_FLAT);
            return 1;
        }
#ifdef HGSS_A0_DIAG
        sPartial.restTerms.layout = layout;
#endif
        PartialBAdopt(&parts, rest, oamRegs, oam, under, PARTIAL_BAND_PART, y0, y1);
        PartialBKeepMap(changedBg, 1, 1, screenBase, tilesX, tilesY);
        sPartial.bgTakes++;
        sPartial.partialLines += (u64)(y1 - y0);
        return 1;
    }
    /* Both changed, or anything else: whole. */
    PARTIAL_REASON(mixed);
    sPartial.fallbacks++;
    Render(1, under, split, R2D_TRY_FLAT);
    return 1;
}

void NitroPort_Render2DRefHashStats(u64 *refBytes, u64 *fullBytes, unsigned *disagreements)
{
    if (refBytes) *refBytes = sRefHashBytes;
    if (fullBytes) *fullBytes = sFullHashBytes;
    if (disagreements) *disagreements = sRefHashDisagreements;
}

/* P3: what the flat passes did. skipped: the picture was the same and the
 * pass was skipped. redrawn: it changed, or the frame was not modelled by
 * the referenced-range signature (notModelled counts those). The
 * diagnostic's disagreements: a pass that published something else than
 * the picture the skip would have kept. The why* fields say which input
 * differed when a pass did not skip. Cumulative: the caller differences
 * them. */
void NitroPort_Render2DFlatStats(NitroFlatStats *out)
{
    if (out == NULL) {
        return;
    }
    out->skipped = __atomic_load_n(&sFlatSkip[0], __ATOMIC_RELAXED) +
                   __atomic_load_n(&sFlatSkip[1], __ATOMIC_RELAXED);
    out->redrawn = __atomic_load_n(&sFlatRedraw[0], __ATOMIC_RELAXED) +
                   __atomic_load_n(&sFlatRedraw[1], __ATOMIC_RELAXED);
    out->notModelled = __atomic_load_n(&sFlatNotModelled[0], __ATOMIC_RELAXED) +
                       __atomic_load_n(&sFlatNotModelled[1], __ATOMIC_RELAXED);
    out->disagreements = __atomic_load_n(&sFlatDisagree[0], __ATOMIC_RELAXED) +
                         __atomic_load_n(&sFlatDisagree[1], __ATOMIC_RELAXED);
    out->whySmall = __atomic_load_n(&sFlatWhySmall[0], __ATOMIC_RELAXED) +
                    __atomic_load_n(&sFlatWhySmall[1], __ATOMIC_RELAXED);
    out->whyVram = __atomic_load_n(&sFlatWhyVram[0], __ATOMIC_RELAXED) +
                   __atomic_load_n(&sFlatWhyVram[1], __ATOMIC_RELAXED);
    out->whyScroll = __atomic_load_n(&sFlatWhyScroll[0], __ATOMIC_RELAXED) +
                     __atomic_load_n(&sFlatWhyScroll[1], __ATOMIC_RELAXED);
    out->whyBuffers = __atomic_load_n(&sFlatWhyBuffers[0], __ATOMIC_RELAXED) +
                      __atomic_load_n(&sFlatWhyBuffers[1], __ATOMIC_RELAXED);
}

static u32 EngineSignature(int engine)
{
    return (EngineSmallSignature(engine, 0, 0, 0, 0) * 31u) ^
           EngineVramSignature(engine, NitroPort_IoReg + (engine ? 0x1000 : 0), NitroPort_Oam);
}

static u32 SplitLayoutSignature(int engine, const NitroRender2DSplit *split)
{
    int e = engine ? 1 : 0;
    const WindowCircle *circles = split->snapshot ? sState[e].circles : sCircles[e];
    u32 words[16] = {
        (u32)split->width, (u32)split->origin,
        split->clip[0], split->clip[1], split->clip[2], split->clip[3],
        (u32)split->anchorWindows, (u32)split->windowsToEdge,
        (u32)split->snapshot, (u32)split->height, (u32)split->top,
        (u32)split->stretch, (u32)split->gpuBgs,
        (u32)__atomic_load_n(&sSmoothObjs[e], __ATOMIC_ACQUIRE),
        split->snapshot ? sState[e].winSerial : NitroPort_WideWindowsSerial(),
        NitroPort_WideArtSerial()
    };
    u32 h = HashWords((const u8 *)words, sizeof(words), 2166136261u);

    for (int w = 0; w < 2; w++) {
        const WindowCircle *c = &circles[w];
        h = (h ^ (u32)(c->on ? 1 + c->cx * 7 + c->cy * 131 + c->r * 4099 : 0)) * 16777619u;
    }
    return h;
}

/* P3: keep the pair that says "this flat picture is unchanged". The small
 * signature covers the registers (the committed BGs' scroll left out),
 * the palettes, the OAM, the VRAM bank control, the per-line registers
 * and the layout; the VRAM signature covers the bytes the frame's layers
 * can read. The scroll is stored apart: a scrolled BG is a new picture. */
static void FlatStore(int engine, u32 layout, int snapshot, u32 mask)
{
    int e = engine ? 1 : 0;
    FlatReuse *f = &sFlatReuse[e];
    const u8 *io = snapshot ? sState[e].io : NitroPort_IoReg;
    const u8 *oam = snapshot ? sState[e].oam : NitroPort_Oam;
    u32 vram = 0;
    int bg;

    f->valid = 1;
    f->mask = mask;
    f->small = (EngineSmallSignature(e, snapshot, mask, 0, 0) * 31u) ^ layout;
    f->vramValid = sRefHashA &&
                   EngineReferencedOnly(e, io + (e ? 0x1000 : 0), oam, &vram);
    if (f->vramValid) {
        f->vram = vram;
    }
    for (bg = 1; bg < 4; bg++) {
        f->scroll[bg] = Rd32(io, 0x10 + bg * 4);
    }
}

/* P3: is the flat picture the same as the one the last flat pass drew?
 * Every input the flat picture depends on is covered by the two
 * signatures or by the scroll check (the dependency list is in the
 * claim sheet, docs/bench/claims/2026-11-10-p3-flat-skip.md). *why says
 * which input differed (0: the picture is the same). */
static int FlatUnchanged(int engine, const NitroRender2DSplit *split, int *why)
{
    int e = engine ? 1 : 0;
    FlatReuse *f = &sFlatReuse[e];
    const u8 *io = split->snapshot ? sState[e].io : NitroPort_IoReg;
    const u8 *oam = split->snapshot ? sState[e].oam : NitroPort_Oam;
    u32 small, vram = 0;
    int bg;

    *why = FLAT_WHY_NONE;
    if (!f->valid || !sRefHashA) {
        /* Nothing stored, or the referenced-range signature is off (the
         * skip rides refHashA: without it there is no VRAM signature that
         * could stand in for the full hash, which must not be paid here). */
        return 0;
    }
    small = (EngineSmallSignature(e, split->snapshot, f->mask, 0, 0) * 31u) ^
            SplitLayoutSignature(e, split);
    if (small != f->small) {
        *why = FLAT_WHY_SMALL;
        return 0;
    }
    *why = FLAT_WHY_VRAM;
    if (!EngineReferencedOnly(e, io + (e ? 0x1000 : 0), oam, &vram)) {
        FLAT_COUNT_ADD(sFlatNotModelled[e]);
        return 0;
    }
    if (!f->vramValid || vram != f->vram) {
        *why = FLAT_WHY_VRAM;
        return 0;
    }
    *why = FLAT_WHY_SCROLL;
    for (bg = 1; bg < 4; bg++) {
        if ((f->mask & (1u << bg)) && Rd32(io, 0x10 + bg * 4) != f->scroll[bg]) {
            return 0;
        }
    }
    *why = FLAT_WHY_NONE;
    return 1;
}

/* P3: the diagnostic's comparison: what this pass would publish against
 * what the last flat pass published. A difference means the skip would
 * have left a stale picture on the screen. */
static int FlatMetaDiffers(const NitroRender2DSplit *a, const NitroRender2DSplit *b)
{
    int slot;

    if (a->nGpuQuads != b->nGpuQuads || a->gpuQuadsBehind != b->gpuQuadsBehind ||
        a->has3d != b->has3d || a->scroll3d != b->scroll3d ||
        a->scroll3dLines != b->scroll3dLines || a->tint3d != b->tint3d ||
        a->eva != b->eva || a->evb != b->evb || a->evy != b->evy ||
        a->backdrop != b->backdrop || a->flatCpuPlanes != b->flatCpuPlanes) {
        return 1;
    }
    for (slot = 0; slot < NITRO_GPU_BG_SLOTS; slot++) {
        if (a->gpuBgKey[slot] != b->gpuBgKey[slot] ||
            a->gpuBgSlotBg[slot] != b->gpuBgSlotBg[slot] ||
            a->gpuBgW[slot] != b->gpuBgW[slot] || a->gpuBgH[slot] != b->gpuBgH[slot] ||
            a->gpuBgWrap[slot] != b->gpuBgWrap[slot]) {
            return 1;
        }
    }
    if (a->nGpuQuads > 0 &&
        memcmp(a->gpuQuads, b->gpuQuads,
               (size_t)a->nGpuQuads * sizeof(a->gpuQuads[0])) != 0) {
        return 1;
    }
    return 0;
}

/* P3: what the last flat pass published, and a copy of it to compare
 * against (the diagnostic only; the flat path is engine A's). */
static NitroRender2DSplit sFlatMeta;
static NitroRender2DSplit sFlatScratch;
static int sFlatMetaValid;

static void RememberSplit(int engine, u16 *under, const NitroRender2DSplit *split,
                         u32 layout, int snapshot)
{
    int e = engine ? 1 : 0;
    SplitReuse *r = &sSplitReuse[e];
    const u8 *io = snapshot ? sState[e].io : NitroPort_IoReg;
    u32 mask = sCommittedTextOnly[e];

    if (sRefHashDiagnostic && e == 0) {
        /* P3: what this pass published (the diagnostic compares it with
         * the next pass's). Kept whatever the picture is: a flat picture
         * that stops being flat must not be compared against a stale one. */
        sFlatMeta = *split;
        sFlatMetaValid = 1;
    }
    if (!mask || e != 0) {
        return;
    }
    r->under = under;
    r->over = split->over;
    r->alpha = split->overAlpha;
    r->mask = mask;
    if (r->flat) {
        /* The next call will freshly validate/resolve this picture, NOT
         * reuse its pixels or infer that VRAM is unchanged. No full-VRAM
         * signature is needed to certify an unchanged-memory claim here.
         * P3: the pair that says "the same flat picture" is kept, so the
         * next pass can skip the whole validation when nothing changed. */
        FlatStore(engine, layout, snapshot, mask);
        r->valid = 1;
        return;
    }
    r->small = (EngineSmallSignature(e, snapshot, mask, 0, 0) * 31u) ^ layout;
    r->vram = EngineVramSignature(e, io, snapshot ? sState[e].oam : NitroPort_Oam);
    for (int bg = 1; bg < 4; bg++) {
        r->scroll[bg] = Rd32(io, 0x10 + bg * 4);
    }
    r->valid = 1;
}

/* Slice A: a copy of the per-engine draw profile (which keeps running).
 * The copy can tear mid-draw on a 32-bit CPU; a diagnostic read once per
 * interval doesn't need it exact. */
void NitroPort_Render2DProfile(int engine, NitroRender2DProfile *out)
{
    int e = engine ? 1 : 0;

    *out = sProfile[e];
}

#ifdef HGSS_A0_DIAG
/* A0 redraw attribution (plan item A0.5): which legacy term triggered each
 * engine's decision. Buckets follow evaluation order. `layout` is split out
 * of `small`; the small sub-terms (registers, palette, OAM) hash only on
 * the already-decided redraw path, so unchanged frames pay nothing extra.
 * The sub-hashes mirror EngineSmallSignature's mask-0 inputs exactly, so a
 * small match implies matching subs; H-blank line effects fall into `misc`
 * (they are part of `small` but not of any sub-term). */
static struct {
    u64 flat[2], reuseHit[2], reuseScroll[2], forced[2], firstNote[2];
    u64 layoutChanged[2], regsChanged[2], paletteChanged[2], oamChanged[2], miscChanged[2];
    u64 vramChanged[2], unchanged[2];
    u64 vramBytes[2], scanlines[2], smallBytes[2];
    u32 lastLayout[2], lastRegs[2], lastPal[2], lastOam[2];
} sA0Redraw;

static void A0SubHashes(int e, int snapshot, NitroRender2DSplit *split,
                        u32 *layout, u32 *regs, u32 *pal, u32 *oam)
{
    const State2D *st = &sState[e];
    const u8 *ioAll = snapshot ? st->io : NitroPort_IoReg;
    const u8 *io = ioAll + (e ? 0x1000 : 0);
    u32 r[0x70 / 4];
    u32 h = 2166136261u;

    memcpy(r, io, sizeof(r));
    if (!e) r[0x60 / 4] &= ~0x3000u;
    r[0x6C / 4] = 0;
    h = HashWords((const u8 *)r, sizeof(r), h);
    h = HashWords(ioAll + 0x240, 0x10, h);
    h = (h ^ Rd16(ioAll, 0x304)) * 16777619u;
    *regs = h;
    *pal = HashWords((snapshot ? st->pltt : NitroPort_Pltt) + (e ? 0x400 : 0), 0x400, 2166136261u);
    *oam = HashWords((snapshot ? st->oam : NitroPort_Oam) + (e ? 0x400 : 0), 0x400, 2166136261u);
    *layout = SplitLayoutSignature(e, split);
}

/* Compare the sub-terms against the retained ones when countIt is set,
 * then retain the current ones either way. */
static void A0SubTrack(int e, int snapshot, NitroRender2DSplit *split, int countIt)
{
    u32 layoutNow, regsNow, palNow, oamNow;

    A0SubHashes(e, snapshot, split, &layoutNow, &regsNow, &palNow, &oamNow);
    if (countIt) {
        if (layoutNow != sA0Redraw.lastLayout[e]) {
            sA0Redraw.layoutChanged[e]++;
        } else {
            if (regsNow != sA0Redraw.lastRegs[e]) sA0Redraw.regsChanged[e]++;
            if (palNow != sA0Redraw.lastPal[e]) sA0Redraw.paletteChanged[e]++;
            if (oamNow != sA0Redraw.lastOam[e]) sA0Redraw.oamChanged[e]++;
            if (regsNow == sA0Redraw.lastRegs[e] && palNow == sA0Redraw.lastPal[e] &&
                oamNow == sA0Redraw.lastOam[e])
                sA0Redraw.miscChanged[e]++;
        }
    }
    sA0Redraw.lastLayout[e] = layoutNow;
    sA0Redraw.lastRegs[e] = regsNow;
    sA0Redraw.lastPal[e] = palNow;
    sA0Redraw.lastOam[e] = oamNow;
}

/* Hashed-byte accounting mirrors what the legacy path reads: the small
 * signature (registers, VRAMCNT, POWCNT, palettes, OAM, layout) plus the
 * full VRAM sweep when it runs. Counted separately from converted bytes. */
static u32 A0SmallBytes(int e, int snapshot)
{
    const State2D *st = &sState[e];
    u32 bytes = 0x70u + 0x10u + 2u + 0x400u + 0x400u + 72u;

    if (snapshot ? st->lines : NitroPort_LineRegs.active) bytes += 0x70u * 192u;
    return bytes;
}

static u32 A0FullVramBytes(int e, const u8 *io)
{
    if (!e) {
        if ((((Rd32(io, 0) >> 16) & 3) == 2)) return 0xD0000u + 0x20000u;
        return 0xD0000u;
    }
    return 0x4C000u;
}

void NitroPort_Render2DRedrawStats(int engine, NitroRedrawStats *out)
{
    int e = engine ? 1 : 0;

    if (out == NULL) return;
    out->flat = sA0Redraw.flat[e];
    out->reuseHit = sA0Redraw.reuseHit[e];
    out->reuseScroll = sA0Redraw.reuseScroll[e];
    out->forced = sA0Redraw.forced[e];
    out->firstNote = sA0Redraw.firstNote[e];
    out->layoutChanged = sA0Redraw.layoutChanged[e];
    out->regsChanged = sA0Redraw.regsChanged[e];
    out->paletteChanged = sA0Redraw.paletteChanged[e];
    out->oamChanged = sA0Redraw.oamChanged[e];
    out->miscChanged = sA0Redraw.miscChanged[e];
    out->vramChanged = sA0Redraw.vramChanged[e];
    out->unchanged = sA0Redraw.unchanged[e];
    out->vramBytes = sA0Redraw.vramBytes[e];
    out->scanlines = sA0Redraw.scanlines[e];
    out->smallBytes = sA0Redraw.smallBytes[e];
}
#endif

/* NitroPort_Render2D, unless nothing it draws from changed since the last
 * time for this engine (then out is left as it is, and 0 returned). Working
 * that out reads the engine's VRAM once: far less than drawing it. A 3D
 * layer counts as changed whenever a new 3D image is out. While the 3D
 * keeps changing, the screen is drawn for each new 3D image only, nothing
 * hashed: in step with the 3D (a moving camera moves evenly), and no
 * drawing in between that would hold the next 3D image up. */
#define QUIET_3D_CALLS 4   /* calls without a new 3D image: the 3D has stopped */
int NitroPort_Render2DSplitIfChanged(int engine, u16 *under, NitroRender2DSplit *split)
{
    /* (its own record: the 3D doesn't come into these layers) */
    static struct { u32 small, vram, serial; int valid, vramValid; } last[2];
    static u32 lastFull[2];
    static int lastFullValid[2];
    int e = engine ? 1 : 0;
    SplitReuse *r = &sSplitReuse[e];
    int force = r->valid || last[e].serial != sSplitPictureSerial[e];
    u32 small, vram;

    /* Half-A L4: engine B's split-signature decision, when enabled. A
     * non-negative return is the decision (1 drawn, 0 unchanged); -1 falls
     * through to the legacy logic below, which also refreshes the
     * retained state through whole renders. Engine A is untouched. */
    if (e == 1 && sPartialB) {
        int partial = PartialBTry(under, split);

        if (partial >= 0) {
            return partial;
        }
    }

    if (r->valid && r->flat) {
        /* P3: the flat picture (every layer a GPU take or proven
         * transparent) is the same when its two signatures and the
         * committed BGs' scroll are the same: the pass is then skipped
         * and the retained layers stay on screen (the caller publishes
         * them again). Otherwise the pass runs as before: current
         * metadata is rebuilt and the software planes refilled. New text,
         * OAM, layout, windows, effects or buffer identities go through
         * the full current validators; if flat admission fails the normal
         * scanlines run. Never skip using last frame's emptiness. */
        int why = FLAT_WHY_NONE;
        int same;

        if (r->under == under && r->over == split->over && r->alpha == split->overAlpha) {
            same = FlatUnchanged(engine, split, &why);
        } else {
            same = 0;
            why = FLAT_WHY_BUFFERS;
        }
        if (!same) {
            switch (why) {
            case FLAT_WHY_SMALL: FLAT_COUNT_ADD(sFlatWhySmall[e]); break;
            case FLAT_WHY_VRAM: FLAT_COUNT_ADD(sFlatWhyVram[e]); break;
            case FLAT_WHY_SCROLL: FLAT_COUNT_ADD(sFlatWhyScroll[e]); break;
            case FLAT_WHY_BUFFERS: FLAT_COUNT_ADD(sFlatWhyBuffers[e]); break;
            default: break;
            }
        }
        if (same && !sRefHashDiagnostic) {
            FLAT_COUNT_ADD(sFlatSkip[e]);
            return 0;
        }
#ifdef HGSS_A0_DIAG
        sA0Redraw.flat[e]++;
        sA0Redraw.scanlines[e] += 192;
#endif
        if (same && sRefHashDiagnostic && sFlatMetaValid) {
            sFlatScratch = sFlatMeta;
        }
        FLAT_COUNT_ADD(sFlatRedraw[e]);
        Render(engine, under, split, R2D_TRY_FLAT);
        if (same && sRefHashDiagnostic && sFlatMetaValid) {
            /* The diagnostic: the pass ran, so compare what it published
             * with what the last flat pass published. A difference means
             * the skip would have left a stale picture on the screen. */
            if (FlatMetaDiffers(&sFlatScratch, split)) {
                FLAT_COUNT_ADD(sFlatDisagree[e]);
                NitroPort_Log("[2d] flat disagreement #%u: the pass published something else",
                              (unsigned)__atomic_load_n(&sFlatDisagree[e], __ATOMIC_RELAXED));
            }
        }
        return 1;
    }
    if (r->valid && r->under == under && r->over == split->over && r->alpha == split->overAlpha) {
        const u8 *io = split->snapshot ? sState[e].io : NitroPort_IoReg;
#ifndef HGSS_PROFILE_OFF
        u64 tCheck = R2D_NOW();
#endif
        u32 candidate = (EngineSmallSignature(e, split->snapshot, r->mask, 0, 0) * 31u) ^
                        SplitLayoutSignature(e, split);

#ifdef HGSS_A0_DIAG
        sA0Redraw.smallBytes[e] += A0SmallBytes(e, split->snapshot);
#endif
        if (candidate == r->small && EngineVramSignature(e, io,
                                                         split->snapshot ? sState[e].oam : NitroPort_Oam) == r->vram) {
            int moved = 0;
            for (int bg = 1; bg < 4; bg++) {
                if ((r->mask & (1u << bg)) && Rd32(io, 0x10 + bg * 4) != r->scroll[bg]) {
                    moved = 1;
                }
            }
#ifndef HGSS_PROFILE_OFF
            sProfile[e].setupCheckTicks += R2D_NOW() - tCheck;
#endif
#ifdef HGSS_A0_DIAG
            sA0Redraw.vramBytes[e] += e ? 0x4C000u : A0FullVramBytes(e, io);
            A0SubTrack(e, split->snapshot, split, 0);
#endif
            if (!moved) {
#ifdef HGSS_A0_DIAG
                sA0Redraw.reuseHit[e]++;
#endif
                return 0;   /* brightness-only is handled by the shell */
            }
#ifdef HGSS_A0_DIAG
            sA0Redraw.reuseScroll[e]++;
            sA0Redraw.scanlines[e] += 192;
#endif
            u32 mask = r->mask;
            Render(engine, under, split, mask | R2D_TRY_FLAT);
            return 1;       /* current quads must be published, pixels reused */
        }
    }
#ifndef HGSS_PROFILE_OFF
    u64 tCheckSmall = R2D_NOW();
#endif
    small = (EngineSmallSignature(e, split->snapshot, 0, 0, 0) * 31u) ^
            SplitLayoutSignature(e, split);
#ifndef HGSS_PROFILE_OFF
    /* P1: the small signature runs on every non-reuse pass; time it here so
     * the early (small-changed) return below still attributes its check. */
    sProfile[e].setupCheckTicks += R2D_NOW() - tCheckSmall;
#endif
    /* The small parts changed (a scroll, a sprite, a colour: most frames of
     * anything moving): drawn, without reading the VRAM through to find
     * out about that as well. Only when they're the same is the VRAM
     * compared (once it's known: the first time after a change it's just
     * noted, and drawn). */
    if (force || !last[e].valid || small != last[e].small) {
#ifdef HGSS_A0_DIAG
        sA0Redraw.smallBytes[e] += A0SmallBytes(e, split->snapshot);
        if (force) {
            sA0Redraw.forced[e]++;
            A0SubTrack(e, split->snapshot, split, 0);
        } else if (!last[e].valid) {
            sA0Redraw.firstNote[e]++;
            A0SubTrack(e, split->snapshot, split, 0);
        } else {
            A0SubTrack(e, split->snapshot, split, 1);
        }
        sA0Redraw.scanlines[e] += 192;
#endif
        last[e].small = small;
        last[e].valid = 1;
        last[e].vramValid = 0;
        Render(engine, under, split, R2D_TRY_FLAT);
        last[e].serial = sSplitPictureSerial[e];
        return 1;
    }
#ifndef HGSS_PROFILE_OFF
    u64 tCheckVram = R2D_NOW();
#endif
    vram = EngineVramSignature(e, (split->snapshot ? sState[e].io : NitroPort_IoReg) + (e ? 0x1000 : 0),
                               split->snapshot ? sState[e].oam : NitroPort_Oam);
#ifndef HGSS_PROFILE_OFF
    /* P1: the referenced-range VRAM signature (the expensive check). */
    sProfile[e].setupCheckTicks += R2D_NOW() - tCheckVram;
#endif
    int forceChanged = 0;
    if (sRefHashDiagnostic && (e == 0 ? sRefHashA : sRefHashB)) {
        const u8 *dio = (split->snapshot ? sState[e].io : NitroPort_IoReg) + (e ? 0x1000 : 0);
        u32 full = EngineFullVramSignature(e, dio);
        int fullChanged = !lastFullValid[e] || full != lastFull[e];

        if (last[e].vramValid && (vram != last[e].vram) != fullChanged) {
            sRefHashDisagreements++;
            NitroPort_Log("[2d] refHash%c disagreement #%u: reference-changed %d full-changed %d",
                          e ? 'B' : 'A', sRefHashDisagreements, vram != last[e].vram, fullChanged);
            /* The reference set is experimental: a disagreement must never
             * become a stale skip. Treat the frame as changed. */
            forceChanged = 1;
        }
        lastFull[e] = full;
        lastFullValid[e] = 1;
    }
    if (last[e].vramValid && vram == last[e].vram && !forceChanged) {
#ifdef HGSS_A0_DIAG
        sA0Redraw.unchanged[e]++;
        sA0Redraw.vramBytes[e] += e ? 0x4C000u : A0FullVramBytes(e, (split->snapshot ? sState[e].io : NitroPort_IoReg));
        A0SubTrack(e, split->snapshot, split, 0);
#endif
        return 0;
    }
    last[e].vram = vram;
    last[e].vramValid = 1;
#ifdef HGSS_A0_DIAG
    sA0Redraw.vramChanged[e]++;
    sA0Redraw.vramBytes[e] += e ? 0x4C000u : A0FullVramBytes(e, (split->snapshot ? sState[e].io : NitroPort_IoReg));
    sA0Redraw.scanlines[e] += 192;
    A0SubTrack(e, split->snapshot, split, 0);
#endif
    Render(engine, under, split, R2D_TRY_FLAT);
    last[e].serial = sSplitPictureSerial[e];
    return 1;
}

unsigned NitroPort_Render2DMasterBright(int engine, int snapshot)
{
    const u8 *io = (snapshot ? sState[engine ? 1 : 0].io : NitroPort_IoReg) + (engine ? 0x1000 : 0);

    return Rd16(io, 0x6C);
}

int NitroPort_Render2DIfChanged(int engine, u16 *out)
{
    static struct { u32 signature; u32 images; int valid; int following; } last[2];
    int e = engine ? 1 : 0;
    u32 images = NitroPort_Gx3dImageCount();
    u32 dispcnt = Rd32(NitroPort_IoReg, 0);
    BOOL uses3D = e == 0 && (dispcnt & 0x0108) == 0x0108;   /* BG0 on, as 3D */
    u32 signature;

    if (uses3D && images != last[e].images) {
        /* New 3D: draw, and check the rest again once the 3D stops. */
        last[e].images = images;
        last[e].valid = 0;
        last[e].following = QUIET_3D_CALLS;
        NitroPort_Render2D(engine, out);
        return 1;
    }
    if (uses3D && last[e].following > 0) {
        last[e].following--;
        return 0;
    }
    signature = EngineSignature(e);
    if (last[e].valid && signature == last[e].signature) {
        return 0;
    }
    last[e].signature = signature;
    last[e].images = images;
    last[e].valid = 1;
    NitroPort_Render2D(engine, out);
    return 1;
}
