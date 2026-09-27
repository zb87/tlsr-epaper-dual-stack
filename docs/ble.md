# Bluetooth Low Energy (BLE) Subsystem Reference

This document provides the complete engineering reference for the Bluetooth Low Energy 5.0 subsystem on the Hanshow Stellar-M3N@ / E31HA dual-stack electronic shelf label (Telink TLSR8258). It details the advertising scheme (BTHome v2), connection management, GATT table layout, EPD Display Management protocol (characteristics `0x1315` and `0x1316`), and image chunk streaming pipeline.

---

## 1. BLE Subsystem Architecture

The ESL features a dual-stack architecture capable of operating either as a **Zigbee 3.0 Sleepy End Device** or as a **Bluetooth Low Energy 5.0 Sensor & Actuator**.

When running in BLE mode:
1. **Autonomous Periodic Beaconing:** Broadcasts ambient temperature, battery voltage, battery percentage, and display state using the open **BTHome v2** format. Any Home Assistant instance, ESPHome Bluetooth proxy, or smartphone can ingest sensor telemetry without establishing a connection.
2. **Interactive GATT Connection:** Supports bidirectional GATT connections for telemetry monitoring, configuration adjustments, full-resolution image uploading, flash slot retrieval, firmware updates (Telink BLE OTA), and dual-stack mode switching.
3. **Deep Sleep with Retention:** When disconnected and between advertising events, the MCU enters deep sleep with SRAM retention ($I_{sleep} \approx 2.5\,\mu\text{A}$), waking up briefly on internal timer ticks or NFC carrier assertion (`PC4`).

---

## 2. Advertising & BTHome v2 Specification

The device transmits non-connectable undirected beacons during normal operation, alternating with connectable undirected advertising when a connection window is open.

### 2.1 BTHome v2 Packet Format

The Service Data payload (`0x16`) with UUID `0xFCD2` conforms to the BTHome v2 unencrypted specification:

```
┌────────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│ Length (1 B)   │ Type (0x16)  │ UUID (0xFCD2)│ Header (0x40)│ Sensor TLVs  │
└────────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
```

| Offset | Field | Value | Description |
| :--- | :--- | :--- | :--- |
| 0 | Length | `0x15` (21 B) | Total service data payload length |
| 1 | AD Type | `0x16` | Service Data - 16-bit UUID |
| 2..3 | UUID | `0xD2`, `0xFC` | BTHome 16-bit UUID (`0xFCD2`) |
| 4 | BTHome Header | `0x40` | BTHome v2 format, unencrypted |
| 5..7 | Battery Level | `0x01`, `<pct>` | Object ID `0x01` (Battery percentage, 1 byte uint8) |
| 8..11 | Temperature | `0x02`, `<lo>`, `<hi>` | Object ID `0x02` (Temperature, 2 bytes signed int16, $0.01^\circ\text{C}$ factor) |
| 12..15 | Voltage | `0x0C`, `<lo>`, `<hi>` | Object ID `0x0C` (Battery voltage, 2 bytes uint16, $0.001\,\text{V}$ factor) |
| 16..19 | Refresh Count | `0x49`, `<lo>`, `<hi>` | Object ID `0x49` (Generic counter, 2 bytes uint16) |
| 20..22 | Active Slot | `0x3F`, `<slot>` | Object ID `0x3F` (Custom 1-byte status: active slot `0`..`9` on M3Na, `0`..`5` on XL3Na) |

### 2.2 Advertising Timing & Power Profile

- **Fast Advertising Window:** Following a boot, stack switch, or NFC interaction, the tag advertises at **100 ms intervals for 30 seconds** to allow rapid discovery and connection by the Web UI.
- **Normal Beaconing Interval:** After the fast window expires, the interval transitions to **2,500 ms (2.5 s)**, reducing average current to $<12\,\mu\text{A}$.
- **Transmit Power:** Configured to `+3.01 dBm` (`RF_POWER_P3p01dBm`) for reliable link budget through store shelving and walls.

---

## 3. GATT Service & Characteristic Architecture

The GATT server defines four primary services:

```
TLSR8258 GATT Server
├── Generic Access Profile (0x1800)
├── Device Information Service (0x180A)
├── Battery Service (0x180F)
├── Telink BLE OTA Service (00010203-0405-0607-0809-0a0b0c0d1912)
└── EPD Display Management Service (0x1314)
    ├── EPD Command & Status (0x1315)  [Read | Write | WriteWithoutRsp | Notify]
    └── EPD Data Streaming   (0x1316)  [Write | WriteWithoutRsp]
```

### 3.1 Device Information Service (`0x180A`)

| Characteristic | UUID | Value / Format | Example |
| :--- | :--- | :--- | :--- |
| Model Number | `0x2A24` | String | `"Stellar-M3N@"` |
| Serial Number | `0x2A25` | String | `"E31HA-Dual"` |
| Firmware Revision | `0x2A26` | String | Version string (e.g. `"1.0.12"`) |
| Hardware Revision | `0x2A27` | String | `"TLSR8258"` |
| Software Revision | `0x2A28` | String | `"Dual-Stack ZB/BLE"` |
| Manufacturer Name | `0x2A29` | String | `"Hanshow / DualStack"` |

### 3.2 Battery Service (`0x180F`)

| Characteristic | UUID | Properties | Format | Description |
| :--- | :--- | :--- | :--- | :--- |
| Battery Level | `0x2A19` | Read, Notify | 1 byte (`0`..`100`) | Battery state of charge percentage calculated from SAR ADC VDD reading. |

---

## 4. EPD Display Management Service (`0x1314`)

This custom service provides comprehensive remote control over display slots, rendering styles, waveform refresh modes, user image upload/fetch, and system reboot.

### 4.1 Characteristic Summary

| Characteristic | UUID | Handle | Properties | Role |
| :--- | :--- | :--- | :--- | :--- |
| **EPD Command & Status** | `0x1315` | `EPD_CMD_DP_H` | Read, Write, WriteWithoutRsp, Notify | Control command dispatcher and 23-byte telemetry status stream. |
| **EPD Data Streaming** | `0x1316` | `EPD_DATA_DP_H` | Write, WriteWithoutRsp | High-speed chunk streaming endpoint for image uploads. |

---

### 4.2 Status Packet Format (`0x1315` Read / Notification)

Whenever read directly or when a status notification is triggered, `0x1315` emits a 23-byte payload:

```
 0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16..21   22
┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬───────┬────┐
│0x83│Slot│Vbat│Vbat│Temp│Ref │Ref │Mode│Busy│Styl│Bank│Err │   File Version   │  MAC  │LEDs│
│    │    │ Hi │ Lo │ 'C │ Hi │ Lo │    │    │    │    │    │  (32-bit uint)   │(6 B)  │Mask│
└────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴───────┴────┘
```

| Byte Offset | Field | Description |
| :--- | :--- | :--- |
| `0` | **Marker** | Always `0x83` (status packet identifier). |
| `1` | **Active Slot** | Currently active display slot (`0`..`9` on M3Na, `0`..`5` on XL3Na). |
| `2..3` | **Battery Voltage** | Unsigned 16-bit big-endian millivolts (e.g. `0x0B`, `0xCC` = 3,020 mV). |
| `4` | **Temperature** | Signed 8-bit ambient temperature in $^\circ\text{C}$ read from UC8151 internal thermal sensor. |
| `5..6` | **Refresh Count** | Unsigned 16-bit big-endian total lifetime screen refresh counter. |
| `7` | **Active Mode** | `1` = Zigbee 3.0, `2` = BLE 5.0. |
| `8` | **EPD State** | `0` = Idle / Ready, `1` = Screen refresh in progress (BUSY pin asserted, Red LED lit). |
| `9` | **Render Style** | Active style (`0` = Standard, `1` = B&W, `2` = B&W Inv, `3` = R&W, `4` = R&W Inv). |
| `10` | **Active Bank** | Active boot address index: `0` = Bank 0 (`0x00000`), `1` = Bank 1 (`0x40000`). |
| `11` | **OTA Error Code** | Last OTA completion error code from analog retention register `0x3a` (`0` = OK). |
| `12..15` | **Firmware Version** | 32-bit big-endian build version number (`FILE_VERSION`). |
| `16..21` | **Public MAC** | 6-byte public IEEE MAC address (`mac_public[5]` down to `mac_public[0]`). |
| `22` | **LED State Mask** | 3-color LED states: Bit 0 = Blue (`PA7`), Bit 1 = Green (`PD3`), Bit 2 = Red (`PD2`). |

---

### 4.3 Control Command Opcodes (`0x1315` Write)

Clients send commands to `0x1315` using standard GATT write operations:

| Opcode | Payload Format | Description |
| :--- | :--- | :--- |
| `0x10` | `[0x10, <slot>, <style>]` | **Set Slot & Style:** Select active slot (`0`..`9` on M3Na, `0`..`5` on XL3Na) and style (`0`..`4`). Saves to wear-leveled flash and triggers screen refresh (forces Red LED ON during refresh). |
| `0x11` | `[0x11]` | **Force Manual Refresh:** Re-renders current slot with standard full 3-color BWR refresh waveform. |
| `0x12` | `[0x12, <slot>]` | **Erase Slot:** Erases flash sectors associated with user slot (`1`..`8` on M3Na, `1`..`4` on XL3Na). |
| `0x13` | `[0x13]` | **Query Telemetry:** Prompts MCU to push updated 23-byte status packet via notification. |
| `0x14` | `[0x14, <slot>, <plane>]` | **Prepare Slot Upload:** Prepares tag to receive streamed image chunks. `plane`: `0` = Black/White, `1` = Red. Tag clears RAM buffers, disables sleep latency, and negotiates fast 12.5–20 ms connection intervals. |
| `0x15` | `[0x15, <auto_display>, <has_red>]` | **Commit Slot Upload:** Erases flash sectors and programs bit-planes to flash. Automatically updates active slot, saves wear-leveled settings, refreshes display (if `auto_display`), and restores power-saving latency. |
| `0x16` | `[0x16, <slot>, <plane>]` | **Autonomous Stream Fetch:** Initiates high-speed notification streaming of the 4 KB bit-plane. Tag continuously pushes 20-byte chunks via `0x86` notifications followed by an EOF marker (`len=0`). |
| `0x16` | `[0x16, <slot>, <plane>, <off_lo>, <off_hi>, <len>]` | **Fetch Slot Chunk:** Requests a single chunk (up to 16 bytes) from flash slot or RAM via notification `0x86`. |
| `0x20` | `[0x20, <led_mask>, <toggle_flag>]` | **RGB LED Control:** Controls the 3 hardware LEDs: Bit 0 = Blue (`PA7`), Bit 1 = Green (`PD3`), Bit 2 = Red (`PD2`). If `toggle_flag == 1`, specified LEDs are toggled; otherwise, state is set to `<led_mask>`. |
| `0x30` | `[0x30, <mode>]` | **Switch Protocol Mode:** `1` = Zigbee 3.0, `2` = BLE 5.0. Returns `ATT_WRITE_RSP` and pushes status notification immediately to confirm switch to the caller, then executes mode switch and reboot after 300 ms. |
| `0x31` | `[0x31]` | **Reboot Device:** Acknowledges write command immediately to caller, then executes software reset (`start_reboot()`) after 300 ms. |
| `0x32` | `[0x32]` | **Reset Zigbee Network:** Erases all 16 sectors of Zigbee NVRAM (`0x30000`–`0x3FFFF`), clearing all network keys and pairings. Tag enters pairing mode on next Zigbee boot. |

---

## 5. Image Upload & Fetch Streaming Protocol

Each full-resolution $250 \times 122$ image consists of two 4,000-byte bit-planes:
- **Plane 0 (Black/White Plane):** 4,000 bytes ($250 \times 16$ bytes per column). `0` = Black pixel, `1` = White pixel.
- **Plane 1 (Red Plane):** 4,000 bytes. `1` = Red pixel, `0` = Non-red pixel.

### 5.1 OTA-Style Pipelined Upload Sequence (`0x1315` + `0x1316`)

Image slots use the exact same high-speed pipelined protocol as the Telink BLE Wireless OTA Flasher:

1. **Prepare Handshake (`0x14` on `0x1315`):**
   - Web UI sends `[0x14, slot, plane]`.
   - MCU clears target RAM plane buffer, resets upload telemetry (`s_upload_telemetry`), sets manual latency to 0, and requests **10 ms connection intervals** (`bls_l2cap_requestConnParamUpdate(8, 8, 0, 400)`).
   - Zero flash operations are performed during prepare (0 ms blocking).
2. **Pipelined Block Streaming (`0x1316` Write Without Response):**
   - Web UI blasts **16-byte blocks** using `writeValueWithoutResponse()` with a 2 ms pacing delay:
     ```
     [block_idx_lo, block_idx_hi, data (16 B), crc16_lo, crc16_hi] (20 bytes total)
     ```
   - 20 bytes fits perfectly within the standard 23-byte default BLE ATT MTU on any platform without requiring MTU exchange.
   - Total blocks per plane: $4,000 / 16 = 250$ blocks.
   - MCU validates CRC16-Modbus and enforces strictly sequential block numbers (`cmd_type == exp_blk`).
3. **Periodic Flow Control Barriers (GATT Read on `0x1316`):**
   - Every **32 blocks** (512 bytes) and after the last block, the Web UI executes a synchronous `epdDataChar.readValue()`.
   - The MCU responds with `epd_upload_telemetry_t`:
     ```
     [confirmed_offset (2 B uint16), error_code (1 B), reserved (1 B)]
     ```
   - The sync read acts as an execution barrier ensuring the MCU's RX FIFO never overflows and confirms exact byte count and zero CRC errors.
   - Streaming time per plane: **~580 ms**.
   - Total upload time for tri-color BWR (both planes: 500 blocks): **~1.2 to 1.5 seconds** (10&times; faster than stop-and-wait write requests).
4. **Commit Phase (`0x15` on `0x1315`):**
   - Web UI sends `[0x15, mode, has_red]`.
   - MCU erases target flash sectors, programs both planes in 128-byte pages, updates settings, initiates display refresh, and restores normal power-saving connection parameters (`latency=19`).

### 5.2 Autonomous High-Speed Fetch Sequence (`0x16` Stream)

1. **Stream Request (`0x16`):**
   - Web UI sends `[0x16, slot, plane]`.
   - MCU sets latency to 0, sets connection parameters to 12.5–20 ms, and enters autonomous stream mode.
2. **Push Notifications:**
   - In its main loop (`epd_ble_fetch_task`), whenever the BLE TX FIFO has room, the MCU pushes 20-byte chunk notifications on `0x1315`:
     ```
     Notify 0x1315: [0x86, <slot>, <plane>, <off_lo>, <off_hi>, <len=20>, data (20 B)...]
     ```
   - In each connection interval (15 ms), 3–5 notifications are transmitted back-to-back without requiring client write requests.
3. **EOF Marker:**
   - When all 4,000 bytes have been transmitted, the MCU sends a final completion packet:
     ```
     Notify 0x1315: [0x86, <slot>, <plane>, 0xA0, 0x0F, len=0]
     ```
   - MCU restores power-saving latency (`latency=19`).
   - Total fetch time: **~1 to 1.5 seconds per plane**. Zero timeouts!

---

## 6. Rendering Styles & Hardware Waveform Translation

The ESL supports 5 hardware rendering styles executed on-the-fly inside `EPD_BWR_213_DisplayWithStyle()` without altering stored image data, using the factory-calibrated 3-color full BWR OTP waveform (PSR `0x0F`, DRF `0x12`):

```
Stored Image Pixel (BW bit, Red bit)
           │
           ▼
┌────────────────────────────────────────────────────────────────────────┐
│  On-the-Fly Style Bitwise Mapping (src/epd/epd_bwr_213.c)              │
│                                                                        │
│  Style 0 (Standard BWR):   DTM1 = bw | red,       DTM2 = red           │
│  Style 1 (B&W Standard):   DTM1 = (~red) & bw,    DTM2 = 0x00          │
│  Style 2 (B&W Inverted):   DTM1 = ~((~red) & bw), DTM2 = 0x00          │
│  Style 3 (R&W Standard):   DTM1 = 0xFF,           DTM2 = ~((~red) & bw)│
│  Style 4 (R&W Inverted):   DTM1 = 0xFF,           DTM2 = (~red) & bw   │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
                                   ▼
             UC8151 EPD Controller (DTM1 = 0x10, DTM2 = 0x13)
```

| Style Index | Style Name | Visual Behavior | DTM1 (BW) Formula | DTM2 (Red) Formula | Waveform |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **0** | `Standard` | Follows stored artwork: Black $\to$ Black, Red $\to$ Red, White $\to$ White | `bw \| red` | `red` | Full BWR (~15s) |
| **1** | `B&W Standard` | Monochromatic: Black $\to$ Black, Red $\to$ Black, White $\to$ White | `(~red) & bw` | `0x00` | Full BWR (~15s) |
| **2** | `B&W Inverted` | Negative mono: Black $\to$ White, Red $\to$ White, White $\to$ Black | `~((~red) & bw)` | `0x00` | Full BWR (~15s) |
| **3** | `R&W Standard` | Red artwork: Black $\to$ Red, Red $\to$ Red, White $\to$ White | `0xFF` | `~((~red) & bw)` | Full BWR (~15s) |
| **4** | `R&W Inverted` | Inverted red: Black $\to$ White, Red $\to$ White, White $\to$ Red | `0xFF` | `(~red) & bw` | Full BWR (~15s) |
