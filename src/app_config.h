#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

#define GPIO_UART_TX             GPIO_PB1
#ifndef DEBUG_INFO_TX_PIN
#define DEBUG_INFO_TX_PIN        GPIO_UART_TX
#endif

#include "tl_common.h"

#ifdef DEBUG_MODE
#undef DEBUG
#define DEBUG DEBUG_MODE
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define BOARD_HANSHOW_E31HA      0x31
#define BOARD_HANSHOW_E31PA      0x42

#ifndef BOARD
#define BOARD                    BOARD_HANSHOW_E31HA
#endif

// Firmware Identification
#if (BOARD == BOARD_HANSHOW_E31PA)
#define DEV_NAME                 "TLSR-XL3Na-E31PA"
#define DEV_MODEL                "TLSR-XL3Na-E31PA"
#else
#define DEV_NAME                 "TLSR-M3Na-E31HA"
#define DEV_MODEL                "TLSR-M3Na-E31HA"
#endif
#define DEV_MANUFACTURER         "ZB-DIY"
#include "version_cfg.h"

#ifndef APP_RELEASE
#define APP_RELEASE              0x10
#endif
#ifndef APP_BUILD
#define APP_BUILD                0x00
#endif
#ifndef STACK_RELEASE
#define STACK_RELEASE            0x30
#endif
#ifndef STACK_BUILD
#define STACK_BUILD              0x01
#endif
#ifndef HW_VERSION
#define HW_VERSION               0x01
#endif

#ifndef CHIP_TYPE
#define CHIP_TYPE_8258           0x02
#define CHIP_TYPE                CHIP_TYPE_8258
#endif
#ifndef MANUFACTURER_CODE_TELINK
#define MANUFACTURER_CODE_TELINK 0x1141
#endif
#ifndef IMAGE_TYPE
#define IMAGE_TYPE               ((CHIP_TYPE << 8) | BOARD)
#endif
#ifndef FILE_VERSION
#define FILE_VERSION             ((APP_RELEASE << 24) | (APP_BUILD << 16) | (STACK_RELEASE << 8) | STACK_BUILD)
#endif

// System Clock Configuration (24 MHz Crystal)
#define CLOCK_SYS_CLOCK_HZ       24000000
#define SYS_CLK_TYPE             SYS_CLK_24M_Crystal
#define BLE_DEFAULT_TX_POWER_IDX RF_POWER_P0p04dBm
#define ZB_DEFAULT_TX_POWER_IDX  RF_POWER_P0p04dBm

enum {
    CLOCK_SYS_CLOCK_1S  = CLOCK_SYS_CLOCK_HZ,
    CLOCK_SYS_CLOCK_1MS = (CLOCK_SYS_CLOCK_1S / 1000),
    CLOCK_SYS_CLOCK_1US = (CLOCK_SYS_CLOCK_1S / 1000000),
};

#define pm_wait_ms(t)            cpu_stall_wakeup_by_timer0((t) * CLOCK_SYS_CLOCK_1MS)
#define pm_wait_us(t)            cpu_stall_wakeup_by_timer0((t) * CLOCK_SYS_CLOCK_1US)

// Power Management Configuration
#define PM_ENABLE                1
#define BLE_APP_PM_ENABLE        1
#define USE_BLE_OTA              1
#define ZCL_OTA_SUPPORT          1

#define RAM                      _attribute_data_retention_

#ifndef _attribute_custom_bss_
#define _attribute_custom_bss_   __attribute__((section(".custom_bss")))
#endif

/* =========================================================================
   Shared Scratch RAM (EPD Virtual Screen Buffer & BLE OTA Cache)
   ========================================================================= */
#define SHARED_SCRATCH_RAM_SIZE  (16 * 1024) // 16 KB shared buffer for both M3NA and XL3NA

/* =========================================================================
   Hardware Peripheral GPIO Mapping (Hanshow Stellar-M3N@ / E31HA)
   ========================================================================= */

// Status RGB LEDs (Active Low)
// Blue: BLE Mode indication
// Green: Zigbee Mode indication
// Red: Error / Warning indication
#define GPIO_LED_BLUE            GPIO_PA7 // Shared with SWS debug
#define GPIO_LED_GREEN           GPIO_PD3
#define GPIO_LED_RED             GPIO_PD2

// E-Paper Display (2.13" 250x122 BWR - UC8151 / SSD1619)
#define GPIO_EPD_RESET           GPIO_PD4
#define GPIO_EPD_DC              GPIO_PD7
#define GPIO_EPD_BUSY            GPIO_PA1
#define PA1_INPUT_ENABLE         1
#define PA1_OUTPUT_ENABLE        0
#define PA1_DATA_OUT             0
#define PA1_FUNC                 AS_GPIO
#define GPIO_EPD_CS              GPIO_PB4
#define GPIO_EPD_CLK             GPIO_PB5
#define GPIO_EPD_MOSI            GPIO_PB6
#if (BOARD == BOARD_HANSHOW_E31PA)
#define GPIO_EPD_PWR_ENABLE      GPIO_PB7 // Active Low MOSFET gate control on Stellar-XL3N@ 4.2"
#define PB7_INPUT_ENABLE         0
#define PB7_OUTPUT_ENABLE        1
#define PB7_DATA_OUT             1        // Default HIGH (Power OFF)
#define PB7_FUNC                 AS_GPIO
#define PULL_WAKEUP_SRC_PB7      PM_PIN_PULLUP_10K
#define PULL_WAKEUP_SRC_PA1      PM_PIN_UP_DOWN_FLOAT
#else
#define GPIO_EPD_PWR_ENABLE      GPIO_PC5 // Active Low MOSFET gate control on Stellar-M3N@ 2.13"
#define PC5_INPUT_ENABLE         0
#define PC5_OUTPUT_ENABLE        1
#define PC5_DATA_OUT             1        // Default HIGH (Power OFF)
#define PC5_FUNC                 AS_GPIO
#define PULL_WAKEUP_SRC_PC5      PM_PIN_PULLUP_1M
#define PULL_WAKEUP_SRC_PA1      PM_PIN_PULLUP_1M
#endif

// NFC Transceiver (Fudan Micro FM11NC08 I2C)
#define GPIO_NFC_SDA             GPIO_PC0
#define GPIO_NFC_SCL             GPIO_PC1
#define GPIO_NFC_IRQ             GPIO_PC4 // Active Low interrupt on RxDone / Field detect
#define GPIO_NFC_CS              GPIO_PC6
#define I2C_GROUP                I2C_GPIO_GROUP_C0C1
#define I2C_CLOCK                400000 // 400 kHz fast I2C

// Battery Voltage Measurement (Internal SAR ADC via PB0 / VDD)
#define SHL_ADC_VBAT             1
#define GPIO_VBAT                GPIO_PB0

// UART Logging (PB1, optional)
#define GPIO_UART_TX             GPIO_PB1

#ifndef DEBUG
#define DEBUG                    0
#endif

#if DEBUG
#undef UART_PRINTF_MODE
#define UART_PRINTF_MODE         1
#ifndef DEBUG_INFO_TX_PIN
#define DEBUG_INFO_TX_PIN        GPIO_UART_TX
#endif
#define PB1_INPUT_ENABLE         1
#define PB1_OUTPUT_ENABLE        1
#define PB1_DATA_OUT             1
#define PB1_FUNC                 AS_UART
#define PULL_WAKEUP_SRC_PB1      PM_PIN_PULLUP_10K
#else
#undef UART_PRINTF_MODE
#define UART_PRINTF_MODE         0
#define PB1_INPUT_ENABLE         0
#define PB1_OUTPUT_ENABLE        0
#define PB1_DATA_OUT             0
#define PB1_FUNC                 AS_GPIO
#define PULL_WAKEUP_SRC_PB1      0
#endif


/* =========================================================================
   Display Chemistry & Dimensions
   ========================================================================= */
#if (BOARD == BOARD_HANSHOW_E31PA)
#define EPD_WIDTH                400
#define EPD_HEIGHT               300
#define EPD_BUFFER_HEIGHT        304
#define EPD_LINE_BYTES           50 // 50 bytes per line (400 bits horizontal)
#define EPD_PLANE_SIZE           (EPD_WIDTH * EPD_HEIGHT / 8) // 15,000 bytes exact UC8176 plane size
#define EPD_OBD_BUFFER_SIZE      (EPD_WIDTH * (EPD_BUFFER_HEIGHT / 8)) // 15,200 bytes for OneBitDisplay

// Slot Definitions for XL3Na (400x300):
// Slot 0: System Info Screen (Device info, mode, MAC, battery, temp, refresh count, style)
// Slots 1..4: User Uploaded Compressed BWR Images (8 KB each: 2 flash sectors)
// Slot 5: Blank Screen (White)
#define EPD_SLOT_INFO            0
#define EPD_USER_SLOT_START      1
#define EPD_USER_SLOT_COUNT      4
#define EPD_SLOT_BLANK           5
#define EPD_SLOT_COUNT           6
#define EPD_SLOT_SIZE            8192 // 8 KB per user slot (2 sectors)
#define EPD_TOTAL_LINES          (EPD_PLANE_SIZE / EPD_LINE_BYTES) // 300 lines

#else // BOARD_HANSHOW_E31HA (Default 250x122)
#define EPD_WIDTH                250
#define EPD_HEIGHT               122
#define EPD_BUFFER_HEIGHT        128
#define EPD_LINE_BYTES           16 // 16 bytes per column (128 bits vertical)
#define EPD_PLANE_SIZE           (EPD_WIDTH * EPD_LINE_BYTES) // 4,000 bytes exact UC8151 plane size
#define EPD_OBD_BUFFER_SIZE      (EPD_WIDTH * (EPD_BUFFER_HEIGHT / 8)) // 4,000 bytes for OneBitDisplay

// Slot Definitions for M3Na (250x122):
// Slot 0: System Info Screen (Device info, mode, MAC, battery, temp, refresh count, style)
// Slots 1..8: User Uploaded Compressed BWR Images (4 KB each: 1 flash sector)
// Slot 9: Blank Screen (White)
#define EPD_SLOT_INFO            0
#define EPD_USER_SLOT_START      1
#define EPD_USER_SLOT_COUNT      8
#define EPD_SLOT_BLANK           9
#define EPD_SLOT_COUNT           10
#define EPD_SLOT_SIZE            4096 // 4 KB per user slot (1 sector)
#define EPD_TOTAL_LINES          (EPD_PLANE_SIZE / EPD_LINE_BYTES) // 250 columns
#endif

// 5 Pre-defined Rendering Styles:
// Style 0: Standard (follow color definition from stored image)
// Style 1: B&W (black -> black, red -> black, white -> white)
// Style 2: B&W inverted (black -> white, red -> white, white -> black)
// Style 3: Red & White (black -> red, red -> red, white -> white)
// Style 4: Red & White inverted (black -> white, red -> white, white -> red)
#define STYLE_STANDARD           0
#define STYLE_BW_STANDARD        1
#define STYLE_BW_INVERTED        2
#define STYLE_RW_STANDARD        3
#define STYLE_RW_INVERTED        4
#define STYLE_COUNT              5

/* =========================================================================
   Flash Memory Map (512 KB SPI Flash)
   ========================================================================= */
#define BANK0_START              0x00000
#define BANK0_MAX_SIZE           0x30000 // 192 KB max firmware size

#ifndef NV_BASE_ADDRESS
#define NV_BASE_ADDRESS          0x30000 // Consolidated Zigbee NVRAM (64 KB: 0x30000 - 0x3FFFF)
#endif

#define BANK1_OTA_START          0x40000 // 192 KB OTA target partition (0x40000 - 0x6FFFF)
#define BANK1_OTA_MAX_SIZE       0x30000

#define PROTOTYPE_CODE_ADDR      0x70000 // Dynamic prototype code execution area (16 KB: 0x70000 - 0x73FFF)
#define PROTOTYPE_CODE_MAX_SIZE  0x4000  // 16 KB max snippet size

#define FMEMORY_EEP_BASE_ADDR    0x74000 // User settings wear-leveled storage (8 KB: 0x74000 - 0x75FFF)
#define CFG_ADR_MAC              0x76000 // Public MAC address (4 KB)
#define CUST_CAP_INFO_ADDR       0x77000 // Crystal load capacitor trim (4 KB)
#define USER_IMAGE_BASE_ADDR     0x78000 // Contiguous user image storage (32 KB: 0x78000 - 0x7FFFF)
#define STOCK_LEGACY_MAC_ADDR    0x01F000 // Hanshow factory MAC fallback

/* =========================================================================
   Operating Modes
   ========================================================================= */
typedef enum {
    DEVICE_MODE_ZIGBEE = 1, // Factory default
    DEVICE_MODE_BLE    = 2,
} device_mode_t;

#if defined(__cplusplus)
}
#endif

#endif // _APP_CONFIG_H_
