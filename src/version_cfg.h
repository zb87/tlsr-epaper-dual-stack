#ifndef _VERSION_CFG_H_
#define _VERSION_CFG_H_

#define BOOT_LOADER_MODE           0

/* APP image address. */
#if (BOOT_LOADER_MODE)
    #define APP_IMAGE_ADDR         0x8000
#else
    #define APP_IMAGE_ADDR         0x0
#endif

/* Pre-compiled link configuration. */
#define IS_BOOT_LOADER_IMAGE       0
#define RESV_FOR_APP_RAM_CODE_SIZE 0
#define IMAGE_OFFSET               APP_IMAGE_ADDR

#define APP_RELEASE                0x10        // app release 1.0
#define APP_BUILD                  0x06        // app build 06, full version - v1.0.06
#define STACK_RELEASE              0x30        // stack release 3.0
#define STACK_BUILD                0x01        // stack build 01
#define HW_VERSION                 0x01

#ifndef BUILD_DATE
#define BUILD_DATE                 {8, '2','0','2','6','0','9','2','7'}
#endif

#ifndef ZCL_BASIC_DATE_CODE
#define ZCL_BASIC_DATE_CODE        BUILD_DATE
#endif

#ifndef ZCL_BASIC_LOC_DESC
#define ZCL_BASIC_LOC_DESC         {7,'U','N','K','N','O','W','N'}
#endif

#ifndef ZCL_BASIC_BUILD_ID
#define ZCL_BASIC_BUILD_ID         {10,'0','1','2','2','0','5','2','0','1','7'}
#endif

#ifndef ZCL_BASIC_SW_BUILD_ID // max 16 chars v1.3.02
#define ZCL_BASIC_SW_BUILD_ID      {7, 'v', (APP_RELEASE>>4)+0x30, '.', (APP_RELEASE&0xf)+0x30, '.', (APP_BUILD>>4)+0x30, (APP_BUILD&0xf)+0x30, 0}
#endif

#ifndef BOARD_HANSHOW_E31HA
#define BOARD_HANSHOW_E31HA        0x31
#endif
#ifndef BOARD_HANSHOW_E31PA
#define BOARD_HANSHOW_E31PA        0x42
#endif

#if (defined(BOARD) && (BOARD == BOARD_HANSHOW_E31PA))
#define BOARD_IMAGE_ID             BOARD_HANSHOW_E31PA
#else
#define BOARD_IMAGE_ID             BOARD_HANSHOW_E31HA
#endif

#define MANUFACTURER_CODE_TELINK   0x1141
#define CHIP_TYPE                  0x02
#define IMAGE_TYPE                 ((CHIP_TYPE << 8) | BOARD_IMAGE_ID)
#define FILE_VERSION               ((APP_RELEASE << 24) | (APP_BUILD << 16) | (STACK_RELEASE << 8) | STACK_BUILD)

#endif /* _VERSION_CFG_H_ */
