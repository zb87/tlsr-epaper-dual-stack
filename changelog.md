# Changelog

All notable changes to the TLSR E-Paper Dual-Stack Firmware are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
