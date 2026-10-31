/*
 * Host test for the ARM9 command sender (patch 1404): a blocking flush must hand
 * the reserve list to the virtual ARM7, and the finished-command tag must advance
 * once the reply is reaped. This is the regression that made the vendored
 * test_arm7 spin in SND_RecvCommandReply: snd_flush_command.c (a later NitroSDK
 * revision) read SND_command.c's four-byte sFreeList as a CommandState, so it never
 * saw the reserve list and the finished tag could not advance.
 *
 * Separate binary from test_arm7_sound.c: this one links the ARM9 SND_CommandInit
 * (and would collide with the ARM7 driver's same-named -DSDK_ARM7 objects).
 * Synthetic commands only; the fake PXI transport is the virtual ARM7.
 */
/* No <stdio.h>: the project include path (lib/include) shadows stdarg.h, which
 * breaks the system stdio.h. Output goes through printf, declared by hand. */
#include <nitro/types.h>
#include <stdint.h>
#include <OS_system.h>
#include <OS_cache.h>
#include <OS_emulator.h>
#include <PXI_fifo.h>
#include <SND_command.h>
#include <SND_work.h>
#include <SND_alarm.h>

int printf(const char *, ...);

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- fake ARM7 PXI transport ---- */
extern struct SNDCommand sCommandArray[];

static int sends;
static int invalidSends;
static u32 lastHead;
static int arm7Ticks;

/* A command list is only meaningful if it points at an entry of the sender's own
 * command array. The old snd_flush_command.c read a different structure and could
 * hand over a stray pointer; refusing it here keeps the regression a failed check
 * instead of a crash while walking it. */
static BOOL isValidList(u32 data)
{
    u32 base = (u32)(uintptr_t)&sCommandArray[0];
    u32 size = (u32)sizeof(sCommandArray[0]);

    return data >= base && data < base + size * SND_CMD_COUNT &&
           (data - base) % size == 0;
}

/* The virtual ARM7 takes each list synchronously and counts it finished. */
static void fakeArm7TakesList(void)
{
    if (SNDi_SharedWork != NULL) {
        (void)__atomic_add_fetch(&SNDi_SharedWork->finishedCommandTag, 1,
                                 __ATOMIC_RELEASE);
        arm7Ticks++;
    }
}

s32 PXI_SendWordByFifo(s32 fifotag, u32 data, BOOL err)
{
    (void)err;
    if (fifotag != PXI_FIFO_TAG_SOUND) {
        return PXI_FIFO_FAIL_SEND_ERR;
    }
    if (data != 0) {
        sends++;
        lastHead = data;
        if (isValidList(data)) {
            fakeArm7TakesList();
        } else {
            invalidSends++;
        }
    }
    return PXI_FIFO_SUCCESS;
}

void PXI_SetFifoRecvCallback(s32 fifotag, PXIFifoCallback callback)
{
    (void)fifotag;
    (void)callback;
}

BOOL PXI_IsCallbackReady(s32 fifotag, PXIProc proc)
{
    (void)fifotag;
    (void)proc;
    return TRUE;
}

/* ---- OS / cache stubs the sender calls ---- */
BOOL OS_IsRunOnEmulator(void)
{
    return FALSE;
}

void OS_SpinWait(u32 cycles)
{
    (void)cycles;
}

OSIntrMode OS_DisableInterrupts(void)
{
    return 0;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state)
{
    (void)state;
    return 0;
}

void DC_FlushRange(const void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void DC_InvalidateRange(void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void SNDi_CallAlarmHandler(s32 idx)
{
    (void)idx;
}

/* SND_work.c reads under the port audio lock; this test has no audio thread. */
void NitroPort_SoundSharedLock(void)
{
}

void NitroPort_SoundSharedUnlock(void)
{
}

int main(void)
{
    struct SNDCommand *cmd;
    u32 tag;

    /* SND_CommandInit queues SND_CMD_SET_SHARED_WORK and flushes it blocking.
     * A flush that does not reach PXI is exactly the old bug. */
    SND_CommandInit();
    CHECK(sends == 1, "init blocking flush sent %d lists, want 1", sends);
    CHECK(lastHead != 0, "init blocking flush sent a NULL list");
    CHECK(invalidSends == 0,
          "init blocking flush sent %d stray list pointer(s)", invalidSends);
    CHECK(lastHead == (u32)(uintptr_t)&sCommandArray[0],
          "init flush head = %p, want the first command %p",
          (void *)(uintptr_t)lastHead, (void *)&sCommandArray[0]);
    CHECK(arm7Ticks == 1, "fake ARM7 got %d lists, want 1", arm7Ticks);
    CHECK(SND_CountWaitingCommand() == 1,
          "init left %d lists in flight, want 1", (int)SND_CountWaitingCommand());

    /* With the reserve list empty, SND_GetCurrentCommandTag reports the last
     * reaped tag (0). The list just sent is in flight, but 4.2's current-tag
     * rule exposes a pending tag only while commands are reserved. */
    tag = SND_GetCurrentCommandTag();
    CHECK(tag == 0, "current tag after init = %u, want 0", (unsigned)tag);
    CHECK(SND_IsFinishedCommandTag(0), "tag 0 should be finished");
    if (sends != 1 || invalidSends != 0 ||
        lastHead != (u32)(uintptr_t)&sCommandArray[0]) {
        printf("skipping blocking receives: the flush did not send the list\n");
        printf("arm9 blocking flush: %d checks, %d failures\n", checks, fails);
        return fails ? 1 : 0;
    }
    CHECK(SND_RecvCommandReply(SND_CMD_FLAG_BLOCK) != NULL,
          "blocking receive returned nothing after the fake ARM7 finished");
    CHECK(SND_CountWaitingCommand() == 0,
          "reply left %d lists in flight, want 0", (int)SND_CountWaitingCommand());
    CHECK(SND_CountFreeCommand() == SND_CMD_COUNT,
          "free command count = %d, want %d", (int)SND_CountFreeCommand(),
          SND_CMD_COUNT);

    /* Now a real command through a blocking flush: the tag taken while the
     * reserve list is pending must be finished after the reply is reaped. */
    cmd = SND_AllocCommand(SND_CMD_FLAG_NOBLOCK);
    CHECK(cmd != NULL, "SND_AllocCommand returned NULL");
    if (cmd == NULL) {
        printf("arm9 blocking flush: %d checks, %d failures\n", checks, fails);
        return fails ? 1 : 0;
    }
    cmd->id = SND_CMD_MASTER_VOLUME;
    cmd->arg[0] = 0;
    cmd->arg[1] = 0;
    cmd->arg[2] = 0;
    cmd->arg[3] = 0;
    cmd->llNext = NULL;
    SND_PushCommand(cmd);

    tag = SND_GetCurrentCommandTag();
    CHECK(tag == 2, "current tag with a reserved command = %u, want 2",
          (unsigned)tag);
    CHECK(!SND_IsFinishedCommandTag(tag), "tag %u finished before flushing",
          (unsigned)tag);
    CHECK(SND_FlushCommand(SND_CMD_FLAG_BLOCK) == TRUE,
          "blocking flush reported failure");
    CHECK(sends == 2, "blocking flush sent %d lists, want 2", sends);
    CHECK(lastHead == (u32)(uintptr_t)cmd,
          "flushed head = %p, want the pushed command %p", (void *)(uintptr_t)lastHead,
          (void *)cmd);
    CHECK(arm7Ticks == 2, "fake ARM7 got %d lists, want 2", arm7Ticks);
    CHECK(SND_CountWaitingCommand() == 1,
          "%d lists in flight after the flush, want 1",
          (int)SND_CountWaitingCommand());
    CHECK(!SND_IsFinishedCommandTag(tag),
          "tag %u finished before its reply was reaped", (unsigned)tag);

    if (invalidSends != 0 || lastHead != (u32)(uintptr_t)cmd) {
        printf("skipping the blocking receive: the flushed head is not the pushed command\n");
        printf("arm9 blocking flush: %d checks, %d failures\n", checks, fails);
        return fails ? 1 : 0;
    }
    CHECK(SND_RecvCommandReply(SND_CMD_FLAG_BLOCK) != NULL,
          "blocking receive returned nothing for the flushed list");
    CHECK(SND_IsFinishedCommandTag(tag),
          "tag %u not finished after the blocking flush and reply", (unsigned)tag);
    CHECK(SND_CountWaitingCommand() == 0,
          "%d lists still in flight, want 0", (int)SND_CountWaitingCommand());
    CHECK(SND_CountFreeCommand() == SND_CMD_COUNT,
          "free command count = %d, want %d", (int)SND_CountFreeCommand(),
          SND_CMD_COUNT);

    printf("arm9 blocking flush: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
