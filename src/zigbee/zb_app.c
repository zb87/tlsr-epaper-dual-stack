#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "zb_endpoint_cfg.h"
#include "zb_app.h"
#include "app_config.h"
#include "flash_eep.h"
#include "epd.h"
#include "nfc_fm11nc08.h"
#include "battery.h"
#include "led.h"
#include "mode_switch.h"
#include "debug_uart.h"
#include "power_tracker.h"
#include "proj/os/ev_timer.h"

static uint8_t s_pending_mode_switch = 0;
static uint32_t s_pending_mode_switch_tick = 0;

#if ZCL_OTA_SUPPORT
#include "ota.h"
#endif

static bool s_zb_ota_active = false;
static uint32_t s_zb_ota_last_activity_tick = 0;

void zb_ota_set_active(bool active) {
    s_zb_ota_active = active;
    if (active) {
        s_zb_ota_last_activity_tick = clock_time();
    } else {
        s_zb_ota_last_activity_tick = 0;
    }
}

void zb_ota_on_activity(void) {
    s_zb_ota_last_activity_tick = clock_time();
}

bool zb_is_ota_active(void) {
    if (s_zb_ota_active) {
        return true;
    }
#if ZCL_OTA_SUPPORT
    if (zcl_attr_imageUpgradeStatus != IMAGE_UPGRADE_STATUS_NORMAL) {
        return true;
    }
#endif
    if (s_zb_ota_last_activity_tick != 0 && !clock_time_exceed(s_zb_ota_last_activity_tick, 15 * 1000 * 1000)) {
        return true;
    }
    return false;
}

void zb_schedule_mode_switch(uint8_t target_mode) {
    s_pending_mode_switch = target_mode;
    s_pending_mode_switch_tick = clock_time();
    DEBUG_LOG("ZIGBEE", "Mode switch to %u scheduled; awaiting ZCL response transmission...", target_mode);
}

bool zb_is_mode_switch_pending(void) {
    return (s_pending_mode_switch != 0);
}

extern bdb_appCb_t g_zbBdbCb;
extern u32 g_u32MacFlashAddr;

bdb_commissionSetting_t g_bdbCommissionSetting = {
    .linkKey.tcLinkKey.keyType = SS_GLOBAL_LINK_KEY,
    .linkKey.tcLinkKey.key = (u8 *)tcLinkKeyCentralDefault,
    .linkKey.distributeLinkKey.keyType = MASTER_KEY,
    .linkKey.distributeLinkKey.key = (u8 *)linkKeyDistributedMaster,
    .linkKey.touchLinkKey.keyType = MASTER_KEY,
    .linkKey.touchLinkKey.key = (u8 *)touchLinkKeyMaster,
    .touchlinkEnable = 0,
    .touchlinkChannel = DEFAULT_CHANNEL,
    .touchlinkLqiThreshold = 0xA0,
};

RAM static uint16_t s_zb_nwk_pan_id = 0;
RAM static uint8_t  s_zb_nwk_channel = 0;

uint16_t zb_get_pan_id(void) {
    return s_zb_nwk_pan_id;
}

uint8_t zb_get_channel(void) {
    return s_zb_nwk_channel;
}

static void sensorDevice_startDevCnfHandler(zdo_start_device_confirm_t *pStartDevCnf) {
    if (pStartDevCnf) {
        if (pStartDevCnf->status == ZDO_SUCCESS) {
            s_zb_nwk_pan_id = pStartDevCnf->pan_id;
            s_zb_nwk_channel = pStartDevCnf->channel_num;
        } else if (s_zb_nwk_channel == 0 && g_zbMacPib.phyChannelCur >= 11 && g_zbMacPib.phyChannelCur <= 26) {
            s_zb_nwk_channel = g_zbMacPib.phyChannelCur;
        }
        DEBUG_LOG("ZIGBEE", "ZDO Start Device Confirm (status: 0x%02X, short: 0x%04X, pan: 0x%04X, ch: %u)",
                  pStartDevCnf->status, pStartDevCnf->short_addr, pStartDevCnf->pan_id, pStartDevCnf->channel_num);
        if (pStartDevCnf->status == 0xC3) {
            zb_on_rejoin_security_not_permitted();
        }
    }
    bdb_zdoStartDevCnf(pStartDevCnf);
}

static void sensorDevice_resetCnfHandler(nlme_reset_cnf_t *pResetCnf) {
    DEBUG_LOG("ZIGBEE", "ZDO Reset Confirm (status: 0x%02X)", pResetCnf ? pResetCnf->status : 0);
}

static void sensorDevice_devAnnounceIndHandler(zdo_device_annce_req_t *pDevAnnounceInd) {
    if (pDevAnnounceInd) {
        DEBUG_LOG("ZIGBEE", "ZDO Device Announce Indication (short: 0x%04X)", pDevAnnounceInd->nwk_addr_local);
    }
}

static void sensorDevice_leaveIndHandler(nlme_leave_ind_t *pLeaveInd) {
    DEBUG_LOG("ZIGBEE", "ZDO Leave Indication (rejoin: %u)", pLeaveInd ? pLeaveInd->rejoin : 0);
    if (!pLeaveInd || !pLeaveInd->rejoin) {
        if (!zb_isDeviceFactoryNew()) {
            zb_resetDevice2FN();
        }
        zb_on_network_leave();
    }
}

static void sensorDevice_leaveCnfHandler(nlme_leave_cnf_t *pLeaveCnf) {
    DEBUG_LOG("ZIGBEE", "ZDO Leave Confirm (status: 0x%02X)", pLeaveCnf ? pLeaveCnf->status : 0);
    if (!pLeaveCnf || pLeaveCnf->status == SUCCESS) {
        zb_on_network_leave();
    }
}

static void sensorDevice_permitJoinIndHandler(nlme_permitJoining_req_t *pPermitJoinInd) {
    if (pPermitJoinInd) {
        DEBUG_LOG("ZIGBEE", "ZDO Permit Join Indication (duration: %us)", pPermitJoinInd->permitDuration);
    }
}

const zdo_appIndCb_t appCbLst = {
    .zdpStartDevCnfCb    = sensorDevice_startDevCnfHandler,
    .zdpResetCnfCb       = sensorDevice_resetCnfHandler,
    .zdpDevAnnounceIndCb = sensorDevice_devAnnounceIndHandler,
    .zdpLeaveIndCb       = sensorDevice_leaveIndHandler,
    .zdpLeaveCnfCb       = sensorDevice_leaveCnfHandler,
    .zdpNwkUpdateIndCb   = NULL,
    .zdpPermitJoinIndCb  = sensorDevice_permitJoinIndHandler,
    .zdoNlmeSyncCnfCb    = NULL,
    .zdoTcJoinIndCb      = NULL,
    .ssTcFrameCntReachedCb = NULL,
};

static void stack_init(void) {
    DEBUG_LOG("ZB_APP", "stack_init: calling zb_init");
    zb_init();
    DEBUG_LOG("ZB_APP", "stack_init: zb_init done, calling zb_zdoCbRegister");
    zb_zdoCbRegister((zdo_appIndCb_t *)&appCbLst);
    DEBUG_LOG("ZB_APP", "stack_init: done");
}

RAM static uint16_t s_zb_poll_cycles = 0;
RAM static uint16_t s_zb_last_reported_mv = 0;
RAM static uint8_t  s_zb_last_reported_pct = 0xFF;
RAM static bool     s_zb_initial_reported = false;
RAM static bool     s_epd_was_busy = false;

void zb_reset_reporting_state(void) {
    s_zb_poll_cycles = 0;
    s_zb_last_reported_mv = 0;
    s_zb_last_reported_pct = 0xFF;
    s_zb_initial_reported = false;
}

void zb_send_initial_reports(void) {
    if (!zb_isDeviceJoinedNwk() || s_zb_initial_reported) return;
    s_zb_initial_reported = true;
    DEBUG_LOG("ZIGBEE", "Sending initial telemetry reports...");
    zb_battery_report();
    zb_epaper_report_attrs();
}

static status_t zcl_sendReportAttrs(u8 srcEp, epInfo_t *pDstEpInfo, u8 disableDefaultRsp, u8 direction,
                                    u16 clusterId, zclReportCmd_t *pReportAttrs) {
    u16 len = 0;
    for (u8 i = 0; i < pReportAttrs->numAttr; i++) {
        zclReport_t *pAttr = &(pReportAttrs->attrList[i]);
        len += 2 + 1; // attrID (2) + dataType (1)
        len += zcl_getAttrSize(pAttr->dataType, pAttr->attrData);
    }

    u8 *buf = (u8 *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    u8 *pBuf = buf;
    for (u8 i = 0; i < pReportAttrs->numAttr; i++) {
        zclReport_t *pAttr = &(pReportAttrs->attrList[i]);
        *pBuf++ = (uint8_t)(pAttr->attrID & 0xFF);
        *pBuf++ = (uint8_t)((pAttr->attrID >> 8) & 0xFF);
        *pBuf++ = pAttr->dataType;
        u16 dataLen = zcl_getAttrSize(pAttr->dataType, pAttr->attrData);
        memcpy(pBuf, pAttr->attrData, dataLen);
        pBuf += dataLen;
    }

    u8 status = zcl_sendCmd(srcEp, pDstEpInfo, clusterId, ZCL_CMD_REPORT, FALSE,
                            direction, disableDefaultRsp, MANUFACTURER_CODE_NONE,
                            ZCL_SEQ_NUM, len, buf);
    ev_buf_free(buf);
    return status;
}

void zb_battery_report(void) {
    if (!zb_isDeviceJoinedNwk()) return;

    uint16_t mv = get_battery_mv_forced();
    uint8_t pct = get_battery_level(mv);
    s_zb_last_reported_mv = mv;
    s_zb_last_reported_pct = pct;
    g_zcl_powerAttrs.batteryVoltage    = (uint8_t)(mv / 100);
    g_zcl_powerAttrs.batteryPercentage = (uint8_t)(pct * 2);

    DEBUG_LOG("ZIGBEE", "Report battery: %u mV (%u%%)", mv, pct);

    epInfo_t dstEpInfo;
    TL_SETSTRUCTCONTENT(dstEpInfo, 0);
    dstEpInfo.profileId = HA_PROFILE_ID;
    dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
    dstEpInfo.dstAddr.shortAddr = 0x0000;
    dstEpInfo.dstEp = 1;

    // Send both battery attributes in a single atomic report command (1 packet)
    struct {
        u8 numAttr;
        zclReport_t attrList[2];
    } cmd;

    cmd.numAttr = 2;
    cmd.attrList[0].attrID   = ZCL_ATTRID_BATTERY_PERCENTAGE_REMAINING;
    cmd.attrList[0].dataType = ZCL_DATA_TYPE_UINT8;
    cmd.attrList[0].attrData = &g_zcl_powerAttrs.batteryPercentage;

    cmd.attrList[1].attrID   = ZCL_ATTRID_BATTERY_VOLTAGE;
    cmd.attrList[1].dataType = ZCL_DATA_TYPE_UINT8;
    cmd.attrList[1].attrData = &g_zcl_powerAttrs.batteryVoltage;

    zcl_sendReportAttrs(APP_ENDPOINT_1, &dstEpInfo, TRUE, ZCL_FRAME_SERVER_CLIENT_DIR,
                        ZCL_CLUSTER_GEN_POWER_CFG, (zclReportCmd_t *)&cmd);
}

void zb_epaper_report_attrs(void) {
    if (!zb_isDeviceJoinedNwk()) return;

    // Synchronize latest runtime power telemetry into cluster attributes
    power_stats_t pstats;
    power_tracker_get_stats(&pstats);
    g_zcl_epaperAttrs.wakeupCount    = pstats.wakeup_count;
    g_zcl_epaperAttrs.wakeupDuration = pstats.wakeup_duration_ms;
    g_zcl_epaperAttrs.txDuration     = pstats.tx_duration_ms;
    g_zcl_epaperAttrs.rxDuration     = pstats.rx_duration_ms;

    DEBUG_LOG("ZIGBEE", "Report epaper: slot %u, style %u, mode %u, refreshes %u",
              g_zcl_epaperAttrs.activeSlot, g_zcl_epaperAttrs.renderStyle,
              g_zcl_epaperAttrs.activeMode, g_zcl_epaperAttrs.refreshCount);

    epInfo_t dstEpInfo;
    TL_SETSTRUCTCONTENT(dstEpInfo, 0);
    dstEpInfo.profileId = HA_PROFILE_ID;
    dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
    dstEpInfo.dstAddr.shortAddr = 0x0000;
    dstEpInfo.dstEp = 1;

    // Batch all 9 custom epaper and power telemetry attributes into a single atomic report command (1 packet)
    struct {
        u8 numAttr;
        zclReport_t attrList[9];
    } cmd;

    cmd.numAttr = 9;
    cmd.attrList[0].attrID   = ZCL_ATTRID_EPAPER_ACTIVE_SLOT;
    cmd.attrList[0].dataType = ZCL_DATA_TYPE_UINT8;
    cmd.attrList[0].attrData = &g_zcl_epaperAttrs.activeSlot;

    cmd.attrList[1].attrID   = ZCL_ATTRID_EPAPER_RENDER_STYLE;
    cmd.attrList[1].dataType = ZCL_DATA_TYPE_UINT8;
    cmd.attrList[1].attrData = &g_zcl_epaperAttrs.renderStyle;

    cmd.attrList[2].attrID   = ZCL_ATTRID_EPAPER_ACTIVE_MODE;
    cmd.attrList[2].dataType = ZCL_DATA_TYPE_UINT8;
    cmd.attrList[2].attrData = &g_zcl_epaperAttrs.activeMode;

    cmd.attrList[3].attrID   = ZCL_ATTRID_EPAPER_REFRESH_COUNT;
    cmd.attrList[3].dataType = ZCL_DATA_TYPE_UINT16;
    cmd.attrList[3].attrData = (uint8_t *)&g_zcl_epaperAttrs.refreshCount;

    cmd.attrList[4].attrID   = ZCL_ATTRID_EPAPER_LED_STATE;
    cmd.attrList[4].dataType = ZCL_DATA_TYPE_UINT8;
    cmd.attrList[4].attrData = &g_zcl_epaperAttrs.ledState;

    cmd.attrList[5].attrID   = ZCL_ATTRID_EPAPER_WAKEUP_COUNT;
    cmd.attrList[5].dataType = ZCL_DATA_TYPE_UINT32;
    cmd.attrList[5].attrData = (uint8_t *)&g_zcl_epaperAttrs.wakeupCount;

    cmd.attrList[6].attrID   = ZCL_ATTRID_EPAPER_WAKEUP_DURATION;
    cmd.attrList[6].dataType = ZCL_DATA_TYPE_UINT32;
    cmd.attrList[6].attrData = (uint8_t *)&g_zcl_epaperAttrs.wakeupDuration;

    cmd.attrList[7].attrID   = ZCL_ATTRID_EPAPER_TX_DURATION;
    cmd.attrList[7].dataType = ZCL_DATA_TYPE_UINT32;
    cmd.attrList[7].attrData = (uint8_t *)&g_zcl_epaperAttrs.txDuration;

    cmd.attrList[8].attrID   = ZCL_ATTRID_EPAPER_RX_DURATION;
    cmd.attrList[8].dataType = ZCL_DATA_TYPE_UINT32;
    cmd.attrList[8].attrData = (uint8_t *)&g_zcl_epaperAttrs.rxDuration;

    zcl_sendReportAttrs(APP_ENDPOINT_1, &dstEpInfo, TRUE, ZCL_FRAME_SERVER_CLIENT_DIR,
                        ZCL_CLUSTER_CUSTOM_EPAPER, (zclReportCmd_t *)&cmd);
}

void zb_power_stats_report(void) {
    zb_epaper_report_attrs();
}

void user_zb_init(bool isRetention) {
    if (!isRetention) {
        DEBUG_LOG("ZB_APP", "user_zb_init(isRetention=0)");
        s_zb_poll_cycles = 0;
        s_zb_last_reported_mv = 0;
        s_zb_last_reported_pct = 0xFF;
        s_zb_initial_reported = false;
        s_epd_was_busy = false;

        // Initialize 2.4 GHz radio hardware and MAC CSMA Timer 3 for Zigbee 250K mode
        ZB_RADIO_INIT();
        ZB_TIMER_INIT();
        rf_setTrxState(RF_STATE_OFF);
        ZB_RADIO_TX_POWER_SET(ZB_DEFAULT_TX_POWER_IDX);

        // Cold boot initialization
        DEBUG_LOG("ZB_APP", "user_zb_init: calling stack_init");
        stack_init();
        DEBUG_LOG("ZB_APP", "user_zb_init: stack_init done, calling app_zcl_endpoint_init");
        app_zcl_endpoint_init();
        DEBUG_LOG("ZB_APP", "user_zb_init: app_zcl_endpoint_init done");

        u8 repower = drv_pm_deepSleep_flag_get() ? 0 : 1;
        DEBUG_LOG("ZIGBEE", "Initializing Zigbee stack (repower: %u, factoryNew: %u)",
                  repower, zb_isDeviceFactoryNew());
        bdb_init((af_simple_descriptor_t *)&app_ep1Desc, &g_bdbCommissionSetting, &g_zbBdbCb, repower);
        DEBUG_LOG("ZB_APP", "user_zb_init: bdb_init done");

        // Configure 2-second indirect parent poll rate
        zb_setPollRate(2000);
        if (s_zb_nwk_channel == 0 && g_zbMacPib.phyChannelCur >= 11 && g_zbMacPib.phyChannelCur <= 26) {
            s_zb_nwk_channel = g_zbMacPib.phyChannelCur;
        }
        ev_timer_setPrevSysTick(clock_time());
        DEBUG_LOG("ZB_APP", "user_zb_init: completed successfully");
    } else {
        // Recovery from retention sleep:
        // drv_platform_init() already ran ZB_RADIO_INIT(); mac_phyReconfig() restores
        // RF_STATE_OFF, TX power, active channel (g_zbMacPib.phyChannelCur), RX buffer, and TRX config.
        mac_phyReconfig();
        rf_setTrxState(RF_STATE_OFF);
        if (g_zbMacPib.phyChannelCur >= 11 && g_zbMacPib.phyChannelCur <= 26) {
            s_zb_nwk_channel = g_zbMacPib.phyChannelCur;
        }
    }
}

void zb_task(void) {
    ev_main();
    tl_zbTaskProcedure();

    if (s_pending_mode_switch && clock_time_exceed(s_pending_mode_switch_tick, 350 * 1000)) {
        uint8_t target = s_pending_mode_switch;
        s_pending_mode_switch = 0;
        DEBUG_LOG("ZIGBEE", "Executing deferred mode switch to %u", target);
        mode_switch_to(target);
    }

    if (zb_is_pairing()) {
        static uint32_t last_pairing_pulse_tick = 0;
        if (clock_time_exceed(last_pairing_pulse_tick, 1000 * 1000)) {
            last_pairing_pulse_tick = clock_time();
            led_blink(LED_GREEN, 40);
        }
    }

    if (s_zb_ota_last_activity_tick != 0 && clock_time_exceed(s_zb_ota_last_activity_tick, 16 * 1000 * 1000)) {
        s_zb_ota_last_activity_tick = 0;
        if (!s_zb_ota_active && !zb_is_interviewing()) {
            zb_setPollRate(2000);
        }
    }
}

bool zb_is_idle(void) {
    return (bdb_isIdle() && !tl_stackBusy() && zb_isTaskDone() && !ev_timer_process(true));
}

void zb_pm_task(void) {
    // 1. Never sleep while pairing, identify, NFC, pending mode switch, or active OTA are running
    if (zb_is_pairing() || (g_zcl_identifyAttrs.identifyTime > 0) ||
        nfc_fm11nc08_is_active() || !gpio_read(GPIO_NFC_IRQ) || s_pending_mode_switch || zb_is_ota_active()) {
        if (epd_is_busy()) {
            s_epd_was_busy = true;
        }
        return;
    }

    // During EPD refresh, use SUSPEND_MODE so digital GPIO outputs (EPD power gate, RESET, CS)
    // remain actively driven while gating the MCU/RF clocks between Zigbee polls (~35 uA vs 3.8 mA)
    if (epd_is_busy()) {
        s_epd_was_busy = true;
        if (zb_is_idle()) {
            apsCleanToStopSecondClock();
            u32 r = drv_disable_irq();
            u32 sleepTime = 2000;
            ev_timer_event_t *timerEvt = ev_timer_nearestGet();
            if (timerEvt && timerEvt->timeout > 0 && timerEvt->timeout < sleepTime) {
                sleepTime = timerEvt->timeout;
            }
            if (sleepTime > 0 && gpio_read(GPIO_NFC_IRQ)) {
                debug_uart_flush();
                cpu_set_gpio_wakeup(GPIO_NFC_IRQ, Level_Low, 1);
                if (gpio_read(GPIO_EPD_BUSY) == 0) {
                    cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 1);
                } else {
                    cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 0);
                }
                rf_paShutDown();
                power_tracker_on_sleep();
                drv_pm_sleep(PM_SLEEP_MODE_SUSPEND, PM_WAKEUP_SRC_TIMER | PM_WAKEUP_SRC_PAD,
                             clock_time() + sleepTime * 1000 * S_TIMER_CLOCK_1US);
                mac_phyReconfig();
                rf_setTrxState(RF_STATE_OFF);
                cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 0);
                power_tracker_on_wake();
            }
            drv_restore_irq(r);
        }
        return;
    }

    if (s_epd_was_busy) {
        s_epd_was_busy = false;
        zb_epaper_report_attrs();
    }

    // 2. If unjoined: sleep dynamically according to scheduled rejoin backoff timer,
    // or enter 1-hour battery-saver deep sleep if no timers are pending
    if (!zb_isDeviceJoinedNwk()) {
        if (zb_is_idle()) {
            debug_uart_flush();
            cpu_set_gpio_wakeup(GPIO_NFC_IRQ, Level_Low, 1);
            ev_timer_event_t *nearestEvt = ev_timer_nearestGet();
            if (nearestEvt && nearestEvt->timeout > 0) {
                power_tracker_on_sleep();
                drv_pm_lowPowerEnter();
            } else {
                apsCleanToStopSecondClock();
                u32 r = drv_disable_irq();
                rf_paShutDown();
                power_tracker_on_sleep();
                drv_pm_longSleep(PM_SLEEP_MODE_DEEP_WITH_RETENTION, PM_WAKEUP_SRC_TIMER | PM_WAKEUP_SRC_PAD, 3600 * 1000);
                drv_restore_irq(r);
            }
        }
        return;
    }

    // 3. Normal joined operation: sleep matching parent poll interval
    if (zb_is_idle()) {
        // Initial report after pairing & interview completes
        if (!s_zb_initial_reported && !zb_is_interviewing()) {
            zb_send_initial_reports();
            return;
        }

        s_zb_poll_cycles++;
        uint16_t mv = get_battery_mv();
        uint8_t pct = get_battery_level(mv);
        bool need_report = false;

        // Heartbeat: report at least once every 1 hour (1800 cycles * 2000ms = 3600s)
        if (s_zb_poll_cycles >= 1800) {
            need_report = true;
        } else if (s_zb_last_reported_mv > 0) {
            int16_t diff_mv = (int16_t)mv - (int16_t)s_zb_last_reported_mv;
            int16_t diff_pct = (int16_t)pct - (int16_t)s_zb_last_reported_pct;
            if (diff_mv < 0) diff_mv = -diff_mv;
            if (diff_pct < 0) diff_pct = -diff_pct;
            // Report if voltage changed by >= 100 mV or percentage changed by >= 5%
            if (diff_mv >= 100 || diff_pct >= 5) {
                need_report = true;
            }
        }

        if (need_report) {
            s_zb_poll_cycles = 0;
            zb_battery_report();
            zb_epaper_report_attrs();
            return;
        }

        debug_uart_flush();
        cpu_set_gpio_wakeup(GPIO_NFC_IRQ, Level_Low, 1);
        power_tracker_on_sleep();
        drv_pm_lowPowerEnter();
    }
}
