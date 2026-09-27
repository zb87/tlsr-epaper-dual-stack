# Fast Dynamic Prototyping & Live BLE Debug Console

This document specifies the zero-reboot rapid prototyping subsystem for the **Hanshow Stellar-M3N@ / E31HA** (2.13", UC8151) and **Hanshow Stellar-XL3N@ / E31PA** (4.2", UC8176) electronic shelf labels (Telink TLSR8258).

---

## 1. Overview & Problem Statement

### 1.1 The Iteration Bottleneck
Optimizing e-paper display refresh waveforms, LUT registers, fast partial update sequences, and power-gating behavior traditionally requires:
1. Recompiling the full dual-stack application firmware (~154 KB).
2. Entering the Web flasher or CLI.
3. Transmitting the full 154 KB binary over BLE OTA (30–60 seconds).
4. Waiting for bank verification, watchdog reboot, and Bluetooth reconnection.
5. Connecting a separate USB-to-UART serial adapter to `PB1` (`115,200 baud`) to inspect runtime logs.

This multi-step loop takes **3 to 5 minutes per experiment**, making rapid waveform exploration and register trial-and-error prohibitively slow and tedious.

### 1.2 The Fast Prototype Solution
The **Fast Dynamic Prototyping Subsystem** reduces this iteration cycle to **under 2 seconds**:
- **Zero Reboots:** Keep the BLE connection alive and active.
- **Dynamic Flash Execution:** Compile small, self-contained C snippets (< 16 KB) relocatable to flash offset `0x70000` &ndash; `0x73FFF`.
- **Fast Upload:** Transfer the snippet binary in ~100 ms via BLE L2CAP stream.
- **Instant Execution:** Commit to flash and call the snippet entrypoint directly through an exported API jump table.
- **Wireless Live Logging:** Log debug messages into an in-memory 56-entry circular buffer in lower 32 KB retention RAM (`.bss`), streamed or polled wirelessly over BLE without needing any physical wires or serial dongles.

```
┌────────────────────────────────────────────────────────────────────────┐
│ Host PC / Laptop / Browser                                             │
│                                                                        │
│   tools/snippets/test_fast_refresh.c                                   │
│            │                                                           │
│     tc32-elf-gcc (-Ttext 0x70000)                                      │
│            ▼                                                           │
│   test_fast_refresh.bin (< 1 KB)                                       │
│            │                                                           │
│   Python Runner CLI / Web Dashboard                                    │
│   (tools/prototype_runner.py / web/index.html)                         │
└────────────┬───────────────────────────────────────────────────────────┘
             │ BLE Stream Upload (0x1316) ~100 ms
             │ CMD_COMMIT_EXEC [0x41] / CMD_RERUN [0x42]
             ▼
┌────────────────────────────────────────────────────────────────────────┐
│ Telink TLSR8258 ESL Tag (Hanshow Stellar-M3N@ / Stellar-XL3N@)         │
│                                                                        │
│  Flash Partition 0x70000 - 0x73FFF (16 KB Prototype Code)              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ int snippet_main(const epd_test_api_t *api) { ... }              │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│            ▲                                                           │
│            │ Instruction Cache (Tag Table Invalidation)                │
│            │                                                           │
│  Active Firmware (Bank 0 / Bank 1)                                     │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ epd_prototype_execute() -> calls snippet_main(&g_epd_test_api)   │  │
│  │                                                                  │  │
│  │ Retention RAM (.bss < 0x848000):                                 │  │
│  │   56-Entry Ring Buffer (3,360 B) -> BLE Delta Log Push [0x8A]    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Flash Partition Layout & Memory Isolation

The top 64 KB of flash (`0x70000` &ndash; `0x7FFFF`) is partitioned as follows:

```
0x70000 ┌─────────────────────────────────────────────────────────────┐
        │  Dynamic Prototype Snippet Storage (4 sectors)             │  16 KB
0x74000 ├─────────────────────────────────────────────────────────────┤
        │  Wear-Leveled Settings EEPROM (2 ping-pong sectors)         │   8 KB
0x76000 ├─────────────────────────────────────────────────────────────┤
        │  Factory Calibration: Public IEEE MAC Address (1 sector)    │   4 KB
0x77000 ├─────────────────────────────────────────────────────────────┤
        │  Factory Calibration: 24 MHz Crystal Trim (1 sector)        │   4 KB
0x78000 ├─────────────────────────────────────────────────────────────┤
        │  User Image Slot Storage (8 sectors contiguous)             │  32 KB
        │  - M3Na: Slots 1..8 (4 KB each)                             │
        │  - XL3Na: Slots 1..4 (8 KB each)                            │
0x7FFFF └─────────────────────────────────────────────────────────────┘
```

### Partition Safeguards
1. **Bank 0 / Bank 1 Isolation:** The prototype partition (`0x70000` &ndash; `0x73FFF`) never touches Bank 0 (`0x00000` &ndash; `0x2FFFF`) or Bank 1 OTA target (`0x40000` &ndash; `0x6FFFF`).
2. **Settings Isolation:** The Wear-Leveled Settings EEPROM resides safely at `0x74000` &ndash; `0x75FFF`.
3. **Factory Protection:** Sectors `0x76000` (MAC) and `0x77000` (Crystal Trim) are strictly read-only and preserved.
4. **Execution Bounds:** The prototype loader enforces that uploaded snippets cannot exceed 16 KB (`16,384 bytes`).

---

## 3. TC32 Execution Mechanics & Cache Coherency

### 3.1 Direct Flash Execution
The Telink TLSR8258 core uses an internal instruction cache for code executing above flash address `0x00000`. When branching to an address such as `0x70000`:
- The MCU hardware instruction cache controller decodes and fetches instructions directly over SPI flash.
- Functions execute in-place (`XIP`) without needing to be copied into SRAM.

### 3.2 Cache Tag Coherency Invalidation
When new binary code is written to SPI flash over an existing address, the instruction cache tag table still contains the tags and cached lines from previous code runs. If the CPU jumps to the newly written flash code without flushing the cache, it may execute **stale instructions**, leading to undefined behavior or processor halt.

Before branching into snippet code, [`epd_prototype_execute()`](../src/epd/epd_prototype.c) invalidates the hardware instruction cache:

```c
extern uint32_t _ictag_start_;

// Invalidate all 64 lines (256 bytes) of the instruction cache tag table
memset((void *)&_ictag_start_, 0, 256);
```

This guarantees immediate coherency between SPI flash and the TC32 execution pipeline.

---

## 4. In-Memory Debug Log Ring Buffer

To enable log monitoring without physical UART wires:
- An in-memory circular ring buffer is allocated in [`src/debug_uart.c`](../src/debug_uart.c).
- Size: **56 entries**, each storing a 4-byte millisecond timestamp + 2-byte sequence number + 54-byte null-terminated message = 60 bytes. Total RAM: **3,360 bytes**.
- Memory Placement: Placed in **lower 32 KB retention SRAM (`.bss`)** below the `0x848000` retention boundary (`_end_bss_ = 0x847F88`).
  - **Deep Retention Persistence:** Because the entire ring buffer sits inside the lower 32 KB retention SRAM, cold-boot logs (`[BOOT]`) and periodic sleep telemetry (`Wake up #N`) **survive across `DEEPSLEEP_MODE_RET_SRAM_LOW32K` sleep cycles** without extra power draw.
  - **System Stack:** Stack headroom is **15,808 bytes (~15.4 KB)**, leaving plenty of space for deep call trees.

### Log Entry Structure
Each log entry in the ring buffer stores a 32-bit hardware timestamp (system uptime in milliseconds derived from `clock_time()`), a 16-bit monotonic sequence number, and the formatted log message string:
```c
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;              // System uptime in milliseconds
    uint16_t seq;                       // 1-based monotonic sequence number
    char     text[LOG_ENTRY_TEXT_LEN];  // Null-terminated message "[TAG] Text"
} ble_log_entry_t;                      // Exactly 60 bytes
```

### Automatic Logging Pipeline
All calls to [`debug_log()`](../src/debug_uart.h) or `api->log()` automatically push formatted strings into the ring buffer with the current hardware millisecond timestamp when `DEBUG = 1`. When `DEBUG = 0`, debug logging calls, macros, and the in-memory ring buffer are completely stripped from the build.

---

## 5. BLE GATT Protocol Specification

The prototype engine uses the existing EPD Custom GATT Service (`UUID: 0x1314`):
- **Command Characteristic:** `UUID: 0x1315` (Write With Response / Write Without Response, Notify)
- **Data Characteristic:** `UUID: 0x1316` (Write Without Response, Read)

### 5.1 Opcodes Sent to Command Characteristic (`0x1315`)

| Opcode | Payload Format | Description |
| :---: | :--- | :--- |
| `0x40` | `0x40 <len_lo> <len_hi>` | **Prepare Prototype Snippet Upload:** Sets upload target mode, resets offset, and temporarily updates BLE connection parameters to 10 ms interval for ultra-fast transfer. |
| `0x41` | `0x41 [flags] [addr_b0..b3]` | **Commit & Execute:** Erases required 4 KB sectors in flash, writes snippet from RAM cache to flash, invalidates instruction cache, and executes `snippet_main()`. Returns notification `0x89`. Default address is `0x70000`. |
| `0x42` | `0x42 [addr_b0..b3]` | **Re-Execute Snippet:** Re-runs the snippet currently stored at `0x70000` without uploading again. Returns notification `0x89`. |
| `0x50` | `0x50 <last_seen_seq_lo> <last_seen_seq_hi>` | **Pull Logs:** MCU responds with notification `0x8A` packets for any log entries with sequence number $>$ `last_seen_seq`. |
| `0x51` | `0x51 <1=enable, 0=disable>` | **Toggle Real-time Log Streaming:** When enabled, MCU pushes `0x8A` notifications as new logs occur. |
| `0x52` | `0x52` | **Clear Log Buffer:** Flushes all entries from the in-memory log ring buffer and resets sequence counter. |

### 5.2 Streaming Data Chunks to Data Characteristic (`0x1316`)

During snippet upload (following `0x40`), the binary is streamed in variable-sized chunks (typically 16 bytes):
```text
[off_lo, off_hi, data_byte_0, ..., data_byte_N]
```
The MCU acknowledges each block directly into `s_ota_cache` in RAM without writing to flash during the streaming phase.

### 5.3 Notifications Received from Command Characteristic (`0x1315`)

#### Snippet Execution Result (`0x89`)
Sent by the MCU as soon as `snippet_main()` returns:
```text
Byte 0:    0x89 (Marker)
Byte 1..2: Return Code (int16 little-endian: 0 = SUCCESS)
Byte 3..6: Execution Duration in milliseconds (uint32 little-endian)
```

#### Log Message Notification (`0x8A`)
Sent during log pulls or real-time streaming:
```text
Byte 0:    0x8A (Marker)
Byte 1..2: Sequence Number (uint16 little-endian)
Byte 3..6: Hardware Timestamp (uint32 little-endian, system uptime in milliseconds)
Byte 7..N: Null-terminated ASCII Log Text
```
*Note:* If `seq == 0` and packet length is 5 bytes, bytes 3..4 report the current latest sequence number (idle/sync packet).

---

## 6. Writing Prototyping Snippets

Snippet source files are standard C programs implementing the `snippet_main` entrypoint:
```c
#include "epd_prototype.h"

int snippet_main(const epd_test_api_t *api) {
    int i;
    uint32_t t = 0;
    uint32_t t_start = 0;

    api->log("SNIPPET", "=== Fast EPD Refresh Experiment ===");
    api->log("SNIPPET", "Board type: %s", api->board_type ? "4.2 XL3Na" : "2.13 M3Na");

    // 1. Initialize pins & power on panel
    api->epd_init_pins();
    api->epd_power_on();
    api->sleep_ms(5);

    // Hardware reset pulse: EPD_PIN_RESET = PD4 (0x0310) on both M3Na and XL3Na
    api->gpio_write(EPD_PIN_RESET, 0);
    api->sleep_ms(10);
    api->gpio_write(EPD_PIN_RESET, 1);
    api->sleep_ms(20);

    // 2. Booster Soft Start (0x06)
    api->write_cmd(0x06);
    api->write_data(0x17);
    api->write_data(0x17);
    api->write_data(0x17);

    // 3. Power On (0x04)
    api->write_cmd(0x04);
    api->sleep_ms(1);

    // Wait for BUSY (PA1 = 0x0002) to go HIGH (ready / idle).
    // Note: BUSY is LOW (0) during active operations and HIGH (!= 0) when idle.
    t = 0;
    while (epd_proto_is_busy(api) && ++t < 100) {
        api->sleep_ms(1);
    }
    api->log("SNIPPET", "Power on took %lu ms (BUSY high)", t);

    // 4. Panel setting test: Fast B/W OTP LUT mode (PSR 0x00 = 0x1F) ~2.5s
    // (Or register LUT 0x3F with custom waveform tables for ~800ms partial update)
    api->write_cmd(0x00);
    api->write_data(0x1F); // Fast monochrome OTP mode (vs 0x0F for 15s 3-color BWR)
    api->write_data(0x0F);

    api->write_cmd(0x50);  // VCOM & data interval
    api->write_data(0x97);

    // Load monochrome image data (DTM1 0x10)
    api->write_cmd(0x10);
    for (i = 0; i < 4000; i++) {
        uint8_t b = (api->render_buffer && i < (int)api->buffer_size) ? api->render_buffer[i] : 0x00;
        if ((i & 0x0F) == 15) b |= 0x3F; // UC8151 122-line padding
        api->write_data(b);
    }

    // Clear red plane (DTM2 0x13)
    api->write_cmd(0x13);
    for (i = 0; i < 4000; i++) {
        uint8_t b = 0x00;
        if ((i & 0x0F) == 15) b &= 0xC0;
        api->write_data(b);
    }

    // 5. Trigger display refresh (0x12)
    api->log("SNIPPET", "Triggering DRF (0x12)...");
    api->write_cmd(0x12);

    // Wait for BUSY to drop LOW (confirming refresh has begun)
    t_start = 0;
    while (!epd_proto_is_busy(api) && ++t_start < 100) {
        api->sleep_ms(1);
    }

    // Wait while BUSY is LOW (panel is actively refreshing)
    t = 0;
    while (epd_proto_is_busy(api) && ++t < 25000) {
        api->sleep_ms(1);
        if (t % 500 == 0) {
            api->log("SNIPPET", "Refreshing... (%lu ms)", t);
        }
    }
    api->log("SNIPPET", "Refresh completed in %lu ms", t);

    // 6. Power off & sleep
    api->write_cmd(0x02); // Power off
    api->sleep_ms(1);
    while (epd_proto_is_busy(api)) {
        api->sleep_ms(1);
    }
    api->write_cmd(0x07); // Deep sleep
    api->write_data(0xA5);

    api->epd_power_off();
    api->epd_isolate_pins();

    return 0; // Return code passed back via BLE notification 0x89
}
```

### The `epd_test_api_t` Jump Table
The host firmware provides an exported function pointer table [`epd_test_api_t`](../src/epd/epd_prototype.h):
- **Metadata:** `struct_version` (1), `board_type` (0 = M3Na 2.13", 1 = XL3Na 4.2")
- **Logging:** `log(tag, fmt, ...)` (streams live via ring buffer over BLE)
- **Timing:** `sleep_us(us)`, `sleep_ms(ms)` (cooperatively pumps BLE event loop and logs every 2 ms)
- **Hardware Pins:** `gpio_write(pin, val)`, `gpio_read(pin)`
- **EPD Power & Isolation:** `epd_power_on()`, `epd_power_off()`, `epd_init_pins()`, `epd_isolate_pins()`
- **EPD SPI Primitives:** `write_cmd(cmd)`, `write_data(data)`, `read_spi()`
- **Framebuffer Scratch Access:** `render_buffer` (pointer to shared RAM buffer), `buffer_size`
- **Active Settings:** `active_slot`, `render_style`

### Compilation Command
Snippets are compiled with `tc32-elf-gcc` and linked using an explicit linker script to guarantee `snippet_main` at offset 0:
```bash
# Bank 0 (Base firmware @ 0x000000):
python3 tools/prototype_runner.py --compile-only --bank 0 tools/snippets/test_fast_refresh.c

# Bank 1 (OTA firmware @ 0x040000, XIP link @ 0x30000):
python3 tools/prototype_runner.py --compile-only --bank 1 tools/snippets/test_fast_refresh.c

# Both banks simultaneously:
python3 tools/prototype_runner.py --compile-only --bank auto tools/snippets/test_fast_refresh.c
```

---

## 7. Using the Tools

### 7.1 Python CLI Runner (`tools/prototype_runner.py`)

The automated runner compiles, uploads, executes, and streams logs over BLE.

```bash
# 1. Compile and run once
python3 tools/prototype_runner.py tools/snippets/test_fast_refresh.c

# 2. Watch mode: automatically recompiles and runs whenever you save snippet.c
python3 tools/prototype_runner.py --watch tools/snippets/test_fast_refresh.c

# 3. Specify device MAC address directly
python3 tools/prototype_runner.py -m A4:C1:38:XX:YY:ZZ tools/snippets/test_fast_refresh.c

# 4. Compile-only verification without connecting over BLE
python3 tools/prototype_runner.py --compile-only tools/snippets/test_fast_refresh.c
```

### 7.2 Web Dashboard UI (`web/index.html`)

1. Open `web/index.html` in Chrome or Edge.
2. Click **⚡ Connect Device** and pair with your ESL tag.
3. In the **⚡ Fast Dynamic Prototyping & Live BLE Debug Console** card:
   - Click **📁 Choose Snippet (.bin)** and pick your compiled `.bin`.
   - Click **🚀 Upload & Run**: The dashboard streams the binary to RAM, commits to flash, triggers execution, and displays exit code and duration.
   - Click **🔄 Re-Run**: Re-executes the snippet without re-uploading.
   - **Live Log Terminal:** Toggle **Auto-Poll (1.5s)** or click **📥 Pull Logs** to inspect device logs in real time. Click **🧹 Device Clear** to wipe the MCU ring buffer.

---

## 8. Empirical Case Study: E-Paper Fast Refresh Investigation

The prototyping subsystem was put to production use to investigate whether the Hanshow Stellar-M3N@ (2.13" UC8151 3-Color BWR) could support fast partial refresh.

### 8.1 Evaluated Snippets
1. **`tools/snippets/test_fast_refresh.c`**: Evaluated internal factory OTP ROM waveform modes (`PSR = 0x0F` vs `0x1F`).
   - Confirmed Full BWR OTP runs in **15.15s**; Monochrome KW OTP runs in **12.74s** with degraded, washed-out contrast.
2. **`tools/snippets/test_fast_lut.c`**: Evaluated custom 32-frame differential register LUTs (`0x20..0x24`):
   - With Partial Mode disabled (`0x92`), the UC8151 ignored the register LUT and executed the 12-second monochrome OTP sequence.
   - With Partial Mode enabled (`0x91 (PTIN)` + `0x90 (PTL)` + `PSR = 0xBF`), the UC8151 hardware sequencer stalled indefinitely (`BUSY = 0`), proving that the controller silicon on 3-color BWR panels cannot execute register LUTs.

### 8.2 Prototyping Subsystem Strengths & Enhancements
- **Zero-Bricking Safety:** When the EPD controller hung, the snippet failsafe loop timed out safely at 15s, executed emergency power-down (`POF + DSLP`), restored standard BLE connection parameters, and returned cleanly with code 0.
- **BLE Transfer Robustness (`web/index.html`):**
  - Added 1,200 ms settling delay after opcode `0x40` to allow BLE central and peripheral to complete L2CAP connection parameter updates (`interval = 10 ms, latency = 0`).
  - Implemented BLE packet pacing (8 ms delay per 20-byte block, with 20 ms settling every 8 blocks) to prevent RF RX FIFO overflows on the Telink SoC.
  - Added automatic one-time retry in the web dashboard before reporting an error.
- **Monotonic 32 kHz Real-Time Timestamps (`src/debug_uart.c`):** Switched ring buffer timestamps to `pm_get_32k_tick() / 32`, providing strictly monotonic millisecond timebases that survive suspend and deep-sleep cycles.

