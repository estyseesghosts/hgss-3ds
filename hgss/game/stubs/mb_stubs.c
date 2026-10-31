/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Multiboot (MB) stand-ins: "download play is unavailable".
 *
 * nintendo DS Download Play lets one DS (the parent) broadcast a "game image"
 * segment to up to 15 children over the wireless hardware and boot them from
 * it. HGSS uses it only in the Wi-Fi Setup screen's "Download Play" parent
 * flow (src/wifi_setup/wifi_setup_local_wireless.c, WifiSetupMbParent_*): the
 * parent initialises MB, answers children's join requests, reads a segment of
 * its own ROM image (MB_GetSegmentLength/MB_ReadSegment) and registers it for
 * broadcast (MB_RegisterFile).
 *
 * Neither the PC nor the 3DS has a DS-compatible multiboot transport (the 3DS
 * cannot act as a DS download-play parent either; local play there is a
 * different, higher-level protocol). Every MB_* request therefore answers the
 * way a DS whose wireless hardware is disabled does, and no callback or
 * buffer is ever touched:
 *
 * - MB_Init returns a non-zero error, so WifiSetupMbParent_Start fails into
 *   its existing error path instead of entering a state machine that can never
 *   receive a child.
 * - MB_StartParentFromIdle returns a non-zero error for the same reason.
 * - MB_GetSegmentLength returns 0, which the caller checks (`!= 0`) before it
 *   reads or registers a segment.
 * - The BOOL predicates return FALSE and the requests are refused. Their
 *   callers are the parent state callback, which nothing invokes because no
 *   child can join.
 * - MB_End, MB_SetParentCommParam, MB_CommSetParentStateCallback and
 *   MB_DisconnectChild are void cleanup/configuration calls; they are no-ops.
 * - MB_CommGetChildUser returns NULL (no child exists).
 *
 * The game declares these itself in wifi_setup_local_wireless.c (lines ~68-79)
 * and wifi_setup_share.c (lines 18-19); the signatures here match those
 * declarations, including the game's `MB_RegisterFile(const void *, void *)`
 * (the SDK header's second parameter is its W36016Data type; the game passes
 * the segment buffer it read). No SDK header is included because the flat
 * HGSS headers and lib/NitroSDK disagree about the types, and this file only
 * needs the DS-width integer types.
 *
 * Linked into the program only (tools/hgss_game.py app_sources), never into
 * libnitro.a: lib/NitroSDK/src/mb is real hardware code and must not be
 * reachable.
 */

#include <stddef.h>
#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int BOOL;

#define FALSE 0

/* The game passes FSFile pointers but only to these stand-ins; the type is
 * opaque here on purpose. */
typedef struct FSFile FSFile;

/*
 * Real DS call: initialise the MB library with a work buffer, user pointer,
 * game/type ids and a DMA channel, then put the parent into the idle state.
 * Returns 0 on success, a non-zero MB error otherwise. Stand-in: non-zero, so
 * the parent never starts.
 */
int MB_Init(void *work, void *user, u32 ggid, u32 tgid, u32 dmaNo)
{
    (void)work;
    (void)user;
    (void)ggid;
    (void)tgid;
    (void)dmaNo;
    return 1;
}

/*
 * Real DS call: tear down the MB session (beacon, DMA, work area). Stand-in:
 * nothing was set up, so there is nothing to tear down.
 */
void MB_End(void)
{
}

/*
 * Real DS call: set the parent's send segment size and maximum child count.
 * Stand-in: no beacons are sent and no children are accepted.
 */
void MB_SetParentCommParam(u16 sendSize, u16 maxChildren)
{
    (void)sendSize;
    (void)maxChildren;
}

/*
 * Real DS call: store the function MB calls when a child changes state.
 * Stand-in: no child ever joins, so the callback is never stored or called.
 */
void MB_CommSetParentStateCallback(void *callback)
{
    (void)callback;
}

/*
 * Real DS call: start broadcasting as a download-play parent on a wireless
 * channel. Returns 0 when the beacon started. Stand-in: non-zero failure.
 */
int MB_StartParentFromIdle(u32 channel)
{
    (void)channel;
    return 1;
}

/*
 * Real DS call: the length of the next segment of this program's image that
 * can be sent to a child. Stand-in: 0, the caller's "nothing to send" value.
 */
u32 MB_GetSegmentLength(FSFile *file)
{
    (void)file;
    return 0;
}

/*
 * Real DS call: read `len` bytes of the program image at the current segment
 * position into `buf`. Returns TRUE on success. Stand-in: FALSE and `buf` is
 * never written.
 */
BOOL MB_ReadSegment(FSFile *file, void *buf, u32 len)
{
    (void)file;
    (void)buf;
    (void)len;
    return FALSE;
}

/*
 * Real DS call: register the game image buffer the parent will stream to
 * children. Returns TRUE when registered. Stand-in: FALSE; neither pointer is
 * dereferenced.
 */
BOOL MB_RegisterFile(const void *game, void *buf)
{
    (void)game;
    (void)buf;
    return FALSE;
}

/*
 * Real DS call: accept or refuse a child's join/boot request. Returns TRUE
 * when the response was queued. Stand-in: FALSE; no request is answered.
 */
BOOL MB_CommResponseRequest(u16 aid, u32 ack)
{
    (void)aid;
    (void)ack;
    return FALSE;
}

/*
 * Real DS call: disconnect one child by AID. Stand-in: no child is connected.
 */
void MB_DisconnectChild(u16 aid)
{
    (void)aid;
}

/*
 * Real DS call: whether a child's download is complete and can be booted.
 * Stand-in: FALSE.
 */
BOOL MB_CommIsBootable(u16 aid)
{
    (void)aid;
    return FALSE;
}

/*
 * Real DS call: the user pointer that child `aid` supplied at join time.
 * Stand-in: NULL; no child has joined, and the caller treats NULL as "not
 * ready" (wifi_setup_share.c checks it before dereferencing).
 */
void *MB_CommGetChildUser(u16 aid)
{
    (void)aid;
    return NULL;
}
