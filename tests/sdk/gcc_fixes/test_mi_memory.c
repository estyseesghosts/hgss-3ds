/* Host test: MIi_Cpu* prototypes in lib/include/nitro/mi/memory.h under SDK_PORT_3DS.
 * The port (3ds_port/nitro/src/mi_memory.c) defines these with plain void pointer parameters
 * (and adds the two Send routines, which the DS header does not declare); the DS halfword and
 * word pointer parameters are a different type from void pointers under GCC even with -m32
 * (u32 is long), so each definition below repeats the port's signature exactly. With the macro
 * they match the header; without it every redefinition is a "conflicting types" error, and the
 * Send references are undeclared. */
#include <nitro/mi/memory.h>

void MIi_CpuClear16(u16 data, void *destp, u32 size)
{
    u16 *d = destp;
    (void)data;
    (void)d;
    (void)size;
}

void MIi_CpuCopy16(const void *srcp, void *destp, u32 size)
{
    (void)srcp;
    (void)destp;
    (void)size;
}

void MIi_CpuClear32(u32 data, void *destp, u32 size)
{
    (void)data;
    (void)destp;
    (void)size;
}

void MIi_CpuCopy32(const void *srcp, void *destp, u32 size)
{
    (void)srcp;
    (void)destp;
    (void)size;
}

void MIi_CpuClearFast(u32 data, void *destp, u32 size)
{
    (void)data;
    (void)destp;
    (void)size;
}

void MIi_CpuCopyFast(const void *srcp, void *destp, u32 size)
{
    (void)srcp;
    (void)destp;
    (void)size;
}

/* The Send routines exist only under the macro; a bare reference checks the declaration. */
void (*sSend16Ref)(const void *srcp, volatile void *destp, u32 size) = MIi_CpuSend16;
void (*sSend32Ref)(const void *srcp, volatile void *destp, u32 size) = MIi_CpuSend32;

int main(void)
{
    (void)sSend16Ref;
    (void)sSend32Ref;
    return 0;
}
