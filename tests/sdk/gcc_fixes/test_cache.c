/* Host test: DC_StoreRange in lib/include/nitro/os/cache.h and
 * lib/NitroSDK/include/OS_cache.h under SDK_PORT_3DS.
 * The port (3ds_port/nitro/src/port_os.c:315) defines DC_StoreRange(const void *, u32); the DS
 * void * parameter is a different type under GCC, so the definition below (the port's signature)
 * matches the headers only with the macro and is a "conflicting types" error without it. Both
 * headers are included: under the macro their declarations are identical, as on the DS side. */
#include <nitro/types.h>
#include <nitro/os/cache.h>
#include "OS_cache.h"

void DC_StoreRange(const void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

int main(void)
{
    return 0;
}
