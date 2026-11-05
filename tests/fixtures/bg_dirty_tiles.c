/* Exercise the real 256x256 BG tile updater against full conversion. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nitro_port_upscale.h"

/* Double2x writes the padding row below the 512x512 texture too, just as
 * BG_TEX_ALLOC reserves space for it in the compositor. */
static uint16_t source[256 * 256], old[512 * (512 + 8)], fresh[512 * (512 + 8)];
static uint16_t olderSet[512 * (512 + 8)];
static uint16_t *flushBase = old;
static unsigned flushes, flushedBytes, badFlush;
static void flush(void *ptr, uint32_t bytes)
{
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)flushBase;
    flushes++; flushedBytes += bytes;
    if ((offset % 128) != 0 || (bytes % 128) != 0 || offset + bytes > 512 * 512 * 2) badFlush++;
}

int main(void)
{
    unsigned state = 1234;
    int checks = 0, failed = 0;
    for (int i = 0; i < 256 * 256; i++) {
        state = state * 1664525u + 1013904223u;
        source[i] = (uint16_t)(state >> 16);
    }
    NitroPort_Double2xTiled16(source, 256, 256, old, 512);
    memcpy(olderSet, old, sizeof(old));
    memcpy(fresh, old, sizeof(old));
    checks += 2; if (NitroPort_Update2xBgTiles16(source, old, flush) != 0) failed++;
    if (flushes != 0) failed++;
    for (int k = 0; k < 500; k++) {
        state = state * 1664525u + 1013904223u;
        unsigned pos = (state >> 8) & 65535u;
        source[pos] ^= (uint16_t)(1u << (k & 15));
        NitroPort_Double2xTiled16(source, 256, 256, fresh, 512);
        flushes = flushedBytes = badFlush = 0;
        unsigned dirty = NitroPort_Update2xBgTiles16(source, old, flush);
        checks += 3;
        if (dirty != 1) failed++;
        if (flushes != 1 || flushedBytes != 128 || badFlush) failed++;
        if (memcmp(old, fresh, 512 * 512 * sizeof(uint16_t))) failed++;
    }
    /* A set can have missed hundreds of updates while another was in use. */
    flushBase = olderSet;
    flushes = flushedBytes = badFlush = 0;
    unsigned staleDirty = NitroPort_Update2xBgTiles16(source, olderSet, flush);
    checks += 3;
    if (staleDirty == 0 || staleDirty > 500) failed++;
    if (badFlush || flushes != 1) failed++;
    if (memcmp(olderSet, fresh, 512 * 512 * sizeof(uint16_t))) failed++;
    flushBase = old;
    /* A palette sweep can alter every source pixel: all tiles must update. */
    for (unsigned i = 0; i < 256 * 256; i++) source[i] ^= 0xffff;
    NitroPort_Double2xTiled16(source, 256, 256, fresh, 512);
    flushes = flushedBytes = badFlush = 0;
    checks += 3;
    if (NitroPort_Update2xBgTiles16(source, old, flush) != 4096) failed++;
    if (flushes != 1 || flushedBytes != 512 * 512 * 2 || badFlush) failed++;
    if (memcmp(old, fresh, 512 * 512 * sizeof(uint16_t))) failed++;
    printf("dirty-tiles: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
