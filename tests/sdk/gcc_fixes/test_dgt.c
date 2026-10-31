/* Host test: DGT prototypes in lib/NitroSDK/include/dgt.h under SDK_PORT_3DS.
 * The port (3ds_port/nitro/src/dgt.c) defines DGT_Hash1SetSource with (const void *,
 * unsigned long), DGT_Hash1GetDigest_R with (void *, ...) and DGT_Hash2SetSource with
 * (const unsigned char *, unsigned long); the DS u8 * parameters are a different type under GCC.
 * Each definition below repeats the port's signature exactly: with the macro they match the
 * header; without it they are "conflicting types" errors. (u32 is unsigned long, so the length
 * spelling alone already agrees; the const-ness is what changes.) */
#include "dgt.h"

void DGT_Hash1SetSource(struct DGTHash1Context *context, const void *input, unsigned long length)
{
    (void)context;
    (void)input;
    (void)length;
}

void DGT_Hash1GetDigest_R(void *digest, struct DGTHash1Context *context)
{
    (void)digest;
    (void)context;
}

void DGT_Hash2SetSource(struct DGTHash2Context *context, const unsigned char *input, unsigned long length)
{
    (void)context;
    (void)input;
    (void)length;
}

/* Unchanged by the patch: defining these checks the test itself matches the header either way. */
void DGT_Hash1Reset(struct DGTHash1Context *context)
{
    (void)context;
}

void DGT_Hash2Reset(struct DGTHash2Context *context)
{
    (void)context;
}

void DGT_Hash2GetDigest(struct DGTHash2Context *context, u8 *digest)
{
    (void)context;
    (void)digest;
}

int main(void)
{
    return 0;
}
