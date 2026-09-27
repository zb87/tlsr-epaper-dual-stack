#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "led.h"
#include "app_config.h"
#include "zb_app.h"
#include "debug_uart.h"
#include "nfc_fm11nc08.h"

#if ZCL_OTA_SUPPORT
#include "ota.h"

ota_preamble_t app_otaInfo = {
    .fileVer          = FILE_VERSION,
    .imageType        = IMAGE_TYPE,
    .manufacturerCode = MANUFACTURER_CODE_TELINK,
};

static void app_otaProcessMsgHandler(u8 evt, u8 status) {
    if (evt == OTA_EVT_START) {
        if (status == ZCL_STA_SUCCESS) {
            DEBUG_LOG("OTA", "Zigbee OTA started; setting fast poll rate");
            zb_setPollRate(QUEUE_POLL_RATE);
            zb_ota_set_active(true);
            led_blink(LED_BLUE, 100);
        } else {
            DEBUG_LOG("OTA", "Zigbee OTA start failed with status 0x%02X", status);
        }
    } else if (evt == OTA_EVT_COMPLETE) {
        DEBUG_LOG("OTA", "Zigbee OTA completed with status 0x%02X", status);
        zb_setPollRate(2000);
        zb_ota_set_active(false);
        if (status == ZCL_STA_SUCCESS) {
            DEBUG_LOG("OTA", "OTA upgrade complete! Rebooting into new firmware...");
            debug_uart_flush();
            WaitMs(100);
            ota_mcuReboot();
        } else {
            DEBUG_LOG("OTA", "OTA failed; scheduling periodic retry");
            ota_queryStart(OTA_PERIODIC_QUERY_INTERVAL);
        }
    } else if (evt == OTA_EVT_IMAGE_DONE) {
        DEBUG_LOG("OTA", "Zigbee OTA image download complete; awaiting upgrade countdown");
        zb_setPollRate(2000);
    }
}

ota_callBack_t app_otaCb = {
    app_otaProcessMsgHandler,
};
#endif

void zb_bdbInitCb(u8 status, u8 joinedNetwork);
void zb_bdbCommissioningCb(u8 status, void *arg);
void zb_bdbIdentifyCb(u8 endpoint, u16 srcAddr, u16 identifyTime);
void zb_bdbFindBindSuccessCb(findBindDst_t *pDstInfo);

bdb_appCb_t g_zbBdbCb = {
    zb_bdbInitCb,
    zb_bdbCommissioningCb,
    zb_bdbIdentifyCb,
    zb_bdbFindBindSuccessCb
};

static bool s_zb_pairing_active = false;
static uint32_t s_zb_pairing_start_tick = 0;
static ev_timer_event_t *s_steer_timer_evt = NULL;

static ev_timer_event_t *s_rejoin_timer_evt = NULL;
static uint8_t s_rejoin_attempts = 0;

static bool s_zb_interview_active = false;
static ev_timer_event_t *s_interview_timer_evt = NULL;

#if ZCL_OTA_SUPPORT
extern ev_timer_event_t otaTimer;
#endif

static void zb_stop_periodic_timers(void) {
#if ZCL_OTA_SUPPORT
    ev_unon_timer(&otaTimer);
#endif
    reportAttrTimerStop();
}

static u32 zb_get_rejoin_channel_mask(bool full_scan) {
    if (!full_scan) {
        uint8_t ch = zb_get_channel();
        if (ch < 11 || ch > 26) {
            uint8_t len = 0;
            tl_zbMacAttrGet(MAC_PHY_ATTR_CURRENT_CHANNEL, &ch, &len);
        }
        if (ch >= 11 && ch <= 26) {
            return (1UL << ch);
        }
    }
    return zb_apsChannelMaskGet();
}

static s32 zb_interview_timeout_cb(void *arg) {
    (void)arg;
    s_interview_timer_evt = NULL;
    s_zb_interview_active = false;
    if (zb_isDeviceJoinedNwk() && !zb_is_ota_active()) {
        zb_setPollRate(2000); // Interview complete: revert to normal 2s SED poll rate
    }
    DEBUG_LOG("ZIGBEE", "Interview window complete; reverted to 2000ms SED poll rate");
    return -1;
}

bool zb_is_pairing(void) {
    return s_zb_pairing_active;
}

bool zb_is_interviewing(void) {
    return s_zb_interview_active;
}

void zb_start_interview_window(void) {
    s_zb_interview_active = true;
    if (s_interview_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_interview_timer_evt);
        s_interview_timer_evt = NULL;
    }
    s_interview_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_interview_timeout_cb, NULL, 60 * 1000);
    zb_setPollRate(200); // 200ms fast parent poll during interview & configuration!
    DEBUG_LOG("ZIGBEE", "Interview window started: fast polling at 200ms for 60s");
}

static int zb_steer_retry_cb(void *arg) {
    (void)arg;
    s_steer_timer_evt = NULL;
    if (s_zb_pairing_active && !zb_isDeviceJoinedNwk()) {
        if (clock_time_exceed(s_zb_pairing_start_tick, 180 * 1000 * 1000)) {
            // 3-minute pairing window timed out
            DEBUG_LOG("ZIGBEE", "Pairing mode timed out (180s)");
            s_zb_pairing_active = false;
            led_blink(LED_RED, 300);
            return -1;
        }
        if (bdb_networkSteerStart() != BDB_STATE_IDLE) {
            DEBUG_LOG("ZIGBEE", "BDB network steer busy; retrying in 100ms");
            if (!s_steer_timer_evt) {
                s_steer_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_steer_retry_cb, NULL, 100);
            }
        } else {
            DEBUG_LOG("ZIGBEE", "BDB network steer started");
        }
    }
    return -1;
}

void zb_start_pairing(void) {
    DEBUG_LOG("ZIGBEE", "Starting network steering / pairing mode (180s window)...");
    s_zb_pairing_active = true;
    s_zb_pairing_start_tick = clock_time();
    if (s_steer_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_steer_timer_evt);
        s_steer_timer_evt = NULL;
    }
    if (s_rejoin_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
        s_rejoin_timer_evt = NULL;
    }
    if (s_interview_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_interview_timer_evt);
        s_interview_timer_evt = NULL;
    }
    s_zb_interview_active = false;
    s_rejoin_attempts = 0;
    zb_stop_periodic_timers();
    if (bdb_networkSteerStart() != BDB_STATE_IDLE) {
        s_steer_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_steer_retry_cb, NULL, 100);
    }
    led_blink(LED_GREEN, 200);
}

void zb_on_network_leave(void) {
    if (s_zb_pairing_active) {
        return;
    }
    DEBUG_LOG("ZIGBEE", "Network leave: entering pairing mode");

    // 1. Mark device as factory new
    zb_deviceFactoryNewSet(true);

    // 2. Set pairing active immediately to prevent PM deep sleep
    s_zb_pairing_active = true;
    s_zb_pairing_start_tick = clock_time();

    // 3. Cancel any active rejoin, interview, or steer timers
    if (s_steer_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_steer_timer_evt);
        s_steer_timer_evt = NULL;
    }
    if (s_rejoin_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
        s_rejoin_timer_evt = NULL;
    }
    if (s_interview_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_interview_timer_evt);
        s_interview_timer_evt = NULL;
    }
    s_zb_interview_active = false;
    s_rejoin_attempts = 0;
    zb_stop_periodic_timers();

    // 4. Reset reporting state for new network
    zb_reset_reporting_state();

    // 5. Visual indication: pairing mode active
    led_blink(LED_GREEN, 200);

    // 6. Update NFC telemetry so NFC reads immediately show ZB-P
    app_update_nfc_telemetry();

    // 7. Reset BDB state machine to cleanly trigger pairing
    tl_bdbReset();

    // 8. Defensive fallback: schedule steering retry in 100ms if steering hasn't started yet
    if (!s_steer_timer_evt) {
        s_steer_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_steer_retry_cb, NULL, 100);
    }
}

static u32 zb_get_rejoin_backoff_ms(uint8_t attempts_done) {
    /* Exponential backoff schedule across 15 attempts:
     * Attempt 1..2:   15s
     * Attempt 3..4:   30s
     * Attempt 5..6:   60s (1 min)
     * Attempt 7..9:   5 min
     * Attempt 10..12: 15 min
     * Attempt 13..15: 1 hour
     */
    if (attempts_done < 2) {
        return 15 * 1000;
    } else if (attempts_done < 4) {
        return 30 * 1000;
    } else if (attempts_done < 6) {
        return 60 * 1000;
    } else if (attempts_done < 9) {
        return 5 * 60 * 1000;
    } else if (attempts_done < 12) {
        return 15 * 60 * 1000;
    } else {
        return 60 * 60 * 1000;
    }
}

static s32 zb_rejoin_backoff_cb(void *arg) {
    (void)arg;
    s_rejoin_timer_evt = NULL;

    if (zb_isDeviceFactoryNew() || zb_isDeviceJoinedNwk() || s_rejoin_attempts >= 15) {
        return -1;
    }

    s_rejoin_attempts++;

    bool is_secure = (s_rejoin_attempts <= 2) || (s_rejoin_attempts & 1);
    // Scan single operating channel on 2 out of 3 attempts (~138ms vs 2.21s), full 16-channel scan every 3rd attempt
    bool full_scan = ((s_rejoin_attempts % 3) == 0);
    u32 ch_mask = zb_get_rejoin_channel_mask(full_scan);

    DEBUG_LOG("ZIGBEE", "Rejoin attempt #%u (%s rejoin, channel mask: 0x%08X)",
              s_rejoin_attempts,
              is_secure ? "secure" : "insecure",
              (unsigned int)ch_mask);

    zb_rejoinSecModeSet(is_secure ? REJOIN_SECURITY : REJOIN_INSECURITY);
    zb_rejoinReq(ch_mask, g_bdbAttrs.scanDuration);

    return -1;
}

void zb_start_rejoin(void) {
    if (zb_isDeviceFactoryNew() || zb_isDeviceJoinedNwk()) {
        return;
    }
    DEBUG_LOG("ZIGBEE", "Starting network rejoin request...");
    if (s_rejoin_timer_evt) {
        TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
        s_rejoin_timer_evt = NULL;
    }
    s_rejoin_attempts = 0;
    zb_rejoinSecModeSet(REJOIN_SECURITY);
    zb_rejoinReq(zb_get_rejoin_channel_mask(false), g_bdbAttrs.scanDuration);
}

void zb_bdbInitCb(u8 status, u8 joinedNetwork) {
    DEBUG_LOG("ZIGBEE", "BDB Init: status 0x%02X, joinedNetwork=%u", status, joinedNetwork);
    if (status == BDB_INIT_STATUS_SUCCESS) {
        if (joinedNetwork) {
            // Rejoined existing network: set 2-second parent data poll
            DEBUG_LOG("ZIGBEE", "Restored existing network connection (Short: 0x%04X, Pan: 0x%04X, Ch: %u)",
                      zb_getLocalShortAddr(), zb_get_pan_id(), zb_get_channel());
            s_zb_pairing_active = false;
            if (s_rejoin_timer_evt) {
                TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
                s_rejoin_timer_evt = NULL;
            }
            s_rejoin_attempts = 0;
            zb_setPollRate(2000);
            led_blink(LED_GREEN, 150);
#if ZCL_OTA_SUPPORT
            ota_queryStart(15 * 60);
#endif
        } else {
            // Not joined: start 180s pairing window
            DEBUG_LOG("ZIGBEE", "Device not joined; initiating pairing mode");
            zb_start_pairing();
        }
    } else if (joinedNetwork || !zb_isDeviceFactoryNew()) {
        // Cold boot when parent/coordinator is temporarily offline: start exponential backoff rejoin
        DEBUG_LOG("ZIGBEE", "Cold-boot rejoin failed; starting exponential backoff rejoin in 15s");
        zb_stop_periodic_timers();
        s_rejoin_attempts = 0;
        if (!s_rejoin_timer_evt) {
            s_rejoin_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_rejoin_backoff_cb, NULL, 15 * 1000);
        }
    }
}

static s32 zb_initial_report_cb(void *arg) {
    (void)arg;
    zb_send_initial_reports();
    return -1;
}

void zb_bdbCommissioningCb(u8 status, void *arg) {
    (void)arg;
    if (status == BDB_COMMISSION_STA_SUCCESS) {
        // Successfully joined / rejoined network!
        bool was_pairing = s_zb_pairing_active;
        DEBUG_LOG("ZIGBEE", "BDB Commissioning: SUCCESS (%s)", was_pairing ? "initial join" : "rejoin");
        DEBUG_LOG("ZIGBEE", "Network details: Short: 0x%04X, Pan: 0x%04X, Ch: %u, Parent: 0x%04X",
                  zb_getLocalShortAddr(), zb_get_pan_id(), zb_get_channel(), zb_getParentShortAddr());
        s_zb_pairing_active = false;
        if (s_steer_timer_evt) {
            TL_ZB_TIMER_CANCEL(&s_steer_timer_evt);
            s_steer_timer_evt = NULL;
        }
        if (s_rejoin_timer_evt) {
            TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
            s_rejoin_timer_evt = NULL;
        }
        s_rejoin_attempts = 0;

        if (was_pairing) {
            // Start 60-second interview / commissioning window (200ms fast poll with deep sleep between polls)
            zb_start_interview_window();

            // Send Device Announcement to coordinator so coordinator identifies us immediately
            zb_zdoSendDevAnnance();
            DEBUG_LOG("ZIGBEE", "Sent ZDO Device Announcement (0x%04X)", zb_getLocalShortAddr());

            // Schedule initial telemetry reports after interview window starts (5 seconds delay)
            TL_ZB_TIMER_SCHEDULE(zb_initial_report_cb, NULL, 5000);

            // Confirmation: 1 clean blink on initial pairing
            led_blink(LED_GREEN, 200);
        } else {
            // Routine rejoin: immediately restore 2000ms SED poll rate without 60s interview penalty
            zb_setPollRate(2000);
        }

#if ZCL_OTA_SUPPORT
        ota_queryStart(15 * 60);
#endif
    } else if (status == BDB_COMMISSION_STA_IN_PROGRESS) {
        // Steering or rejoin in progress
        DEBUG_LOG("ZIGBEE", "BDB Commissioning: IN_PROGRESS");
    } else if (status == BDB_COMMISSION_STA_PARENT_LOST) {
        // Parent lost: stop periodic OTA/reporting timers and immediately trigger single-channel secure rejoin
        DEBUG_LOG("ZIGBEE", "BDB Commissioning: PARENT_LOST, starting single-channel secure rejoin");
        zb_stop_periodic_timers();
        if (s_rejoin_timer_evt) {
            TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
            s_rejoin_timer_evt = NULL;
        }
        if (s_interview_timer_evt) {
            TL_ZB_TIMER_CANCEL(&s_interview_timer_evt);
            s_interview_timer_evt = NULL;
        }
        s_zb_interview_active = false;
        s_rejoin_attempts = 0;
        zb_rejoinSecModeSet(REJOIN_SECURITY);
        zb_rejoinReq(zb_get_rejoin_channel_mask(false), g_bdbAttrs.scanDuration);
    } else if (status == BDB_COMMISSION_STA_REJOIN_FAILURE) {
        if (zb_isDeviceFactoryNew()) {
            DEBUG_LOG("ZIGBEE", "BDB Commissioning: REJOIN_FAILURE (device is factory new)");
            if (s_rejoin_timer_evt) {
                TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
                s_rejoin_timer_evt = NULL;
            }
            s_rejoin_attempts = 0;
            return;
        }
        if (s_rejoin_attempts >= 15) {
            DEBUG_LOG("ZIGBEE", "Rejoin retry limit reached (15 attempts); entering battery-saver deep sleep");
            if (s_rejoin_timer_evt) {
                TL_ZB_TIMER_CANCEL(&s_rejoin_timer_evt);
                s_rejoin_timer_evt = NULL;
            }
            return;
        }
        if (!s_rejoin_timer_evt) {
            u32 delay_ms = zb_get_rejoin_backoff_ms(s_rejoin_attempts);
            DEBUG_LOG("ZIGBEE", "BDB Commissioning: REJOIN_FAILURE (attempt %u/15), next retry in %us",
                      s_rejoin_attempts, (unsigned int)(delay_ms / 1000));
            s_rejoin_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_rejoin_backoff_cb, NULL, delay_ms);
        }
    } else {
        // Scan cycle finished without finding permit-join coordinator
        DEBUG_LOG("ZIGBEE", "BDB Commissioning: scan cycle completed (status: 0x%02X)", status);
        if (s_zb_pairing_active && !zb_isDeviceJoinedNwk()) {
            if (clock_time_exceed(s_zb_pairing_start_tick, 180 * 1000 * 1000)) {
                // Pairing timeout after 180 seconds
                DEBUG_LOG("ZIGBEE", "Pairing mode timed out (180s)");
                s_zb_pairing_active = false;
                led_blink(LED_RED, 300);
            } else {
                // Schedule next channel scan in 800 ms
                DEBUG_LOG("ZIGBEE", "Scheduling next steering scan in 800ms");
                if (!s_steer_timer_evt) {
                    s_steer_timer_evt = TL_ZB_TIMER_SCHEDULE(zb_steer_retry_cb, NULL, 800);
                }
            }
        }
    }
}

void zb_bdbIdentifyCb(u8 endpoint, u16 srcAddr, u16 identifyTime) {
    (void)endpoint;
    (void)srcAddr;
    if (identifyTime > 0) {
        DEBUG_LOG("ZIGBEE", "Identify command received (ep: %u, src: 0x%04X, duration: %us)",
                  endpoint, srcAddr, identifyTime);
        led_blink(LED_GREEN, 200);
    }
}

void zb_bdbFindBindSuccessCb(findBindDst_t *pDstInfo) {
    (void)pDstInfo;
}
