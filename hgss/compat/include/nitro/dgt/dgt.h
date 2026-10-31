// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/dgt/dgt.h: MD5 ("Hash1") and SHA-1 ("Hash2") entry points for the
 * port's C hash implementation, under the filename the port uses.
 *
 * The context structures come straight from HGSS's flat dgt.h
 * (lib/NitroSDK/include/dgt.h), included below with angle brackets (a quoted
 * "dgt.h" would find THIS file first and include itself). Under
 * SDK_PORT_3DS the flat `struct DGTHash2Context` is an anonymous union of
 * the ntrtwl fields 3ds_port/nitro/src/dgt.c works (h0..h4, Nl, Nh, num,
 * data, dummy) and the HGSS fields the DS game uses
 * (Intermediate_Hash/Length_Low/...), so both worlds share one layout and
 * one sizeof (see patch 1202); the flat header also declares the six
 * DGT_Hash* functions with the port's (const void *, unsigned long)
 * signatures under the macro. The typedefs below give the port's bare
 * DGTHash1Context/DGTHash2Context names (the flat header only declares the
 * struct tags). The remaining declarations (HMAC helpers, overlay-table
 * mode) exist only here; the six DGT_Hash* lines repeat the flat header's
 * port-macro signatures so this file still documents what dgt.c defines.
 */

#ifndef NITRO_DGT_DGT_H_
#define NITRO_DGT_DGT_H_

#include <nitro/types.h>
#include <nitro/dgt/common.h>
#include <dgt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DGTHash1Context DGTHash1Context;
typedef struct DGTHash2Context DGTHash2Context;

void DGT_Hash1Reset(DGTHash1Context *);
void DGT_Hash1SetSource(DGTHash1Context *, const void *, unsigned long);
void DGT_Hash1GetDigest_R(void *digest, DGTHash1Context *);

void DGT_Hash2Reset(DGTHash2Context *);
void DGT_Hash2SetSource(DGTHash2Context *, const unsigned char *, unsigned long);
void DGT_Hash2GetDigest(DGTHash2Context *, unsigned char *digest);

void DGT_Hash1CalcHmac(void *digest, void *bin_ptr, int bin_len, void *key_ptr, int key_len);
void DGT_Hash2CalcHmac(void *digest, void *bin_ptr, int bin_len, void *key_ptr, int key_len);

int DGT_SetOverlayTableMode(int flag);

#ifdef __cplusplus
}
#endif

#endif /* NITRO_DGT_DGT_H_ */
