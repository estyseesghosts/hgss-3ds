// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/misc.h: stand-in for the vendored port's ARM7 sound build.
 *
 * ntrtwl's NitroSDK ships include/nitro/misc.h; HGSS's SDK has no such
 * header. The vendored 3ds_port/nitro/Makefile compiles the ARM7 sound
 * sources with `-include nitro/misc.h -include stdint.h`, so without this
 * file every ARM7 TU fails before it starts. The forced include is empty on
 * purpose: HGSS's sub/lib/src/sound_*.c only need <nitro/types.h> (which
 * they include themselves) plus stdint.h, and an empty header keeps the
 * vendored Makefile working unchanged. If a future ARM7 source needs a real
 * misc name, add it here with its ntrtwl value and note the source.
 */

#ifndef HGSS_COMPAT_NITRO_MISC_H
#define HGSS_COMPAT_NITRO_MISC_H

#endif /* HGSS_COMPAT_NITRO_MISC_H */
