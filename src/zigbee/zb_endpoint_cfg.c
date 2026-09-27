#include "tl_common.h"
#include "zcl_include.h"
#include "zb_endpoint_cfg.h"
#include "zb_version.h"
#include "flash_eep.h"
#include "battery.h"
#include "led.h"
#include "power_tracker.h"
#include "zb_app.h"
#include "debug_uart.h"
#if ZCL_OTA_SUPPORT
#include "ota.h"
#endif

// Satisfies internal reference in libzb_ed.a (nwk_data.o) without linking full ZLL cluster
u8 deviceInfoRsp = 0;


// -----------------------------------------------------------------------------
// Cluster Attributes Storage
// -----------------------------------------------------------------------------

zcl_basicAttr_t g_zcl_basicAttrs = {
    .zclVersion   = 0x03,
    .appVersion   = APP_RELEASE,
    .stackVersion = (STACK_RELEASE | STACK_BUILD),
    .hwVersion    = HW_VERSION,
    .manuName     = ZCL_BASIC_MFG_NAME,
    .modelId      = ZCL_BASIC_MODEL_ID,
    .dateCode     = ZCL_BASIC_DATE_CODE,
    .powerSource  = POWER_SOURCE_BATTERY,
    .swBuildId    = ZCL_BASIC_SW_BUILD_ID,
};

zcl_powerAttr_t g_zcl_powerAttrs = {
    .batteryVoltage    = 30, // 3.0V in 100mV units
    .batteryPercentage = 200, // 100% in half-percent units (0..200)
};

zcl_identifyAttr_t g_zcl_identifyAttrs = {
    .identifyTime = 0,
};

zcl_epaper_attr_t g_zcl_epaperAttrs = {
    .activeSlot     = 0,
    .renderStyle    = 0,
    .activeMode     = 1, // 1=Zigbee
    .refreshCount   = 0,
    .ledState       = 0,
    .wakeupCount    = 0,
    .wakeupDuration = 0,
    .txDuration     = 0,
    .rxDuration     = 0,
};

// -----------------------------------------------------------------------------
// Cluster Attribute Tables
// -----------------------------------------------------------------------------

const zclAttrInfo_t basic_attrTbl[] = {
    { ZCL_ATTRID_BASIC_ZCL_VER,          ZCL_DATA_TYPE_UINT8,     ACCESS_CONTROL_READ, (u8*)&g_zcl_basicAttrs.zclVersion },
    { ZCL_ATTRID_BASIC_APP_VER,          ZCL_DATA_TYPE_UINT8,     ACCESS_CONTROL_READ, (u8*)&g_zcl_basicAttrs.appVersion },
    { ZCL_ATTRID_BASIC_STACK_VER,        ZCL_DATA_TYPE_UINT8,     ACCESS_CONTROL_READ, (u8*)&g_zcl_basicAttrs.stackVersion },
    { ZCL_ATTRID_BASIC_HW_VER,           ZCL_DATA_TYPE_UINT8,     ACCESS_CONTROL_READ, (u8*)&g_zcl_basicAttrs.hwVersion },
    { ZCL_ATTRID_BASIC_MFR_NAME,         ZCL_DATA_TYPE_CHAR_STR,  ACCESS_CONTROL_READ, (u8*)g_zcl_basicAttrs.manuName },
    { ZCL_ATTRID_BASIC_MODEL_ID,         ZCL_DATA_TYPE_CHAR_STR,  ACCESS_CONTROL_READ, (u8*)g_zcl_basicAttrs.modelId },
    { ZCL_ATTRID_BASIC_DATE_CODE,        ZCL_DATA_TYPE_CHAR_STR,  ACCESS_CONTROL_READ, (u8*)g_zcl_basicAttrs.dateCode },
    { ZCL_ATTRID_BASIC_POWER_SOURCE,     ZCL_DATA_TYPE_ENUM8,     ACCESS_CONTROL_READ, (u8*)&g_zcl_basicAttrs.powerSource },
    { ZCL_ATTRID_BASIC_SW_BUILD_ID,      ZCL_DATA_TYPE_CHAR_STR,  ACCESS_CONTROL_READ, (u8*)g_zcl_basicAttrs.swBuildId },
    { ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ, (u8*)&zcl_attr_global_clusterRevision },
};
#define BASIC_ATTR_NUM (sizeof(basic_attrTbl) / sizeof(zclAttrInfo_t))

const zclAttrInfo_t power_attrTbl[] = {
    { ZCL_ATTRID_BATTERY_VOLTAGE,              ZCL_DATA_TYPE_UINT8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_powerAttrs.batteryVoltage },
    { ZCL_ATTRID_BATTERY_PERCENTAGE_REMAINING, ZCL_DATA_TYPE_UINT8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_powerAttrs.batteryPercentage },
    { ZCL_ATTRID_GLOBAL_CLUSTER_REVISION,      ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8*)&zcl_attr_global_clusterRevision },
};
#define POWER_ATTR_NUM (sizeof(power_attrTbl) / sizeof(zclAttrInfo_t))

const zclAttrInfo_t identify_attrTbl[] = {
    { ZCL_ATTRID_IDENTIFY_TIME,           ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8*)&g_zcl_identifyAttrs.identifyTime },
    { ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8*)&zcl_attr_global_clusterRevision },
};
#define IDENTIFY_ATTR_NUM (sizeof(identify_attrTbl) / sizeof(zclAttrInfo_t))

const zclAttrInfo_t custom_epaper_attrTbl[] = {
    { ZCL_ATTRID_EPAPER_ACTIVE_SLOT,      ZCL_DATA_TYPE_UINT8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.activeSlot },
    { ZCL_ATTRID_EPAPER_RENDER_STYLE,     ZCL_DATA_TYPE_UINT8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.renderStyle },
    { ZCL_ATTRID_EPAPER_ACTIVE_MODE,      ZCL_DATA_TYPE_UINT8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.activeMode },
    { ZCL_ATTRID_EPAPER_REFRESH_COUNT,    ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.refreshCount },
    { ZCL_ATTRID_EPAPER_LED_STATE,        ZCL_DATA_TYPE_UINT8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.ledState },
    { ZCL_ATTRID_EPAPER_WAKEUP_COUNT,     ZCL_DATA_TYPE_UINT32, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.wakeupCount },
    { ZCL_ATTRID_EPAPER_WAKEUP_DURATION,  ZCL_DATA_TYPE_UINT32, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.wakeupDuration },
    { ZCL_ATTRID_EPAPER_TX_DURATION,      ZCL_DATA_TYPE_UINT32, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.txDuration },
    { ZCL_ATTRID_EPAPER_RX_DURATION,      ZCL_DATA_TYPE_UINT32, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_epaperAttrs.rxDuration },
    { ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8*)&zcl_attr_global_clusterRevision },
};
#define CUSTOM_EPAPER_ATTR_NUM (sizeof(custom_epaper_attrTbl) / sizeof(zclAttrInfo_t))

// -----------------------------------------------------------------------------
// Endpoint Simple Descriptor
// -----------------------------------------------------------------------------

const uint16_t app_ep1_inClusterList[] = {
    ZCL_CLUSTER_GEN_BASIC,
    ZCL_CLUSTER_GEN_POWER_CFG,
    ZCL_CLUSTER_GEN_IDENTIFY,
    ZCL_CLUSTER_CUSTOM_EPAPER,
};

const uint16_t app_ep1_outClusterList[] = {
    ZCL_CLUSTER_OTA,
};

const af_simple_descriptor_t app_ep1Desc = {
    .app_profile_id        = HA_PROFILE_ID,
    .app_dev_id            = 0x000F, // Generic Custom Device
    .endpoint              = APP_ENDPOINT_1,
    .app_dev_ver           = 1,
    .reserved              = 0,
    .app_in_cluster_count  = sizeof(app_ep1_inClusterList) / sizeof(uint16_t),
    .app_out_cluster_count = sizeof(app_ep1_outClusterList) / sizeof(uint16_t),
    .app_in_cluster_lst    = (uint16_t *)app_ep1_inClusterList,
    .app_out_cluster_lst   = (uint16_t *)app_ep1_outClusterList,
};

extern void zcl_rx_handler(void *arg);
extern void afApsAckCb(void *arg);
extern void app_zclProcessIncomingMsg(zclIncoming_t *pInHdlrMsg);

static void app_zcl_rx_handler(void *pData) {
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pData;
#if ZCL_OTA_SUPPORT
    if (pApsdeInd && pApsdeInd->indInfo.cluster_id == ZCL_CLUSTER_OTA) {
        // If this is a query response indicating no image or failure, immediately revert poll rate
        uint8_t cmd_idx = (pApsdeInd->asdu[0] & ZCL_FRAME_CONTROL_MANU_SPECIFIC) ? 4 : 2;
        if (pApsdeInd->asduLen > cmd_idx + 1) {
            uint8_t cmd = pApsdeInd->asdu[cmd_idx];
            uint8_t status = pApsdeInd->asdu[cmd_idx + 1];
            if (cmd == ZCL_CMD_OTA_QUERY_NEXT_IMAGE_RSP && status != ZCL_STA_SUCCESS) {
                zb_ota_set_active(false);
                if (!zb_is_interviewing()) {
                    zb_setPollRate(2000);
                }
                zcl_rx_handler(pData);
                return;
            }
        }
        zb_ota_on_activity();
        zb_setPollRate(QUEUE_POLL_RATE);
    }
#endif
    zcl_rx_handler(pData);
}

void app_zcl_endpoint_init(void) {
    DEBUG_LOG("ZCL", "app_zcl_endpoint_init start");
    // 1. Sync in-memory cluster attributes with wear-leveled flash settings & LEDs
    g_zcl_epaperAttrs.activeSlot   = settings.active_slot;
    g_zcl_epaperAttrs.renderStyle  = settings.render_style;
    g_zcl_epaperAttrs.activeMode   = settings.active_mode;
    g_zcl_epaperAttrs.refreshCount = settings.screen_refresh_count;
    g_zcl_epaperAttrs.ledState     = led_get_state();

    power_stats_t pstats;
    power_tracker_get_stats(&pstats);
    g_zcl_epaperAttrs.wakeupCount    = pstats.wakeup_count;
    g_zcl_epaperAttrs.wakeupDuration = pstats.wakeup_duration_ms;
    g_zcl_epaperAttrs.txDuration     = pstats.tx_duration_ms;
    g_zcl_epaperAttrs.rxDuration     = pstats.rx_duration_ms;

    // 2. Sync battery readings into power cluster attributes
    uint16_t mv = get_battery_mv();
    uint8_t pct = get_battery_level(mv);
    g_zcl_powerAttrs.batteryVoltage    = (uint8_t)(mv / 100);
    g_zcl_powerAttrs.batteryPercentage = (uint8_t)(pct * 2);

    DEBUG_LOG("ZCL", "Calling zcl_init");
    // 3. Initialize ZCL foundation FIRST (resets cluster list and registers incoming handler)
    zcl_init(app_zclProcessIncomingMsg);

    DEBUG_LOG("ZCL", "Calling af_endpointRegister");
    // 4. Register endpoint with Zigbee Application Framework (AF)
    af_endpointRegister(APP_ENDPOINT_1, (af_simple_descriptor_t *)&app_ep1Desc, app_zcl_rx_handler, NULL);

    DEBUG_LOG("ZCL", "Calling zcl_reportingTabInit");
    // 5. Initialize ZCL reporting table
    extern void zcl_reportingTabInit(void);
    zcl_reportingTabInit();

    DEBUG_LOG("ZCL", "Registering basic, power, identify, and custom epaper clusters");
    // 6. Register clusters with ZCL foundation
    zcl_basic_register(APP_ENDPOINT_1, MANUFACTURER_CODE_NONE, BASIC_ATTR_NUM, basic_attrTbl, app_basicCb);
    zcl_registerCluster(APP_ENDPOINT_1, ZCL_CLUSTER_GEN_POWER_CFG, MANUFACTURER_CODE_NONE, POWER_ATTR_NUM, power_attrTbl, NULL, NULL);
    zcl_identify_register(APP_ENDPOINT_1, MANUFACTURER_CODE_NONE, IDENTIFY_ATTR_NUM, identify_attrTbl, app_identifyCb);
    zcl_registerCluster(APP_ENDPOINT_1, ZCL_CLUSTER_CUSTOM_EPAPER, MANUFACTURER_CODE_NONE, CUSTOM_EPAPER_ATTR_NUM, custom_epaper_attrTbl, NULL, NULL);

#if ZCL_OTA_SUPPORT
    extern ota_preamble_t app_otaInfo;
    extern ota_callBack_t app_otaCb;
    DEBUG_LOG("ZCL", "Calling ota_init");
    ota_init(OTA_TYPE_CLIENT, (af_simple_descriptor_t *)&app_ep1Desc, &app_otaInfo, &app_otaCb);
    DEBUG_LOG("ZCL", "ota_init returned");
#endif
    DEBUG_LOG("ZCL", "app_zcl_endpoint_init finished");
}
