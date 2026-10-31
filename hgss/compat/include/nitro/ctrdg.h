// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/ctrdg.h: GBA-cartridge backup/task types, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/ctrdg.h only pulls in ctrdg/common/ctrdg_common.h
 * (one function) and declares CTRDG_Init. The CTRDGBackupType and
 * CTRDG_TASK_FUNC types 3ds_port/nitro/src/ctrdg.c needs live in HGSS's flat
 * headers, but those cannot be included wholesale here: the flat CTRDG / OS
 * / MI headers redeclare structures the public headers already define
 * (e.g. struct OSLockWord, the MI cartridge-cycle types), so the wrapper
 * takes only the dependency-free CTRDG_task.h and spells out the backup
 * type enum itself, with HGSS's values from
 * lib/NitroSDK/include/CTRDG_backup.h. The IdentifyAgbBackup prototype
 * matches the port's definition. The guard is compat-specific so the chained
 * HGSS header is still processed.
 */

#ifndef HGSS_COMPAT_CTRDG_H
#define HGSS_COMPAT_CTRDG_H

#include_next <nitro/ctrdg.h>

#include <CTRDG_task.h>

typedef enum {
    CTRDG_BACKUP_TYPE_FLASH_512K,
    CTRDG_BACKUP_TYPE_FLASH_1M,
    CTRDG_BACKUP_TYPE_SRAM
} CTRDGBackupType;

u16 CTRDG_IdentifyAgbBackup(CTRDGBackupType type);

#endif /* HGSS_COMPAT_CTRDG_H */
