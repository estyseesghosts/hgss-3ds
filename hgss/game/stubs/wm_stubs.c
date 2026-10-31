/* WM (local wireless) stand-ins: "wireless off".
 *
 * The game talks to the DS wireless hardware through NitroSDK's WM_* API
 * (src/wireless_manager.c, src/comm_wireless.c, src/wifi_setup/,
 * src/application/main_menu/ranger_link.c, ...). On the DS each WM_ request
 * travels over PXI to the ARM7 wireless driver and completes in an async
 * callback. The port has no wireless ARM7, so real WM code would return
 * WM_ERRCODE_OPERATING and never call back: every wireless state machine
 * would stall. Instead this file answers at the WM_* API level, the way an
 * uninitialised / switched-off wireless stack does:
 *
 * - ARM7 round-trips (scan, connect, MP, DCF, key/data sharing, ...) return
 *   WM_ERRCODE_WM_DISABLE and never invoke their callback. Every game caller
 *   checks `!= WM_ERRCODE_OPERATING` first and takes its error branch, so
 *   union room, link trades/battles, Pal Pad scans and Ranger Link fail
 *   cleanly with the game's own "no partner" handling.
 * - WM_SetPortCallback / WM_SetIndCallback are local-only in real WM (no
 *   ARM7 round-trip) and return WM_ERRCODE_SUCCESS. This matters:
 *   Wireless_SetReceiveCallback spins `for (;;) {}` on any other value, and
 *   RangerLink drops to its error state. The callbacks are stored nowhere
 *   and never fire: there is no wireless traffic.
 * - WM_Init / WM_Finish are local-only and return WM_ERRCODE_SUCCESS; their
 *   results are unchecked by the game.
 * - Getters return the uninitialised-WM values: WM_GetAllowedChannel 0x8000
 *   (the game's own "no hardware" sentinel, checked in all three channel
 *   loops), WM_GetLinkLevel 0, IDs/counters 0, buffer sizes 0, pointers NULL,
 *   and an empty WMOtherElementResults (count 0, so the AP-search loop body
 *   never runs).
 * - WM_ReadStatus returns WM_ERRCODE_WM_DISABLE without touching the struct:
 *   every `do { WM_ReadStatus(); } while (state != POWER_ON)` loop in
 *   wifi_setup is guarded by a `!= WM_ERRCODE_OPERATING` check that fails
 *   first, so none of them is reachable.
 * - WMi_GetStatusAddress returns NULL, as real WM does before WM_Init; its
 *   only game caller (Wireless_StartParentRun) runs solely from the
 *   WM_SetParentParameter callback, which never fires.
 *
 * Signatures match build/upstream lib/include/nitro/wm/wm_internal.h and
 * lib/NitroSDK/include/WM.h; the WMOthersElement* layouts are copied from
 * there so the by-value return stays ABI-identical. This file includes no
 * SDK headers (they disagree with each other's types); NitroPort_* is
 * declared, not included, and resolved from the port at link time.
 */

#include <stddef.h>
#include <stdint.h>

/* Local spellings of the Nitro integer types used below. */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* WMErrCode values from lib/NitroSDK/include/WM.h. */
#define WM_ERRCODE_SUCCESS 0
#define WM_ERRCODE_OPERATING 2
#define WM_ERRCODE_WM_DISABLE 4

/* WmCallback from lib/include/nitro/wm/wm_internal.h. */
typedef void (*WmCallback)(void *arg);

/* Opaque to this file; only ever passed through as pointers. */
typedef struct WmParentParam WmParentParam;
typedef struct WmScanParam WmScanParam;
typedef struct WmScanExParam WmScanExParam;
typedef struct WmStatus WmStatus;
typedef struct WmMPReceiveBuffer WmMPReceiveBuffer;
typedef struct WMBssDesc WMBssDesc;
typedef struct WMDataSharingInfo WMDataSharingInfo;
typedef struct WMDataSet WMDataSet;
typedef struct WMKeySetBuf WMKeySetBuf;

/* WM_GetOtherElements returns this struct by value: the layout must match
 * wm_internal.h exactly (4 + 16 * 8 = 132 bytes). */
typedef struct WMOtherElement {
    u8 id;
    u8 length;
    u8 pad[2];
    u8 *body;
} WMOtherElement;

typedef struct WMOtherElementResults {
    u8 count;
    u8 pad[3];
    WMOtherElement element[16];
} WMOtherElementResults;

/* Provided by the port (3ds_port/nitro/src/port_log.c): logged once per
 * name. Declared here so this file needs no port headers. */
extern int NitroPort_Unimplemented(const char *what);

/* An ARM7 round-trip the port cannot run: report "wireless off". */
static int WirelessOff(const char *name)
{
    NitroPort_Unimplemented(name);
    return WM_ERRCODE_WM_DISABLE;
}

/* Local-only calls: succeed, as real WM does without initialisation. */
static int WirelessLocalOk(const char *name)
{
    NitroPort_Unimplemented(name);
    return WM_ERRCODE_SUCCESS;
}

int WM_Init(void *buffer, u32 dma)
{
    (void)buffer;
    (void)dma;
    return WirelessLocalOk("WM_Init");
}

int WM_Finish(void)
{
    return WirelessLocalOk("WM_Finish");
}

int WM_SetIndCallback(WmCallback callback)
{
    (void)callback;
    return WirelessLocalOk("WM_SetIndCallback");
}

int WM_SetPortCallback(u16 port, WmCallback callback, void *arg)
{
    (void)port;
    (void)callback;
    (void)arg;
    return WirelessLocalOk("WM_SetPortCallback");
}

int WM_ReadStatus(WmStatus *status)
{
    (void)status;
    return WirelessOff("WM_ReadStatus");
}

int WM_GetMPSendBufferSize(void)
{
    NitroPort_Unimplemented("WM_GetMPSendBufferSize");
    return 0;
}

int WM_GetMPReceiveBufferSize(void)
{
    NitroPort_Unimplemented("WM_GetMPReceiveBufferSize");
    return 0;
}

void *WM_ReadMPData(WmMPReceiveBuffer *buffer, u16 aid)
{
    (void)buffer;
    (void)aid;
    NitroPort_Unimplemented("WM_ReadMPData");
    return NULL;
}

u16 WM_GetAllowedChannel(void)
{
    NitroPort_Unimplemented("WM_GetAllowedChannel");
    return 0x8000;
}

u16 WM_GetLinkLevel(void)
{
    NitroPort_Unimplemented("WM_GetLinkLevel");
    return 0;
}

u16 WM_GetDispersionBeaconPeriod(void)
{
    NitroPort_Unimplemented("WM_GetDispersionBeaconPeriod");
    return 0;
}

u16 WM_GetDispersionScanPeriod(void)
{
    NitroPort_Unimplemented("WM_GetDispersionScanPeriod");
    return 0;
}

WMOtherElementResults WM_GetOtherElements(WMBssDesc *bss)
{
    WMOtherElementResults empty;
    int i;
    (void)bss;
    NitroPort_Unimplemented("WM_GetOtherElements");
    empty.count = 0;
    empty.pad[0] = 0;
    empty.pad[1] = 0;
    empty.pad[2] = 0;
    for (i = 0; i < 16; i++) {
        empty.element[i].id = 0;
        empty.element[i].length = 0;
        empty.element[i].pad[0] = 0;
        empty.element[i].pad[1] = 0;
        empty.element[i].body = NULL;
    }
    return empty;
}

u16 WM_GetNextTgid(void)
{
    NitroPort_Unimplemented("WM_GetNextTgid");
    return 0;
}

u16 WM_GetAID(void)
{
    NitroPort_Unimplemented("WM_GetAID");
    return 0;
}

u16 WM_GetConnectedAIDs(void)
{
    NitroPort_Unimplemented("WM_GetConnectedAIDs");
    return 0;
}

int WM_Enable(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_Enable");
}

int WM_Disable(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_Disable");
}

int WM_PowerOn(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_PowerOn");
}

int WM_PowerOff(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_PowerOff");
}

int WM_Initialize(void *buffer, WmCallback callback, u32 dma)
{
    (void)buffer;
    (void)callback;
    (void)dma;
    return WirelessOff("WM_Initialize");
}

int WM_InitializeForListening(void *buffer, WmCallback callback, u32 dma, int unk)
{
    (void)buffer;
    (void)callback;
    (void)dma;
    (void)unk;
    return WirelessOff("WM_InitializeForListening");
}

int WM_Reset(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_Reset");
}

int WM_End(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_End");
}

int WM_SetParentParameter(WmCallback callback, WmParentParam *param)
{
    (void)callback;
    (void)param;
    return WirelessOff("WM_SetParentParameter");
}

int WM_StartParent(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_StartParent");
}

/* The data tables lib/asm/nitro_sdk_data.s and lib/NitroSDK/asm/mb_data.s
 * name this internal entry point directly (WM_StartParent is a wrapper
 * around it), so without a stand-in the linker pulls the real wm_requests.o
 * and then wm_core.o, whose WM_* definitions collide with this file. Same
 * signature as wm_requests.c: WMErrCode WMi_StartParentEx(WmCallback, u32). */
int WMi_StartParentEx(WmCallback callback, u32 unk)
{
    (void)callback;
    (void)unk;
    return WirelessOff("WMi_StartParentEx");
}

int WM_EndParent(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_EndParent");
}

int WM_StartScan(WmCallback callback, WmScanParam *param)
{
    (void)callback;
    (void)param;
    return WirelessOff("WM_StartScan");
}

int WM_StartScanEx(WmCallback callback, WmScanExParam *param)
{
    (void)callback;
    (void)param;
    return WirelessOff("WM_StartScanEx");
}

int WM_EndScan(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_EndScan");
}

int WM_StartConnectEx(WmCallback callback, const u16 *bssDesc, const void *data, u32 unk, u16 unk2)
{
    (void)callback;
    (void)bssDesc;
    (void)data;
    (void)unk;
    (void)unk2;
    return WirelessOff("WM_StartConnectEx");
}

int WM_Disconnect(WmCallback callback, u16 aid)
{
    (void)callback;
    (void)aid;
    return WirelessOff("WM_Disconnect");
}

int WM_StartMPEx(WmCallback callback, void *recvBuf, u16 recvBufSize, void *sendBuf, u16 sendBufSize,
    u16 frequency, u16 defaultRetryCount, u32 minPollBmpMode, u32 singlePacketMode, int fixFrequency,
    u32 ignoreFatalErrorMode)
{
    (void)callback;
    (void)recvBuf;
    (void)recvBufSize;
    (void)sendBuf;
    (void)sendBufSize;
    (void)frequency;
    (void)defaultRetryCount;
    (void)minPollBmpMode;
    (void)singlePacketMode;
    (void)fixFrequency;
    (void)ignoreFatalErrorMode;
    return WirelessOff("WM_StartMPEx");
}

int WM_StartMP(WmCallback callback, void *recvBuf, u16 recvBufSize, void *sendBuf, u16 sendBufSize,
    u16 frequency)
{
    (void)callback;
    (void)recvBuf;
    (void)recvBufSize;
    (void)sendBuf;
    (void)sendBufSize;
    (void)frequency;
    return WirelessOff("WM_StartMP");
}

int WM_SetMPDataToPortEx(WmCallback callback, void *arg, const void *data, u16 length, u16 aidBitmap,
    u16 port, u16 priority)
{
    (void)callback;
    (void)arg;
    (void)data;
    (void)length;
    (void)aidBitmap;
    (void)port;
    (void)priority;
    return WirelessOff("WM_SetMPDataToPortEx");
}

int WM_EndMP(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_EndMP");
}

int WM_StartDCF(WmCallback callback, void *recvBuf, u32 recvBufSize)
{
    (void)callback;
    (void)recvBuf;
    (void)recvBufSize;
    return WirelessOff("WM_StartDCF");
}

int WM_SetDCFData(WmCallback callback, const u8 *destAdr, const u16 *sendData, u16 sendDataSize)
{
    (void)callback;
    (void)destAdr;
    (void)sendData;
    (void)sendDataSize;
    return WirelessOff("WM_SetDCFData");
}

int WM_EndDCF(WmCallback callback)
{
    (void)callback;
    return WirelessOff("WM_EndDCF");
}

int WM_StartDataSharing(WMDataSharingInfo *info, u16 port, u16 aidBitmap, u16 dataLength,
    int doubleMode)
{
    (void)info;
    (void)port;
    (void)aidBitmap;
    (void)dataLength;
    (void)doubleMode;
    return WirelessOff("WM_StartDataSharing");
}

int WM_EndDataSharing(WMDataSharingInfo *info)
{
    (void)info;
    return WirelessOff("WM_EndDataSharing");
}

int WM_StepDataSharing(WMDataSharingInfo *info, const void *sendData, void *receiveData)
{
    (void)info;
    (void)sendData;
    (void)receiveData;
    return WirelessOff("WM_StepDataSharing");
}

void *WM_GetSharedDataAddress(WMDataSharingInfo *info, WMDataSet *dataSet, int aid)
{
    (void)info;
    (void)dataSet;
    (void)aid;
    NitroPort_Unimplemented("WM_GetSharedDataAddress");
    return NULL;
}

int WM_StartKeySharing(WMKeySetBuf *buf, u16 port)
{
    (void)buf;
    (void)port;
    return WirelessOff("WM_StartKeySharing");
}

int WM_EndKeySharing(WMKeySetBuf *buf)
{
    (void)buf;
    return WirelessOff("WM_EndKeySharing");
}

int WM_SetWEPKey(WmCallback callback, u32 mode, const void *key)
{
    (void)callback;
    (void)mode;
    (void)key;
    return WirelessOff("WM_SetWEPKey");
}

int WM_SetWEPKeyEx(WmCallback callback, u32 mode, u32 keyId, const void *key)
{
    (void)callback;
    (void)mode;
    (void)keyId;
    (void)key;
    return WirelessOff("WM_SetWEPKeyEx");
}

int WM_SetGameInfo(WmCallback callback, const void *gameInfo, u16 size, u32 unk, u16 unk2, u8 unk3)
{
    (void)callback;
    (void)gameInfo;
    (void)size;
    (void)unk;
    (void)unk2;
    (void)unk3;
    return WirelessOff("WM_SetGameInfo");
}

int WM_SetBeaconIndication(WmCallback callback, u32 flag)
{
    (void)callback;
    (void)flag;
    return WirelessOff("WM_SetBeaconIndication");
}

int WM_SetLifeTime(WmCallback callback, u16 unk, u16 unk2, u16 unk3, u16 unk4)
{
    (void)callback;
    (void)unk;
    (void)unk2;
    (void)unk3;
    (void)unk4;
    return WirelessOff("WM_SetLifeTime");
}

int WM_MeasureChannel(WmCallback callback, u16 unk, u16 unk2, u16 unk3, u16 unk4)
{
    (void)callback;
    (void)unk;
    (void)unk2;
    (void)unk3;
    (void)unk4;
    return WirelessOff("WM_MeasureChannel");
}

int WM_SetEntry(WmCallback callback, u32 flag)
{
    (void)callback;
    (void)flag;
    return WirelessOff("WM_SetEntry");
}

/* Internal WM helper the game calls directly (wireless_manager.c): NULL, as
 * real WM answers before WM_Init. Reachable only from the parent-parameter
 * callback, which never fires (see above). */
void *WMi_GetStatusAddress(void)
{
    NitroPort_Unimplemented("WMi_GetStatusAddress");
    return NULL;
}
