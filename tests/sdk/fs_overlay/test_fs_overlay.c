/*
 * Host test for the SDK_PORT_3DS ROM-archive and overlay hooks (patch 0603): the libpak
 * registration in FSi_InitRom and the port calls in FS_ClearOverlayImage,
 * FS_LoadOverlayInfo, FS_LoadOverlayImage(Async) and FS_UnloadOverlayImage.
 * Built and run by run.sh. Synthetic values only.
 *
 * The port side (3ds_port/nitro/src/port_overlay.c, port_fs.c) is stubs that record
 * calls; the rest of the filesystem (archives, files, card) is stubs too. Result values
 * are passed through from the port, and every FALSE path is checked as well.
 */
/* No <stdio.h>: the project include path (lib/include) shadows stdarg.h, which breaks the
 * system stdio.h. Output goes through write(1); printf is declared by hand. */
#include <stdint.h>
#include <unistd.h>

#include <nitro.h>

int printf(const char *, ...);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQU32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)

/* ---- the port side (stubs) ---- */
static int infoCalls, loadCalls, clearCalls, unloadedCalls;
static FSOverlayInfo *infoSeen, *loadSeen, *clearSeen, *unloadedSeen;
static MIProcessor infoTarget;
static FSOverlayID infoId;
static BOOL infoResult = TRUE;
static BOOL loadResult = TRUE;

BOOL NitroPort_OverlayInfo(FSOverlayInfo *p_ovi, MIProcessor target, FSOverlayID id)
{
    infoCalls++;
    infoSeen = p_ovi;
    infoTarget = target;
    infoId = id;
    return infoResult;
}

BOOL NitroPort_OverlayLoadImage(const FSOverlayInfo *p_ovi)
{
    loadCalls++;
    loadSeen = (FSOverlayInfo *)p_ovi;
    return loadResult;
}

void NitroPort_OverlayClearBss(const FSOverlayInfo *p_ovi)
{
    clearCalls++;
    clearSeen = (FSOverlayInfo *)p_ovi;
}

void NitroPort_OverlayUnloaded(const FSOverlayInfo *p_ovi)
{
    unloadedCalls++;
    unloadedSeen = (FSOverlayInfo *)p_ovi;
}

FSResult NitroPort_PakArchiveProc(FSFile *file, FSCommandType command)
{
    (void)file;
    (void)command;
    return FS_RESULT_SUCCESS;
}

/* ---- the rest of the filesystem (stubs) ---- */
static int initArchiveCalls, regNameCalls, setProcCalls, loadArchiveCalls;
static FSArchive *procArc;
static FS_ARCHIVE_PROC_FUNC procSeen;
static u32 procFlags;
static FSArchive *loadArc;
static u32 loadArgs[5];
static FS_ARCHIVE_READ_FUNC loadRead;
static FS_ARCHIVE_WRITE_FUNC loadWrite;
static char regName[8];
static int regNameLen;
static int cardInitCalls;
static int initFileCalls, openFastCalls, closeFileCalls;
static FSFileID openFastId;
static BOOL openFastResult = TRUE;
static FSFile *openFastFile;

void FS_InitArchive(struct FSArchive *p_arc)
{
    initArchiveCalls++;
    (void)p_arc;
}

BOOL FS_RegisterArchiveName(struct FSArchive *p_arc, const char *name, int name_len)
{
    int i;

    (void)p_arc;
    regNameCalls++;
    for (i = 0; i < 7 && i < name_len; i++) {
        regName[i] = name[i];
    }
    regName[i] = 0;
    regNameLen = name_len;
    return TRUE;
}

void FS_SetArchiveProc(struct FSArchive *p_arc, FS_ARCHIVE_PROC_FUNC proc, u32 flags)
{
    setProcCalls++;
    procArc = p_arc;
    procSeen = proc;
    procFlags = flags;
}

BOOL FS_LoadArchive(struct FSArchive *p_arc, u32 base, u32 fat, u32 fat_size, u32 fnt,
                    u32 fnt_size, FS_ARCHIVE_READ_FUNC read_func, FS_ARCHIVE_WRITE_FUNC write_func)
{
    loadArchiveCalls++;
    loadArc = p_arc;
    loadArgs[0] = base;
    loadArgs[1] = fat;
    loadArgs[2] = fat_size;
    loadArgs[3] = fnt;
    loadArgs[4] = fnt_size;
    loadRead = read_func;
    loadWrite = write_func;
    return TRUE;
}

void CARD_Init(void)
{
    cardInitCalls++;
}

void CARD_LockRom(u16 lock_id)
{
    (void)lock_id;
}

void CARD_UnlockRom(u16 lock_id)
{
    (void)lock_id;
}

BOOL CARD_IsPulledOut(void)
{
    return FALSE;
}

void CARDi_ReadRom(u32 dma, const void *src, void *dst, u32 len, MIDmaCallback callback,
                   void *arg, BOOL is_async)
{
    (void)dma;
    (void)src;
    (void)dst;
    (void)len;
    (void)callback;
    (void)arg;
    (void)is_async;
}

void FS_InitFile(FSFile *p_file)
{
    initFileCalls++;
    (void)p_file;
}

BOOL FS_OpenFileFast(FSFile *p_file, FSFileID file_id)
{
    openFastCalls++;
    openFastFile = p_file;
    openFastId = file_id;
    return openFastResult;
}

BOOL FS_CloseFile(FSFile *p_file)
{
    closeFileCalls++;
    (void)p_file;
    return TRUE;
}

int FS_ReadFile(FSFile *p_file, void *dst, s32 len)
{
    (void)p_file;
    (void)dst;
    (void)len;
    return 0;
}

int FS_ReadFileAsync(FSFile *p_file, void *dst, s32 len)
{
    (void)p_file;
    (void)dst;
    (void)len;
    return 0;
}

u32 FS_LoadArchiveTables(struct FSArchive *p_arc, void *p_mem, u32 max_size)
{
    (void)p_arc;
    (void)p_mem;
    (void)max_size;
    return 0;
}

void FS_NotifyArchiveAsyncEnd(struct FSArchive *p_arc, FSResult ret)
{
    (void)p_arc;
    (void)ret;
}

BOOL FS_OpenFileDirect(FSFile *p_file, FSArchive *p_arc, u32 image_top, u32 image_bottom,
                       u32 file_index)
{
    (void)p_file;
    (void)p_arc;
    (void)image_top;
    (void)image_bottom;
    (void)file_index;
    return FALSE;
}

BOOL FS_SuspendArchive(struct FSArchive *p_arc)
{
    (void)p_arc;
    return TRUE;
}

BOOL FS_ResumeArchive(struct FSArchive *p_arc)
{
    (void)p_arc;
    return TRUE;
}

void DC_InvalidateRange(void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void DC_FlushRange(const void *data, u32 size)
{
    (void)data;
    (void)size;
}

void IC_InvalidateRange(void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

static int cpuFillCalls;
void MI_CpuFill8(void *dest, u8 data, u32 size)
{
    cpuFillCalls++;
    (void)dest;
    (void)data;
    (void)size;
}

void MI_CpuCopy8(const void *src, void *dst, u32 size)
{
    (void)src;
    (void)dst;
    (void)size;
}

void MIi_UncompressBackward(void *p)
{
    (void)p;
}

void DGT_Hash2CalcHmac(void *digest, const void *bin_ptr, u32 bin_len, const void *key_ptr,
                       u32 keylen)
{
    (void)digest;
    (void)bin_ptr;
    (void)bin_len;
    (void)key_ptr;
    (void)keylen;
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

s32 OS_GetLockID(void)
{
    return 0;
}

void OS_Terminate(void)
{
}

/* Linker-provided digest table (multiboot path only; never read here). */
u8 SDK_OVERLAY_DIGEST[1];
u8 SDK_OVERLAY_DIGEST_END[1];

/* Destructor chain (FS_EndOverlay walks it; empty here). */
void *__global_destructor_chain;

/* ---- the functions under test ---- */
void FSi_InitRom(u32 default_dma_no);
void FS_ClearOverlayImage(FSOverlayInfo *p_ovi);
BOOL FS_LoadOverlayInfo(FSOverlayInfo *p_ovi, MIProcessor target, FSOverlayID id);
BOOL FS_LoadOverlayImageAsync(FSOverlayInfo *p_ovi, FSFile *p_file);
BOOL FS_LoadOverlayImage(FSOverlayInfo *p_ovi);
BOOL FS_UnloadOverlayImage(FSOverlayInfo *p_ovi);
extern FSArchive fsi_arc_rom;

static void CheckInitRom(void)
{
    FSi_InitRom(4);
    CHECK(initArchiveCalls == 1, "init archive calls %d, want 1", initArchiveCalls);
    CHECK(regNameCalls == 1, "reg name calls %d, want 1", regNameCalls);
    CHECK(regName[0] == 'r' && regName[1] == 'o' && regName[2] == 'm' && regName[3] == 0,
          "archive name is not rom");
    CHECK(regNameLen == 3, "name len %d, want 3", regNameLen);
    CHECK(setProcCalls == 1, "set proc calls %d, want 1", setProcCalls);
    CHECK(procArc == &fsi_arc_rom, "proc archive is not fsi_arc_rom");
    CHECK(procSeen == NitroPort_PakArchiveProc, "proc is not the pak proc");
    EQU32("proc flags", procFlags, (u32)FS_ARCHIVE_PROC_ALL);
    CHECK(loadArchiveCalls == 1, "load archive calls %d, want 1", loadArchiveCalls);
    CHECK(loadArc == &fsi_arc_rom, "load archive is not fsi_arc_rom");
    CHECK(loadArgs[0] == 0 && loadArgs[1] == 0 && loadArgs[2] == 0 && loadArgs[3] == 0 &&
          loadArgs[4] == 0, "load archive args are not all zero");
    CHECK(loadRead == NULL && loadWrite == NULL, "load archive callbacks are not NULL");
    /* No cartridge on the port: CARD_Init never runs. */
    CHECK(cardInitCalls == 0, "card init calls %d, want 0", cardInitCalls);
}

static void CheckClear(void)
{
    FSOverlayInfo ovi;
    u8 buffer[64];
    u32 i;

    for (i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0xA5;
    }
    ovi.header.ram_address = buffer;
    ovi.header.ram_size = sizeof(buffer);
    ovi.header.bss_size = 0;
    clearCalls = 0;
    cpuFillCalls = 0;
    FS_ClearOverlayImage(&ovi);
    CHECK(clearCalls == 1, "clear calls %d, want 1", clearCalls);
    CHECK(clearSeen == &ovi, "clear overlay mismatch");
    /* The DS body (cache ops + MI_CpuFill8) does not run. */
    CHECK(cpuFillCalls == 0, "cpu fill calls %d, want 0", cpuFillCalls);
    CHECK(buffer[0] == 0xA5, "buffer was written");
}

static void CheckInfo(void)
{
    FSOverlayInfo ovi;

    infoCalls = 0;
    infoResult = TRUE;
    CHECK(FS_LoadOverlayInfo(&ovi, MI_PROCESSOR_ARM9, 7) == TRUE, "info TRUE failed");
    CHECK(infoCalls == 1, "info calls %d, want 1", infoCalls);
    CHECK(infoSeen == &ovi, "info overlay mismatch");
    CHECK(infoTarget == MI_PROCESSOR_ARM9, "info target mismatch");
    EQU32("info id", infoId, 7);
    infoResult = FALSE;
    CHECK(FS_LoadOverlayInfo(&ovi, MI_PROCESSOR_ARM9, 7) == FALSE, "info FALSE failed");
    CHECK(infoCalls == 2, "info calls %d, want 2", infoCalls);
}

static void CheckImage(void)
{
    FSOverlayInfo ovi;

    loadCalls = 0;
    loadResult = TRUE;
    CHECK(FS_LoadOverlayImage(&ovi) == TRUE, "image TRUE failed");
    CHECK(loadCalls == 1, "load calls %d, want 1", loadCalls);
    CHECK(loadSeen == &ovi, "load overlay mismatch");
    loadResult = FALSE;
    CHECK(FS_LoadOverlayImage(&ovi) == FALSE, "image FALSE failed");
}

static void CheckImageAsync(void)
{
    FSOverlayInfo ovi;
    FSFile file;

    ovi.header.file_id = 0x1234;
    loadCalls = 0;
    initFileCalls = 0;
    openFastCalls = 0;
    loadResult = TRUE;
    openFastResult = TRUE;
    CHECK(FS_LoadOverlayImageAsync(&ovi, &file) == TRUE, "async TRUE failed");
    CHECK(loadCalls == 1, "async load calls %d, want 1", loadCalls);
    CHECK(loadSeen == &ovi, "async overlay mismatch");
    CHECK(initFileCalls == 1, "async init file calls %d, want 1", initFileCalls);
    CHECK(openFastCalls == 1, "async open calls %d, want 1", openFastCalls);
    CHECK(openFastFile == &file, "async file mismatch");
    EQU32("async file id", openFastId.file_id, 0x1234);
    /* A failed load never opens. */
    loadResult = FALSE;
    openFastCalls = 0;
    CHECK(FS_LoadOverlayImageAsync(&ovi, &file) == FALSE, "async load FALSE failed");
    CHECK(openFastCalls == 0, "async open calls %d, want 0", openFastCalls);
    /* A failed open fails the load. */
    loadResult = TRUE;
    openFastResult = FALSE;
    CHECK(FS_LoadOverlayImageAsync(&ovi, &file) == FALSE, "async open FALSE failed");
}

static void CheckUnload(void)
{
    FSOverlayInfo ovi;

    ovi.header.ram_address = (u8 *)0x1000;
    ovi.header.ram_size = 0x100;
    ovi.header.bss_size = 0;
    unloadedCalls = 0;
    CHECK(FS_UnloadOverlayImage(&ovi) == TRUE, "unload failed");
    CHECK(unloadedCalls == 1, "unloaded calls %d, want 1", unloadedCalls);
    CHECK(unloadedSeen == &ovi, "unloaded overlay mismatch");
}

int main(void)
{
    CheckInitRom();
    CheckClear();
    CheckInfo();
    CheckImage();
    CheckImageAsync();
    CheckUnload();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
