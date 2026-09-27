#ifndef _ZB_ENDPOINT_CFG_H_
#define _ZB_ENDPOINT_CFG_H_

#include <stdint.h>
#include "tl_common.h"
#include "zcl_include.h"

#define APP_ENDPOINT_1                1

// Custom Cluster for E-Paper Display Control
#define ZCL_CLUSTER_CUSTOM_EPAPER     0xFC00

// Custom Cluster Attributes
#define ZCL_ATTRID_EPAPER_ACTIVE_SLOT   0x0000 // uint8, RW, 0..7
#define ZCL_ATTRID_EPAPER_RENDER_STYLE  0x0001 // uint8, RW, 0..4
#define ZCL_ATTRID_EPAPER_ACTIVE_MODE   0x0002 // uint8, RW, 1=Zigbee, 2=BLE
#define ZCL_ATTRID_EPAPER_REFRESH_COUNT 0x0003 // uint16, RO
#define ZCL_ATTRID_EPAPER_LED_STATE     0x0004 // uint8, RW (bit 0=Blue, bit 1=Green, bit 2=Red)
#define ZCL_ATTRID_EPAPER_WAKEUP_COUNT      0x0005 // uint32, RO, reportable
#define ZCL_ATTRID_EPAPER_WAKEUP_DURATION   0x0006 // uint32, RO, reportable (ms)
#define ZCL_ATTRID_EPAPER_TX_DURATION       0x0007 // uint32, RO, reportable (ms)
#define ZCL_ATTRID_EPAPER_RX_DURATION       0x0008 // uint32, RO, reportable (ms)

typedef struct {
    uint8_t  activeSlot;
    uint8_t  renderStyle;
    uint8_t  activeMode;
    uint16_t refreshCount;
    uint8_t  ledState;
    uint32_t wakeupCount;
    uint32_t wakeupDuration;
    uint32_t txDuration;
    uint32_t rxDuration;
} zcl_epaper_attr_t;

typedef struct {
    uint8_t zclVersion;
    uint8_t appVersion;
    uint8_t stackVersion;
    uint8_t hwVersion;
    uint8_t manuName[ZCL_BASIC_MAX_LENGTH];
    uint8_t modelId[ZCL_BASIC_MAX_LENGTH];
    uint8_t swBuildId[ZCL_BASIC_MAX_LENGTH];
    uint8_t dateCode[ZCL_BASIC_MAX_LENGTH];
    uint8_t powerSource;
    uint8_t deviceEnable;
} zcl_basicAttr_t;

typedef struct {
    uint8_t batteryVoltage;      // 0x20
    uint8_t batteryPercentage;   // 0x21
} zcl_powerAttr_t;

typedef struct {
    uint16_t identifyTime;
} zcl_identifyAttr_t;

extern zcl_epaper_attr_t  g_zcl_epaperAttrs;
extern zcl_basicAttr_t    g_zcl_basicAttrs;
extern zcl_powerAttr_t    g_zcl_powerAttrs;
extern zcl_identifyAttr_t g_zcl_identifyAttrs;
extern const af_simple_descriptor_t app_ep1Desc;

void app_zcl_endpoint_init(void);
status_t app_identifyCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload);
status_t app_basicCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload);

#endif // _ZB_ENDPOINT_CFG_H_
