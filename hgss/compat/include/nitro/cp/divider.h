// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/cp/divider.h: divider inline helpers, under the ntrtwl path.
 *
 * HGSS has only lib/include/nitro/cp/context.h; the register names this
 * file's helpers use (REG_DIV_*_ADDR, REG_CP_DIVCNT_* in
 * lib/include/nitro/hw/ARM9/io_reg.h, reached via <nitro/ioreg.h>) match
 * ntrtwl's, so the helpers are ntrtwl's
 * (include/nitro/cp/divider.h) except for the <stdint.h> include: HGSS's
 * lib/include/nitro/types.h provides no INT64_MIN (the Platinum port got it
 * from its patched types.h), and 3ds_port/nitro/src/port_cp.c needs it.
 * The port's software hooks call CPi_PortUpdateDiv after every operand
 * write, exactly as Platinum's patch 0002 does to its SDK copy (no divider
 * hardware on the port; this directory is only on the port build's include
 * path, so the DS build is unaffected).
 */

#ifndef NITRO_CP_DIVIDER_H_
#define NITRO_CP_DIVIDER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <nitro/ioreg.h>

void CPi_PortUpdateDiv(void);
#define CPi_PORT_DIV_WRITTEN() CPi_PortUpdateDiv()

#define CP_DIV_32_32BIT_MODE    (0UL << REG_CP_DIVCNT_MODE_SHIFT)
#define CP_DIV_64_32BIT_MODE    (1UL << REG_CP_DIVCNT_MODE_SHIFT)
#define CP_DIV_64_64BIT_MODE    (2UL << REG_CP_DIVCNT_MODE_SHIFT)

static inline void CP_SetDivControl(u16 parameter)
{
    reg_CP_DIVCNT = parameter;
    CPi_PORT_DIV_WRITTEN();
}

static inline void CP_SetDivImm32_32_NS_(u32 numer, u32 denom)
{
    *(REGType32 *)REG_DIV_NUMER_ADDR = numer;
    *(REGType64 *)REG_DIV_DENOM_ADDR = denom;
    CPi_PORT_DIV_WRITTEN();
}

static inline void CP_SetDivImm64_32_NS_(u64 numer, u32 denom)
{
    *(REGType64 *)REG_DIV_NUMER_ADDR = numer;
    *(REGType64 *)REG_DIV_DENOM_ADDR = denom;
    CPi_PORT_DIV_WRITTEN();
}

static inline void CP_SetDivImm64_64_NS_(u64 numer, u64 denom)
{
    *(REGType64 *)REG_DIV_NUMER_ADDR = numer;
    *(REGType64 *)REG_DIV_DENOM_ADDR = denom;
    CPi_PORT_DIV_WRITTEN();
}

static inline void CP_SetDivImm32_32(u32 numer, u32 denom)
{
    CP_SetDivImm32_32_NS_(numer, denom);
}

static inline void CP_SetDivImm64_32(u64 numer, u32 denom)
{
    CP_SetDivImm64_32_NS_(numer, denom);
}

static inline void CP_SetDivImm64_64(u64 numer, u64 denom)
{
    CP_SetDivImm64_64_NS_(numer, denom);
}

static inline void CP_SetDiv32_32(u32 numer, u32 denom)
{
    reg_CP_DIVCNT = CP_DIV_32_32BIT_MODE;
    CP_SetDivImm32_32_NS_(numer, denom);
}

static inline void CP_SetDiv64_32(u64 numer, u32 denom)
{
    reg_CP_DIVCNT = CP_DIV_64_32BIT_MODE;
    CP_SetDivImm64_32_NS_(numer, denom);
}

static inline void CP_SetDiv64_64(u64 numer, u64 denom)
{
    reg_CP_DIVCNT = CP_DIV_64_64BIT_MODE;
    CP_SetDivImm64_64_NS_(numer, denom);
}

static inline s32 CP_IsDivBusy(void)
{
    return (reg_CP_DIVCNT & REG_CP_DIVCNT_BUSY_MASK);
}

static inline void CP_WaitDiv(void)
{
    while (CP_IsDivBusy()) {
    }
}

static inline s64 CP_GetDivResultImm64(void)
{
    return (s64)(*(REGType64 *)REG_DIV_RESULT_ADDR);
}

static inline s32 CP_GetDivResultImm32(void)
{
    return (s32)(*(REGType32 *)REG_DIV_RESULT_ADDR);
}

static inline s16 CP_GetDivResultImm16(void)
{
    return (s16)(*(REGType16 *)REG_DIV_RESULT_ADDR);
}

static inline s8 CP_GetDivResultImm8(void)
{
    return (s8)(*(REGType8 *)REG_DIV_RESULT_ADDR);
}

static inline s64 CP_GetDivResult64(void)
{
    CP_WaitDiv();
    return CP_GetDivResultImm64();
}

static inline s32 CP_GetDivResult32(void)
{
    CP_WaitDiv();
    return CP_GetDivResultImm32();
}

static inline s16 CP_GetDivResult16(void)
{
    CP_WaitDiv();
    return CP_GetDivResultImm16();
}

static inline s8 CP_GetDivResult8(void)
{
    CP_WaitDiv();
    return CP_GetDivResultImm8();
}

static inline s64 CP_GetDivRemainderImm64(void)
{
    return (s64)(*(REGType64 *)REG_DIVREM_RESULT_ADDR);
}

static inline s32 CP_GetDivRemainderImm32(void)
{
    return (s32)(*(REGType32 *)REG_DIVREM_RESULT_ADDR);
}

static inline s16 CP_GetDivRemainderImm16(void)
{
    return (s16)(*(REGType16 *)REG_DIVREM_RESULT_ADDR);
}

static inline s8 CP_GetDivRemainderImm8(void)
{
    return (s8)(*(REGType8 *)REG_DIVREM_RESULT_ADDR);
}

static inline s64 CP_GetDivRemainder64(void)
{
    CP_WaitDiv();
    return CP_GetDivRemainderImm64();
}

static inline s32 CP_GetDivRemainder32(void)
{
    CP_WaitDiv();
    return CP_GetDivRemainderImm32();
}

static inline s16 CP_GetDivRemainder16(void)
{
    CP_WaitDiv();
    return CP_GetDivRemainderImm16();
}

static inline s8 CP_GetDivRemainder8(void)
{
    CP_WaitDiv();
    return CP_GetDivRemainderImm8();
}

#ifdef __cplusplus
}
#endif

#endif /* NITRO_CP_DIVIDER_H_ */
