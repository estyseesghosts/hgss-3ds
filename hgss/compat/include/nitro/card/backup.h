// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/card/backup.h: the save-size and program/verify/erase helpers,
 * wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/card/backup.h declares the async wait/cancel
 * helpers and the read/write/write-verify inlines over
 * CARDi_RequestStreamCommand, but not the backup geometry getters (HGSS's
 * CARD_backup.c only defines CARD_GetBackupSectorSize) nor the
 * program/verify/erase entry points. The getters' bodies live in
 * hgss/compat/src/sdk_standins.c (ntrtwl's: the spec fields of the
 * identified backup, like HGSS's sector-size getter). The program/verify/
 * erase inlines below mirror HGSS's own write inline and ntrtwl's
 * (include/nitro/card/backup.h) request/mode/retry choices.
 */

#ifndef HGSS_COMPAT_CARD_BACKUP_H
#define HGSS_COMPAT_CARD_BACKUP_H

#include_next <nitro/card/backup.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 CARD_GetBackupTotalSize(void);
u32 CARD_GetBackupPageSize(void);

#ifdef __cplusplus
}
#endif

static inline BOOL CARDi_ProgramBackup(u32 dst, const void *src, u32 len,
                                       MIDmaCallback callback, void *arg, BOOL is_async)
{
    return CARDi_RequestStreamCommand((u32)src, (u32)dst, len, callback, arg, is_async,
                                      CARD_REQ_PROGRAM_BACKUP, CARD_RETRY_COUNT_MAX,
                                      CARD_REQUEST_MODE_SEND);
}

static inline BOOL CARD_ProgramBackup(u32 dst, const void *src, u32 len)
{
    return CARDi_ProgramBackup(dst, src, len, NULL, NULL, FALSE);
}

static inline BOOL CARDi_VerifyBackup(u32 dst, const void *src, u32 len,
                                      MIDmaCallback callback, void *arg, BOOL is_async)
{
    return CARDi_RequestStreamCommand((u32)src, (u32)dst, len, callback, arg, is_async,
                                      CARD_REQ_VERIFY_BACKUP, 1, CARD_REQUEST_MODE_SEND);
}

static inline BOOL CARD_VerifyBackup(u32 dst, const void *src, u32 len)
{
    return CARDi_VerifyBackup(dst, src, len, NULL, NULL, FALSE);
}

static inline BOOL CARDi_EraseBackupSector(u32 dst, u32 len,
                                           MIDmaCallback callback, void *arg, BOOL is_async)
{
    return CARDi_RequestStreamCommand(0, (u32)dst, len, callback, arg, is_async,
                                      CARD_REQ_ERASE_SECTOR_BACKUP, CARD_RETRY_COUNT_MAX,
                                      CARD_REQUEST_MODE_SPECIAL);
}

static inline BOOL CARD_EraseBackupSector(u32 dst, u32 len)
{
    return CARDi_EraseBackupSector(dst, len, NULL, NULL, FALSE);
}

#endif /* HGSS_COMPAT_CARD_BACKUP_H */
