/* Half-A lane G wave 2 behavioral fixture: the G3 display-list bounds
 * cache and the G5 matrix/state dedupe, driven through the actual worker
 * (ring, work_packets, skip path) with synthetic recorded-style streams.
 *
 * Two binaries are built from this file: with and without
 * HGSS_GX3D_LISTCACHE / HGSS_GX3D_STATEDEDUP. Both print one checksum per
 * scene on stdout; the outputs must be identical (hidden/not-hidden
 * decisions and all trailing state bit-identical, including map
 * transitions, in-place rewrites and heap reuse). The flagged binary
 * additionally asserts cache behavior on stderr (hits, skips, census,
 * auto-disable) and exits nonzero if any of it is missing. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include GX3D_SOURCE

unsigned char NitroPort_IoReg[0x2000] __attribute__((aligned(8)));
int NitroPort_Gx3dTriangleTranslucent(const NitroGx3dTriangle *tri)
{
    return ((tri->polygon_attr >> 16) & 31u) < 31u;
}

/* ---- minimal host stubs for the worker path ---- */
#include <stdarg.h>
void NitroPort_Log(const char *fmt, ...)
{
    va_list ap;
    const char *debug = getenv("LC_DEBUG");
    if (!debug || !debug[0]) return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
int NitroPort_SunShadows(void)
{
    return 0;
}
uint64_t NitroPortPlatform_Nanoseconds(void)
{
    return 0;
}
static int sMDisplay, sMBuilding;
static uint32_t sMSerial;
void NitroPort_MailboxInit(NitroGx3dMailbox *box)
{
    (void)box;sMDisplay=0;sMBuilding=1;sMSerial=0;
}
void NitroPort_MailboxTag(NitroGx3dMailbox *box, uint32_t tag)
{
    (void)box;(void)tag;
}
int NitroPort_MailboxPublish(NitroGx3dMailbox *box, int *next, uint32_t *serial)
{
    (void)box;
    sMDisplay=sMBuilding;sMBuilding=(sMBuilding+1)%3;
    *next=sMBuilding;*serial=++sMSerial;
    return 1;
}
int NitroPort_MailboxAcquire(NitroGx3dMailbox *box, uint32_t *serial)
{
    (void)box;(void)serial;return -1;
}
void NitroPort_MailboxRelease(NitroGx3dMailbox *box, int index)
{
    (void)box;(void)index;
}
int NitroPort_MailboxLatest(NitroGx3dMailbox *box)
{
    (void)box;return sMDisplay;
}
int NitroPort_MailboxNewer(NitroGx3dMailbox *box, uint32_t serial)
{
    (void)box;(void)serial;return 0;
}
uint32_t NitroPort_MailboxNewerCount(NitroGx3dMailbox *box, uint32_t serial)
{
    (void)box;(void)serial;return 0;
}
uint32_t NitroPort_MailboxUnread(NitroGx3dMailbox *box)
{
    (void)box;return 0;
}
void NitroPort_MailboxMarkTaken(NitroGx3dMailbox *box)
{
    (void)box;
}
static void stub_wait(void *context)
{
    (void)context;
}
static void stub_wake(void *context)
{
    (void)context;
}

/* ---- deterministic stream builder ---- */
static uint32_t rng_state = 0x12345678u;
static uint32_t next_rand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}
typedef struct {
    uint32_t *w;
    size_t n, cap;
} Words;
static void put_word(Words *b, uint32_t w)
{
    if (b->n >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->w = realloc(b->w, b->cap * sizeof(*b->w));
        if (!b->w) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    b->w[b->n++] = w;
}
/* One opcode per command word (the rest NOPs): always whole commands. */
static void emit_op(Words *b, unsigned op, const uint32_t *params, unsigned nparams)
{
    unsigned k;
    put_word(b, (uint32_t)op);
    for (k = 0; k < nparams; k++) put_word(b, params[k]);
}
static void emit_mode(Words *b, unsigned mode)
{
    uint32_t p = mode;
    emit_op(b, 0x10, &p, 1);
}
/* Translation load: row 3 = (tx, ty, tz) in clip units (the port, like
 * the DS, is 20.12 throughout: 4096 is 1.0, so small integers move
 * pixels, not worlds). */
static void emit_translate(Words *b, int tx, int ty, int tz)
{
    uint32_t p[16];
    unsigned k;
    for (k = 0; k < 16; k++) p[k] = 0;
    p[0] = p[5] = p[10] = p[15] = 4096;
    p[12] = (uint32_t)(int32_t)tx;
    p[13] = (uint32_t)(int32_t)ty;
    p[14] = (uint32_t)(int32_t)tz;
    emit_op(b, 0x16, p, 16);
}
static void emit_push_pop(Words *b)
{
    uint32_t p = 1;   /* POP one entry: restores the PUSH below */
    emit_op(b, 0x11, 0, 0);
    emit_op(b, 0x12, &p, 1);
}
static void emit_store_restore(Words *b, unsigned slot)
{
    uint32_t p = slot;
    emit_op(b, 0x13, &p, 1);
    emit_op(b, 0x14, &p, 1);
}
static void emit_vtx(Words *b, int x, int y, int z)
{
    uint32_t p[2];
    p[0] = ((uint32_t)(uint16_t)x) | ((uint32_t)(uint16_t)y << 16);
    p[1] = (uint32_t)(uint16_t)z;
    emit_op(b, 0x23, p, 2);
}
static void emit_vtx_diff(Words *b, int dx, int dy, int dz)
{
    uint32_t p = ((uint32_t)(dx & 0x3ff)) | ((uint32_t)(dy & 0x3ff) << 10) |
                 ((uint32_t)(dz & 0x3ff) << 20);
    emit_op(b, 0x28, &p, 1);
}
static void emit_normal(Words *b, int nx, int ny, int nz)
{
    uint32_t p = ((uint32_t)(nx & 0x3ff)) | ((uint32_t)(ny & 0x3ff) << 10) |
                 ((uint32_t)(nz & 0x3ff) << 20);
    emit_op(b, 0x21, &p, 1);
}
static void emit_texcoord(Words *b, int s, int t)
{
    uint32_t p = ((uint32_t)(uint16_t)s) | ((uint32_t)(uint16_t)t << 16);
    emit_op(b, 0x22, &p, 1);
}
static void emit_color(Words *b, uint32_t rgb)
{
    emit_op(b, 0x20, &rgb, 1);
}
/* A 12-vertex quad-strip-ish triangle list at (ox, oy): hidden when the
 * model translation leaves it past the frustum, visible otherwise. */
static void emit_box_list(Words *b, int ox, int oy, int use_diff, int with_state)
{
    uint32_t p;
    int v;
    p = 0;   /* triangles */
    emit_op(b, 0x40, &p, 1);
    if (with_state) {
        emit_texcoord(b, 16, 32);
        emit_normal(b, 0, 0, 511);
        emit_color(b, 0x1f);
    }
    emit_vtx(b, ox, oy, 0);
    for (v = 1; v < 12; v++) {
        int x = ox + (int)(next_rand() % 200) - 100;
        int y = oy + (int)(next_rand() % 200) - 100;
        if (use_diff)
            emit_vtx_diff(b, (x - ox) & 0x1ff, (y - oy) & 0x1ff, 0);
        else
            emit_vtx(b, x, y, 0);
    }
    emit_op(b, 0x41, 0, 0);
}
static void emit_swap(Words *b, uint32_t param)
{
    emit_op(b, 0x50, &param, 1);
}

/* Feed a buffer to the worker in fixed-size pieces (like NitroSystem's
 * geometry-buffer flushes), so lists span several packets entries. */
static void feed_pieces(const uint32_t *w, size_t n, size_t piece)
{
    size_t at = 0;
    while (at < n) {
        size_t take = n - at > piece ? piece : n - at;
        NitroPort_GxFifoWords(w + at, take);
        at += take;
    }
}
static int run_worker(void)
{
    static int not_running = 0;
    NitroPort_Gx3dThreadMain(&not_running);
    return 0;
}
static uint64_t scene_checksum(void)
{
    const NitroGx3dScene *scene = NitroPort_Gx3dScene();
    uint64_t h = 1469598103934665603ull;
    unsigned i, k;
    h ^= scene->count + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    for (i = 0; i < scene->count; i++) {
        const NitroGx3dTriangle *t = &scene->triangles[i];
        h ^= t->polygon_attr + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= t->texture_param + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= t->palette_base + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= t->sort_key + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        for (k = 0; k < 3; k++) {
            h ^= (uint64_t)(uint32_t)t->v[k].x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= (uint64_t)(uint32_t)t->v[k].y + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= (uint64_t)t->v[k].depth + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= (uint64_t)t->v[k].color + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        }
    }
    return h;
}

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

int main(void)
{
    NitroGx3dThreadOps ops;
    /* Persistent display lists: the same source address + length reused
     * across scenes (the cacheable case), plus churn around them. */
    Words listA = {0}, listB = {0}, listC = {0};
    /* Transitional lists, replaced every map change. */
    Words *trans = 0;
    unsigned ntrans = 0;
    unsigned scene;
    memset(&ops, 0, sizeof(ops));
    ops.wait_worker = stub_wait;
    ops.wake_worker = stub_wake;
    ops.wait_producer = stub_wait;
    ops.wake_producer = stub_wake;
    ops.keep_every_scene = 1;
    NitroPort_Gx3dReset();
    NitroPort_Gx3dStartThread(&ops);

    rng_state = 0x12345678u;
    emit_box_list(&listA, -6000, 0, 0, 1);   /* hidden at rest */
    emit_box_list(&listB, 300, -200, 1, 0);   /* visible, VTX_DIFF */
    emit_box_list(&listC, -6000, 500, 0, 0); /* hidden, second list */
    for (scene = 0; scene < 12000; scene++) {
        Words head = {0}, tail = {0};
        int drift = (int)(scene % 101) - 50;
        int jump = (int)((scene / 400) % 2) * 8000;
        unsigned mode = (scene & 1);   /* alternate texcoord modes */
        /* G5 matrix redundancy every scene: same-mode set, push/pop and
         * store/restore with no change between them, then the model's
         * matrix twice (the second load is free). */
        emit_mode(&head, mode);
        emit_mode(&head, mode);
        emit_push_pop(&head);
        emit_store_restore(&head, 3);
        emit_translate(&head, jump + drift, drift / 2, 0);
        emit_translate(&head, jump + drift, drift / 2, 0);
        feed_pieces(head.w, head.n, 64);
        free(head.w);
        if ((scene % 400) == 0) {
            /* Map transition: retire one list, build two new ones. */
            unsigned k;
            for (k = 0; k < ntrans; k++) free(trans[k].w);
            free(trans);
            ntrans = 2;
            trans = calloc(ntrans, sizeof(*trans));
            for (k = 0; k < ntrans; k++)
                emit_box_list(&trans[k], -6000 + (int)(k * 900) + (int)(scene % 7),
                              100 * (int)k, (k & 1), (k & 2) ? 1 : 0);
        }
        if ((scene % 1200) == 500) {
            /* Sampled-word rewrite (BEGIN's own primitive word, always a
             * sample): the next lookup misses the samples, rescans and
             * reinserts. No stale window, output stays identical. */
            listB.w[1] ^= 1u;
        }
        feed_pieces(listA.w, listA.n, 37);
        feed_pieces(listB.w, listB.n, 37);
        if ((scene % 3) == 0)
            feed_pieces(listC.w, listC.n, 41);
        {
            unsigned k;
            for (k = 0; k < ntrans; k++) feed_pieces(trans[k].w, trans[k].n, 37);
        }
        emit_swap(&tail, 0);
        feed_pieces(tail.w, tail.n, 64);
        free(tail.w);
        run_worker();
        printf("%04u %016llx\n", scene,
               (unsigned long long)scene_checksum());
    }
    /* Singles phase (FIFO origin): the same lists one word at a time, so
     * scan_packets cannot complete runs and every command arrives as a
     * single entry, like CPU FIFO writes on hardware. The reference build
     * transforms them directly; the FIFOCACHE build takes the hidden-vertex
     * scan. Checksums must agree either way. */
    for (scene = 12000; scene < 12200; scene++) {
        Words head = {0}, tail = {0};
        emit_mode(&head, 0);
        emit_translate(&head, 0, 0, 0);
        feed_pieces(head.w, head.n, 1);
        free(head.w);
        feed_pieces(listA.w, listA.n, 1);
        feed_pieces(listB.w, listB.n, 1);
        emit_swap(&tail, 0);
        feed_pieces(tail.w, tail.n, 1);
        free(tail.w);
        run_worker();
        printf("%04u %016llx\n", scene,
               (unsigned long long)scene_checksum());
    }
    /* Tag-queue overflow scene: far more packets entries than source
     * tags. Early entries lose their tags, so those lists scan instead
     * of hitting; output is still identical. */
    {
        Words head = {0}, tail = {0};
        unsigned k;
        emit_mode(&head, 0);
        emit_translate(&head, 0, 0, 0);
        feed_pieces(head.w, head.n, 64);
        free(head.w);
        for (k = 0; k < 5000; k++) feed_pieces(listA.w, listA.n, 37);
        emit_swap(&tail, 0);
        feed_pieces(tail.w, tail.n, 64);
        free(tail.w);
        run_worker();
        printf("overflow %016llx\n", (unsigned long long)scene_checksum());
    }
#ifdef HGSS_GX3D_LISTCACHE
    {
        NitroGx3dListCacheStats st;
        NitroPort_Gx3dListCacheStats(&st);
        fprintf(stderr, "listcache: lookups=%u hits=%u inserts=%u evicts=%u "
                "sampleFails=%u resumeFails=%u untagged=%u audits=%u auditFails=%u "
                "disabled=%u entries=%u census=%u tagCollide=%u singlesBegin=%u\n",
                st.lookups, st.hits, st.inserts, st.evicts,
                st.sampleFails, st.resumeFails, st.untagged, st.audits,
                st.auditFails, st.disabled, st.entries, st.censusSources,
                st.tagCollide, st.singlesBegin);
        CHECK(st.lookups > 10000);
        CHECK(st.hits > 5000);
        CHECK(st.inserts > 0);
        CHECK(st.resumeFails == 0);
        CHECK(st.untagged > 0);
        CHECK(st.auditFails == 0);
        CHECK(!st.disabled);
        CHECK(st.censusSources > 0);
        CHECK(st.sampleFails > 0);
        /* Heap hook: freeing a cached range must not corrupt output. */
        {
            uint64_t before, after;
            Words frame = {0};
            NitroPort_Gx3dListCacheNoteFree(listA.w,
                (unsigned long)listA.n * 4u);
            emit_mode(&frame, 0);
            emit_translate(&frame, 0, 0, 0);
            feed_pieces(listA.w, listA.n, 37);
            emit_swap(&frame, 0);
            feed_pieces(frame.w, frame.n, 64);
            free(frame.w);
            run_worker();
            after = scene_checksum();
            NitroPort_Gx3dListCacheStats(&st);
            CHECK(st.disabled == 0);
            /* The same scene through a fresh cache must agree. */
            NitroPort_Gx3dListCacheEpochBump();
            {
                Words frame2 = {0};
                emit_mode(&frame2, 0);
                emit_translate(&frame2, 0, 0, 0);
                feed_pieces(listA.w, listA.n, 37);
                emit_swap(&frame2, 0);
                feed_pieces(frame2.w, frame2.n, 64);
                free(frame2.w);
            }
            run_worker();
            before = scene_checksum();
            CHECK(before == after);
            fprintf(stderr, "listcache: post-free scene %016llx ok\n",
                    (unsigned long long)after);
        }
        /* Audit trip: a rewrite the samples miss must be caught by the
         * full-hash audit, which auto-disables. Each offset gets a fresh
         * buffer (no cross-offset staleness) pinned to the same
         * coordinates by a listA feed. listB-template geometry stays
         * hidden under translate 8000 so the cache actually engages. */
        {
            unsigned t;
            int tripped = 0;
            Words *bufs = calloc(12, sizeof(*bufs));
            if (!bufs) { fprintf(stderr, "out of memory\n"); return 1; }
            for (t = 0; t < 12 && !tripped; t++) {
                Words frame = {0};
                size_t off = listB.n - 3 - 2 * t;
                bufs[t].n = listB.n;
                bufs[t].w = malloc(listB.n * sizeof(*bufs[t].w));
                if (!bufs[t].w) { fprintf(stderr, "out of memory\n"); return 1; }
                memcpy(bufs[t].w, listB.w, listB.n * sizeof(*bufs[t].w));
                if (off < 1 || off >= listB.n) continue;
                emit_mode(&frame, 0);
                emit_translate(&frame, 8000, 0, 0);
                feed_pieces(listA.w, listA.n, 37);
                feed_pieces(bufs[t].w, bufs[t].n, 37);
                emit_swap(&frame, 0);
                feed_pieces(frame.w, frame.n, 64);
                free(frame.w);
                run_worker();
                bufs[t].w[off] ^= 0x55u;
                NitroPort_Gx3dListCacheAuditNow();
                {
                    Words frame2 = {0};
                    emit_mode(&frame2, 0);
                    emit_translate(&frame2, 8000, 0, 0);
                    feed_pieces(listA.w, listA.n, 37);
                    feed_pieces(bufs[t].w, bufs[t].n, 37);
                    emit_swap(&frame2, 0);
                    feed_pieces(frame2.w, frame2.n, 64);
                    free(frame2.w);
                }
                run_worker();
                fprintf(stderr, "audit %016llx\n", (unsigned long long)scene_checksum());
                NitroPort_Gx3dListCacheStats(&st);
                if (st.disabled) {
                    tripped = 1;
                    CHECK(st.auditFails > 0);
                }
            }
            for (t = 0; t < 12; t++) free(bufs[t].w);
            free(bufs);
            CHECK(tripped);
            /* Disabled: scenes still process (fallback scans). */
            {
                Words frame = {0};
                emit_mode(&frame, 0);
                emit_translate(&frame, 0, 0, 0);
                feed_pieces(listB.w, listB.n, 37);
                emit_swap(&frame, 0);
                feed_pieces(frame.w, frame.n, 64);
                free(frame.w);
                run_worker();
                fprintf(stderr, "postdisable %016llx\n", (unsigned long long)scene_checksum());
            }
        }
    }
#endif
#ifdef HGSS_GX3D_STATEDEDUP
    {
        NitroGx3dMatrixDedupStats st;
        /* Direct unit checks: redundant commands change nothing. */
        {
            uint32_t ident[16] = {4096,0,0,0, 0,4096,0,0, 0,0,4096,0, 0,0,0,4096};
            uint32_t dummy[16] = {0};
            uint32_t pmode = 1, pslot = 0, pzero = 0;
            uint32_t g0, g1;
            Matrix savePos, saveVec;
            int r, modeSave, pvSave, dirtySave;
            pmode = 1;
            (void)matrix_command(&s, 0x10, &pmode);
            (void)matrix_command(&s, 0x15, dummy);
            /* LOAD of the current matrix: skipped, generation still. */
            g0 = NitroPort_Gx3dStackGeneration();
            s.clipDirty = 0;
            r = matrix_command(&s, 0x16, ident);
            g1 = NitroPort_Gx3dStackGeneration();
            CHECK(r == 0);
            CHECK(g0 == g1);
            CHECK(s.clipDirty == 0);
            /* A real LOAD applies and bumps the generation. */
            {
                uint32_t trans[16] = {4096,0,0,0, 0,4096,0,0, 0,0,4096,0,
                                      8192,0,0,4096};
                s.clipDirty = 0;
                r = matrix_command(&s, 0x16, trans);
                CHECK(r == 1);
                CHECK(NitroPort_Gx3dStackGeneration() == g0 + 1);
                CHECK(s.clipDirty == 1);
                /* ... and repeating it is skipped with identical state. */
                savePos = s.pos;saveVec = s.vec;
                modeSave = s.mode;pvSave = s.pvLevel;dirtySave = s.clipDirty;
                s.clipDirty = 0;
                r = matrix_command(&s, 0x16, trans);
                CHECK(r == 0);
                CHECK(s.clipDirty == 0);
                CHECK(!memcmp(&savePos, &s.pos, sizeof(savePos)));
                CHECK(!memcmp(&saveVec, &s.vec, sizeof(saveVec)));
                CHECK(s.mode == modeSave && s.pvLevel == pvSave);
                (void)dirtySave;
            }
            /* STORE then RESTORE of the same slot: the restore is free. */
            r = matrix_command(&s, 0x13, &pslot);
            CHECK(r == 1);
            savePos = s.pos;saveVec = s.vec;
            r = matrix_command(&s, 0x14, &pslot);
            CHECK(r == 0);
            CHECK(!memcmp(&savePos, &s.pos, sizeof(savePos)));
            CHECK(!memcmp(&saveVec, &s.vec, sizeof(saveVec)));
            /* PUSH, STORE the free slot, then POP(0): the pop loads
             * what was just stored, i.e. the current matrices, so it
             * is free and the level is unchanged. */
            r = matrix_command(&s, 0x11, &pzero);
            CHECK(r == 1);
            pslot = (uint32_t)s.pvLevel;
            r = matrix_command(&s, 0x13, &pslot);
            CHECK(r == 1);
            savePos = s.pos;saveVec = s.vec;
            pvSave = s.pvLevel;
            pzero = 0;
            r = matrix_command(&s, 0x12, &pzero);
            CHECK(r == 0);
            CHECK(s.pvLevel == pvSave);
            CHECK(!memcmp(&savePos, &s.pos, sizeof(savePos)));
            CHECK(!memcmp(&saveVec, &s.vec, sizeof(saveVec)));
            /* A POP that restores different content still applies:
             * slot 1 still holds the earlier translate (tx 8192). */
            {
                uint32_t trans[16] = {4096,0,0,0, 0,4096,0,0, 0,0,4096,0,
                                      4096,0,0,4096};
                (void)matrix_command(&s, 0x16, trans);
                r = matrix_command(&s, 0x12, &pzero);
                CHECK(r == 1);
                CHECK(s.pos.a[12] == (int32_t)8192);
                CHECK(s.pvLevel == 1);
            }
            /* Same-mode MTXMODE is free; an invalid RESTORE still errors. */
            pmode = 1;
            r = matrix_command(&s, 0x10, &pmode);
            CHECK(r == 0);
            pslot = 31;
            s.stackError = 0;
            r = matrix_command(&s, 0x14, &pslot);
            CHECK(r == 1);
            CHECK(s.stackError != 0);
            /* Redundant IDENTITY is free. */
            (void)matrix_command(&s, 0x15, dummy);
            r = matrix_command(&s, 0x15, dummy);
            CHECK(r == 0);
        }
        NitroPort_Gx3dMatrixDedupStats(&st);
        fprintf(stderr, "matrixdedup: mode=%u load=%u identity=%u restore=%u pop=%u gen=%u\n",
                st.modeSkip, st.loadSkip, st.identitySkip, st.restoreSkip,
                st.popSkip, st.generation);
        CHECK(st.modeSkip > 0);
        CHECK(st.loadSkip > 0);
        CHECK(st.identitySkip > 0);
        CHECK(st.restoreSkip > 0);
        CHECK(st.popSkip > 0);
        CHECK(st.generation > 0);
        CHECK(NitroPort_Gx3dStackGeneration() == st.generation);
    }
#endif
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    return 0;
}
