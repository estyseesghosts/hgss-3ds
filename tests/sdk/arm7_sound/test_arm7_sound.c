/*
 * Host test for the SDK_PORT_3DS ARM7 sound driver (patch 1401): the sound-register
 * redirect, the SND_Init threading skip, the command-queue pop, the alarm routing, the
 * channel-start transitions, the full pointer setup and the shared-work lock.
 * Built and run by run.sh. Synthetic values only.
 *
 * Data the SDK leaves to the sound data object or the linker (work structs, tables,
 * thread stacks, message queues) is defined here with matching types and sizes. The OS,
 * PXI, MI, cache, power and SVC imports the checks never exercise are stubs; the port
 * audio service (NitroPort_SoundPopCommand and friends) is stubs that record calls.
 * The sound array stands in for the port mixer registers. Sound setup and driver calls
 * go to the real linked objects, so the register effects are checked, not stubbed.
 */
/* No <stdio.h>: the project include path (lib/include) shadows stdarg.h, which breaks the
 * system stdio.h. Output goes through write(1); printf is declared by hand. */
#include <stdint.h>
#include <unistd.h>

/* As in the sound sources: <nitro/types.h> first, then only the headers needed.
 * A full <nitro.h> would pull the ARM9 snd/common headers, which collide with the
 * ARM7 SND_*_shared.h types used here. */
#include <nitro/types.h>
#include <OS_thread.h>
#include <OS_message.h>
#include <OS_alarm.h>
#include <OS_tick.h>
#include <nitro/pxi/fifo.h>
#include <nitro/mi/dma.h>
#include <nitro/SND_command_shared.h>
#include <nitro/SND_work_shared.h>
#include <nitro/SND_bank_shared.h>
#include <nitro/SND_exChannel_shared.h>

int printf(const char *, ...);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQU32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)

/* ---- the port mixer registers and audio service (stubs) ---- */
volatile u8 NitroPort_SoundRegisters[0x600] __attribute__((aligned(4)));

static const struct SNDCommand *popList[4];
static int popCount, popCalls;

const struct SNDCommand *NitroPort_SoundPopCommand(void)
{
    if (popCount <= 0) {
        return NULL;
    }
    popCalls++;
    popCount--;
    return popList[popCount];
}

static int chStartCalls, chStartCh[16];

void NitroPort_SoundChannelStart(int chNo)
{
    if (chStartCalls < 16) {
        chStartCh[chStartCalls] = chNo;
    }
    chStartCalls++;
}

static int alarmStartCalls, alarmStopCalls;
static int alarmStartNo, alarmStopNo;
static u64 alarmStartTick, alarmStartPeriod;
static u32 alarmStartMsg;

void NitroPort_SoundAlarmStart(int alarmNo, u64 tick, u64 period, u32 id)
{
    alarmStartCalls++;
    alarmStartNo = alarmNo;
    alarmStartTick = tick;
    alarmStartPeriod = period;
    alarmStartMsg = id;
}

void NitroPort_SoundAlarmStop(int alarmNo)
{
    alarmStopCalls++;
    alarmStopNo = alarmNo;
}

static int sharedLocks, sharedUnlocks;

void NitroPort_SoundSharedLock(void)
{
    sharedLocks++;
}

void NitroPort_SoundSharedUnlock(void)
{
    sharedUnlocks++;
}

/* ---- sound data object contents (normally from sound data / the linker) ---- */
struct SNDWork SNDi_Work;
extern struct SNDSharedWork *SNDi_SharedWork;
static struct SNDSharedWork sharedWork;
u32 u$3681;
int initialized$3619;
u32 sWeakLockChannel[2];
int sMasterPan;
u8 sOrgPan[16];
u8 sOrgVolume[16];
int sSurroundDecay;
s8 SinTable[65];
const u8 attack_table$3790[19] = {0};
const u8 shift$4005[4] = {0};
const u8 channel_order$3829[16] = {0};
const s16 SNDi_DecibelTable[128] = {0};
OSMessageQueue sndMesgQueue;
OSMessageQueue sCommandMesgQueue;
OSMessage sndMesgBuffer[8];
OSMessage sCommandMesgBuffer[8];
OSAlarm sndAlarm;

/* SeqCacheView repeats sound_seq_player.c: the cache bounds are addressed through
 * sMmlPrintEnable, the word that precedes the cache. */
typedef struct TestSeqCacheView {
    u32 mmlPrintEnable;
    const u8 *start;
    const u8 *end;
    u32 words[4];
} TestSeqCacheView;

TestSeqCacheView sMmlPrintEnable;
u8 seqCache[32];

/* ---- OS stubs ---- */
OSIntrMode OS_DisableInterrupts(void)
{
    return 0;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state)
{
    (void)state;
    return 0;
}

void OS_InitMessageQueue(OSMessageQueue *queue, OSMessage *buffer, s32 capacity)
{
    (void)queue;
    (void)buffer;
    (void)capacity;
}

BOOL OS_ReceiveMessage(OSMessageQueue *queue, OSMessage *message, s32 flags)
{
    (void)queue;
    (void)message;
    (void)flags;
    return FALSE;
}

BOOL OS_SendMessage(OSMessageQueue *queue, OSMessage message, s32 flags)
{
    (void)queue;
    (void)message;
    (void)flags;
    return TRUE;
}

void OS_CreateThread(OSThread *thread, void (*func)(void *), void *arg, void *stack,
                     u32 stackSize, u32 prio)
{
    (void)thread;
    (void)func;
    (void)arg;
    (void)stack;
    (void)stackSize;
    (void)prio;
}

void OS_WakeupThreadDirect(OSThread *thread)
{
    (void)thread;
}

void OS_CreateAlarm(OSAlarm *alarm)
{
    (void)alarm;
}

void OS_CancelAlarm(OSAlarm *alarm)
{
    (void)alarm;
}

void OS_SetAlarm(OSAlarm *alarm, OSTick tick, OSAlarmHandler handler, void *arg)
{
    (void)alarm;
    (void)tick;
    (void)handler;
    (void)arg;
}

void OS_SetPeriodicAlarm(OSAlarm *alarm, OSTick start, OSTick period, OSAlarmHandler handler,
                         void *arg)
{
    (void)alarm;
    (void)start;
    (void)period;
    (void)handler;
    (void)arg;
}

OSTick OS_GetTick(void)
{
    return 0;
}

void OS_SpinWait(u32 cycles)
{
    (void)cycles;
}

/* ---- PXI / MI / cache / power / SVC stubs ---- */
int PXI_SendWordByFifo(int fifotag, u32 data, BOOL err)
{
    (void)fifotag;
    (void)data;
    (void)err;
    return 0;
}

void MIi_CpuCopy32(const void *srcp, void *destp, u32 size)
{
    const u8 *src = (const u8 *)srcp;
    u8 *dst = (u8 *)destp;
    u32 i;

    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

void DC_FlushRange(const void *data, u32 size)
{
    (void)data;
    (void)size;
}

void DC_InvalidateRange(void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void PMi_ResetControl(u32 bit)
{
    (void)bit;
}

void PMi_SetControl(u32 bit)
{
    (void)bit;
}

u32 SVC_GetPitchTable(int index)
{
    return (u32)index;
}

u32 SVC_GetVolumeTable(int index)
{
    return (u32)index;
}

void SVC_ResetSoundBias(u32 delay)
{
    (void)delay;
}

void SVC_SetSoundBias(u32 delay)
{
    (void)delay;
}

/* ---- bank stubs (sound_bank.c is not under test) ---- */
int SND_ReadInstData(struct SNDBankData *bank, int index, int key, struct SNDInstData *out)
{
    (void)bank;
    (void)index;
    (void)key;
    (void)out;
    return 0;
}

int SND_NoteOn(struct SNDExChannel *chn, int key, int velocity, int length,
               const struct SNDBankData *bank, const struct SNDInstData *inst)
{
    (void)chn;
    (void)key;
    (void)velocity;
    (void)length;
    (void)bank;
    (void)inst;
    return 0;
}

/* ---- the functions under test ---- */
void SND_Init(int priority);
void SND_CommandProc(void);
void SND_SetupAlarm(int index, OSTick tick, OSTick period, int id);
void SND_StartAlarm(int index);
void SND_StopAlarm(int index);
u32 SND_GetPlayerStatus(void);
u16 SND_GetChannelStatus(void);
u32 SND_GetPlayerTickCounter(u32 playerId);
u32 SNDi_GetFinishedCommandTag(void);

static void CheckInit(void)
{
    int i;

    /* Poison the state the inits clear, so the checks observe them running. */
    SNDi_Work.channels[0].flags.syncFlag = 7;
    SNDi_Work.channels[0].flags.active = 1;
    sWeakLockChannel[0] = 5;
    sWeakLockChannel[1] = 6;
    SNDi_Work.players[0].flags.active = 1;
    SNDi_Work.alarms[3].enable = 1;
    SNDi_Work.alarms[3].id = 9;
    for (i = 0; i < 0x600; i++) {
        NitroPort_SoundRegisters[i] = 0;
    }
    sMasterPan = -1;
    initialized$3619 = 0;
    SNDi_SharedWork = NULL;
    SND_Init(5);
    CHECK(SNDi_Work.channels[0].flags.syncFlag == 0, "exchannel sync not cleared");
    CHECK(SNDi_Work.channels[0].flags.active == 0, "exchannel active not cleared");
    CHECK(sWeakLockChannel[0] == 0 && sWeakLockChannel[1] == 0, "weak locks not cleared");
    CHECK(SNDi_Work.players[0].flags.active == 0, "player not cleared");
    CHECK(SNDi_Work.alarms[3].enable == 0, "alarm not cleared");
    CHECK(SNDi_Work.alarms[3].id == 0, "alarm id not cleared");
    CHECK(SNDi_SharedWork == NULL, "shared work touched");
    /* Enable, output selector (0,0,0,0) and master volume 127 in the registers. */
    EQU32("master enable", NitroPort_SoundRegisters[0x501], 0x80);
    EQU32("master vol", NitroPort_SoundRegisters[0x500], 127);
    /* Once only. */
    SNDi_Work.channels[1].flags.syncFlag = 7;
    SND_Init(5);
    CHECK(SNDi_Work.channels[1].flags.syncFlag == 7, "second init ran again");
}

static struct SNDCommand cmdShared, cmdStart, cmdPcm, cmdStop;

static void CheckCommandProc(void)
{
    int i;

    cmdShared.llNext = NULL;
    cmdShared.id = SND_CMD_SET_SHARED_WORK;
    cmdShared.arg[0] = (u32)&sharedWork;
    cmdStart.llNext = NULL;
    cmdStart.id = SND_CMD_START_TIMER;
    cmdStart.arg[0] = 0x3;
    cmdStart.arg[1] = 0x3;
    cmdStart.arg[2] = 0x0;
    cmdStop.llNext = NULL;
    cmdStop.id = SND_CMD_STOP_TIMER;
    cmdStop.arg[0] = 0x3;
    cmdStop.arg[1] = 0x3;
    cmdStop.arg[2] = 0x0;
    cmdStop.arg[3] = 0;
    cmdPcm.llNext = NULL;
    cmdPcm.id = SND_CMD_SETUP_CHANNEL_PCM;
    cmdPcm.arg[0] = 2;
    cmdPcm.arg[1] = 0x08FFFFFF;
    cmdPcm.arg[2] = 0x1000;
    cmdPcm.arg[3] = 0;

    for (i = 0; i < 0x600; i++) {
        NitroPort_SoundRegisters[i] = 0;
    }
    SNDi_SharedWork = NULL;
    sharedWork.finishedCommandTag = 0;
    sMasterPan = -1;
    sSurroundDecay = 0;
    chStartCalls = 0;

    /* Shared work, then channel starts. */
    popList[0] = &cmdStart;
    popList[1] = &cmdShared;
    popCount = 2;
    popCalls = 0;
    SND_CommandProc();
    CHECK(popCalls == 2, "pop calls %d, want 2", popCalls);
    CHECK(SNDi_SharedWork == &sharedWork, "shared work not set");
    EQU32("command tag", sharedWork.finishedCommandTag, 2);
    CHECK(chStartCalls == 2, "channel starts %d, want 2", chStartCalls);
    CHECK(chStartCh[0] == 0 && chStartCh[1] == 1, "channel start order");
    EQU32("ch0 enable", NitroPort_SoundRegisters[0x403], 0x80);
    EQU32("ch1 enable", NitroPort_SoundRegisters[0x413], 0x80);
    EQU32("cap0", NitroPort_SoundRegisters[0x508], 0x80);
    EQU32("cap1", NitroPort_SoundRegisters[0x509], 0x80);

    /* A PCM setup keeps all 32 pointer bits (the DS mask is 27). */
    popList[0] = &cmdPcm;
    popCount = 1;
    SND_CommandProc();
    EQU32("pcm src", *(u32 *)&NitroPort_SoundRegisters[0x400 + 2 * 16 + 4], 0x08FFFFFF);
    EQU32("command tag2", sharedWork.finishedCommandTag, 3);

    /* A second start on running channels does not restart them. */
    chStartCalls = 0;
    popList[0] = &cmdStart;
    popCount = 1;
    SND_CommandProc();
    CHECK(chStartCalls == 0, "restarts %d, want 0", chStartCalls);

    /* Stopping clears the enables and the capture bits. */
    popList[0] = &cmdStop;
    popCount = 1;
    SND_CommandProc();
    EQU32("ch0 stopped", NitroPort_SoundRegisters[0x403], 0);
    EQU32("ch1 stopped", NitroPort_SoundRegisters[0x413], 0);
    EQU32("command tag3", sharedWork.finishedCommandTag, 5);
}

static void CheckAlarms(void)
{
    alarmStartCalls = 0;
    alarmStopCalls = 0;
    SND_SetupAlarm(3, 1000, 100, 5);
    CHECK(alarmStartCalls == 0, "setup started an alarm");
    SND_StartAlarm(3);
    CHECK(alarmStartCalls == 1, "start calls %d, want 1", alarmStartCalls);
    CHECK(alarmStartNo == 3, "start no %d, want 3", alarmStartNo);
    CHECK(alarmStartTick == 1000, "start tick %d, want 1000", (int)alarmStartTick);
    CHECK(alarmStartPeriod == 100, "start period %d, want 100", (int)alarmStartPeriod);
    EQU32("start msg", alarmStartMsg, (5u << 8) | 3u);
    /* Restarting stops the old one first. */
    SND_StartAlarm(3);
    CHECK(alarmStopCalls == 1, "restart stop %d, want 1", alarmStopCalls);
    CHECK(alarmStopNo == 3, "stop no %d, want 3", alarmStopNo);
    CHECK(alarmStartCalls == 2, "restart start %d, want 2", alarmStartCalls);
    SND_StopAlarm(3);
    CHECK(alarmStopCalls == 2, "stop calls %d, want 2", alarmStopCalls);
    /* Stopped twice is once. */
    SND_StopAlarm(3);
    CHECK(alarmStopCalls == 2, "double stop %d, want 2", alarmStopCalls);
}

static void CheckSharedLock(void)
{
    sharedWork.playerStatus = 0x11111111;
    sharedWork.channelStatus = 0x2222;
    sharedWork.players[2].tickCounter = 0x33333333;
    sharedWork.finishedCommandTag = 0x44444444;
    SNDi_SharedWork = &sharedWork;
    sharedLocks = 0;
    sharedUnlocks = 0;
    EQU32("player status", SND_GetPlayerStatus(), 0x11111111);
    EQU32("channel status", SND_GetChannelStatus(), 0x2222);
    EQU32("tick counter", SND_GetPlayerTickCounter(2), 0x33333333);
    EQU32("finished tag", SNDi_GetFinishedCommandTag(), 0x44444444);
    CHECK(sharedLocks == 4, "locks %d, want 4", sharedLocks);
    CHECK(sharedUnlocks == 4, "unlocks %d, want 4", sharedUnlocks);
}

int main(void)
{
    CheckInit();
    CheckCommandProc();
    CheckAlarms();
    CheckSharedLock();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
