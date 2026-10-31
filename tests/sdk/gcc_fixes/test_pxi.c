/* Host test: PXI prototypes in lib/include/nitro/pxi/fifo.h under SDK_PORT_3DS.
 * The port (3ds_port/nitro/src/pxi.c) defines these with int tags (s32 is long, a different type
 * from int under GCC even with -m32) and adds PXI_SetFifoSendCallback, whose PXIFifoEmtpyCallback
 * keeps the spelling the port uses. Each definition below repeats the port's signature exactly:
 * with the macro they match the header; without it the redefinitions are "conflicting types"
 * errors and the typedef is unknown. */
#include <nitro/types.h>
#include <nitro/pxi/fifo.h>

void PXI_SetFifoRecvCallback(int fifotag, PXIFifoCallback callback)
{
    (void)fifotag;
    (void)callback;
}

BOOL PXI_IsCallbackReady(int fifotag, PXIProc proc)
{
    (void)fifotag;
    (void)proc;
    return FALSE;
}

int PXI_SendWordByFifo(int fifotag, u32 data, BOOL err)
{
    (void)fifotag;
    (void)data;
    (void)err;
    return 0;
}

void PXI_SetFifoSendCallback(PXIFifoEmtpyCallback callback)
{
    (void)callback;
}

int main(void)
{
    PXIFifoEmtpyCallback cb = 0;
    (void)cb;
    return 0;
}
