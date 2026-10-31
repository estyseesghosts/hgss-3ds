/*
 * Host checks for the SDK_PORT_3DS C versions of small NitroSDK functions that were MWCC asm.
 * Built against the patched tree by run.sh (32-bit, the survey's flags). Synthetic data only:
 * the lock words, IME and VCOUNT live in the port's arrays (patch 1301), linked from
 * tests/sdk/port_memory.c, and are filled by the test.
 *
 * Covered: OsCountZeroBits, OS_GetLockID, OS_ReleaseLockID, OS_UnLockCartridge, OS_UnLockCard,
 * OS_Halt, OSi_WaitVCount0 (IME restored), GX_SendFifo48B/64B and GXi_NopClearFifo128_ (final
 * word at each address; the order of the stores is checked by reading the asm, not here).
 * Stubs below stand in for the functions those objects call: the test records the calls.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The port's arrays (patch 1301); defined in tests/sdk/port_memory.c. */
extern unsigned char NitroPort_IoReg[];
extern unsigned char NitroPort_MainMem[];

/* Prototypes with the NitroSDK types (u32 unsigned int, s32 int, u16 unsigned short). */
unsigned int OsCountZeroBits(unsigned int bitmap);
int OS_GetLockID(void);
void OS_ReleaseLockID(unsigned short lockID);
int OS_UnLockCartridge(unsigned short lockID);
int OS_UnLockCard(unsigned short lockID);
void OS_Halt(void);
void OSi_WaitVCount0(void);
void GX_SendFifo48B(const void *pSrc, void *pDest);
void GX_SendFifo64B(const void *pSrc, void *pDest);
void GXi_NopClearFifo128_(void *reg);

/* Stubs for what os_spin_lock_id.o and os_terminate_proc.o call. */
static int gUnlockCartridgeCalls, gUnlockCardCalls, gHaltCalls;
static unsigned short gUnlockCartridgeArg, gUnlockCardArg;

int OS_UnlockCartridge(unsigned short lockID)
{
    gUnlockCartridgeCalls++;
    gUnlockCartridgeArg = lockID;
    return 0x5A5A;
}

int OS_UnlockCard(unsigned short lockID)
{
    gUnlockCardCalls++;
    gUnlockCardArg = lockID;
    return 0x3C3C;
}

void OSi_AllocateCardBus(void)
{
}

int OSi_DoTryLockByWord(unsigned short lockID, void *lockWord, void (*alloc)(void), int unused)
{
    (void)lockID;
    (void)lockWord;
    (void)alloc;
    (void)unused;
    return 0;
}

void NitroPort_Halt(void)
{
    gHaltCalls++;
}

/* The SDK addresses the lock words in main memory and IME/VCOUNT in the I/O array
 * (lib/include/nitro/hw/mmap_shared.h, io_reg.h, spinLock.h). */
#define LOCK_FLAG0 ((volatile uint32_t *)(NitroPort_MainMem + 0x007FFFB0u)) /* HW_LOCK_ID_FLAG_MAIN */
#define LOCK_FLAG1 ((volatile uint32_t *)(NitroPort_MainMem + 0x007FFFB4u))
#define REG_IME    ((volatile uint32_t *)(NitroPort_IoReg + 0x208u))
#define REG_VCOUNT ((volatile uint16_t *)(NitroPort_IoReg + 0x006u))
#define LOCK_START 0x40 /* OS_MAINP_LOCK_ID_START */
#define LOCK_ERROR (-3) /* OS_LOCK_ID_ERROR */

static int gFailures;

static void report(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        gFailures++;
    }
}

/* Reference models, written loop by loop (no clz, no variable shifts of 32 or more). */
static uint32_t refClz(uint32_t x)
{
    uint32_t n = 0;
    if (x == 0) {
        return 32;
    }
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

/* Highest set bit of the first non-empty word gives the lowest free ID. */
static int refGetLockID(uint32_t w[2])
{
    int word, b;
    for (word = 0; word < 2; word++) {
        for (b = 31; b >= 0; b--) {
            if (w[word] & (1u << b)) {
                w[word] &= ~(1u << b);
                return LOCK_START + 32 * word + (31 - b);
            }
        }
    }
    return LOCK_ERROR;
}

/* The ARM's LSR by a register uses the bottom byte of the register; 32 or more gives 0. */
static void refReleaseLockID(uint32_t w[2], uint16_t id)
{
    int word = 0, i;
    uint32_t off, shift, mask = 0;

    if (id >= LOCK_START + 32) {
        word = 1;
        off = (uint32_t)id - (LOCK_START + 32);
    } else {
        off = (uint32_t)id - LOCK_START;
    }
    shift = off & 0xFFu;
    if (shift < 32) {
        mask = 0x80000000u;
        for (i = 0; i < (int)shift; i++) {
            mask >>= 1;
        }
    }
    w[word] |= mask;
}

static uint32_t gSeed = 12345u;

static uint32_t next_random(void)
{
    gSeed = gSeed * 1103515245u + 12345u;
    return gSeed >> 8;
}

static void set_flags(const uint32_t w[2])
{
    *LOCK_FLAG0 = w[0];
    *LOCK_FLAG1 = w[1];
}

static void test_count_zero_bits(void)
{
    char name[80];
    uint32_t v;
    int k, i, ok;

    /* Interesting inputs: 0, each single bit (1 .. 0x80000000) and all ones: 34 values. */
    for (k = -1; k <= 32; k++) {
        v = k < 0 ? 0u : (k < 32 ? 1u << k : 0xFFFFFFFFu);
        snprintf(name, sizeof(name), "OsCountZeroBits(0x%08X) = %u", (unsigned)v, (unsigned)refClz(v));
        report(name, OsCountZeroBits(v) == refClz(v));
    }

    ok = 1;
    for (i = 0; i < 2000; i++) {
        v = next_random();
        if (OsCountZeroBits(v) != refClz(v)) {
            ok = 0;
        }
    }
    report("OsCountZeroBits: 2000 random values match the loop reference", ok);
}

static void test_lock_id_cases(void)
{
    uint32_t w[2];
    int got;

    w[0] = 0xFFFFFFFFu; w[1] = 0xFFFF0000u;
    set_flags(w);
    got = OS_GetLockID();
    report("OS_GetLockID from the initial flags returns 0x40",
           got == 0x40 && *LOCK_FLAG0 == 0x7FFFFFFFu && *LOCK_FLAG1 == 0xFFFF0000u);
    OS_ReleaseLockID(0x40);
    report("OS_ReleaseLockID(0x40) gives the flag back", *LOCK_FLAG0 == 0xFFFFFFFFu);

    w[0] = 0; w[1] = 0xFFFF0000u;
    set_flags(w);
    got = OS_GetLockID();
    report("OS_GetLockID with word 0 empty returns 0x60 (second word)",
           got == 0x60 && *LOCK_FLAG0 == 0 && *LOCK_FLAG1 == 0x7FFF0000u);

    w[0] = 0; w[1] = 0;
    set_flags(w);
    got = OS_GetLockID();
    report("OS_GetLockID with no free ID returns OS_LOCK_ID_ERROR and changes nothing",
           got == LOCK_ERROR && *LOCK_FLAG0 == 0 && *LOCK_FLAG1 == 0);

    w[0] = 0x7FFFFFFFu; w[1] = 0;
    set_flags(w);
    OS_ReleaseLockID(0x7F);
    report("OS_ReleaseLockID(0x7F) sets bit 0 of word 1 (lowest bit of the second word)",
           *LOCK_FLAG1 == 1u && *LOCK_FLAG0 == 0x7FFFFFFFu);

    w[0] = 0; w[1] = 0;
    set_flags(w);
    OS_ReleaseLockID(0x60);
    report("OS_ReleaseLockID(0x60) sets bit 31 of word 1", *LOCK_FLAG1 == 0x80000000u);

    w[0] = 0x12345678u; w[1] = 0x9ABCDEF0u;
    set_flags(w);
    OS_ReleaseLockID(0x3F);
    report("OS_ReleaseLockID(0x3F), below the first ID, changes nothing",
           *LOCK_FLAG0 == 0x12345678u && *LOCK_FLAG1 == 0x9ABCDEF0u);

    set_flags(w);
    OS_ReleaseLockID(0x80);
    report("OS_ReleaseLockID(0x80), past the second word, changes nothing",
           *LOCK_FLAG0 == 0x12345678u && *LOCK_FLAG1 == 0x9ABCDEF0u);

    set_flags(w);
    OS_ReleaseLockID((unsigned short)LOCK_ERROR);
    report("OS_ReleaseLockID(OS_LOCK_ID_ERROR) changes nothing",
           *LOCK_FLAG0 == 0x12345678u && *LOCK_FLAG1 == 0x9ABCDEF0u);

    w[0] = 0; w[1] = 0;
    set_flags(w);
    OS_ReleaseLockID(0x160);
    report("OS_ReleaseLockID(0x160) sets bit 31 of word 1, as the ARM's byte shift does",
           *LOCK_FLAG1 == 0x80000000u && *LOCK_FLAG0 == 0);
}

static void test_lock_id_random(void)
{
    uint32_t w[2], ref[2], flags[2];
    uint16_t id;
    int i, ok_get = 1, ok_rel = 1, got, want;

    for (i = 0; i < 1000; i++) {
        w[0] = next_random() & next_random();
        w[1] = next_random() | next_random();
        ref[0] = w[0];
        ref[1] = w[1];
        set_flags(w);
        got = OS_GetLockID();
        want = refGetLockID(ref);
        flags[0] = *LOCK_FLAG0;
        flags[1] = *LOCK_FLAG1;
        if (got != want || flags[0] != ref[0] || flags[1] != ref[1]) {
            ok_get = 0;
        }
    }
    report("OS_GetLockID: 1000 random flag pairs match the loop reference", ok_get);

    for (i = 0; i < 1000; i++) {
        w[0] = next_random();
        w[1] = next_random();
        id = (uint16_t)next_random();
        ref[0] = w[0];
        ref[1] = w[1];
        refReleaseLockID(ref, id);
        set_flags(w);
        OS_ReleaseLockID(id);
        if (*LOCK_FLAG0 != ref[0] || *LOCK_FLAG1 != ref[1]) {
            ok_rel = 0;
        }
    }
    report("OS_ReleaseLockID: 1000 random IDs (valid and not) match the ARM shift model", ok_rel);
}

static void test_unlock_and_halt(void)
{
    int r;

    gUnlockCartridgeCalls = 0;
    r = OS_UnLockCartridge(0x42);
    report("OS_UnLockCartridge tail-calls OS_UnlockCartridge with the same ID and returns its result",
           r == 0x5A5A && gUnlockCartridgeCalls == 1 && gUnlockCartridgeArg == 0x42);

    gUnlockCardCalls = 0;
    r = OS_UnLockCard(0x43);
    report("OS_UnLockCard tail-calls OS_UnlockCard with the same ID and returns its result",
           r == 0x3C3C && gUnlockCardCalls == 1 && gUnlockCardArg == 0x43);

    gHaltCalls = 0;
    OS_Halt();
    report("OS_Halt calls NitroPort_Halt once and returns", gHaltCalls == 1);
}

static void test_wait_vcount0(void)
{
    *REG_IME = 0x12345678u;
    *REG_VCOUNT = 0; /* the loop exits at once */
    OSi_WaitVCount0();
    report("OSi_WaitVCount0 restores the saved IME word", *REG_IME == 0x12345678u);
}

static void test_fifo_sends(void)
{
    uint32_t src[16], dst[9];
    int i, ok;

    for (i = 0; i < 16; i++) {
        src[i] = 0x1000u + (uint32_t)i;
    }

    /* 48B: word i goes to pDest + 4 * (i % 3), so the last word at each address is 9, 10, 11. */
    memset(dst, 0xAB, sizeof(dst));
    GX_SendFifo48B(src, dst);
    ok = dst[0] == src[9] && dst[1] == src[10] && dst[2] == src[11] && dst[3] == 0xABABABABu;
    report("GX_SendFifo48B: 12 words, last word at each address kept, no store past 3 words", ok);

    /* 64B: word i goes to pDest + 4 * (i % 8), so the last word at each address is 8 .. 15. */
    memset(dst, 0xAB, sizeof(dst));
    GX_SendFifo64B(src, dst);
    ok = 1;
    for (i = 0; i < 8; i++) {
        if (dst[i] != src[8 + i]) {
            ok = 0;
        }
    }
    report("GX_SendFifo64B: 16 words, last word at each address kept, no store past 8 words",
           ok && dst[8] == 0xABABABABu);

    /* NOP clear: 128 zero words, word i goes to reg + 4 * (i % 4). */
    memset(dst, 0xFF, sizeof(dst));
    GXi_NopClearFifo128_(dst);
    report("GXi_NopClearFifo128_: first four words zero, nothing past them",
           dst[0] == 0 && dst[1] == 0 && dst[2] == 0 && dst[3] == 0 && dst[4] == 0xFFFFFFFFu);
}

int main(void)
{
    test_count_zero_bits();
    test_lock_id_cases();
    test_lock_id_random();
    test_unlock_and_halt();
    test_wait_vcount0();
    test_fifo_sends();

    if (gFailures) {
        printf("sdk small: %d case group(s) FAILED\n", gFailures);
        return 1;
    }
    printf("sdk small: all cases ok\n");
    return 0;
}
