# Power Consumption Analysis & Engineering Guide
**Hardware Platforms:**
- **Hanshow Stellar-M3N@ / E31HA (`BOARD_HANSHOW_E31HA`, `tlsr-epaper-m3na`):** Telink TLSR8258 SoC, Fudan Micro FM11NC08 NFC, 2.13" BWR E-Paper Display (250×122, UC8151 controller)
- **Hanshow Nebular / Stellar-XL3N (`BOARD_HANSHOW_E31PA`, `tlsr-epaper-xl3na`):** Telink TLSR8258 SoC, Fudan Micro FM11NC08 NFC, 4.2" BWR E-Paper Display (400×300, UC8176 controller)

**Firmware:** `tlsr-epaper-dual-stack` (Zigbee 3.0 Sleepy End Device / BLE BTHome V2 Dual Stack)  
**Nominal Polling / Advertising Interval:** 2.0 seconds (`zb_setPollRate(2000)` / 2000 ms BLE advertising interval)  
**Methodology:** All timing, active duration, and radio duty cycles are derived from empirical real-device hardware telemetry ([`src/power_tracker.c`](../src/power_tracker.c)) captured on production ESL tags, combined with full source-level power audits of the SDK and application layers.

---

## 1. Executive Summary

The `tlsr-epaper-dual-stack` firmware operates as either an ultra-low-power **Bluetooth Low Energy (BLE) BTHome V2 beacon** or a **Zigbee 3.0 Sleepy End Device (SED)**. In both operating modes, the system relies on Telink TLSR8258 deep retention sleep (`DEEPSLEEP_MODE_RET_SRAM_LOW32K`) to minimize quiescent power consumption between active 2.0-second communication bursts.

```
+---------------------------------------------------------------------------------------------------+
|                                  Steady-State Current Comparison                                  |
|                                                                                                   |
| BLE Beacon (2.0s):                [ 13.9 µA ]  =================>  4.10 Years (CR2450)            |
|                                                                                                   |
| Zigbee SED (2.0s):                [ 18.6 µA ]  =======================>  3.06 Years (CR2450)      |
|                                                                                                   |
| Sleep Floor (Joined / Backoff):   [  4.0 µA ]  =======> (32KB Retention SRAM + NFC Standby)       |
+---------------------------------------------------------------------------------------------------+
```

### Key Performance Metrics

1. **Sleep Floor Current ($I_{\text{sleep}}$):**  
   In deep retention sleep (`DEEPSLEEP_MODE_RET_SRAM_LOW32K`), total system quiescent draw is **~4.0 µA** (1.8 µA TLSR8258 32KB Retention SRAM + 32K RC timer + 1.8 µA FM11NC08 passive NFC standby + 0.4 µA PCB/decoupling leakage).
2. **BLE BTHome V2 Beacon Mode (2.0s Interval):**
   - **Active Wake Duration ($t_{\text{wake}}$):** **2.53 ms** per 2.0s advertising cycle (duty cycle: **0.13%**).
   - **Radio Transmit ($t_{\text{TX}}$):** **0.83 ms** across channels 37, 38, and 39 at 0 dBm output power.
   - **Radio Receive ($t_{\text{RX}}$):** **0.38 ms** (380 µs) for scan and connection request listen windows.
   - **MCU Overhead ($t_{\text{MCU}}$):** **1.32 ms** for crystal stabilization, vector restore, and cached BTHome payload preparation.
   - **Average Steady-State Current ($I_{\text{avg}}$):** **~13.94 µA** at 2.0s advertising interval.
   - **Projected Battery Life (CR2450, 500 mAh usable):** **4.10 years (1,495 days)** with 0 refreshes/day; **3.72 years (1,357 days)** with 1 refresh/day.
   - **Projected Battery Life (CR2477, 850 mAh usable):** **6.96 years (2,541 days)** with 0 refreshes/day; **6.32 years (2,307 days)** with 1 refresh/day.
3. **Zigbee 3.0 Sleepy End Device Mode (2.0s Indirect Poll):**
   - **Active Wake Duration ($t_{\text{wake}}$):** **~4.12 ms** per 2.0s poll cycle (duty cycle: **0.21%**).
   - **Radio Transmit ($t_{\text{TX}}$):** **~0.54 ms** for the IEEE 802.15.4 MAC Data Request frame + PA ramp at 0 dBm.
   - **Radio Receive ($t_{\text{RX}}$):** **1.00 ms** (CCA clear-channel assessment + parent MAC ACK reception).
   - **MCU Overhead ($t_{\text{MCU}}$):** **~2.58 ms** for wakeup, `ev_timer` evaluation, CSMA/CA backoff with radio off (`RF_STATE_OFF`), and retention entry.
   - **Average Steady-State Current ($I_{\text{avg}}$):** **~18.64 µA** at 2.0s indirect poll rate.
   - **Projected Battery Life (CR2450, 500 mAh usable):** **3.06 years (1,118 days)** with 0 refreshes/day; **2.85 years (1,039 days)** with 1 refresh/day.
   - **Projected Battery Life (CR2477, 850 mAh usable):** **5.21 years (1,900 days)** with 0 refreshes/day; **4.84 years (1,766 days)** with 1 refresh/day.
4. **E-Paper Display Refresh Cycle (2.13" UC8151 & 4.2" UC8176 BWR):**
   - Refresh duration is **~15.15 seconds** using full BWR OTP waveform.
   - During active refresh, **both BLE mode and Zigbee mode** sleep in **`SUSPEND_MODE`** (`PM_SLEEP_MODE_SUSPEND`) between radio events so digital GPIO output latches (`GPIO_EPD_PWR_ENABLE`, `GPIO_EPD_RESET`, `GPIO_EPD_CS`, `GPIO_LED_RED`) stay continuously driven without glitching the panel's high-voltage charge pumps, and automatically resume `DEEPSLEEP_MODE_RET_SRAM_LOW32K` as soon as `GPIO_EPD_BUSY` (`PA1`) goes high to signal completion.
   - Energy consumption per full screen update is **~122 mA·s (~0.034 mAh)**.

---

## 2. Hardware Architecture & Electrical Baseline

Both supported Hanshow ESL boards integrate four electrical subsystems powered directly by a 3.0V Lithium coin cell (or pack):

```
        +-------------------------------------------------------------+
        |           3.0V Lithium Primary Cell (CR2450 / CR2477)       |
        +-------+---------------------+-----------------------+-------+
                |                     |                       |
                v                     v                       v
       +-----------------+   +-----------------+   +--------------------+
       |  Telink TLSR8258|   | Fudan FM11NC08  |   |  P-MOSFET Power    |
       |  SoC (MCU & RF) |   | NFC Transceiver |   |  Gate (PC5 / PB7)  |
       +--------+--------+   +--------+--------+   +----------+---------+
                |                     |                       |
                | I2C (PC0, PC1)      | IRQ (PC4)             | VDD_EPD
                |                     |                       v
                |                     |            +--------------------+
                |                     |            | BWR E-Paper Panel  |
                +---------------------+----------->| (UC8151 / UC8176)  |
                          SPI (PB4, PB5, PB6, PD7) +--------------------+
```

### Board-Specific Power-Gate & Pin Configurations

| Signal / Function | `BOARD_HANSHOW_E31HA` (2.13" M3Na) | `BOARD_HANSHOW_E31PA` (4.2" XL3Na) | Sleep Pull Configuration |
| :--- | :--- | :--- | :--- |
| **`GPIO_EPD_PWR_ENABLE`** | `GPIO_PC5` (Active Low P-MOSFET) | `GPIO_PB7` (Active Low P-MOSFET, external pull-up) | `E31HA`: `PULLUP_1M` (Off) / `PULLDOWN_100K` (On)<br>`E31PA`: `PULLUP_1M` (Off) / `FLOAT` (On, avoids fighting ext pull-up) |
| **`GPIO_EPD_BUSY`** | `GPIO_PA1` (LOW = Busy, HIGH = Ready) | `GPIO_PA1` (LOW = Busy, HIGH = Ready) | `PULLDOWN_100K` when isolated; `PULLUP_1M` (`E31HA`) or `FLOAT` (`E31PA`) when active |
| **`GPIO_EPD_CS` / `DC` / `RST`** | `PB6` / `PD7` / `PB5` | `PB6` / `PD7` / `PB5` | `PULLDOWN_100K` when isolated (`EPD_isolate_pins()`) |
| **`GPIO_EPD_CLK` / `MOSI`** | `PB4` / `PC2` (`EPD_SHD`) | `PB4` / `PB7` (`EPD_SHD` on `PC5`) | `PULLDOWN_100K` when isolated |
| **`GPIO_NFC_IRQ`** | `GPIO_PC4` (Active Low) | `GPIO_PC4` (Active Low) | `PULLUP_1M` + `PM_WAKEUP_PAD` (`Level_Low`) |
| **`GPIO_NFC_SCL` / `SDA`** | `GPIO_PC1` / `GPIO_PC0` | `GPIO_PC1` / `GPIO_PC0` | `PULLUP_1M` (Idle I2C bus high, 0 µA static draw) |
| **`GPIO_VBAT` (ADC Reference)** | `GPIO_PB0` (`B0P` SAR input) | `GPIO_PB0` (`B0P` SAR input) | Output disabled (`0`) after ADC sample to eliminate divider leakage |
| **LEDs (Active Low)** | Red `PD2`, Green `PD3`, Blue `PA7` | Red `PD2`, Green `PD3`, Blue `PA7` | `PULLUP_1M` when off |

### Subsystem Current Characteristics (VDD = 3.0V, 25°C)

| Component | Operating State | Typical Current | Role in System |
| :--- | :--- | :--- | :--- |
| **TLSR8258** | Deep Retention Sleep (`DEEPSLEEP_MODE_RET_SRAM_LOW32K`) | **1.8 µA** | Retention LDO active, 32KB SRAM retained, 32K RC timer running |
| **TLSR8258** | Suspend Sleep (`SUSPEND_MODE`) | **~35 µA** | Digital GPIO output latches retained during EPD refresh |
| **TLSR8258** | Active CPU (24 MHz CCLK) | **3.8 mA** | Executing code from retention RAM / flash cache |
| **TLSR8258** | Radio Receiver (RX) | **13.0 mA** | Listening for MAC ACK, CCA, or BLE scan/conn requests |
| **TLSR8258** | Radio Transmit (TX @ 0 dBm) | **12.0 mA** | Transmitting IEEE 802.15.4 MAC poll or BLE adv frames |
| **FM11NC08** | Standby / Idle Mode | **1.8 µA** | VDD powered, passive RF field detector armed |
| **FM11NC08** | Active I2C Communication | **150 µA** | EEPROM / FIFO read-write while NFC field is present |
| **FM11NC08** | Contactless RF Carrier | **0.0 µA (from VDD)**| RF front-end powered by external NFC reader field |
| **EPD Panel** | Powered Down (`PWR_ENABLE = HIGH`) | **< 0.05 µA** | VDD rail isolated via high-side P-MOSFET switch |
| **EPD Panel** | Active OTP Refresh (UC8151 / UC8176) | **8.0 mA** | High-voltage charge pumps active (~15.15 s duration) |
| **LEDs** | Green (`PD3`) / Blue (`PA7`) / Red (`PD2`) | **2.5 mA** | Active-low status indicators |

---

## 3. Current Operating Profiles & Power Modeling

### Profile 1: BLE BTHome V2 Beacon Mode (2.0s Interval)

In BLE mode, the device wakes every 2000 ms nominal (empirical period $T = 2005.0\text{ ms}$), updates its BTHome V2 telemetry payload every even advertisement using cached battery voltage ([`get_battery_mv()`](../src/battery.c)) and cached EPD temperature ([`epd_read_temp()`](../src/epd/epd.c)), and transmits connectable undirected advertising frames across channels 37, 38, and 39.

```
Cycle Timeline (2005.0 ms Total):
+------------------------------------+---------------------------------------------------+
| Wake Phase (2.53 ms, avg 7.87 mA)  | Deep Retention Sleep (~2002.5 ms, 4.0 µA)         |
+------------------------------------+---------------------------------------------------+
  |- Crystal & MCU Overhead: 1.32 ms (3.8 mA)
  |- Radio TX (ch 37,38,39): 0.83 ms (12.0 mA @ 0 dBm)
  |- Radio RX (Listen): 0.38 ms (13.0 mA)
```

#### Energy & Charge Breakdown per 2.0s Beacon Burst

1. **Active Wake Phase ($t_{\text{wake}} = 2.53\text{ ms}$):**
   - **Radio TX (0 dBm @ 12.0 mA):** $0.83\text{ ms} \times 12.0\text{ mA} = 9.98\text{ µC}$
   - **Radio RX (13.0 mA):** $0.38\text{ ms} \times 13.0\text{ mA} = 4.93\text{ µC}$
   - **MCU Core & Housekeeping (3.8 mA):** $1.32\text{ ms} \times 3.8\text{ mA} = 5.02\text{ µC}$
   $$Q_{\text{wake}} = 9.98\text{ µC} + 4.93\text{ µC} + 5.02\text{ µC} = \mathbf{19.93\text{ µC}} \quad (\bar{I}_{\text{wake}} \approx 7.87\text{ mA})$$

2. **Sleep Phase ($t_{\text{sleep}} = 2002.5\text{ ms} = 2.0025\text{ s}$):**
   $$Q_{\text{sleep}} = 2.0025\text{ s} \times 4.0\text{ µA} = \mathbf{8.01\text{ µC}}$$

3. **Total Cycle Energy & Average Current:**
   $$Q_{\text{total}} = Q_{\text{wake}} + Q_{\text{sleep}} = 19.93\text{ µC} + 8.01\text{ µC} = \mathbf{27.94\text{ µC}}$$
   $$I_{\text{avg}} = \frac{27.94\text{ µC}}{2.0050\text{ s}} = \mathbf{13.94\text{ µA}}$$

---

### Profile 2: Zigbee 3.0 Sleepy End Device Mode (2.0s Indirect Poll)

In Zigbee SED mode, the device wakes every 2000 ms nominal (empirical period $T = 2007.4\text{ ms}$) to query its parent router/coordinator for pending frames via an indirect IEEE 802.15.4 MAC Data Request.

#### Steady-State Polling Cycle (~4.12 ms Wake, 18.64 µA Average)

The transceiver remains in `RF_STATE_OFF` during [`rf_reset()`](../SDK/stack/zigbee/mac/mac_phy.c) (`mac_phyReconfig()`), ensuring the 2.4 GHz radio stays powered down during crystal stabilization, `ev_timer` processing, and the CSMA/CA random backoff delay, turning on only for `rf_performCCA()` and the MAC Data Request transmission:

```
Cycle Timeline (2007.4 ms Total):
+------------------------------------+---------------------------------------------------+
| Wake Phase (4.12 ms, avg 7.11 mA)  | Deep Retention Sleep (~2003.3 ms, 4.0 µA)         |
+------------------------------------+---------------------------------------------------+
  |- Crystal, Stack Wake & CSMA Backoff (RF OFF): 2.58 ms (3.8 mA)
  |- MAC Data Request TX: 0.54 ms (12.0 mA @ 0 dBm)
  |- CCA + Coordinator MAC ACK RX: 1.00 ms (13.0 mA)
```

1. **Active Wake Phase ($t_{\text{wake}} \approx 4.12\text{ ms}$):**
   - **Radio TX (0 dBm @ 12.0 mA):** $0.54\text{ ms} \times 12.0\text{ mA} = 6.53\text{ µC}$
   - **Radio RX (CCA + ACK @ 13.0 mA):** $1.00\text{ ms} \times 13.0\text{ mA} = 13.00\text{ µC}$
   - **MCU Core & CSMA Backoff with Radio Off (3.8 mA):** $2.58\text{ ms} \times 3.8\text{ mA} = 9.78\text{ µC}$
   $$Q_{\text{wake}} = 6.53\text{ µC} + 13.00\text{ µC} + 9.78\text{ µC} = \mathbf{29.31\text{ µC}} \quad (\bar{I}_{\text{wake}} \approx 7.11\text{ mA})$$

2. **Sleep Phase ($t_{\text{sleep}} = 2003.3\text{ ms} = 2.0033\text{ s}$):**
   $$Q_{\text{sleep}} = 2.0033\text{ s} \times 4.0\text{ µA} = \mathbf{8.01\text{ µC}}$$

3. **Hourly Telemetry Heartbeat Amortization:**
   - Once per hour (`1800` poll cycles, or immediately if battery changes by $\ge 100\text{ mV}$ / $\ge 5\%$), [`zb_pm_task()`](../src/zigbee/zb_app.c) transmits two ZCL Report Attributes frames (`ZCL_CLUSTER_GEN_POWER_CFG` and `ZCL_CLUSTER_CUSTOM_EPAPER`), adding ~45 µC per hour ($\approx 0.01\text{ µA}$) plus one forced ADC conversion every 10 minutes ($\approx 0.01\text{ µA}$).
   - Periodic ZCL OTA query timer (`15 min`) adds ~30 µC per 900 s ($\approx 0.03\text{ µA}$).
   - Total amortized background overhead: **~0.05 µA** (~0.10 µC per 2.0s cycle).

4. **Total Cycle Energy & Average Current:**
   $$Q_{\text{total}} = 29.31\text{ µC} + 8.01\text{ µC} + 0.10\text{ µC} \approx \mathbf{37.42\text{ µC}}$$
   $$I_{\text{avg}} = \frac{37.42\text{ µC}}{2.0074\text{ s}} \approx \mathbf{18.64\text{ µA}}$$

*(Note: During debug profiling, enabling serial UART output on every wake event adds ~3.30 ms of CPU execution per cycle ($38\text{ bytes} \times 87\text{ µs/byte}$ at 115200 baud), resulting in a measured wake duration of ~7.42 ms and an average draw of ~24.84 µA. Suppressing per-wake logs preserves the nominal 4.12 ms / 18.64 µA baseline).*

---

### Profile 3: E-Paper Display Refresh (2.13" UC8151 & 4.2" UC8176 BWR)

Screen updates drive the internal high-voltage electrophoretic charge pumps through the panel's OTP waveform. Real device measurements show a completion time of **15.15 seconds (15,150 ms)**.

- **Panel Active Current:** ~8.0 mA during particle agitation phases.
- **Internal Temperature Piggybacking:** While the panel is already powered on for refresh, [`epd_display_slot()`](../src/epd/epd.c) (2.13" UC8151) and [`EPD_BWR_420_init()`](../src/epd/epd_bwr_420.c) (4.2" UC8176) read command `0x40` and update `epd_temperature` in retention SRAM, so periodic BLE BTHome beacons never need to power on the panel between refreshes.
- **MCU Management (`SUSPEND_MODE` in Both BLE and Zigbee):** The MCU streams compressed slot planes line-by-line from SPI flash (~150 ms @ ~6.0 mA), triggers display refresh (`0x12`), and sleeps in `SUSPEND_MODE` (~35 µA) between 2.0s BLE/Zigbee events with `GPIO_EPD_BUSY` (`Level_High`) and `GPIO_NFC_IRQ` (`Level_Low`) pad wakeups enabled. As soon as `GPIO_EPD_BUSY` goes high, [`epd_state_handler()`](../src/epd/epd.c) powers down the panel (`POF` + `DSLP` + `EPD_POWER_OFF()` + `EPD_isolate_pins()`) and returns the MCU to `DEEPSLEEP_MODE_RET_SRAM_LOW32K`.
- **Total Energy per Refresh:**
  $$E_{\text{refresh}} = (8.0\text{ mA} \times 15.15\text{ s}) + (6.0\text{ mA} \times 0.15\text{ s}) = 122.1\text{ mA}\cdot\text{s} \approx \mathbf{0.0339\text{ mAh}}$$
- **Equivalent Continuous Current (1 refresh/day):**
  $$I_{\text{EPD\_1/day}} = \frac{0.0339\text{ mAh}}{24\text{ h}} \approx \mathbf{1.41\text{ µA}}$$

---

### Profile 4: Zigbee Connection Loss & Exponential Rejoin Backoff

When the coordinator or parent router becomes unreachable (either after **3 consecutive unacknowledged MAC Data Polls** triggering `BDB_COMMISSION_STA_PARENT_LOST`, or on cold boot when `zb_bdbInitCb()` fails to contact the parent), the firmware executes a power-optimized recovery state machine in [`src/zigbee/zb_appCb.c`](../src/zigbee/zb_appCb.c) and [`src/zigbee/zb_app.c`](../src/zigbee/zb_app.c):

1. **Immediate Background Timer Shutdown:**
   - Upon `BDB_COMMISSION_STA_PARENT_LOST`, network leave (`zb_on_network_leave()`), or cold-boot rejoin failure, the firmware immediately stops the periodic ZCL OTA timer (`ev_unon_timer(&otaTimer)`) and ZCL attribute reporting timers (`reportAttrTimerStop()`). This ensures an unjoined device never wakes up every 15 minutes to attempt impossible OTA queries.
2. **Single-Channel vs. Full-Channel Scan Optimization (`zb_get_rejoin_channel_mask()`):**
   - A full 16-channel IEEE 802.15.4 rejoin scan (`0x07FFF800`, channels 11–26) dwells ~138 ms per channel = **~2,212 ms** in RX (~28.8 mC per scan).
   - Because coordinators rarely change channels during a brief outage, the initial `PARENT_LOST` rejoin and **2 out of every 3** backoff attempts (`(s_rejoin_attempts % 3) != 0`) scan **only the last known operating channel** (`1UL << ch`, ~138 ms active RX = **~1.8 mC**, a **16× energy reduction**).
   - Every 3rd attempt (attempts 3, 6, 9, 12, 15) scans the full 16-channel mask (`zb_apsChannelMaskGet()`) in case the coordinator migrated to a new channel.
3. **6-Tier Exponential Backoff Schedule (85 Attempts Total, ~3-Day Window):**
   - Between rejoin attempts, [`zb_pm_task()`](../src/zigbee/zb_app.c) puts the MCU into `DEEPSLEEP_MODE_RET_SRAM_LOW32K` (**4.0 µA**) for the exact duration of `s_rejoin_timer_evt`:

| Backoff Tier | Attempt Numbers | Sleep Interval Between Attempts | Channel Scan Mode | Cumulative Time Elapsed |
| :--- | :--- | :--- | :--- | :--- |
| **Immediate** | Initial (`PARENT_LOST`) | 0 s (immediate) | Single channel (known `ch`) | 0 s |
| **Tier 1** | Attempts 1 – 2 | **15 seconds** | Single-ch | 30 s |
| **Tier 2** | Attempts 3 – 4 | **30 seconds** | Att 3: Full 16-ch; Att 4: Single-ch | 1m 30s |
| **Tier 3** | Attempts 5 – 6 | **60 seconds (1 min)** | Att 5: Single-ch; Att 6: Full 16-ch | 3m 30s |
| **Tier 4** | Attempts 7 – 9 | **300 seconds (5 min)** | Att 7–8: Single-ch; Att 9: Full 16-ch | 18m 30s |
| **Tier 5** | Attempts 10 – 12 | **900 seconds (15 min)** | Att 10–11: Single-ch; Att 12: Full 16-ch | 1h 03m 30s |
| **Tier 6** | Attempts 13 – 85 | **3600 seconds (1 hour)** | 2 Single-ch : 1 Full 16-ch alternating | ~74 hours (~3.08 days) |
| **Battery Saver** | Exhausted (> 85) | **Indefinite Deep Sleep** | Radio Off (wakes on NFC tap) | Indefinite (**4.0 µA**) |

4. **Routine Rejoin vs. Initial Pairing Fast-Poll Differentiation:**
   - When `BDB_COMMISSION_STA_SUCCESS` fires after a routine rejoin (`was_pairing == false`), [`zb_bdbCommissioningCb()`](../src/zigbee/zb_appCb.c) immediately restores `zb_setPollRate(2000)` and resumes normal 2.0s deep retention sleep without triggering a 60-second fast-poll interview window or LED blink.
   - Only initial network steering (`was_pairing == true`) starts the 60-second interview window (`s_interview_timer_evt` at 200 ms poll rate), and even during that 60-second interview window, [`zb_pm_task()`](../src/zigbee/zb_app.c) enters `DEEPSLEEP_MODE_RET_SRAM_LOW32K` between 200 ms polls.

---

### Profile 5: Special & Transient Operating Modes

- **Zigbee Initial Pairing / Steering Mode:** Triggered via NFC tap (`zb_start_pairing()`). Scans 16 channels with periodic green LED pulses (`40 ms` blink every 1s) for up to 180 seconds (**17.5 mA to 19.2 mA** while actively scanning). If no open coordinator is found, enters 1-hour battery-saver deep retention sleep (**4.0 µA**).
- **BLE Connected Mode (Idle vs. Fast Transfer):**
  - Default connected state uses a **50 ms connection interval with slave latency = 19** (`DEF_CON_PAR_UPDATE`), waking once every **1000 ms** and sleeping in `DEEPSLEEP_MODE_RET_SRAM_LOW32K` between anchor points (**~25 µA** average).
  - During slot upload (`0x14`), slot fetch (`0x16`), or prototype upload (`0x40`), slave latency is temporarily set to `0` (10–20 ms interval) for high-speed throughput, and automatically restored to latency `19` (50 ms interval) upon commit (`0x15` / `0x41`), EOF, disconnect (`ble_remote_terminate()`), or after a **30-second inactivity timeout** in [`ble_task()`](../src/ble/ble_app.c).
- **Passive NFC Contactless Tap:** The RF carrier field from the smartphone/reader powers the FM11NC08 RF front-end (**0.0 µA from VDD**). Active I2C command processing wakes the MCU via `GPIO_NFC_IRQ` (`PC4`, active low) and draws ~4.5 mA for 150–300 ms ($\sim 0.00035\text{ mAh}$ per tap).

---

## 4. Low-Power Architecture & Sleep Management

The firmware uses Telink TLSR8258's deep retention sleep architecture (`DEEPSLEEP_MODE_RET_SRAM_LOW32K`), which maintains the lower 32KB of SRAM (`0x840000`–`0x847FFF`) while turning off the core digital power domain, high-speed clocks, and RF transceiver.

```
       Power Domain Partitioning in Retention Sleep
       +---------------------------------------------+
       | ALWAYS ON:                                  |
       |  - Low-Power Retention LDO                  |
       |  - 32KB Retention SRAM (0x840000..0x847FFF) |
       |  - 32K RC Oscillator / Hardware Sleep Timer |
       |  - Analog GPIO Pulls & Pad Wakeup Logic     |
       +---------------------------------------------+
       | SWITCHED OFF DURING SLEEP:                  |
       |  - 24 MHz Crystal Oscillator                |
       |  - 24/48 MHz Core MCU Pipeline              |
       |  - IEEE 802.15.4 / BLE Baseband & Modem     |
       |  - Hardware DMA & Peripherals (SPI, I2C)    |
       +---------------------------------------------+
```

### Sleep & Wakeup Flow

1. **Retention State Preservation (`32,080` / `32,768` Bytes Used):**
   - Stack contexts, Zigbee neighbor tables, security frame counters, PAN ID / operating channel (`s_zb_nwk_pan_id`, `s_zb_nwk_channel`), BLE connection parameters, cached battery voltage (`s_cached_battery_mv`), cached EPD temperature (`epd_temperature`), power telemetry counters, and the 56-entry debug log ring buffer (`s_log_ring`) reside in `.retention_reset`, `.retention_data`, `.data`, and `.bss` (`0x840000`–`0x847D50`, leaving 688 bytes of safety margin below the 32 KB hardware retention boundary `0x848000`).
   - Meanwhile, the 16 KB shared OTA/upload scratch buffer (`shared_scratch_ram`) and NFC NDEF mirror buffers reside in `.custom_bss` (`0x847D50`–`0x84C008`) above the retention boundary where they do not consume retention SRAM.
   - On wake (`ana_reg_0x7e != 0`), [`cstartup_8258.S`](../src/patch_sdk/cstartup_8258.S) restores `tl_multi_addr` and jumps directly to `ENTER_MAIN`, bypassing cold-boot stack filling (`FLL_STK`) and `.custom_bss` zeroing (`ZERO_CUSTOM_BSS`) to achieve sub-150 µs wake-to-`main()` latency.
2. **Wakeup Sources:**
   - **Internal 32K Timer (`PM_WAKEUP_TIMER`):** Wakes the system for scheduled Zigbee data polling, rejoin backoff timers, or BLE advertising/connection events.
   - **`GPIO_NFC_IRQ` (`PC4`, `Level_Low`):** Wakes the MCU immediately when an external NFC reader or smartphone presents an RF field.
   - **`GPIO_EPD_BUSY` (`PA1`, `Level_High`):** Wakes the MCU from `SUSPEND_MODE` immediately when the e-paper refresh waveform completes.
3. **Sleep Gating During E-Paper Refresh (`SUSPEND_MODE`):**
   - Because `DEEPSLEEP_MODE_RET_SRAM_LOW32K` powers down digital GPIO output registers and `drv_platform_init()` re-initializes GPIO registers on wakeup, entering deep retention mid-refresh would glitch `GPIO_EPD_PWR_ENABLE`, `GPIO_EPD_RESET`, and `GPIO_EPD_CS`.
   - In BLE mode, [`ble_pm_task()`](../src/ble/ble_app.c) and [`ble_cpu_sleep_wakeup_32k_rc()`](../src/ble/ble_app.c) hold the MCU in `SUSPEND_MODE` (`SUSPEND_ADV | SUSPEND_CONN`) while `epd_is_busy()` is true.
   - In Zigbee mode, [`zb_pm_task()`](../src/zigbee/zb_app.c) similarly enters `drv_pm_sleep(PM_SLEEP_MODE_SUSPEND, ...)` between 2.0s parent polls while `epd_is_busy()` is true, restoring the RF transceiver via `mac_phyReconfig()` on wakeup and returning to `DEEPSLEEP_MODE_RET_SRAM_LOW32K` as soon as [`epd_set_sleep()`](../src/epd/epd.c) completes.

---

## 5. Low-Power Architecture & Implementation Nuances

The firmware achieves optimal wake duration and low quiescent sleep draw through specific architectural choices across both stacks:

### 1. Transceiver Gating during CSMA/CA Backoff (`RF_STATE_OFF`)
[`rf_reset()`](../SDK/stack/zigbee/mac/mac_phy.c) initializes the transceiver in `RF_STATE_OFF`. The 2.4 GHz radio remains unpowered throughout crystal stabilization, `ev_timer` evaluation, and the CSMA/CA random backoff delay (`0..7 × 320 µs`), turning on only when `rf_performCCA()` and `rf_tx()` execute. Radio output power is configured to `ZB_DEFAULT_TX_POWER_IDX` (0 dBm), and full modem and timer initialization is executed cleanly during boot in [`user_zb_init()`](../src/zigbee/zb_app.c).

### 2. Cached Temperature Piggybacking (UC8151 / UC8176)
In BLE mode, [`bthome_data_beacon()`](../src/ble/ble_bthome.c) incorporates display temperature in its BTHome V2 payload. Rather than waking and powering the high-voltage EPD booster between refreshes, both [`epd_display_slot()`](../src/epd/epd.c) (2.13" UC8151) and [`EPD_BWR_420_init()`](../src/epd/epd_bwr_420.c) (4.2" UC8176) read command `0x40` while the panel is already energized for an active display refresh and cache the temperature in `epd_temperature` (in retention SRAM). [`EPD_BWR_213_read_temp()`](../src/epd/epd_bwr_213.c) and [`EPD_BWR_420_read_temp()`](../src/epd/epd_bwr_420.c) return this cached value in O(1) time with zero GPIO or SPI activity.

### 3. SAR ADC Clock & `GPIO_VBAT` (`PB0`) Divider Rate Limiting
[`get_battery_mv()`](../src/battery.c) caches battery voltage in retention SRAM (`s_cached_battery_mv`) and rate-limits ADC conversions to once every 10 minutes (`600 × 32000` ticks). Immediately after completing a SAR ADC conversion, [`get_adc_mv()`](../src/patch_sdk/adc_drv.c) disables the SAR ADC state machine, clears `FLD_CLK_24M_TO_SAR_EN`, closes the 1/8 ATB scaler (`ADC_PRESCALER_1`), and configures `GPIO_VBAT` (`PB0`) output low and disabled to eliminate resistor divider leakage.

### 4. BLE Deep-Retention Sleep & DMA TX FIFO Management
[`ble_cpu_sleep_wakeup_32k_rc()`](../src/ble/ble_app.c) hooks `cpu_sleep_wakeup` in BLE mode, inspects the binary-accurate `bltPm` offsets (`+16` adv threshold, `+20` conn threshold, `+24` early wakeup ticks, `+28` `deepRt_en`, `+29` `deepRet_type`), subtracts `deepRet_earlyWakeupTick` (`400 µs`), and upgrades sleep to `DEEPSLEEP_MODE_RET_SRAM_LOW32K` (`0x07`, 1.8 µA MCU sleep floor) whenever `!epd_is_busy()` and the sleep interval exceeds 95 ms. On retention wakeup, [`user_ble_init(true)`](../src/ble/ble_app.c) explicitly resets `blt_dma_tx_rptr = 0` and `sdk_mainLoop_run_flag = 0` to preserve BLE connection DMA TX FIFO integrity across deep sleep cycles.

### 5. Fast Retention Wakeup Path (`cstartup_8258.S`)
In [`src/patch_sdk/cstartup_8258.S`](../src/patch_sdk/cstartup_8258.S), retention wakeups inspect `ana_reg_0x7e` and jump directly to `ENTER_MAIN`, bypassing cold-boot stack filling (`FLL_STK`) and `.custom_bss` zeroing (`ZERO_CUSTOM_BSS`). This achieves wake-to-`main()` execution latency of **< 150 µs**.

### 6. Event-Gated Peripheral State (NFC I2C & LED Pull-Ups)
Because the Fudan FM11NC08 NFC transceiver and TLSR8258 analog GPIO pull-up/pull-down registers retain their states across `DEEPSLEEP_MODE_RET_SRAM_LOW32K`, the firmware event-gates NFC I2C re-initialization on active RF field detection (`!gpio_read(GPIO_NFC_IRQ)`) in [`main()`](../src/main.c) and skips analog pull-up writes in [`led_restore_retention()`](../src/led.c) when all LEDs are off.

### 7. Peripheral Clock Gating & Quiet Periodic Operation
Digital peripheral clocks in `reg_clk_en0` (`FLD_CLK0_SPI_EN`, `FLD_CLK0_I2C_EN`, `FLD_CLK0_SWIRE_EN`, `FLD_CLK0_UART_EN`) remain gated during regular retention cycles: `FLD_CLK0_SWIRE_EN` is enabled only on cold boot, SPI is clocked only during active EPD transfers, and I2C is clocked only during active NFC transactions. Furthermore, periodic retention wakeups avoid blocking UART logging, keeping active CPU time minimal.

---

## 6. Battery Longevity Projections

Longevity projections are calculated using an 85% usable battery capacity derating to account for coin-cell internal resistance and pulse discharge characteristics.

### Usable Battery Capacities
- **CR2450 Cell:** 600 mAh nominal $\rightarrow$ **500 mAh usable**
- **CR2477 Cell:** 1000 mAh nominal $\rightarrow$ **850 mAh usable**

### Complete Longevity Model Across Operating Modes

| Operating Mode | Display Updates | Average Current | CR2450 Battery Life (500 mAh) | CR2477 Battery Life (850 mAh) |
| :--- | :--- | :--- | :--- | :--- |
| **BLE BTHome V2 Beacon (2.0s)** | **0 per day** (Static) | **13.94 µA** | **4.10 years (1,495 days)** | **6.96 years (2,541 days)** |
| **BLE BTHome V2 Beacon (2.0s)** | **1 per day** | **15.35 µA** | **3.72 years (1,357 days)** | **6.32 years (2,307 days)** |
| **BLE BTHome V2 Beacon (2.0s)** | **4 per day** | **19.58 µA** | **2.92 years (1,064 days)** | **4.96 years (1,809 days)** |
| **BLE BTHome V2 Beacon (2.0s)** | **12 per day** | **30.87 µA** | **1.85 years (675 days)** | **3.15 years (1,148 days)** |
| **Zigbee 3.0 SED (2.0s Poll)** | **0 per day** (Static) | **18.64 µA** | **3.06 years (1,118 days)** | **5.21 years (1,900 days)** |
| **Zigbee 3.0 SED (2.0s Poll)** | **1 per day** | **20.05 µA** | **2.85 years (1,039 days)** | **4.84 years (1,766 days)** |
| **Zigbee 3.0 SED (2.0s Poll)** | **4 per day** | **24.28 µA** | **2.35 years (858 days)** | **4.00 years (1,459 days)** |
| **Zigbee 3.0 SED (2.0s Poll)** | **12 per day** | **35.57 µA** | **1.60 years (586 days)** | **2.73 years (995 days)** |
| **Zigbee Rejoin Backoff (1h Retry)** | N/A | **7.00 µA** | **8.15 years (2,976 days)** | **13.86 years (5,059 days)** |
| **Zigbee Battery Saver (Exhausted / Idle)** | N/A | **4.00 µA** | **> 10 years** | **> 10 years** |

---

## 7. Potential Optimization Opportunities (Future Work)

The following forward-looking architectural changes offer paths for further power reductions if application requirements permit:

### 1. Non-Connectable BLE Advertising with On-Demand NFC Provisioning
- **Concept:** In BTHome beacon mode, connectable undirected advertising packets require a 380 µs radio RX listening window across the 3 advertising channels ($Q_{\text{RX}} = 4.93\text{ µC}$).
- **Mechanism:** Switch default advertising to `ADV_TYPE_NONCONNECTABLE_UNDIRECTED` and temporarily re-enable connectable advertising for 60 seconds upon an NFC tap (`GPIO_NFC_IRQ`).
- **Projected Impact:** Reduces average BLE current from **13.94 µA down to ~11.48 µA**, extending CR2450 beacon longevity from 4.10 years to **~4.98 years** (and ~8.45 years on CR2477).

### 2. Adaptive Zigbee Polling Interval (Dynamic SED Backoff)
- **Concept:** Rather than maintaining a fixed 2.0-second indirect poll rate 24/7, use adaptive polling based on time since last ZCL command.
- **Mechanism:** Poll at 2.0s for 60 seconds after any ZCL transaction or NFC tap, then relax to 5.0s–10.0s during extended idle periods.
- **Projected Impact:** An average 5.0-second poll rate cuts active Zigbee polling overhead, reducing overall average current from **18.64 µA down to ~9.86 µA** and extending CR2450 battery life from 3.06 years to **~5.79 years** (and ~9.83 years on CR2477).

### 3. Fast Partial Refresh Waveforms (LUT) for Numeric/Price Updates
- **Concept:** Full BWR OTP refresh cycles take 15.15 seconds and consume 122 mA·s per update.
- **Mechanism:** Implement black/white partial refresh waveforms (LUT) for updating numeric prices or stock counts without toggling the red pigment layer.
- **Projected Impact:** Partial refreshes complete in 1.0–1.5 seconds, cutting screen update energy by **~90% (to ~12 mA·s)**.
