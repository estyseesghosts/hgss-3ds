// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/spi/common/type.h: SPI PXI framing and the touch/power command words
 * the port's virtual ARM7 decodes.
 *
 * HGSS has no such header: its lib/include/nitro/spi/common/ holds only
 * pm_common.h (wake-up triggers and pad logic), and no HGSS source mentions
 * SPI_PXI_*, SPITpData, the PMIC register map or the PM utility/result codes.
 * Values below are ntrtwl NitroSDK's (include/nitro/spi/common/type.h and the
 * PM register/utility/result/LED/backlight/baud-rate block of
 * include/nitro/spi/common/pm_common.h, plus the PMSound* enums of
 * include/nitro/spi/ARM9/pm.h); the W2 protocol check found HGSS's ARM9
 * requests use the same command numbers and START/END framing.
 *
 * Names HGSS already provides are NOT repeated here: PM_TRIGGER_*,
 * PM_PAD_LOGIC_* (with PMWakeUpTrigger/PMLogic) come from HGSS's own
 * <nitro/spi/common/pm_common.h>, included below, and PMAmpSwitch/PMAmpGain
 * (with PMBackLightSwitch) from <nitro/spi/ARM9/pm.h> via <nitro/spi.h>.
 */

#ifndef NITRO_SPI_COMMON_TYPE_H_
#define NITRO_SPI_COMMON_TYPE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <nitro/types.h>
#include <nitro/spi/common/pm_common.h>

#define SPI_PXI_START_BIT                   0x02000000
#define SPI_PXI_END_BIT                     0x01000000
#define SPI_PXI_INDEX_MASK                  0x000f0000
#define SPI_PXI_INDEX_SHIFT                 16
#define SPI_PXI_DATA_MASK                   0x0000ffff
#define SPI_PXI_DATA_SHIFT                  0
#define SPI_PXI_RESULT_COMMAND_BIT          0x00008000
#define SPI_PXI_RESULT_COMMAND_MASK         0x00007f00
#define SPI_PXI_RESULT_COMMAND_SHIFT        8
#define SPI_PXI_RESULT_DATA_MASK            0x000000ff
#define SPI_PXI_RESULT_DATA_SHIFT           0
#define SPI_PXI_CONTINUOUS_PACKET_MAX       16

#define SPI_PXI_COMMAND_TP_SAMPLING         0x0000
#define SPI_PXI_COMMAND_TP_AUTO_ON          0x0001
#define SPI_PXI_COMMAND_TP_AUTO_OFF         0x0002
#define SPI_PXI_COMMAND_TP_SETUP_STABILITY  0x0003
#define SPI_PXI_COMMAND_TP_AUTO_SAMPLING    0x0010

#define SPI_PXI_COMMAND_NVRAM_WREN          0x0020
#define SPI_PXI_COMMAND_NVRAM_WRDI          0x0021
#define SPI_PXI_COMMAND_NVRAM_RDSR          0x0022
#define SPI_PXI_COMMAND_NVRAM_READ          0x0023
#define SPI_PXI_COMMAND_NVRAM_FAST_READ     0x0024
#define SPI_PXI_COMMAND_NVRAM_PW            0x0025
#define SPI_PXI_COMMAND_NVRAM_PP            0x0026
#define SPI_PXI_COMMAND_NVRAM_PE            0x0027
#define SPI_PXI_COMMAND_NVRAM_SE            0x0028
#define SPI_PXI_COMMAND_NVRAM_DP            0x0029
#define SPI_PXI_COMMAND_NVRAM_RDP           0x002a
#define SPI_PXI_COMMAND_NVRAM_CE            0x002b
#define SPI_PXI_COMMAND_NVRAM_RSI           0x002c
#define SPI_PXI_COMMAND_NVRAM_SR            0x002d

#define SPI_PXI_COMMAND_MIC_SAMPLING        0x0040
#define SPI_PXI_COMMAND_MIC_AUTO_ON         0x0041
#define SPI_PXI_COMMAND_MIC_AUTO_OFF        0x0042
#define SPI_PXI_COMMAND_MIC_AUTO_ADJUST     0x0043
#define SPI_PXI_COMMAND_MIC_BUFFER_FULL     0x0051

#define SPI_PXI_COMMAND_PM_SYNC             0x0060
#define SPI_PXI_COMMAND_PM_SLEEP_START      0x0061
#define SPI_PXI_COMMAND_PM_SLEEP_END        0x0062
#define SPI_PXI_COMMAND_PM_UTILITY          0x0063
#define SPI_PXI_COMMAND_PM_REG_WRITE        0x0064
#define SPI_PXI_COMMAND_PM_REG_READ         0x0065
#define SPI_PXI_COMMAND_PM_SELF_BLINK       0x0066
#define SPI_PXI_COMMAND_PM_GET_BLINK        0x0067
#define SPI_PXI_COMMAND_PM_REG0VALUE        0x0070
#define SPI_PXI_COMMAND_PM_REG1VALUE        0x0071
#define SPI_PXI_COMMAND_PM_REG2VALUE        0x0072
#define SPI_PXI_COMMAND_PM_REG3VALUE        0x0073
#define SPI_PXI_COMMAND_PM_REG4VALUE        0x0074

#define SPI_PXI_RESULT_SUCCESS              0x0000
#define SPI_PXI_RESULT_INVALID_COMMAND      0x0001
#define SPI_PXI_RESULT_INVALID_PARAMETER    0x0002
#define SPI_PXI_RESULT_ILLEGAL_STATUS       0x0003
#define SPI_PXI_RESULT_EXCLUSIVE            0x0004

#define SPI_TP_SAMPLING_FREQUENCY_MAX       4
#define SPI_TP_DEFAULT_STABILITY_RANGE      20
#define SPI_TP_VALARM_TAG                   0x54505641

#define SPI_NVRAM_PAGE_SIZE                 256
#define SPI_NVRAM_SECTOR_SIZE               (SPI_NVRAM_PAGE_SIZE * 256)
#define SPI_NVRAM_ALL_SIZE                  (SPI_NVRAM_SECTOR_SIZE * 8)

#define SPI_MIC_SAMPLING_TYPE_8BIT          0x0000
#define SPI_MIC_SAMPLING_TYPE_12BIT         0x0001
#define SPI_MIC_SAMPLING_TYPE_S8BIT         0x0002
#define SPI_MIC_SAMPLING_TYPE_S12BIT        0x0003
#define SPI_MIC_SAMPLING_TYPE_FILTER_ON     0x0000
#define SPI_MIC_SAMPLING_TYPE_FILTER_OFF    0x0004
#define SPI_MIC_SAMPLING_TYPE_ADMODE_MASK   0x0007
#define SPI_MIC_SAMPLING_TYPE_BIT_MASK      0x0001
#define SPI_MIC_SAMPLING_TYPE_SIGNED_MASK   0x0002
#define SPI_MIC_SAMPLING_TYPE_FILTER_MASK   0x0004

#define SPI_MIC_SAMPLING_TYPE_LOOP_OFF      0x0000
#define SPI_MIC_SAMPLING_TYPE_LOOP_ON       0x0010
#define SPI_MIC_SAMPLING_TYPE_LOOP_MASK     0x0010

#define SPI_MIC_SAMPLING_TYPE_CORRECT_OFF   0x0000
#define SPI_MIC_SAMPLING_TYPE_CORRECT_ON    0x0020
#define SPI_MIC_SAMPLING_TYPE_CORRECT_MASK  0x0020

#define SPI_MIC_SAMPLING_TYPE_MIX_TP_OFF    0x0000
#define SPI_MIC_SAMPLING_TYPE_MIX_TP_ON     0x0040
#define SPI_MIC_SAMPLING_TYPE_MIX_TP_MASK   0x0040

typedef enum SPIDeviceType {
    SPI_DEVICE_TYPE_TP = 0,
    SPI_DEVICE_TYPE_NVRAM,
    SPI_DEVICE_TYPE_MIC,
    SPI_DEVICE_TYPE_PM,
    SPI_DEVICE_TYPE_ARM7,
    SPI_DEVICE_TYPE_MAX
} SPIDeviceType;

typedef enum SPITpTouch {
    SPI_TP_TOUCH_OFF = 0,
    SPI_TP_TOUCH_ON
} SPITpTouch;

typedef enum SPITpValidity {
    SPI_TP_VALIDITY_VALID = 0,
    SPI_TP_VALIDITY_INVALID_X,
    SPI_TP_VALIDITY_INVALID_Y,
    SPI_TP_VALIDITY_INVALID_XY
} SPITpValidity;

typedef union SPITpData {
    struct {
        u32 x : 12;
        u32 y : 12;
        u32 touch : 1;
        u32 validity : 2;
        u32 dummy : 5;

    } e;
    u32 raw;
    u8 bytes[4];
    u16 halfs[2];
} SPITpData;

/* PM power-management chip register map and utility/result codes (ntrtwl
 * pm_common.h). The trigger/pad-logic group is HGSS's own pm_common.h,
 * included above, so it is not repeated. */

#define REG_PMIC_CTL_ADDR        0
#define REG_PMIC_STAT_ADDR       1
#define REG_PMIC_OP_CTL_ADDR     2
#define REG_PMIC_PGA_GAIN_ADDR   3
#define REG_PMIC_BL_CTL_ADDR     4
#define PMIC_REG_NUMS            5

#define PMIC_CTL_SND_PWR        (1 << 0)
#define PMIC_CTL_SND_VOLCTRL    (1 << 1)
#define PMIC_CTL_BKLT1          (1 << 2)
#define PMIC_CTL_BKLT2          (1 << 3)
#define PMIC_CTL_LED_SW         (1 << 4)
#define PMIC_CTL_LED_SP         (1 << 5)
#define PMIC_CTL_PWR_OFF        (1 << 6)

#define PMIC_STAT_VDET          (1 << 0)

#define PMIC_OP_CTL             (1 << 0)

#define PMIC_PGA_GAIN_SHIFT      0
#define PMIC_PGA_GAIN_MASK      (3 << PMIC_PGA_GAIN_SHIFT)

#define PMIC_BL_CTL_BL_SHIFT     0
#define PMIC_BL_CTL_BL_MASK     (3 << PMIC_BL_CTL_BL_SHIFT)
#define PMIC_BL_CTL_ADPT_SW     (1 << 2)
#define PMIC_BL_CTL_ADPT_DETECT (1 << 3)
#define PMIC_BL_CTL_VERSION     (1 << 6)

#define PMIC_REG_READ            1
#define PMIC_REG_WRITE           0
#define PMIC_REG_OP_SHIFT        7
#define PMIC_REG_OP_MASK         1

enum {
    PM_UTIL_DUMMY = 0,
    PM_UTIL_LED_ON,
    PM_UTIL_LED_BLINK_HIGH_SPEED,
    PM_UTIL_LED_BLINK_LOW_SPEED,
    PM_UTIL_LCD1_BACKLIGHT_ON,
    PM_UTIL_LCD1_BACKLIGHT_OFF,
    PM_UTIL_LCD2_BACKLIGHT_ON,
    PM_UTIL_LCD2_BACKLIGHT_OFF,
    PM_UTIL_LCD12_BACKLIGHT_ON,
    PM_UTIL_LCD12_BACKLIGHT_OFF,
    PM_UTIL_SOUND_POWER_ON,
    PM_UTIL_SOUND_POWER_OFF,
    PM_UTIL_SOUND_VOL_CTRL_ON,
    PM_UTIL_SOUND_VOL_CTRL_OFF,
    PM_UTIL_FORCE_POWER_OFF,
    PM_UTIL_FORCE_POWER_ON
};

#define PM_BAUDRATE_4MHZ             0
#define PM_BAUDRATE_2MHZ             1
#define PM_BAUDRATE_1MHZ             2
#define PM_BAUDRATE_512KHZ           3

#define PM_BAUDRATE_PMIC_DEFAULT    PM_BAUDRATE_1MHZ

#define PM_BACKLIGHT_RECOVER_TOP_SHIFT      5
#define PM_BACKLIGHT_RECOVER_BOTTOM_SHIFT   6
#define PM_BACKLIGHT_RECOVER_TOP_ON        (1 << PM_BACKLIGHT_RECOVER_TOP_SHIFT)
#define PM_BACKLIGHT_RECOVER_TOP_OFF       (0 << PM_BACKLIGHT_RECOVER_TOP_SHIFT)
#define PM_BACKLIGHT_RECOVER_BOTTOM_ON     (1 << PM_BACKLIGHT_RECOVER_BOTTOM_SHIFT)
#define PM_BACKLIGHT_RECOVER_BOTTOM_OFF    (0 << PM_BACKLIGHT_RECOVER_BOTTOM_SHIFT)

typedef enum {
    PM_LED_PATTERN_NONE          = 0,
    PM_LED_PATTERN_ON            = 1,
    PM_LED_PATTERN_BLINK_LOW     = 2,
    PM_LED_PATTERN_BLINK_HIGH    = 3,
    PM_LED_PATTERN_BLINK1        = 4,
    PM_LED_PATTERN_BLINK2        = 5,
    PM_LED_PATTERN_BLINK3        = 6,
    PM_LED_PATTERN_BLINK4        = 7,
    PM_LED_PATTERN_BLINK5        = 8,
    PM_LED_PATTERN_BLINK6        = 9,
    PM_LED_PATTERN_BLINK8        = 10,
    PM_LED_PATTERN_BLINK10       = 11,
    PM_LED_PATTERN_PATTERN1      = 12,
    PM_LED_PATTERN_PATTERN2      = 13,
    PM_LED_PATTERN_PATTERN3      = 14,
    PM_LED_PATTERN_WIRELESS      = 15
} PMLEDPattern;

#define PM_LED_PATTERN_MAX  PM_LED_PATTERN_WIRELESS

typedef enum {
    PM_LED_NONE = 0,
    PM_LED_ON = 1,
    PM_LED_BLINK_LOW = 2,
    PM_LED_BLINK_HIGH = 3
} PMLEDStatus;

typedef enum {
    PM_SOUND_POWER_OFF = 0,
    PM_SOUND_POWER_ON = 1
} PMSoundPowerSwitch;

typedef enum {
    PM_SOUND_VOLUME_OFF = 0,
    PM_SOUND_VOLUME_ON = 1
} PMSoundVolumeSwitch;

#define PM_COMMAND_SHIFT        22
#define PM_COMMAND_MASK         0x3c00000

#define PM_REG_OP_ADDR_SHIFT    16
#define PM_REG_OP_ADDR_MASK     0x3f0000

#define PM_REG_OP_DATA_SHIFT    0
#define PM_REG_OP_DATA_MASK     0xffff

#define PM_READING             -1

#define PM_SUCCESS              0
#define PM_BUSY                 1
#define PM_INVALID_COMMAND      0xffff

#define PM_RESULT_SUCCESS       0
#define PM_RESULT_BUSY          1
#define PM_RESULT_ERROR         2

#ifdef  __cplusplus
}
#endif

#endif /* NITRO_SPI_COMMON_TYPE_H_ */
