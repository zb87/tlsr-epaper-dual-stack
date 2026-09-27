# TLSR E-Paper Dual-Stack Firmware

Dual-protocol firmware for **Hanshow Stellar-M3N@ / E31HA** (2.13" 250×122, UC8151) and **Hanshow Stellar-XL3N@ / E31PA** (4.2" 400×300, UC8176) electronic shelf labels (ESLs) powered by the **Telink TLSR8258** SoC, **FM11NC08** NFC IC, and tri-color BWR E-Paper Displays.

The firmware combines **Zigbee 3.0** (Sleepy End Device) and **Bluetooth Low Energy** (BTHome V2 beacon + GATT) into a single binary with zero-power runtime mode switching via **passive NFC** and wireless **Zigbee / BLE commands**.

---

## Supported Hardware

- **Hanshow Stellar-M3N@ (`E31HA` / `E31H`):** 2.13" BWR E-Paper ($250 \times 122$), UltraChip UC8151.
- **Hanshow Stellar-XL3N@ (`E31PA` / `STELLARP-420`):** 4.2" BWR E-Paper ($400 \times 300$), UltraChip UC8176.

Detailed pinout tables, schematics, component architectures, and hardware differences are documented in [**Motherboard Hardware & Pinout Reference**](docs/motherboard.md). Stock factory firmware dumps and reverse-engineering analyses are preserved in [`original_firmware/`](original_firmware/).

---

## Features (TL;DR)

- **Exclusive Dual-Stack Switching:** Boots cleanly into either Zigbee 3.0 or BLE 5.0 based on wear-leveled flash configuration. Switch protocols anytime via NFC, Web Bluetooth, or Zigbee command.
- **Zero-Power Passive NFC Control:** Uses the onboard FM11NC08 NFC tag. Change modes (`mode:zigbee`, `mode:ble`), switch image slots (`slot:1`), update color styles (`style:2`, `bw`, `rw`), or trigger a factory reset (`zigbee:reset`) with any NFC-equipped smartphone. Zero standby battery drain when NFC is idle. See [NFC Documentation](docs/nfc.md).
- **Multi-Slot E-Paper Engine:** Line-by-line 2D Delta + PackBits decompression streaming directly from flash into the display controller in 64-byte bursts (< 50 bytes RAM overhead, < 1 ms CPU time). Supports 8 user image slots on M3Na (4 KB each) and 4 slots on XL3Na (8 KB each), plus Slot 0 (system info screen) and a blank white screen. See [EPD Waveforms](docs/epaper.md) and [User Images Guide](docs/user-images.md).
- **5 On-the-Fly Color Styles:** Dynamically re-maps BWR bit-planes at refresh time without modifying flash slot data: Standard Tri-Color, B&W Standard, B&W Inverted, Red & White Standard, and Red & White Inverted.
- **Ultra-Low Power & Multi-Year Longevity:** Deep retention sleep current is ~3.8–4.0 µA. At standard 2.0-second intervals, average current is ~13.9 µA in BLE mode and ~18.6 µA in Zigbee SED mode, yielding 3+ years of operation on coin cells. See [Power Consumption Analysis](docs/power-consumption.md).
- **Consolidated Flash Architecture:** 192 KB execution bank, 64 KB contiguous Zigbee NVRAM (`0x30000`–`0x3FFFF`), 192 KB OTA target bank, wear-leveled configuration EEPROM, and 32 KB user image storage. See [Flash Partitions](docs/partitions.md) and [RAM Layout](docs/ram.md).
- **Live Prototyping & Wireless Console:** Execute dynamic code snippets directly in flash without rebooting and inspect debug logs wirelessly via an in-memory ring buffer. See [Dynamic Prototyping Guide](docs/prototype.md).

---

## Technical Documentation Index

| Topic | Document | Description |
| :--- | :--- | :--- |
| **Motherboard & Pinout** | [docs/motherboard.md](docs/motherboard.md) | Pin mapping, chipset specifications, and wiring |
| **Power & Battery** | [docs/power-consumption.md](docs/power-consumption.md) | Empirical current profiles, timing budgets, and battery life |
| **Flash Partitions** | [docs/partitions.md](docs/partitions.md) | Memory partition map and wear-leveling implementation |
| **RAM Architecture** | [docs/ram.md](docs/ram.md) | Retention SRAM, memory pools, and stack layout |
| **Zigbee Subsystem** | [docs/zigbee.md](docs/zigbee.md) | Sleepy End Device profile, cluster map, and OTA update container |
| **Bluetooth LE** | [docs/ble.md](docs/ble.md) | BTHome V2 beacon, GATT services, and fast slot streaming |
| **Passive NFC** | [docs/nfc.md](docs/nfc.md) | FM11NC08 NDEF parsing and event-gated zero-overhead operation |
| **EPD Waveforms** | [docs/epaper.md](docs/epaper.md) | Display waveforms, SPI protocol, and refresh procedures |
| **Image Compression** | [docs/user-images.md](docs/user-images.md) | 2D Delta + PackBits compression, headers, and slot layout |
| **Dynamic Prototyping** | [docs/prototype.md](docs/prototype.md) | Runtime snippet execution and in-memory log ring buffer |
| **ZHA Integration** | [zha_quirks/README.md](zha_quirks/README.md) | Home Assistant ZHA Quirk setup and automation examples |
| **Change Log** | [changelog.md](changelog.md) | Firmware release history and changelog |

---

## Compiling the Firmware

### Prerequisites
- Linux x86_64 environment.
- Telink TC32 toolchain (`tc32-elf-gcc`). Ensure `tc32/bin` is in your `PATH` or set `TC32PATH=/path/to/tc32/bin`.
- Python 3.

### Build Commands

```bash
# Build Hanshow Stellar-M3N@ (2.13" E31HA)
make m3na

# Build Hanshow Stellar-XL3N@ (4.2" E31PA)
make xl3na

# Build both targets
make all
```

### Build Options
- **`DEBUG=0` (Default):** Production build. All debug UART code and the in-memory log ring buffer are completely stripped with zero flash, RAM, or CPU overhead.
- **`DEBUG=1`:** Enables hardware UART diagnostic logging on pin `PB1` at 115,200 baud (8-N-1) and activates the in-memory log ring buffer.

Build outputs are saved to `bin/`:
- `bin/tlsr-epaper-m3na.bin` / `bin/tlsr-epaper-xl3na.bin`: Raw flash binaries for wire flashing and BLE OTA.
- `bin/1141-*-tlsr-epaper-*.zigbee`: Packaged images containing standard 56-byte ZCL OTA headers for Zigbee OTA.

---

## Firmware Updating & Flashing

### 1. Initial Wire Flashing (SWS Interface)

For first-time programming or recovering a blank device, use a standard USB-to-UART adapter (e.g. CP2102, FT232, CH340) and an open-source Telink SWS flasher like [**TLSR825xComFlasher.py**](https://github.com/pvvx/TlsrComSwireWriter):

1. **Hardware Wiring:**
   - Connect **GND** and **3.3V** to the tag.
   - Connect adapter **TX** through a 1 kΩ resistor (or diode) and adapter **RX** directly to **PA7 (SWS)**.
   - *(Optional)* Connect adapter **RX** to **PB1 (TX)** to monitor serial logs when compiled with `DEBUG=1`.
2. **Flash Commands:**
   ```bash
   # Unlock SPI flash write protection
   python3 TLSR825xComFlasher.py -p /dev/ttyUSB0 -t 8258 unprotect

   # Flash firmware binary
   python3 TLSR825xComFlasher.py -p /dev/ttyUSB0 -t 8258 -v wf 0x00000 bin/tlsr-epaper-m3na.bin
   ```

> [!CAUTION]
> Do not perform a full chip erase (`ea`) or erase sectors `0x76000` (factory IEEE MAC address) or `0x77000` (24 MHz crystal calibration trim).

---

### 2. Over-the-Air via Web Bluetooth (BLE OTA)

When running in BLE mode, the firmware supports high-speed pipelined OTA flashing directly from a web browser without installing additional software:

1. Open [`web/index.html`](web/index.html) in a Web Bluetooth compatible browser (Google Chrome, Microsoft Edge, or Bluefy on iOS).
2. Click **Connect Tag** and select your device (`TLSR-XXXXXX`).
3. Under the **Firmware OTA Update** section, select `bin/tlsr-epaper-m3na.bin` (or `xl3na.bin`).
4. Click **Start OTA Flashing**. Pipelined chunk streaming completes the 192 KB upload in ~15–20 seconds, followed by automatic verification and reboot.

---

### 3. Over-the-Air via Zigbee (ZCL OTA)

When running in Zigbee mode, the device periodically queries its coordinator for new firmware images using standard Zigbee Cluster Library (ZCL) OTA upgrade procedures.

#### Zigbee2MQTT Setup
1. Copy the compiled `.zigbee` image and `zigbee2mqtt/index.json` into your Zigbee2MQTT data folder:
   ```bash
   cp bin/1141-*-tlsr-epaper-*.zigbee <z2m_data_dir>/
   cp zigbee2mqtt/index.json <z2m_data_dir>/
   ```
2. Enable the local OTA index in your Zigbee2MQTT `configuration.yaml`:
   ```yaml
   ota:
     zigbee_ota_override_index_location: index.json
   ```
3. Restart Zigbee2MQTT. Navigate to the device's **OTA** tab in the web interface and click **Check for new updates** / **Update firmware**.

#### Home Assistant ZHA Setup
Place the `.zigbee` file in your configured ZHA custom OTA directory (e.g. `zha_ota_path: /config/zigbee_ota`) and trigger the firmware update through the Home Assistant device management page.

---

## Integrations

### Zigbee2MQTT
1. Copy `zigbee2mqtt/zb-epaper.js` to your Zigbee2MQTT `data/` directory.
2. Add the external converter to `configuration.yaml`:
   ```yaml
   external_converters:
     - zb-epaper.js
   ```
3. Restart Zigbee2MQTT. The converter exposes:
   - `active_slot`: Dropdown selector (Info, User 1..8 / 1..4, Blank).
   - `render_style`: Dropdown selector (Standard BWR, BW, BW Inverted, RW, RW Inverted).
   - `mode`: Allows writing `ble` to remotely switch the tag into BLE mode for image uploading.
   - Sensor telemetry: `battery`, `voltage`, and `screen_refresh_count`.

### Home Assistant ZHA
1. Copy `zha_quirks/ts_epaper.py` into your Home Assistant custom quirks directory (e.g. `/config/custom_zha_quirks/`).
2. Add the custom quirks path to `configuration.yaml`:
   ```yaml
   zha:
     custom_quirks_path: /config/custom_zha_quirks
   ```
3. Restart Home Assistant Core. The quirk discovers select entities for active slot, color style, and operating mode, as well as battery and diagnostic counters. See [zha_quirks/README.md](zha_quirks/README.md) for automation examples.

### BTHome V2 (BLE Mode)
In BLE mode, the device broadcasts standard BTHome V2 telemetry beacons (`0xFCD2`) every 2 seconds:
- Battery Percentage (`0x01`)
- Battery Voltage in mV (`0x0C`)
- Temperature in 0.1°C (`0x02`)
- Active Slot index (`0x40`)
- Operating Mode (`0x41`)

Discovered automatically by the native Home Assistant BTHome integration and ESPHome Bluetooth Proxies.

---

## Potential Future Optimizations

- **Fast Partial-Refresh Waveforms:** Implementation of custom 1-bit LUT waveforms for ultra-fast monochrome updates (< 1.0 s) with minimal ghosting.
- **Dynamic Radio Duty Cycle Scaling:** Dynamically extending Zigbee poll intervals or BLE advertising periods based on battery reserve and radio link metrics.
- **Two-Way NFC Synchronization:** Fast configuration transfer enabling mobile apps to write multi-parameter batches or raw image chunks across the contact I2C bus while in the field.

---

## Reference Implementations & Acknowledgments

This project builds upon insights and architectures established by open-source firmware projects in the Telink TLSR825x community:

- [**ATC_TLSR_Paper**](https://github.com/atc1441/ATC_TLSR_Paper) by Aaron Christophel (atc1441) &ndash; Pioneering custom BLE firmware and reverse engineering for Hanshow Stellar electronic shelf labels.
- [**ATC_MiThermometer**](https://github.com/atc1441/ATC_MiThermometer) by Aaron Christophel and the [**pvvx fork**](https://github.com/pvvx/ATC_MiThermometer) &ndash; Foundational work on TLSR825x low-power retention states, flash wear-leveling, and power optimization.
- [**ZigbeeTLc**](https://github.com/pvvx/ZigbeeTLc) & [**TlsrComSwireWriter**](https://github.com/pvvx/TlsrComSwireWriter) by Victor V. (pvvx) &ndash; Telink B85 Zigbee 3.0 Sleepy End Device framework and single-wire UART flasher implementation.
- [**OneBitDisplay**](https://github.com/bitbank2/OneBitDisplay) by Larry Bank &ndash; Lightweight embedded graphics drawing primitives.
- **Telink Semiconductor** &ndash; Official B85 BLE and Zigbee SDK libraries.

---

## Attribution & AI Disclaimer

> [!NOTE]
> All code, documentation, and architecture in this repository were generated by AI, directed solely by human prompts. As the synthesis incorporates design patterns and concepts from across the open-source microcontroller ecosystem, there is a possibility that specific references or upstream credits may have been unintentionally omitted. If you recognize any code, routines, or structural elements originating from your work that lack appropriate attribution, please contact the author or open an issue so that full credit and reference links can be promptly added.

---

## License

This project is licensed under open-source terms. Please refer to individual library headers in `SDK/` and `src/epd/` for respective vendor licenses.
