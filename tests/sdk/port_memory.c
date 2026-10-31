/* The port's memory arrays, for the SDK host suites (patch 1301, lib/include/nitro/hw/ARM9/mmap.h).
 *
 * Under SDK_PORT_3DS the SDK's HW_* address macros point at these eleven arrays instead of the DS's
 * fixed addresses (3ds_port/nitro/src/port_memory.c, port_ioreg.c provide them in the real build).
 * A host test that links an SDK object which touches a register or main memory must provide them,
 * or the link fails with an undefined reference to NitroPort_IoReg and friends.
 *
 * The sizes are the ones in patch 1301, so an offset computed from HW_MAIN_MEM or HW_IOREG lands
 * where the DS would put it. Each array is page-aligned so a test can mprotect it as a guard (see
 * tests/sdk/card_spi_wait). Synthetic memory only: no game data.
 */
#define PORT_ALIGN __attribute__((aligned(4096)))

PORT_ALIGN unsigned char NitroPort_IoReg[0x00110000];
PORT_ALIGN unsigned char NitroPort_Itcm[0x8000];
PORT_ALIGN unsigned char NitroPort_MainMem[0x800000];
PORT_ALIGN unsigned char NitroPort_Wram[0x8000];
PORT_ALIGN unsigned char NitroPort_Pltt[0x800];
PORT_ALIGN unsigned char NitroPort_Oam[0x800];
PORT_ALIGN unsigned char NitroPort_BgVram[0x80000];
PORT_ALIGN unsigned char NitroPort_DbBgVram[0x20000];
PORT_ALIGN unsigned char NitroPort_ObjVram[0x40000];
PORT_ALIGN unsigned char NitroPort_DbObjVram[0x20000];
PORT_ALIGN unsigned char NitroPort_LcdcVram[0xA4000];
