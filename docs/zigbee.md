# Zigbee 3.0 Subsystem & ZCL OTA Reference

This document provides the complete engineering reference for the Zigbee 3.0 subsystem on the Hanshow Stellar-M3N@ / E31HA electronic shelf label (Telink TLSR8258). It covers the Sleepy End Device (SED) architecture, endpoint configuration, ZCL cluster attributes, autonomous reporting, and a detailed breakdown of the `.zigbee` OTA container format required by Zigbee2MQTT and Home Assistant.

---

## 1. Sleepy End Device (SED) Architecture

The ESL operates as a Zigbee 3.0 **Sleepy End Device (SED)** governed by the Zigbee PRO / Base Device Behavior (BDB) specification.

```
┌─────────────────────────────────────────────────────────────┐
│                    Zigbee Coordinator                       │
│             Zigbee2MQTT / Home Assistant / ZHA              │
└──────────────────────────────┬──────────────────────────────┘
                               │ IEEE 802.15.4 RF (Ch 11..26)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                 Zigbee Router (Parent Node)                 │
│  - Indirect Message Queue (Buffers packets for sleeping SED)│
│  - Parent-Child Supervision & Mac Ack Handshake             │
└──────────────────────────────▲──────────────────────────────┘
                               │
                       2.0 s MAC Data Poll
                               │
┌──────────────────────────────┴──────────────────────────────┐
│        Hanshow Stellar-M3N@ SED (Telink TLSR8258)           │
│  - RxOnWhenIdle = FALSE                                     │
│  - Deep Sleep with Retention: I_sleep ≈ 2.5 µA              │
│  - Periodic Wakeup: Every 2.0 s (20 ms wake window)         │
│  - NFC External Wakeup: Interrupt PC4 wakes on RF field     │
│  - Custom Cluster 0xFC01: Display & Mode Control            │
└─────────────────────────────────────────────────────────────┘
```

### 1.1 Power Management & Polling Mechanics

Because the tag is powered by a CR2450 coin cell ($3.0\,\text{V}$, $\sim 600\,\text{mAh}$), continuous radio reception is impossible:
- **`RxOnWhenIdle = FALSE`:** The RF transceiver is powered down into deep retention sleep between scheduled events.
- **Normal Polling Interval (`2.0 s`):** The MCU wakes up every 2,000 ms, turns on its receiver, and transmits an IEEE 802.15.4 MAC Data Request to its parent router. If the router has buffered ZCL commands (e.g. slot switch or style change), it responds with the payload. If the parent responds with an empty Ack (`Frame Pending = 0`), the tag immediately returns to sleep within $20\,\text{ms}$.
- **Fast Polling Mode (`200 ms`):** When actively commissioning, exchanging multi-packet ZCL messages, or handling an active NFC session, the poll rate temporarily increases to 200 ms to ensure responsive command processing.

---

## 2. Network Lifecycle: Commissioning, Connection Loss & Recovery

### 2.1 Network Commissioning & Joining Flow

When the device is uncommissioned (Factory New), triggered by an explicit network reset, or waking after leaving a network, it executes the Zigbee Base Device Behavior (BDB) network steering procedure:

```
┌──────────────┐                 ┌─────────────────┐                 ┌─────────────────┐
│   ESL Tag    │                 │  Parent Router  │                 │   Coordinator   │
│  (Telink ED) │                 │  (or ZC Parent) │                 │   (Z2M / ZHA)   │
└──────┬───────┘                 └────────┬────────┘                 └────────┬────────┘
       │                                  │                                   │
       │ 1. Active Channel Scan (11..26)  │                                   │
       │─────────────────────────────────>│                                   │
       │    Beacon (Permit Join = True)   │                                   │
       │<─────────────────────────────────│                                   │
       │                                  │                                   │
       │ 2. MAC Association Request       │                                   │
       │─────────────────────────────────>│                                   │
       │    MAC Association Response      │                                   │
       │<─────────────────────────────────│                                   │
       │    (Allocates Short Address)     │                                   │
       │                                  │                                   │
       │ 3. BDB Commissioning Success     │                                   │
       │    (Enters 60s Fast Poll Window) │                                   │
       │                                  │                                   │
       │ 4. ZDO Device Announce           │                                   │
       │─────────────────────────────────>│──────────────────────────────────>│
       │                                  │                                   │
       │ 5. Active Interview Window       │                                   │
       │    - Fast Poll (200 ms)          │   Descriptor & ZCL Queries        │
       │    - Deep retention sleep locked │<═════════════════════════════════>│
       │                                  │                                   │
       │ 6. Initial Telemetry Delay (5s)  │                                   │
       │    (Prevents ZHA DB race)        │   ZCL Reports (Battery & EPD)     │
       │─────────────────────────────────>│──────────────────────────────────>│
       │                                  │                                   │
       │ 7. Interview Window Closes (60s) │                                   │
       │    - Revert poll rate to 2000 ms │                                   │
       │    - Low-power SED sleep active  │                                   │
       ▼                                  ▼                                   ▼
```

1. **Trigger & Pairing Mode Entry:**
   - On cold boot when unjoined (`zb_isDeviceFactoryNew()`), after writing `"zb:reset"` via NFC, or upon receiving a network leave command, `zb_start_pairing()` is called.
   - The device activates a 180-second pairing window (`s_zb_pairing_active = true`), blinks the Green LED, and pulses the LED every 1 second.
   - It issues `bdb_networkSteerStart()` to perform active channel scans across primary channels (11–26). If scanning cycles without finding an open network, a backoff timer reschedules the next scan in 800 ms until the 180s pairing timeout expires.
2. **Association & BDB Commissioning Handshake:**
   - Once a router or coordinator with `PermitJoin == true` responds with an 802.15.4 beacon, the tag performs MAC association and receives its 16-bit short network address.
   - The stack signals `BDB_COMMISSION_STA_SUCCESS` to `zb_bdbCommissioningCb()`.
3. **60-Second Active Interview Window (Fast Polling):**
   - Immediately upon joining, `zb_start_interview_window()` accelerates the MAC polling rate to **200 ms** (`zb_setPollRate(200)`).
   - In `zb_pm_task()`, `zb_is_interviewing()` blocks the device from entering deep retention sleep while the coordinator discovers endpoints and queries node/simple descriptors and ZCL attributes.
4. **ZDO Device Announcement:**
   - The device transmits a broadcast `ZDO Device_annce` (`zb_zdoSendDevAnnance()`) to alert the coordinator (Zigbee2MQTT or ZHA) to update its routing table and begin the device interview.
5. **Stabilized Initial Telemetry (5-Second Guard Delay):**
   - Initial ZCL attribute reports (Power Configuration and Custom E-Paper clusters) are deferred by 5 seconds via `TL_ZB_TIMER_SCHEDULE(zb_initial_report_cb, NULL, 5000)`. This prevents RF contention during the time-critical descriptor queries and eliminates foreign-key database race conditions in ZHA/zigpy.
6. **Transition to Low-Power Operation:**
   - After the 60-second window completes, polling reverts to the steady-state 2.0-second interval, and `zb_pm_task()` engages duty-cycled deep retention sleep (`drv_pm_lowPowerEnter()`).

---

### 2.2 Connection Loss Detection Mechanics

A Sleepy End Device detects lost network connectivity through three independent paths:

1. **MAC Data Poll Failure (Consecutive ACK Loss):**
   - The tag periodically wakes every 2,000 ms and transmits an 802.15.4 Data Request to its parent.
   - If the parent fails to reply with an 802.15.4 MAC ACK within the CSMA-CA retry window (`macMaxFrameRetries = 3`), the poll attempt fails.
   - When consecutive poll failures reach the stack threshold (`NWK_MAX_POLL_FAILURE`, typically 3–4 failures), the network layer generates `ZDO_NETWORK_LOST (0x60)`.
   - The BDB layer notifies the application with `BDB_COMMISSION_STA_PARENT_LOST`.
   - The device flags itself unjoined (`zb_isDeviceJoinedNwk() == false`), while non-volatile network credentials remain stored in flash. NFC and screen diagnostics report `"Zigbee (lost)"` or `ZB-X`.
2. **Parent Child Table Eviction (End Device Timeout):**
   - Routers and coordinators track child nodes with an End Device Aging timer. If an end device does not poll within its negotiated timeout, the parent drops the child from its internal routing and neighbor tables.
   - Once evicted, subsequent Data Requests from the child are unacknowledged or rejected, leading to `PARENT_LOST`.
3. **Explicit Network Leave Command (`NLME-LEAVE.indication`):**
   - The coordinator may broadcast or unicast an `NLME-LEAVE` request (e.g. when removed from ZHA/Z2M, or if the initial pairing interview fails validation).

---

### 2.3 Parent Loss Recovery & Backoff Rejoin Flow

When `BDB_COMMISSION_STA_PARENT_LOST` occurs, the firmware initiates an autonomous reconnection routine designed to conserve coin-cell battery life:

```
┌────────────────────────────────────────────────────────────────────────┐
│            Parent Lost Detected (BDB_COMMISSION_STA_PARENT_LOST)       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│   Immediate Secure Rejoin Attempt (zb_rejoinSecModeSet / zb_rejoinReq) │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                   ┌────────────────┴────────────────┐
                   │ Success?                        │
                  Yes                               No
                   │                                 │
                   ▼                                 ▼
┌─────────────────────────────────────┐  ┌───────────────────────────────┐
│ • Cancel backoff timer              │  │ Activate Exponential Backoff  │
│ • Start 60s Fast Poll Window (200ms)│  │ (zb_rejoin_backoff_cb)        │
│ • Broadcast ZDO Device Announcement │  │ • Attempt 1..2:  15 s         │
│ • Delay initial report by 5s        │  │ • Attempt 3..4:  30 s         │
│ • Green LED confirm blink (200 ms)  │  │ • Attempt 5..6:  60 s         │
│ • Return to 2.0s SED sleep          │  │ • Attempt 7..9:   5 min       │
└─────────────────────────────────────┘  │ • Attempt 10..12: 15 min      │
                                         │ • Attempt 13..15:  1 hour     │
                                         └───────────────┬───────────────┘
                                                         │
                                        ┌────────────────┴───────────────┐
                                        │ Retries Exhausted (>15)?       │
                                       No                               Yes
                                        │                                │
                                        ▼                                ▼
┌──────────────────────────────────────────────┐ ┌───────────────────────┐
│ Deep Retention Sleep Between Retries (2.5 µA)│ │ Battery Saver Mode    │
│ • Armed for hardware timer wake              │ │ • Stop auto-retrying  │
│ • Armed for NFC field interrupt (PC4)        │ │ • 1-hour sleep chunks │
└───────────────────────┬──────────────────────┘ │ • Wait for NFC tap    │
                        │                        └───────────────┬───────┘
                        │                                        │
                        └───────────────►◄───────────────────────┘
                                         │
                               Phone NFC Tap Detected
                                         │
                                         ▼
┌────────────────────────────────────────────────────────────────────────┐
│ On-Demand Rejoin Triggered via NFC (zb_start_rejoin, 5s rate-limited)  │
└────────────────────────────────────────────────────────────────────────┘
```

#### Rejoin Schedule & Backoff Intervals
Retries use an escalating backoff schedule implemented in `zb_rejoin_backoff_cb()`:

| Attempt Range | Retry Interval | Security Mode | Sleep State |
| :--- | :--- | :--- | :--- |
| **Attempt 1 – 2** | 15 seconds | Secure (`REJOIN_SECURITY`) | Deep retention sleep with timer wakeup |
| **Attempt 3 – 4** | 30 seconds | Alternating Insecure / Secure | Deep retention sleep with timer wakeup |
| **Attempt 5 – 6** | 60 seconds (1 min) | Alternating | Deep retention sleep with timer wakeup |
| **Attempt 7 – 9** | 5 minutes | Alternating | Deep retention sleep with timer wakeup |
| **Attempt 10 – 12**| 15 minutes | Alternating | Deep retention sleep with timer wakeup |
| **Attempt 13 – 15**| 60 minutes (1 hr) | Alternating | Deep retention sleep with timer wakeup |
| **> 15 Attempts** | **Exhausted** | Auto-retry terminates | **Battery Saver Mode** ($2.5\,\mu\text{A}$ standby, 1 hr intervals) |

#### Sleep Mechanics During Loss State
Between rejoin attempts, `zb_pm_task()` recognizes that the node is unjoined:
- Steady-state 2.0 s polling is halted.
- The MCU enters deep sleep with retention (`PM_SLEEP_MODE_DEEP_WITH_RETENTION`, $I_{\text{sleep}} \approx 2.5\,\mu\text{A}$) for the duration of the nearest backoff timer via `drv_pm_longSleep()`.
- Both the hardware timer and the NFC field interrupt pin (`PC4` / `GPIO_NFC_IRQ`) remain active wake sources.

#### Field-Initiated Tap-to-Rejoin
If auto-retries are in a long backoff period or have halted in Battery Saver Mode, the user does not need to remove the battery. Simply tapping the tag with an NFC-capable smartphone generates an interrupt on `PC4`, wakes the MCU, and triggers `zb_start_rejoin()` immediately (rate-limited to once every 5 seconds).

---

### 2.4 Explicit Network Leave vs. Parent Loss Comparison

| Parameter | Parent Loss (`PARENT_LOST`) | Explicit Leave (`NLME-LEAVE`) |
| :--- | :--- | :--- |
| **Cause** | Missing MAC poll ACKs, router power loss, or child table drop | Coordinator sends explicit `Leave Request` (e.g. deleted from UI) |
| **Flash NVRAM** | Network parameters (PAN ID, keys, short address) **preserved** | Wiped cleanly to Factory New (`zb_resetDevice2FN()`) |
| **Device State** | `!zb_isDeviceJoinedNwk() && !zb_isDeviceFactoryNew()` | `zb_isDeviceFactoryNew() == true` |
| **NFC / EPD Status** | `"Zigbee (lost)"` / `ZB-X` | `"Zigbee (pairing)"` / `ZB-P` |
| **Immediate Action**| Secure Rejoin Request (`zb_rejoinReq()`) | BDB Network Steering (`bdb_networkSteerStart()`) |
| **Retry Strategy** | Exponential backoff (15s $\to$ 1hr), stops after 15 retries | 180s active scan window with LED pulsing |

---

## 3. Endpoints & ZCL Cluster Specification

The device exposes **Endpoint 1** (`APP_ENDPOINT_1 = 0x01`) configured with standard and manufacturer-specific clusters.

### 2.1 Cluster Overview (Endpoint 1)

| Cluster ID | Cluster Name | Type | Purpose |
| :--- | :--- | :--- | :--- |
| `0x0000` | **Basic** | In (Server) | Device identification, hardware revision, manufacturer name, power source. |
| `0x0001` | **Power Configuration** | In (Server) | Battery voltage ($100\,\text{mV}$ units) and remaining battery percentage ($0.5\%$ units). |
| `0x0003` | **Identify** | In (Server) | Remote identification via LED blink sequences. |
| `0x0006` | **On/Off** | In (Server) | Standard On/Off/Toggle commands mapped directly to hardware status LEDs. |
| `0x0019` | **OTA Upgrade** | Out (Client) | Over-the-air firmware image query, block streaming, and upgrade control. |
| `0xFC00` | **Custom E-Paper** | In (Server) | Display slot selection, render style, dual-stack mode switching, refresh count, LED control. |

---

### 2.2 Standard Cluster Attributes

#### Basic Cluster (`0x0000`)
- `0x0000` (ZCL Version): `0x03` (Zigbee 3.0)
- `0x0001` (Application Version): `0x01`
- `0x0002` (Stack Version): `0x02`
- `0x0003` (Hardware Version): `0x01`
- `0x0004` (Manufacturer Name): `"Hanshow"`
- `0x0005` (Model Identifier): `"Stellar-M3N@"`
- `0x0006` (Date Code): `"20260920"`
- `0x0007` (Power Source): `0x03` (Battery)

#### Power Configuration Cluster (`0x0001`)
- `0x0020` (Battery Voltage): 8-bit unsigned integer in units of $100\,\text{mV}$ (e.g. `30` represents $3.0\,\text{V}$).
- `0x0021` (Battery Percentage Remaining): 8-bit unsigned integer in units of $0.5\%$ ($0$ to $200$, where `200` represents $100\%$).

---

### 2.3 Manufacturer-Specific E-Paper Cluster (`0xFC00`)

The Custom E-Paper cluster is governed by manufacturer code **`0x1141`** (Telink Semiconductor):

| Attribute ID | Attribute Name | Type | Access | Values / Description |
| **`0x0000`** | `activeSlot` | `uint8` (`0x20`) | Read / Write / Report | Active display slot (`0`..`9` on M3Na, `0`..`5` on XL3Na):<br>&bull; `0`: Info Screen (live system diagnostics)<br>&bull; `1`..`8` (M3Na) / `1`..`4` (XL3Na): User Uploaded Compressed BWR Images<br>&bull; `9` (M3Na) / `5` (XL3Na): Blank Screen (all white) |
| **`0x0001`** | `renderStyle` | `uint8` (`0x20`) | Read / Write / Report | Hardware rendering style (`0`..`4`):<br>&bull; `0`: Standard (Tri-Color BWR)<br>&bull; `1`: B&W Standard<br>&bull; `2`: B&W Inverted<br>&bull; `3`: Red & White Standard<br>&bull; `4`: Red & White Inverted |
| **`0x0002`** | `activeMode` | `uint8` (`0x20`) | Read / Write / Report | Stack operating mode:<br>&bull; `1`: Zigbee 3.0 Mode<br>&bull; `2`: BLE 5.0 Mode (firmware sends ZCL Write Attributes Response first, then delays 350 ms to switch and reboot) |
| **`0x0003`** | `refreshCount`| `uint16` (`0x21`)| Read / Report | Total lifetime screen refreshes recorded in wear-leveled flash. |
| **`0x0004`** | `ledState`    | `uint8` (`0x20`) | Read / Write / Report | 3-Color LED states: Bit 0 = Blue (`PA7`), Bit 1 = Green (`PD3`), Bit 2 = Red (`PD2`). |

#### Attribute Write Processing (`zcl_appCb.c`)
When a coordinator issues a ZCL `Write Attributes` request:
1. The values are checked against valid bounds (`slot < EPD_SLOT_COUNT`, `style < STYLE_COUNT`, `mode == 1 || mode == 2`).
2. If `activeMode` is written (e.g. switching to BLE mode `2`), `zb_schedule_mode_switch(mode)` defers the hardware switch. The ZCL stack immediately transmits the `ZCL Write Attributes Response` (Status = SUCCESS) back to Zigbee2MQTT. Sleep is inhibited while pending, and after 350 ms, the device writes the target mode and Slot 0 (Info) to flash EEPROM and reboots directly into BLE mode (where the display is refreshed to Slot 0 automatically on boot).
3. If slot or style is modified, `flash_eep_save()` writes the new settings to the wear-leveled flash sector without erasing.
4. `epd_display_slot(active_slot, render_style)` executes the screen refresh (Red LED rhythmically blinks at 500 ms during refresh).
5. If `ledState` is written, `led_set(mask)` updates hardware LEDs immediately without screen refresh.
6. Updated attributes are reported back to the network via `zb_epaper_report_attrs()`.
7. NFC telemetry is refreshed so smartphone taps reflect the newly commanded state.

---

## 4. ZCL Over-The-Air (OTA) Container Format (`.zigbee`)

### 4.1 Why Zigbee OTA Requires the 56-Byte Container

A common source of confusion in embedded development is why Zigbee OTA cannot directly accept a raw binary file (`.bin`), whereas BLE OTA can:

1. **Autonomous Protocol Handshake:** In Zigbee, the client (the ESL tag) autonomously queries the OTA server (e.g. Zigbee2MQTT) using `Query Next Image Request`. The query contains the device's Manufacturer Code (`0x1141`), Image Type (`0x0231`), and current Firmware Version (`0x10003001`).
2. **Server-Side Validation:** Zigbee2MQTT and ZHA parse the uploaded firmware file to verify that the container's Manufacturer Code and Image Type strictly match the querying device. If given a raw `.bin`, the OTA server has no way to determine which devices on the mesh the binary belongs to.
3. **Firmware Version Monotonicity:** The client compares the 32-bit File Version in the ZCL header against its own version. If the server offers a version $\le$ current version, the client rejects the update to prevent accidental downgrades.
4. **Multi-Element Packaging:** The Zigbee OTA specification allows packaging multiple elements (e.g. bootloader + application + security certificates) within a single container. The 56-byte header specifies the total length and sub-element tags.

---

### 4.2 Structure of the 56-Byte ZCL OTA Header

Every `.zigbee` file produced by the build pipeline begins with a 56-byte header defined by Section 11 of the Zigbee Cluster Library:

```
 0               4               8              12              16
┌───────────────┬───────────────┬───────────────┬───────────────┐
│ File ID (4 B) │ Header Ver(2B)│ Header Len(2B)│ Field Ctrl(2B)│
│  0x5A434C01   │    0x0100     │  0x0038 (56)  │    0x0000     │
├───────────────┼───────────────┼───────────────┼───────────────┤
│ Mfg Code (2 B)│Image Type(2 B)│     File Version (4 B)        │
│    0x1141     │    0x0231     │        0x10003001             │
├───────────────┴───────────────┼───────────────────────────────┤
│    Stack Version (2 B)        │    OTA Header String (32 B)   │
│          0x0002               │   "Hanshow:E31HA-dual"        │
├───────────────────────────────┴───────────────────────────────┤
│     Total Image Size (4 B) = 56 + Binary Payload Size         │
└───────────────────────────────────────────────────────────────┘
```

| Offset | Size | Field | Value | Meaning |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | 4 bytes | **File Identifier** | `0x5A434C01` (`\x01\x4C\x43\x5A`) | Standard Zigbee OTA file magic number. |
| `0x04` | 2 bytes | **Header Version** | `0x0100` | ZCL OTA Header specification version 1.0. |
| `0x06` | 2 bytes | **Header Length** | `0x0038` (56 bytes) | Size of this header preceding the firmware payload. |
| `0x08` | 2 bytes | **Field Control** | `0x0000` | No optional security or hardware version fields present. |
| `0x0A` | 2 bytes | **Manufacturer Code** | `0x1141` | Telink Semiconductor vendor identifier. |
| `0x0C` | 2 bytes | **Image Type** | `0x0231` / `0x0242` | `0x0231`: Hanshow Stellar-M3N@ / E31HA dual-stack.<br>`0x0242`: Hanshow Stellar-XL3N@ / E31PA dual-stack. |
| `0x0E` | 4 bytes | **File Version** | `0x10003001` | 32-bit build version matching `FILE_VERSION` (`v1.0.00`). |
| `0x12` | 2 bytes | **Stack Version** | `0x0002` | Zigbee PRO stack revision. |
| `0x14` | 32 bytes| **Header String** | ASCII string (padded with `0x00`) | Human-readable tag (`"Hanshow:E31HA-dual"` or `"Hanshow:E31PA-dual"`). |
| `0x34` | 4 bytes | **Total Image Size** | 32-bit uint | Exact file size in bytes ($56 + \text{payload bytes}$). |

---

### 4.3 Contrast: Zigbee OTA vs BLE OTA Flashing

| Feature | Zigbee OTA (`.zigbee`) | BLE OTA (`.bin`) |
| :--- | :--- | :--- |
| **Target File Format** | 56-byte ZCL Header + adjusted binary | Raw adjusted binary only |
| **Transfer Protocol** | Zigbee ZCL OTA Cluster (`0x0019`) | Telink Custom OTA GATT Service (`0x2B12`) |
| **Initiator** | Client tag autonomously queries server | User initiates flashing in Web UI |
| **Delivery Medium** | 802.15.4 multi-hop mesh network | Direct BLE point-to-point connection |
| **Pacing / Flow Control** | Block Request / Block Response ($32..64\,\text{B}$ per packet) | High-speed RAM caching ($16\,\text{KB}$ chunks, $16\,\text{B}$ packets) |
| **Flash Target** | Staged in Bank 1 (`0x40000` &ndash; `0x6FFFF`) | Staged in Bank 1 (`0x40000` &ndash; `0x6FFFF`) |
| **Execution** | Signature invalidated $\to$ Multi-Address reboot | Signature invalidated $\to$ Multi-Address reboot |

---

### 4.4 Zigbee2MQTT Local OTA Configuration

Because `TLSR-M3Na-E31HA` is a custom firmware, official online repositories (such as `Koenkk/zigbee-OTA`) do not host its firmware images. Querying OTA without an index override causes the error:
```
Update of '0x...' failed (No image currently available)
```

To enable OTA updates via Zigbee2MQTT:

1. **Copy firmware and index into Zigbee2MQTT data folder**:
   ```bash
   cp bin/1141-*-tlsr-epaper-*.zigbee <z2m_data_dir>/
   cp zigbee2mqtt/index.json <z2m_data_dir>/
   ```

2. **Configure Zigbee2MQTT override index** in `configuration.yaml`:
   ```yaml
   ota:
     zigbee_ota_override_index_location: index.json
   ```

3. **Restart Zigbee2MQTT**.
4. In the Zigbee2MQTT UI, navigate to the device's **OTA** tab and click **Check for new updates** / **Update firmware**.

