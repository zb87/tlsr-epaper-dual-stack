# Flash Partition Architecture & Wear-Leveling Reference

This document defines the 512 KB (4 Mbit) SPI NOR flash partition table, hardware multi-bank boot mechanics, Zigbee NVRAM consolidation, user image slot storage, and the wear-leveled settings subsystem on the Hanshow Stellar-M3N@ / E31HA (Telink TLSR8258).

---

## 1. Complete Flash Memory Map (512 KB)

The internal/stacked SPI NOR flash has a total capacity of 524,288 bytes (`0x00000` to `0x7FFFF`), organized into 128 sectors of 4,096 bytes (4 KB) each.

```
0x00000 ┌─────────────────────────────────────────────────────────────┐
        │  Bank 0: Base Firmware Runtime                              │ 192 KB
        │  Active execution partition for cold boot & normal runtime   │
0x30000 ├─────────────────────────────────────────────────────────────┤
        │  Zigbee 3.0 NVRAM (Consolidated)                            │  64 KB
        │  16 sectors: Network credentials, PAN ID, binding, tables   │
0x40000 ├─────────────────────────────────────────────────────────────┤
        │  Bank 1: OTA Target Partition                               │ 192 KB
        │  Staging partition for incoming BLE & Zigbee OTA images     │
0x70000 ├─────────────────────────────────────────────────────────────┤
        │  Dynamic Prototype Snippet Storage (4 sectors)             │  16 KB
0x74000 ├─────────────────────────────────────────────────────────────┤
        │  Wear-Leveled Settings EEPROM (2 sectors ping-pong)         │   8 KB
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

### 1.1 Stellar-M3N@ / E31HA Flash Memory Map (2.13" BWR)

With 2D Delta + PackBits compression, simple e-paper images compress significantly (~3:1 to 10:1 ratio). Each user BWR image is stored as a single contiguous package (16-byte header + compressed BW + compressed Red) within a **4 KB sector**, providing **8 user slots** (Slots 1..8) plus dynamic Info (Slot 0) and Blank White (Slot 9):

| Start Addr | End Addr | Size | Sectors | Partition Name | Purpose & Contents |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00000` | `0x2FFFF` | 192 KB | 48 | **Bank 0 (Main Firmware)** | Primary bootable dual-stack firmware image (`ramcode` + `text` + `data`). |
| `0x30000` | `0x3FFFF` | 64 KB | 16 | **Zigbee NVRAM** | Consolidated Zigbee 3.0 persistent network parameters, security keys, routing tables, and attribute NV items. |
| `0x40000` | `0x6FFFF` | 192 KB | 48 | **Bank 1 (OTA Target)** | Hardware secondary boot bank; target flash area during over-the-air firmware updates. |
| `0x70000` | `0x73FFF` | 16 KB | 4 | **Prototype Code Storage** | Zero-reboot dynamic C snippet execution partition. |
| `0x74000` | `0x75FFF` | 8 KB | 2 | **Settings EEPROM** | Wear-leveled runtime settings (active mode, active slot, render style, refresh count). |
| `0x76000` | `0x76FFF` | 4 KB | 1 | **Factory MAC** | Factory-flashed public IEEE 802.15.4 / BLE MAC address (`0x76000`..`0x76007`). |
| `0x77000` | `0x77FFF` | 4 KB | 1 | **Crystal Trim** | Factory 24 MHz high-frequency oscillator load capacitance calibration value (`0x77000`). |
| `0x78000` | `0x78FFF` | 4 KB | 1 | **Slot 1 (User Image 1)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x79000` | `0x79FFF` | 4 KB | 1 | **Slot 2 (User Image 2)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x7A000` | `0x7AFFF` | 4 KB | 1 | **Slot 3 (User Image 3)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x7B000` | `0x7BFFF` | 4 KB | 1 | **Slot 4 (User Image 4)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x7C000` | `0x7CFFF` | 4 KB | 1 | **Slot 5 (User Image 5)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x7D000` | `0x7DFFF` | 4 KB | 1 | **Slot 6 (User Image 6)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x7E000` | `0x7EFFF` | 4 KB | 1 | **Slot 7 (User Image 7)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |
| `0x7F000` | `0x7FFFF` | 4 KB | 1 | **Slot 8 (User Image 8)** | Compressed BWR image package (Header + BW + Red $\le$ 4,096 B). |

### 1.2 Stellar-XL3N@ / E31PA Flash Memory Map (4.2" BWR)

The 4.2" display resolution is 400 × 300 pixels (15,000 bytes per plane uncompressed). Using 2D Delta + PackBits compression, typical screens compress to 1,500 &ndash; 3,500 bytes. Each user BWR image is stored as a single contiguous package within an **8 KB slot** (2 sectors). This provides **4 user slots** (Slots 1..4) plus dynamic Info (Slot 0) and Blank White (Slot 5):

| Start Addr | End Addr | Size | Sectors | Partition Name | Purpose & Contents |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00000` | `0x2FFFF` | 192 KB | 48 | **Bank 0 (Main Firmware)** | Primary bootable dual-stack firmware image (`ramcode` + `text` + `data`). |
| `0x30000` | `0x3FFFF` | 64 KB | 16 | **Zigbee NVRAM** | Consolidated Zigbee 3.0 persistent network parameters and keys. |
| `0x40000` | `0x6FFFF` | 192 KB | 48 | **Bank 1 (OTA Target)** | Secondary boot bank for OTA updates. |
| `0x70000` | `0x73FFF` | 16 KB | 4 | **Prototype Code Storage** | Zero-reboot dynamic C snippet execution partition. |
| `0x74000` | `0x75FFF` | 8 KB | 2 | **Settings EEPROM** | Wear-leveled runtime settings (active mode, active slot, render style, refresh count). |
| `0x76000` | `0x76FFF` | 4 KB | 1 | **Factory MAC** | Factory public IEEE 802.15.4 / BLE MAC address (`0x76000`..`0x76007`). |
| `0x77000` | `0x77FFF` | 4 KB | 1 | **Crystal Trim** | Factory 24 MHz crystal load capacitance calibration. |
| `0x78000` | `0x79FFF` | 8 KB | 2 | **Slot 1 (User Image 1)** | Compressed BWR image package (Header + BW + Red $\le$ 8,192 B). |
| `0x7A000` | `0x7BFFF` | 8 KB | 2 | **Slot 2 (User Image 2)** | Compressed BWR image package (Header + BW + Red $\le$ 8,192 B). |
| `0x7C000` | `0x7DFFF` | 8 KB | 2 | **Slot 3 (User Image 3)** | Compressed BWR image package (Header + BW + Red $\le$ 8,192 B). |
| `0x7E000` | `0x7FFFF` | 8 KB | 2 | **Slot 4 (User Image 4)** | Compressed BWR image package (Header + BW + Red $\le$ 8,192 B). |

---

## 2. Firmware Bank Sizing & Multi-Address Boot Mechanics

### 192 KB Maximum Bank Size

Both Bank 0 and Bank 1 are strictly sized at **192 KB (`0x30000` bytes)**:
- Bank 0: `0x00000` &ndash; `0x2FFFF`
- Bank 1: `0x40000` &ndash; `0x6FFFF`

In `src/app_config.h`:
```c
#define BANK0_START        0x00000
#define BANK0_MAX_SIZE     0x30000 // 192 KB max firmware size
#define BANK1_OTA_START    0x40000 // 192 KB OTA target partition
#define BANK1_OTA_MAX_SIZE 0x30000
```

The current dual-stack binary builds to approximately **154 KB &ndash; 158 KB**, leaving over 34 KB of headroom for future application expansions while strictly staying below the 192 KB boundary.

### Hardware Multi-Address Boot Execution

The Telink TLSR8258 contains an internal hardware boot vector remapping engine ("Multi-Address Boot"):
1. When the MCU boots or resets, the internal ROM reads the 4-byte reset vector at flash address `0x00008` (the TLSR firmware signature).
2. If `0x00008` contains a valid firmware signature (`0x544c4e4b` / `'TLNK'`), the hardware executes code from **Bank 0 (`0x00000`)**.
3. When an OTA update completes in Bank 1 (`0x40000`), the OTA completion handler invalidates `0x00008` in Bank 0 by clearing its bits to `0x00000000` (or flipping the multi-boot control register `0x63e`).
4. On the subsequent system reset, the hardware bootloader detects that `0x00008` is invalid, automatically adds `0x40000` to the memory bus address decoder, and executes code directly from **Bank 1 (`0x40000`)**.
5. The active boot bank is queryable in firmware at runtime via `mcuBootAddrGet()`, which returns `0x00000` (Bank 0) or `0x40000` (Bank 1).

---

## 3. Consolidated Zigbee 3.0 NVRAM (64 KB)

### Contiguous 64 KB Memory Mapping

Zigbee NVRAM is allocated as a single contiguous **64 KB block from `0x30000` to `0x3FFFF`** (immediately following Bank 0). This contiguous layout:
- Simplifies address mapping logic in the Zigbee stack driver.
- Eliminates sector boundary overflow risks during heavy network churn (large routing tables, APS link keys).
- Preserves contiguous space in the upper flash region for user image slots.

In `SDK/proj/drivers/drv_nv.h`:
```c
#define NV_BASE_ADDRESS       0x30000
#define FLASH_SECTOR_SIZE     0x1000 // 4 KB per sector

#define MODULES_START_ADDR(id)  (NV_BASE_ADDRESS + FLASH_SECTOR_SIZE * (2 * (id)))
#define NV_SECTOR_SIZE(id)      FLASH_SECTOR_SIZE
```

This allocates 8 module slots of 2 sectors (8 KB) each, providing 16 sectors total for:
- Basic device information and network parameters
- Network security material (Network Key, Frame Counters)
- Binding table
- Group table
- APS Key table
- ZCL Reporting table
- ZDO child table and neighbor table

---

## 4. User Image Slot Flash Storage

The ESL tag operates as an open telemetry beacon (BTHome v2) and accepts unbonded GATT connections from smartphones and gateways. Because SMP key storage (pairing PINs, LTKs) is not required, flash memory from `0x78000` to `0x7FFFF` (32 KB) is dedicated entirely to user image slot storage.

### 4.1 2D Delta + PackBits Compression Architecture

Standard uncompressed BWR bitmaps require significant flash storage (8,000 bytes on M3Na, 30,000 bytes on XL3Na). However, typical ESL user images are low-density (text, price tags, QR codes, icons).

By applying **2D Vertical Delta (XOR)** followed by standard **PackBits run-length encoding (RLE)**:
- **Vertical Correlation Exploitation:** Pixels along vertical column/raster lines exhibit strong redundancy. Delta encoding computes $D[l, b] = P[l, b] \oplus P[l-1, b]$. Identical pixel runs across consecutive lines turn into long sequences of `0x00` bytes.
- **PackBits Run Compression:** The delta stream is encoded using Apple/TIFF standard PackBits, compressing continuous zero runs into 2-byte tokens (`[1 - count, 0x00]`).
- **Low RAM & CPU Overhead:** On the TLSR8258, decompression occurs line-by-line directly from flash SPI into the display controller in 64-byte bursts, requiring **less than 50 bytes of RAM** and taking $<1\text{ ms}$ of MCU time per frame.

#### Contiguous BWR Packaging (No Sub-Partitioning)
Instead of statically dividing each slot into equal halves for Black/White and Red, each slot stores a contiguous package:
```
┌──────────────────┬─────────────────────────────┬─────────────────────────────┐
│ Header (16 B)    │ Compressed BW Plane         │ Compressed Red Plane        │
│ Magic: 0x5A42    │ (bw_comp_len bytes)         │ (red_comp_len bytes, if BWR)│
└──────────────────┴─────────────────────────────┴─────────────────────────────┘
```
This contiguous format allows complex BWR images where one color plane is dense and the other is sparse to dynamically share the total slot budget (4 KB on M3Na, 8 KB on XL3Na).

### 4.2 Stellar-M3N@ / E31HA Slots (2.13" Display — 8 User Slots, 4 KB Each)

| Slot Index | Display Type | Flash Address | Slot Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Slot 0** | Info Screen | Dynamic RAM / Custom BSS | 4,000 B | System status, live telemetry, battery, MAC, refresh count. |
| **Slots 1..8** | User Images 1..8 | `0x78000` &ndash; `0x7FFFF` | 4 KB each | 1 sector each; contiguous compressed BWR image package (0x78000, 0x79000, ..., 0x7F000). |
| **Slot 9** | Blank Screen | Program Logic | 0 B | Clean white screen (0 bytes flash overhead). |

### 4.3 Stellar-XL3N@ / E31PA Slots (4.2" Display — 4 User Slots, 8 KB Each)

| Slot Index | Display Type | Flash Address | Slot Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Slot 0** | Info Screen | Dynamic RAM / Custom BSS | 15,000 B | High-resolution 400×300 system telemetry & status screen. |
| **Slots 1..4** | User Images 1..4 | `0x78000` &ndash; `0x7FFFF` | 8 KB each | 2 sectors each; contiguous compressed BWR image package (0x78000, 0x7A000, 0x7C000, 0x7E000). |
| **Slot 5** | Blank Screen | Program Logic | 0 B | Clean white screen. |

Hardcoded artwork images have been completely removed from flash code space, saving flash in the executable binary and enabling 100% dynamic user customizability.

---

## 5. Wear-Leveled Settings EEPROM Subsystem (`flash_eep.c`)

The non-volatile settings subsystem manages persistent runtime state across deep retention sleeps and power cycles. It resides at `0x74000` &ndash; `0x75FFF` (8 KB).

### The Challenge of NOR Flash

SPI NOR flash has two key physical properties:
1. **Write Asymmetry:** Programming can only change bits from `1` to `0`. Changing a bit from `0` back to `1` requires erasing an entire 4,096-byte sector.
2. **Endurance Limits:** Each flash sector is rated for approximately 100,000 erase cycles. If the MCU erased the sector every time the user changed the slot or the refresh counter incremented, the flash would wear out prematurely.

### Wear-Leveling Architecture: Two-Sector Log-Append

To solve this, `src/flash_eep.c` implements a **ping-pong wear-leveled log-append** engine across two 4 KB sectors:
- **Sector A:** `0x74000` &ndash; `0x74FFF` (4,096 bytes)
- **Sector B:** `0x75000` &ndash; `0x75FFF` (4,096 bytes)

```
Sector A (0x74000)                                Sector B (0x75000)
┌──────────────────────────────────────────────┐ ┌──────────────────────────────────────────────┐
│ [Record 0: 0xA5][Payload][CRC8]              │ │ 0xFF 0xFF 0xFF 0xFF ...                      │
│ [Record 1: 0xA5][Payload][CRC8]              │ │ (Erased, ready for next compaction)          │
│ [Record 2: 0xA5][Payload][CRC8]              │ │                                              │
│ ...                                          │ │                                              │
│ [Record N: 0xA5][Payload][CRC8] <- ACTIVE    │ │                                              │
│ 0xFF 0xFF 0xFF 0xFF (Unprogrammed space)     │ │                                              │
└──────────────────────────────────────────────┘ └──────────────────────────────────────────────┘
```

### Record Structure

Each record is a compact 8-byte structure:
```c
typedef struct {
    uint8_t magic;                // 0xA5 indicates valid written record; 0xFF is empty flash
    uint8_t active_mode;          // DEVICE_MODE_ZIGBEE (1) or DEVICE_MODE_BLE (2)
    uint8_t active_slot;          // 0 .. 9 (M3Na) or 0 .. 5 (XL3Na)
    uint8_t render_style;         // 0 .. 4
    uint16_t screen_refresh_count;// Persistent lifetime screen refresh counter
    uint8_t reserved;             // Reserved (preserved 16-byte alignment)
    uint8_t crc;                  // CRC-8 checksum over payload bytes
} __attribute__((packed)) eep_record_t;
```

### Read Algorithm (Startup)

1. At boot, `flash_eep_init()` inspects the first byte of Sector A (`0x74000`) and Sector B (`0x75000`).
2. The sector with valid records (`magic == 0xA5`) is designated the active sector.
3. The driver linearly scans forward in increments of `sizeof(eep_record_t)` (8 bytes) until it encounters the first record where `magic == 0xFF` (unwritten flash) or an invalid CRC.
4. The record immediately preceding the unwritten space is the **most recent state**. It is loaded into the in-memory `settings` struct.
5. If both sectors are blank (`0xFF`), default factory settings are applied and written to Record 0 of Sector A.

### Write Algorithm (Log-Append)

1. When settings change (e.g. user selects a new slot or an e-paper refresh increments the counter), `flash_eep_save()` calculates the CRC-8 for the new record.
2. It writes the 8-byte record into the **next empty offset** (`magic == 0xFF`) of the active sector using `flash_write_page()`.
3. **No flash erase occurs.** The write completes in under 1 millisecond.

### Compaction & Garbage Collection

Each 4 KB sector accommodates $\lfloor 4096 / 8 \rfloor = 512$ writes.

When the 512th slot in the active sector is filled:
1. The driver prepares the inactive sector (e.g. Sector B).
2. It writes the latest state as **Record 0** of Sector B.
3. It performs a 4 KB sector erase on the old sector (Sector A).
4. Sector B becomes the active sector, and Sector A becomes the clean standby sector.

### Longevity Analysis

With 512 appends per sector erase and 100,000 rated flash erase cycles:
$$\text{Total Write Operations} = 512 \times 100,000 \times 2 \approx 102,400,000 \text{ writes}$$

Even if the screen is refreshed 100 times per day, the wear-leveled flash will endure for over **2,800 years**, eliminating flash degradation as a point of failure.

---

## 6. Clearing & Erasing Storage Partitions

To wipe persistent configuration, unpair Zigbee networks, or clear user image slots, use either wired SWS flasher commands or wireless runtime triggers.

### 6.1 Hardware SWS Serial Flasher (`python-flasher / TLSR825xComFlasher.py`)

Using the standalone `TLSR825xComFlasher.py` tool (`python-flasher/`):
```bash
python3 TLSR825xComFlasher.py -p <SERIAL_PORT> es <FLASH_ADDRESS> <SIZE_IN_BYTES>
```

| Target Partition | Address | Size | Command | Effect |
| :--- | :--- | :--- | :--- | :--- |
| **Zigbee NVRAM** | `0x30000` | 64 KB (`0x10000`) | `python3 TLSR825xComFlasher.py -p /dev/ttyUSB0 es 0x30000 0x10000` | Clears all Zigbee network pairings, PAN ID, security keys, and binding tables. Restores tag to factory-new state. |
| **Prototype Snippet** | `0x70000` | 16 KB (`0x4000`) | `python3 TLSR825xComFlasher.py -p /dev/ttyUSB0 es 0x70000 0x4000` | Clears dynamic prototype test snippet storage. |
| **Settings EEPROM** | `0x74000` | 8 KB (`0x2000`) | `python3 TLSR825xComFlasher.py -p /dev/ttyUSB0 es 0x74000 0x2000` | Clears wear-leveled settings. On subsequent boot, default settings (Slot 0, Style 0, Mode 1) are re-initialized. |
| **User Image Slots** | `0x78000` | 32 KB (`0x8000`) | `python3 TLSR825xComFlasher.py -p /dev/ttyUSB0 es 0x78000 0x8000` | Clears all user image slots (Slots 1..8 on M3Na / Slots 1..4 on XL3Na). |

> [!CAUTION]
> **DO NOT perform a full chip erase (`ea`) or erase sectors `0x76000` and `0x77000`.**
> - Sector `0x76000` stores the factory-provisioned **IEEE Public MAC Address**.
> - Sector `0x77000` stores the factory **24 MHz Crystal Load Capacitance Calibration Trim**.
> Erasing these sectors will destroy factory RF calibration and permanently assign a randomized fallback MAC.

### 6.2 Wireless Runtime Erase (No Disassembly or SWS Required)

1. **Wipe Zigbee NVRAM over NFC:**
   - Tap the ESL with a smartphone (using NFC Tools or WebNFC) and write text: `"zigbee:reset"` or `"zb:reset"`.
   - The device blinks green 5 times, calls `zb_resetDevice2FN()`, clears network association credentials from `0x30000`, sets the active slot to `0` (Info page), and reboots into Zigbee commissioning mode.
2. **Wipe Zigbee NVRAM via Zigbee Coordinator:**
   - In Zigbee2MQTT / Home Assistant, select the device and trigger **"Remove Device"** or **"Force Remove with Factory Reset"**.
3. **Erase User Image Slots over BLE:**
   - Connect via `web/index.html` or any BLE GATT tool.
   - Send a write to EPD Command Characteristic `0x1315`:
     ```text
     [0x12, <slot_number>]   (e.g., [0x12, 0x02] erases Slot 2; [0x12, 0x03] erases Slot 3)
     ```
   - The firmware calls `epd_erase_slot()` to erase both the 4 KB B&W and 4 KB Red plane flash sectors.
