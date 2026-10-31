/* Host test for the SDK_PORT_3DS memory map in lib/include/nitro/hw/ARM9/mmap.h: every DS area the SDK
 * addresses directly lands in the port's arrays, at the same sizes and offsets as on the DS. */
#include <nitro.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/fx */

unsigned char NitroPort_IoReg[0x00110000], NitroPort_Itcm[0x8000], NitroPort_MainMem[0x800000],
    NitroPort_Wram[0x8000], NitroPort_Pltt[0x800], NitroPort_Oam[0x800], NitroPort_BgVram[0x80000],
    NitroPort_DbBgVram[0x20000], NitroPort_ObjVram[0x40000], NitroPort_DbObjVram[0x20000],
    NitroPort_LcdcVram[0xA4000];
u32 SDK_AUTOLOAD_DTCM_START[0x4000 / 4];

static int fails, checks;
#define EQ(name, a, b) do { checks++; if ((u32)(a) != (u32)(b)) { fails++; printf("FAIL %s: %08x != %08x\n", name, (unsigned)(u32)(a), (unsigned)(u32)(b)); } } while (0)
#define U(a) ((u32)(a))

int main(void) {
    /* Areas map onto the arrays. */
    EQ("IOREG", HW_IOREG, U(NitroPort_IoReg));
    EQ("REG_BASE alias follows", HW_REG_BASE, U(NitroPort_IoReg));
    EQ("MAIN_MEM", HW_MAIN_MEM, U(NitroPort_MainMem));
    EQ("ITCM", HW_ITCM, U(NitroPort_Itcm));
    EQ("WRAM", HW_WRAM, U(NitroPort_Wram));
    EQ("BG_PLTT", HW_BG_PLTT, U(NitroPort_Pltt));
    EQ("BG_VRAM", HW_BG_VRAM, U(NitroPort_BgVram));
    EQ("DB_BG_VRAM", HW_DB_BG_VRAM, U(NitroPort_DbBgVram));
    EQ("OBJ_VRAM", HW_OBJ_VRAM, U(NitroPort_ObjVram));
    EQ("DB_OBJ_VRAM", HW_DB_OBJ_VRAM, U(NitroPort_DbObjVram));
    EQ("LCDC_VRAM", HW_LCDC_VRAM, U(NitroPort_LcdcVram));
    EQ("OAM", HW_OAM, U(NitroPort_Oam));

    /* Sizes are the DS's (the unchanged *_SIZE macros are END - START). */
    EQ("BG_PLTT size", HW_BG_PLTT_SIZE, 0x200);
    EQ("OBJ_PLTT size", HW_OBJ_PLTT_SIZE, 0x200);
    EQ("PLTT size", HW_PLTT_SIZE, 0x400);
    EQ("DB_PLTT size", HW_DB_PLTT_SIZE, 0x400);
    EQ("BG_VRAM size", HW_BG_VRAM_SIZE, 0x80000);
    EQ("DB_BG_VRAM size", HW_DB_BG_VRAM_SIZE, 0x20000);
    EQ("OBJ_VRAM size", HW_OBJ_VRAM_SIZE, 0x40000);
    EQ("DB_OBJ_VRAM size", HW_DB_OBJ_VRAM_SIZE, 0x20000);
    EQ("LCDC_VRAM size", HW_LCDC_VRAM_SIZE, 0xA4000);
    EQ("OAM size", HW_OAM_SIZE, 0x400);
    EQ("DB_OAM size", HW_DB_OAM_SIZE, 0x400);
    EQ("IOREG size", HW_IOREG_END - HW_IOREG, 0x00110000);
    EQ("WRAM size", HW_WRAM_END - HW_WRAM, 0x8000);

    /* Offsets inside main memory are the DS's, so the SDK's shared-area addresses still work. */
    EQ("ROM_HEADER_BUF", HW_ROM_HEADER_BUF, U(NitroPort_MainMem) + 0x007ffe00);
    EQ("MAIN_MEM_SHARED", HW_MAIN_MEM_SHARED, U(NitroPort_MainMem) + 0x007ff000);
    EQ("ARENA_INFO_BUF", HW_ARENA_INFO_BUF, U(NitroPort_MainMem) + 0x007ffda0);

    /* Register macros resolve into the I/O array at the DS register offsets. */
    EQ("IME register", (u32)&reg_OS_IME, U(NitroPort_IoReg) + 0x208);
    EQ("IE register", (u32)&reg_OS_IE, U(NitroPort_IoReg) + 0x210);
    EQ("IF register", (u32)&reg_OS_IF, U(NitroPort_IoReg) + 0x214);
    reg_OS_IME = 1;
    EQ("write lands in the array", *(volatile u16 *)(NitroPort_IoReg + 0x208), 1);

    /* lib/include/nitro/mi/dma.h spells two registers as absolute addresses (0x04000b8, 0x04000e0)
     * instead of HW_REG_BASE + offset, so the mmap override does not reach them without its own fix
     * (patch 1302). MIi_CheckAnotherAutoDMA and MIi_DmaClearSource use them. */
    EQ("DMA0CNT literal follows", (u32)REG_ADDR_DMA0CNT, U(NitroPort_IoReg) + 0x0b8);
    EQ("DMA0 clear source follows", (u32)REG_ADDR_DMA0_CLR_DATA, U(NitroPort_IoReg) + 0x0e0);
    *(volatile u32 *)(u32)REG_ADDR_DMA0_CLR_DATA = 0x12345678;
    EQ("DMA0 clear write lands in the array", *(volatile u32 *)(NitroPort_IoReg + 0x0e0), 0x12345678);

    printf("memory_map: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
