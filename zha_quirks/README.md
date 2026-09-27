# Home Assistant ZHA Quirks for TLSR8258 E-Paper Dual-Stack

Custom **ZHA (Zigbee Home Automation)** device handler (Quirks V2) for the **Hanshow Stellar E-Paper ESLs** running the dual-stack Zigbee 3.0 + BLE firmware:
- **Hanshow Stellar-M3N@ / E31HA** (2.13" BWR, 250×122): `TLSR-M3Na-E31HA`
- **Hanshow Stellar-XL3N@ / E31PA** (4.2" BWR, 400×300): `TLSR-XL3Na-E31PA`

- **Manufacturer**: `ZB-DIY`
- **Endpoint**: `1`
- **Zigbee Profile**: Home Automation (`0x0104`)
- **Device ID**: `0x000F` (Generic Custom Device)

---

## Features & Exposed Entities

When paired with Home Assistant via ZHA, the quirk (`ts_epaper.py`) automatically discovers and exposes the following entities:

| Entity Type | Entity Name / Translation Key | Values / Units | Description |
| **Select** | `active_slot` | **M3Na**: `Info (0)`, `User_1 (1)` .. `User_8 (8)`, `Blank (9)`<br>**XL3Na**: `Info (0)`, `User_1 (1)` .. `User_4 (4)`, `Blank (5)` | Switches the active E-Paper display slot. |
| **Select** | `render_style` | `Standard_BWR (0)`, `BW_Standard (1)`, `BW_Inverted (2)`, `RW_Standard (3)`, `RW_Inverted (4)` | Changes color rendering mode without altering flash slot data. |
| **Select** | `active_mode` | `Zigbee (1)`, `BLE (2)` | Protocol switch. Selecting `BLE` saves config and reboots the tag into BLE BTHome beacon mode. |
| **Sensor** | `battery` | `%` | Battery percentage remaining (from standard Power Configuration `0x0001`). |
| **Sensor** | `battery_voltage` | `V` | Battery voltage in Volts (from standard Power Configuration `0x0001`). |
| **Sensor** | `screen_refresh_count` | count | *(Diagnostic, Disabled by default)* Cumulative E-Paper display refresh counter. |
| **Sensor** | `wakeup_count` | count | *(Diagnostic, Disabled by default)* Total device wake-up count since boot. |
| **Sensor** | `wakeup_duration` | `ms` | *(Diagnostic, Disabled by default)* Cumulative active awake duration since boot. |
| **Sensor** | `tx_duration` | `ms` | *(Diagnostic, Disabled by default)* Cumulative wireless TX active duration since boot. |
| **Sensor** | `rx_duration` | `ms` | *(Diagnostic, Disabled by default)* Cumulative wireless RX active duration since boot. |
| **Button** | `identify` | — | Standard Zigbee Identify button. |
| **Update** | `firmware` | — | ZHA OTA firmware update entity (via OTA Client cluster `0x0019`). |

> [!TIP]
> **Enabling Diagnostic Sensors**: `screen_refresh_count`, `wakeup_count`, `wakeup_duration`, `tx_duration`, and `rx_duration` are categorized as diagnostic telemetry and **disabled by default** to avoid cluttering your dashboard and database. To enable any of them:
> 1. Go to **Settings** -> **Devices & Services** -> **Zigbee Home Automation** -> **TLSR-M3Na-E31HA**.
> 2. Under **Diagnostic**, click the disabled entity.
> 3. Click the gear icon (**Settings**) -> toggle **Enable entity** -> **Update**.

---

## Installation Guide

### Step 1: Create Custom Quirks Directory in Home Assistant
Connect to your Home Assistant instance (via SSH, Samba share, or the File Editor add-on) and create a folder for custom quirks inside `/config`:
```bash
mkdir -p /config/custom_zha_quirks
```

### Step 2: Copy the Quirk Script
Copy `ts_epaper.py` from this repository into your Home Assistant custom quirks directory:
```text
/config/custom_zha_quirks/ts_epaper.py
```

### Step 3: Enable Custom Quirks in Home Assistant Configuration
Add (or verify) the `custom_quirks_path` setting in your `/config/configuration.yaml`:
```yaml
zha:
  custom_quirks_path: /config/custom_zha_quirks
```

### Step 4: Restart Home Assistant
Restart Home Assistant Core:
- Go to **Settings** -> **System** -> **Restart** -> **Restart Home Assistant**.

### Step 5: Pair the ESL Tag
1. Put your Zigbee coordinator into pairing mode (**Settings** -> **Devices & Services** -> **Zigbee Home Automation** -> **Add Device**).
2. Put the Hanshow ESL tag in pairing mode:
   - **Via NFC**: Tap the tag with your smartphone and write `"zigbee:reset"` or `"zb:reset"`.
   - **Via Web UI (BLE)**: Connect to the tag via Web Bluetooth (`web/index.html`), and under **Zigbee 3.0 Configuration**, click **"⚠️ Reset Zigbee Network"**.
3. The Green LED on the tag will flash rapidly while searching for the network, and solid/slow blink upon successful joining.
4. Home Assistant will recognize the device as **`ZB-DIY TLSR-M3Na-E31HA`** with the quirk applied.

---

## Verification & Troubleshooting

To confirm that Home Assistant is using the custom quirk:
1. Navigate to **Settings** -> **Devices & Services** -> **Zigbee Home Automation** -> **Devices**.
2. Select **`TLSR-M3Na-E31HA`**.
3. Under **Device info**, click the three dots (`⋮`) and select **Zigbee device signature**.
4. Check the `quirk_class` line. It should show:
   ```text
   "quirk_class": "ts_epaper.CustomEpaperCluster"
   ```
   or reference `ts_epaper`.

### Enabling Debug Logs for ZHA Quirks
If the quirk is not loaded, add the following to `configuration.yaml` and restart Home Assistant:
```yaml
logger:
  default: info
  logs:
    homeassistant.components.zha: debug
    zhaquirks: debug
    zigpy: debug
```
Look for lines matching `Loading custom quirk` or `Found quirk` in `/config/home-assistant.log`.

---

## Home Assistant Automation Examples

### 1. Automatically Cycle Display Slot Based on Time of Day
```yaml
alias: "ESL: Switch to Slot 1 in Morning"
description: "Display User Image 1 at 08:00 AM"
trigger:
  - platform: time
    at: "08:00:00"
action:
  - service: select.select_option
    target:
      entity_id: select.tlsr_m3na_e31ha_active_slot
    data:
      option: "User_1"
mode: single
```

```yaml
alias: "ESL: Switch to Status Info at Night"
description: "Show device telemetry and battery status at 22:00"
trigger:
  - platform: time
    at: "22:00:00"
action:
  - service: select.select_option
    target:
      entity_id: select.tlsr_m3na_e31ha_active_slot
    data:
      option: "Info"
mode: single
```

### 2. Remotely Switch Device from Zigbee to BLE Mode
```yaml
alias: "ESL: Switch to BLE Mode"
description: "Reboot tag into BLE mode for uploading new slot images"
trigger: []
action:
  - service: select.select_option
    target:
      entity_id: select.tlsr_m3na_e31ha_operating_mode
    data:
      option: "BLE"
mode: single
```

---

## Architecture & Cluster Map

```
Endpoint 1 (HA Profile 0x0104, Device ID 0x000F)
├── Server (In) Clusters
│   ├── 0x0000: Basic (Model: TLSR-M3Na-E31HA, Manufacturer: ZB-DIY)
│   ├── 0x0001: Power Configuration (Battery Voltage & Percentage)
│   ├── 0x0003: Identify
│   └── 0xFC00: Custom E-Paper Display Cluster (Managed by ts_epaper.py)
│       ├── 0x0000: active_slot (uint8, RW: 0..9 for M3Na, 0..5 for XL3Na)
│       ├── 0x0001: render_style (uint8, RW: 0..4)
│       ├── 0x0002: active_mode (uint8, RW: 1=Zigbee, 2=BLE)
│       ├── 0x0003: refresh_count (uint16, RO)
│       ├── 0x0004: led_state (uint8, RW: 0..7)
│       ├── 0x0005: wakeup_count (uint32, RO)
│       ├── 0x0006: wakeup_duration (uint32, RO, ms)
│       ├── 0x0007: tx_duration (uint32, RO, ms)
│       └── 0x0008: rx_duration (uint32, RO, ms)
└── Client (Out) Clusters
    └── 0x0019: OTA Upgrade
```
