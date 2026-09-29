# Changelog

All notable changes to the TLSR E-Paper Dual-Stack Firmware are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v1.0.07] - 2026-09-30

### Fixed
- **NWK Frame Counter Rollback / `NWK_FRAME_COUNTER_FAILURE` on Cold Boot**:
  - Resolved an issue where a cold boot or battery swap caused coordinators to reject device transmissions with `NWK_FRAME_COUNTER_FAILURE = 1` or reject rejoins with `0xC3` (`NOT_PERMITTED`).
  - In Telink B85 Zigbee SDK, `nv_nwkFrameCountFromFlash()` loaded the raw flash counter without accounting for unwritten RAM increments (which are flushed to flash in batches of 1024 frames via `UPDATE_FRAMECOUNT_THRES`).
  - Updated [`src/patch_sdk/drv_nv.c`](src/patch_sdk/drv_nv.c) to advance the restored frame counter by `UPDATE_FRAMECOUNT_THRES * 2` (2048) and immediately commit it to flash on boot.
- **Permanent Disconnection from Coordinator Rejoin `0xC3` (`NOT_PERMITTED`) & Insecure Rejoin Fallback**:
  - Fixed an issue where devices became permanently locked out from the network if a coordinator rebooted with stale security credentials or rejected a secure rejoin with `0xC3`.
  - Added [`zb_on_rejoin_security_not_permitted()`](src/zigbee/zb_appCb.c) hook in [`src/zigbee/zb_app.c`](src/zigbee/zb_app.c) and [`src/zigbee/zb_app.h`](src/zigbee/zb_app.h) to detect status `0xC3` from `sensorDevice_startDevCnfHandler()` and switch active rejoin mode to `REJOIN_INSECURITY`.
  - Introduced `zb_issue_rejoin_req()` in [`src/zigbee/zb_appCb.c`](src/zigbee/zb_appCb.c) to cleanly configure `aps_authenticated` and `aps_use_insecure_join` before invoking `zb_rejoinReq`.
  - Updated `zb_rejoin_backoff_cb()` to alternate between `REJOIN_SECURITY` and `REJOIN_INSECURITY` on every other attempt, allowing recovery under both strict Zigbee 3.0 coordinators and unauthenticated/rebuilt coordinators without requiring a physical factory reset.
  - Automatically reset rejoin mode to `REJOIN_SECURITY` upon successful commissioning, manual pairing mode entry, or network leave.
- **Dual-Mode BLE-to-Zigbee Radio Context Recovery (`0xE9` / `MAC_STA_NO_ACK`)**:
  - Fixed sporadic transmission failures (`0xE9`) occurring after context switches from BLE back to Zigbee.
  - In [`src/zigbee_ble_switch.c`](src/zigbee_ble_switch.c), updated `switch_to_zb_context()` to mark `CURRENT_SLOT_SET(DUALMODE_SLOT_ZIGBEE)`, select internal 32k RC, reapply the active Zigbee channel frequency (`ZB_TRANSCEIVER_SET_CHANNEL(ch)`), and explicitly place the transceiver into receive mode (`rf_setTrxState(RF_STATE_RX)`).

---

## [v1.0.06] - 2026-09-27

### Fixed
- **Zigbee Rejoin Failure after Coordinator Downtime (`0x8D` / `ZDO_NOT_AUTHORIZED`)**:
  - Resolved an issue where the device failed to rejoin the Zigbee network after coordinator downtime and became stuck failing with `status: 0x8D` (`ZDO_NOT_AUTHORIZED`) across retention sleep cycles.
  - Root cause: Alternating secure and insecure rejoins in [`src/zigbee/zb_appCb.c`](src/zigbee/zb_appCb.c) called `zb_rejoinSecModeSet(REJOIN_INSECURITY)`, which clears `APS_IB().aps_authenticated = 0` in Telink's `libzb_ed.a`. The subsequent call to `zb_rejoinSecModeSet(REJOIN_SECURITY)` only cleared `aps_use_insecure_join = 0` but never restored `aps_authenticated = 1`. Because retention RAM preserves stack BSS across sleep cycles, `zdo_nlme_join_confirm` waited for a Trust Center Transport Key (which Zigbee 3.0 coordinators never send on secure rejoin) and timed out with `0x8D`.
  - Enforced `REJOIN_SECURITY` across all rejoin attempts (insecure rejoin removed) and explicitly reset `APS_IB().aps_authenticated = 1` and `APS_IB().aps_use_insecure_join = 0` before every rejoin request.
- **Permanent Deep Sleep Lockout on Rejoin Exhaustion**:
  - Fixed an issue where reaching 15 failed rejoin attempts cancelled all backoff timers, leaving the device stranded in deep sleep with no wake timer scheduled.
  - Extended rejoin retry schedule to a 3-day window (`ZB_REJOIN_MAX_ATTEMPTS = 85`), continuing to retry once every 1 hour (alternating 2 single-channel scans : 1 full 16-channel scan) for ~74 hours (~3.08 days) at ~7.0 µA before entering battery-saver deep sleep (4.0 µA).
  - Tapping the tag with an NFC smartphone or reader wakes the tag and restarts rejoin backoff at any time.

### Changed
- **Power Consumption Documentation**:
  - Updated [`docs/power-consumption.md`](docs/power-consumption.md) with comprehensive power modeling for the 1-hour rejoin backoff mode (~7.00 µA average current, 8.15 years projected longevity on CR2450) and documented the 3-day recovery window.

---

## [v1.0.05] - 2026-09-27

### Fixed
- **Status LED Output Driver on Retention Wakeup (`PD2` / `PD3`)**: Fixed an issue where the red LED was active during initial boot refresh but remained inactive during subsequent display refreshes triggered without rebooting (via Web Bluetooth, Zigbee attribute write, or NFC).
  - Defined default hardware GPIO table macros in [`src/app_config.h`](src/app_config.h) for `PD2` (`GPIO_LED_RED`) and `PD3` (`GPIO_LED_GREEN`) with `OUTPUT_ENABLE = 1`, `DATA_OUT = 1` (Active Low OFF), and `PM_PIN_PULLUP_1M`. This prevents `gpio_init(0)` from disabling the LED output buffers when waking from deep sleep retention.
  - Updated `led_set()` in [`src/led.c`](src/led.c) to latch data output level before setting output enable, eliminating active-low output glitches.
  - Preserved the zero-overhead fast retention wakeup path in `led_restore_retention()` (`if (s_led_state == 0) return;`), which executes in 4 instructions (~0.25 µs) during routine 2.0s sleep wakeups.
- **SWS Debugger Protection (`PA7`)**: Maintained `PA7` (`GPIO_LED_BLUE`) at `AS_SWIRE` default in `gpio_default.h` to protect the cold-boot SWS programming/debugger attach window in `main.c`. `led_set()` dynamically claims `PA7` as `AS_GPIO` output only while `LED_BLUE` is active, returning it cleanly to `AS_SWIRE` (input enable 1, output enable 0) when turned off.

### Changed
- **Production Build by Default (`DEBUG=0`)**: Changed `Makefile` default from `DEBUG ?= 1` to `DEBUG ?= 0`.
  - Production builds strip hardware UART initialization (`PB1`), UART clock gating (`FLD_CLK0_UART_EN`), blocking `debug_uart_flush()` CPU spins, and in-memory log ring buffer memory.
  - Reduces firmware size by ~10 KB Flash per target (down from 182 KB to 172 KB).
  - Reduces SRAM consumption by ~2.4 KB (down from 32,084 B to 29,672 B), freeing up 18,976 B for stack and runtime operations.
  - Debug builds can still be built anytime via `make DEBUG=1` or `DEBUG=1 ./build.sh`.
- **Build System Portability**:
  - Added auto-discovery fallbacks for `TC32_PATH` and `SDK_PATH` in `Makefile`.
  - Added `SDK` symlink entry to `.gitignore`.

---

## [v1.0.04] - 2026-09-27

### Added
- **Initial Open-Source Release**: Full dual-stack firmware for Telink TLSR8258 ESL tags.
- **Hardware Support**:
  - Hanshow Stellar-M3N@ / `E31HA` (2.13" $250 \times 122$ BWR E-Paper, UC8151).
  - Hanshow Stellar-XL3N@ / `E31PA` (4.2" $400 \times 300$ BWR E-Paper, UC8176).
- **Exclusive Dual-Stack Protocol Engine**:
  - Zigbee 3.0 Sleepy End Device profile (2.0s polling, manufacturer cluster `0xFC00`, standard ZCL OTA).
  - Bluetooth Low Energy 5.0 (BTHome V2 sensor telemetry beacon + custom GATT service).
- **Zero-Power Passive NFC Integration (FM11NC08)**:
  - Event-gated NDEF record processor with zero standby battery consumption.
  - Runtime commands for protocol switching (`mode:zigbee`, `mode:ble`), slot selection (`slot:0`–`slot:8`), color styling (`style:0`–`style:4`), and Zigbee network resetting.
- **Ultra-Low-RAM E-Paper Streaming Engine**:
  - 2D Delta + PackBits line-by-line decompression streaming in 64-byte chunks (< 50 bytes RAM overhead).
  - 8 user image slots on M3Na (4 KB each) and 4 user slots on XL3Na (8 KB each).
  - Slot 0 dynamic hardware info page and white blanking page.
- **5 On-the-Fly Color Rendering Styles**:
  - Standard BWR Tri-Color, B&W Standard, B&W Inverted, Red & White Standard, Red & White Inverted.
- **Power Optimization**:
  - Deep sleep with retention average: ~3.8–4.0 µA.
  - Suspended E-Paper refresh mode: ~35 µA.
  - Coin-cell multi-year battery lifetime.
- **Ecosystem Integrations**:
  - Web Bluetooth progressive web application (`web/index.html`).
  - Zigbee2MQTT external converter (`zigbee2mqtt/zb-epaper.js`).
  - Home Assistant ZHA quirk integration (`zha_quirks/ts_epaper.py`).
