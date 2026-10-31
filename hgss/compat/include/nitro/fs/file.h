// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/fs/file.h: FS_GetPosition and FS_End, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/fs/file.h carries FS_GetLength and the other
 * status inlines but not FS_GetPosition (ntrtwl's include/nitro/fs/file.h
 * reads prop.file.pos minus prop.file.top, same struct) and declares no
 * FS_End (ntrtwl: void FS_End(void)). FS_End's body lives in
 * hgss/compat/src/sdk_standins.c and reverses HGSS's FS_Init.
 */

#ifndef HGSS_COMPAT_FS_FILE_H
#define HGSS_COMPAT_FS_FILE_H

#include_next <nitro/fs/file.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline u32 FS_GetPosition(const FSFile *p_file)
{
    return p_file->prop.file.pos - p_file->prop.file.top;
}

void FS_End(void);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_FS_FILE_H */
