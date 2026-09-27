# E-Paper User Image Subsystem & 2D Delta + PackBits Compression

This document is the authoritative engineering reference for the user image storage, compression architecture, flash layout, and display streaming pipeline on the **Hanshow Stellar-M3N@ / E31HA** (2.13" 250×122, UC8151) and **Hanshow Stellar-XL3N@ / E31PA** (4.2" 400×300, UC8176) dual-stack electronic shelf labels (Telink TLSR8258).

---

## 1. Overview & Architectural Motivation

### 1.1 The Flash Capacity Challenge
The internal SPI NOR flash of the Telink TLSR8258 is 512 KB (524,288 bytes). It is shared among critical firmware components:
* **Bank 0 (Active Firmware):** 192 KB (`0x00000` &ndash; `0x2FFFF`)
* **Zigbee 3.0 NVRAM:** 64 KB (`0x30000` &ndash; `0x3FFFF`)
* **Bank 1 (OTA Target):** 192 KB (`0x40000` &ndash; `0x6FFFF`)
* **Dynamic Prototype Code Area:** 16 KB (`0x70000` &ndash; `0x73FFF`)
* **Wear-Leveled Settings EEPROM:** 8 KB (`0x74000` &ndash; `0x75FFF`)
* **Factory Calibration (MAC & Trim):** 8 KB (`0x76000` &ndash; `0x77FFF`)
* **User Image Storage:** 32 KB (`0x78000` &ndash; `0x7FFFF`), split across eight 4 KB sectors.

### 1.2 Uncompressed Bit Plane Overhead
In raw uncompressed bitmap format, dual-plane (Black/White and Red) e-paper images require substantial flash memory:
* **2.13" Display (250 × 122):** 4,000 bytes per plane $\times$ 2 = **8,000 bytes** per full BWR image.
* **4.2" Display (400 × 300):** 15,000 bytes per plane $\times$ 2 = **30,000 bytes** per full BWR image.

Without compression:
* The 2.13" Stellar-M3N@ could store at most **6 user images** (each occupying two 4 KB sectors).
* The 4.2" Stellar-XL3N@ could store only **1 dual-color BWR image** (30 KB) and **1 mono image** (15 KB), while fragmenting flash across factory sectors.

### 1.3 The Solution: 2D Delta + PackBits with Contiguous Slots
Because typical electronic shelf label artwork (product names, bold prices, barcodes, icons, and frames) consists of structured vector graphics and solid fills, consecutive scanlines exhibit very strong spatial redundancy.

By combining **2D Vertical Delta (XOR)** with standard **PackBits run-length encoding (RLE)** and storing BWR planes contiguously:
* **2.13" Stellar-M3N@ (E31HA):** Provides **8 user slots** (Slots 1..8, each 4 KB / 1 sector), plus dynamic Info (Slot 0) and Blank Screen (Slot 9) (**10 slots total**).
* **4.2" Stellar-XL3N@ (E31PA):** Provides **4 user slots** (Slots 1..4, each 8 KB / 2 sectors), plus dynamic Info (Slot 0) and Blank Screen (Slot 5) (**6 slots total**).
* **RAM & CPU Overhead:** Decompression is executed on-the-fly line-by-line during hardware SPI streaming, using **less than 50 bytes of RAM** and taking $<1\text{ ms}$ of MCU time per frame.

---

## 2. Flash Slot Partitioning & Memory Mapping

All user image slots reside within the top 32 KB of flash (`0x78000` &ndash; `0x7FFFF`), safely bypassing prototype code storage, wear-leveled settings, and factory calibration partitions.

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

### 2.1 Stellar-M3N@ / E31HA Slot Mapping (2.13" BWR, 250×122)

Each user slot is allocated exactly **one 4 KB flash sector** (`0x1000` = 4,096 bytes):

| Slot Index | Display Type | Flash Address | Sector Count | Slot Budget | Contents |
| :---: | :--- | :---: | :---: | :---: | :--- |
| **Slot 0** | System Info | RAM (`.custom_bss`) | 0 | 4,000 B | Dynamic live telemetry, battery, MAC, refresh count |
| **Slot 1** | User Image 1 | `0x78000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 2** | User Image 2 | `0x79000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 3** | User Image 3 | `0x7A000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 4** | User Image 4 | `0x7B000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 5** | User Image 5 | `0x7C000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 6** | User Image 6 | `0x7D000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 7** | User Image 7 | `0x7E000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 8** | User Image 8 | `0x7F000` | 1 | 4,096 B | Contiguous compressed BWR package |
| **Slot 9** | Blank Screen | Program Logic | 0 | 0 B | Clean white display (0 bytes flash overhead) |

### 2.2 Stellar-XL3N@ / E31PA Slot Mapping (4.2" BWR, 400×300)

Each user slot is allocated **two 4 KB flash sectors** (`0x2000` = 8,192 bytes):

| Slot Index | Display Type | Flash Address | Sector Count | Slot Budget | Contents |
| :---: | :--- | :---: | :---: | :---: | :--- |
| **Slot 0** | System Info | RAM (`.custom_bss`) | 0 | 15,000 B | Dynamic 400×300 live telemetry dashboard |
| **Slot 1** | User Image 1 | `0x78000` | 2 | 8,192 B | Contiguous compressed BWR package |
| **Slot 2** | User Image 2 | `0x7A000` | 2 | 8,192 B | Contiguous compressed BWR package |
| **Slot 3** | User Image 3 | `0x7C000` | 2 | 8,192 B | Contiguous compressed BWR package |
| **Slot 4** | User Image 4 | `0x7E000` | 2 | 8,192 B | Contiguous compressed BWR package |
| **Slot 5** | Blank Screen | Program Logic | 0 | 0 B | Clean white display (0 bytes flash overhead) |

---

## 3. Contiguous BWR Package Architecture

### 3.1 Why Avoid Fixed Sub-Partitioning?
Older firmware implementations statically divided each slot into two equal halves (e.g., 2 KB BW + 2 KB Red on 4 KB slots, or 4 KB BW + 4 KB Red on 8 KB slots).

In real-world e-paper designs:
* Some signs are **predominantly Black/White** with only a tiny red highlight (e.g., a small red discount badge or alert icon). In this scenario, the Red plane compresses to $<100$ bytes, while the BW plane may need 2,200 bytes. A rigid 2 KB sub-partition would reject the image even though total size is only 2,300 bytes ($\ll 4,096$ bytes).
* Storing BW and Red **contiguously in a unified slot** allows the two planes to dynamically share the total slot budget.

### 3.2 Slot Package Format
Every programmed slot begins with a 16-byte metadata header, followed immediately by the compressed bit plane streams:

```
┌────────────────────┬──────────────────────────────┬──────────────────────────────┐
│ Header (16 bytes)  │ Compressed BW Plane Stream   │ Compressed Red Plane Stream  │
│ Magic: 0x5A42      │ Length: bw_comp_len bytes    │ Length: red_comp_len bytes   │
└────────────────────┴──────────────────────────────┴──────────────────────────────┘
```

### 3.3 Header Structure Definition (`src/epd/epd_slots.h`)

```c
#define EPD_COMP_MAGIC 0x5A42 // ASCII 'ZB' in little-endian

typedef struct __attribute__((packed)) {
    uint16_t magic;         // 0x5A42 (EPD_COMP_MAGIC)
    uint8_t  version;       // Header version (currently 1)
    uint8_t  flags;         // Bit 0: has_red_plane (1 = BWR 3-Color, 0 = BW Mono)
    uint16_t width;         // Display width in pixels (250 or 400)
    uint16_t height;        // Display height in pixels (122 or 300)
    uint16_t bw_comp_len;   // Byte length of compressed BW plane stream
    uint16_t red_comp_len;  // Byte length of compressed Red plane stream (0 if mono)
    uint16_t reserved;      // Reserved for future alignment / CRC (0x0000)
    uint16_t checksum;      // Header checksum (0x0000)
} epd_slot_header_t;
```

#### Field Specifications
| Offset | Field | Type | Description |
| :---: | :--- | :--- | :--- |
| `0..1` | `magic` | `uint16_t` | Identifies valid compressed slot (`0x5A42`). Unprogrammed flash reads `0xFFFF`. |
| `2` | `version` | `uint8_t` | Compression envelope version (`1`). |
| `3` | `flags` | `uint8_t` | Bit 0: `has_red_plane`. If `0`, DTM2 (Red) is omitted and simulated blank. |
| `4..5` | `width` | `uint16_t` | Image width (`250` for M3Na, `400` for XL3Na). |
| `6..7` | `height` | `uint16_t` | Image height (`122` for M3Na, `300` for XL3Na). |
| `8..9` | `bw_comp_len` | `uint16_t` | Number of bytes in the compressed BW PackBits payload. |
| `10..11` | `red_comp_len` | `uint16_t` | Number of bytes in the compressed Red PackBits payload (0 if `flags & 1 == 0`). |
| `12..13` | `reserved` | `uint16_t` | Reserved (`0x0000`). |
| `14..15` | `checksum` | `uint16_t` | Reserved (`0x0000`). |

---

## 4. Compression Algorithm: 2D Delta + PackBits

The compression pipeline consists of three sequential transformations:
1. **Physical Plane Coordinate Mapping**
2. **2D Delta Transformation (Vertical XOR)**
3. **PackBits Run-Length Encoding**

```
[ Canvas (x, y) ] 
       │
       ▼ (canvasToPlanes)
[ Native Plane Vectors (line_bytes × total_lines) ]
       │
       ▼ (Vertical Delta XOR: Line[k] ^ Line[k-1])
[ Delta Byte Stream ]
       │
       ▼ (PackBits RLE)
[ Compressed Bitstream ]
```

### 4.1 Native Display Plane Coordinate Mapping
Before compression, pixels $(x, y)$ must be formatted into the native SRAM coordinate layout of the physical display controller:

#### Stellar-M3N@ (2.13", UC8151)
The UC8151 controller scans the panel **by vertical columns**:
* Total scan vectors (`total_lines`): **250 columns**.
* Bytes per vector (`line_bytes`): **16 bytes** (128 bits vertical, with bits 0..121 active and bits 122..127 padded to `1`).
* Mapping equation:
  $$\text{col} = 249 - x, \quad \text{byte\_idx} = \text{col} \times 16 + \lfloor y / 8 \rfloor, \quad \text{bit\_mask} = 1 \ll (7 - (y \bmod 8))$$

#### Stellar-XL3N@ (4.2", UC8176)
The UC8176 controller scans the panel **by horizontal raster lines**:
* Total scan vectors (`total_lines`): **300 lines**.
* Bytes per vector (`line_bytes`): **50 bytes** (400 bits horizontal, MSB on the left).
* Mapping equation:
  $$\text{byte\_idx} = y \times 50 + \lfloor x / 8 \rfloor, \quad \text{bit\_mask} = 0x80 \gg (x \bmod 8)$$

### 4.2 2D Delta Transformation (Vertical XOR)
Because adjacent scan vectors $l$ and $l-1$ share significant visual structure (vertical strokes of letters, barcode bars, solid borders, background fills), computing the XOR difference between consecutive vectors eliminates repeated patterns:

$$\Delta[0, b] = P[0, b] \quad \text{for } b \in [0, \text{line\_bytes}-1]$$
$$\Delta[l, b] = P[l, b] \oplus P[l-1, b] \quad \text{for } l \ge 1, \, b \in [0, \text{line\_bytes}-1]$$

Where:
* $P[l, b]$ is byte $b$ of native scanline $l$.
* Whenever a vertical pixel pattern remains unchanged from line $l-1$ to line $l$, $\Delta[l, b] = 0x00$.
* This converts large 2D visual areas into **long contiguous streams of zero bytes (`0x00`)**.

### 4.3 PackBits Byte-Level Encoding
The delta byte stream is compressed using standard TIFF / Apple PackBits run-length encoding. The encoder scans the input stream and emits control-prefixed chunks:

* **Literal Run (non-repeating bytes):**
  * Control byte: $0 \le n \le 127$
  * Followed by: $n + 1$ literal data bytes (1 to 128 bytes).
* **Repeat Run (identical repeating bytes):**
  * Control byte: $-127 \le n \le -1$ (stored as unsigned byte $257 - \text{count}$ or $(1 - \text{count}) \ \& \ 0\text{xFF}$)
  * Followed by: Exactly 1 data byte, representing a repetition of $1 - n$ times (2 to 128 bytes).
  * Control byte `-128` (`0x80`) is a NOP and unused.

#### Example Encoding
* Delta zero run of 64 bytes (`0x00 ... 0x00`):
  $$\text{Header} = (1 - 64) \ \& \ 0\text{xFF} = -63 \ \& \ 0\text{xFF} = \mathbf{0xC1}$$
  $$\text{Encoded bytes: } [\mathbf{0xC1}, \mathbf{0x00}] \quad \text{(64 bytes compressed into 2 bytes, 32:1 ratio)}$$
* An entire uncompressed 4,000-byte plane of solid color compresses to **just 64 bytes** ($31 \times 128 + 32$ byte runs).

---

## 5. Streaming Decompression Engine in Firmware

Traditional decompression algorithms (e.g. zlib, LZMA, CCITT G4) require multi-kilobyte window buffers and intermediate plane allocations. The TLSR8258 has only 64 KB total SRAM, of which ~50 KB is consumed by the dual Zigbee 3.0 + BLE stack.

The production firmware (`src/epd/epd.c`) implements a **zero-window streaming decompressor**:

```
Flash SPI (0x7xxxx)
       │ (reads in 64-byte burst buffer)
       ▼
packbits_flash_stream_t
       │ (decodes PackBits tokens byte-by-byte)
       ▼
delta2d_line_decoder_t
       │ (XOR with previous line: Line[k] = Delta ^ PrevLine)
       ▼
dst_line[EPD_LINE_BYTES] (16 B or 50 B stack buffer)
       │
       ▼ (SPI transmit: DTM1 / DTM2)
E-Paper Controller SRAM (UC8151 / UC8176)
```

### 5.1 Decompressor State Structures

```c
typedef struct {
    uint32_t flash_addr;    // Base address in SPI flash
    uint16_t stream_len;    // Total compressed stream length
    uint16_t stream_idx;    // Current read offset in flash
    uint8_t  buf[64];       // Small 64-byte burst read cache
    uint8_t  buf_idx;       // Index within burst cache
    uint8_t  buf_len;       // Bytes remaining in burst cache
    uint8_t  run_rem;       // Remaining bytes in current PackBits run
    uint8_t  run_val;       // Value for repeat run
    bool     is_repeat;     // True if repeat run, false if literal run
} packbits_flash_stream_t;

typedef struct {
    packbits_flash_stream_t stream;
    uint8_t  prev_line[EPD_LINE_BYTES]; // 16 B (M3Na) or 50 B (XL3Na)
    uint16_t line_bytes;                // 16 or 50
    uint16_t total_lines;               // 250 or 300
    uint16_t current_line;              // 0..total_lines-1
} delta2d_line_decoder_t;
```

### 5.2 Line-by-Line Streaming Execution
When refreshing a slot in `epd_display_slot()`:
1. `delta2d_decoder_init()` initializes the decoder with the flash address of the compressed plane.
2. The display driver loops $0 \dots \text{total\_lines}-1$:
   * Calls `delta2d_read_line(&dec, line_buffer)`.
   * The decompressor fills `line_buffer` by consuming PackBits tokens from flash, then XORs `line_buffer` with `prev_line`.
   * It stores `line_buffer` into `prev_line` for the next line's reference.
   * `line_buffer` is transmitted directly to the display via SPI (`EPD_SPI_Write()`).
3. Total RAM overhead:
   * **Stellar-M3N@:** 64 B (burst cache) + 16 B (`prev_line`) + state $\approx$ **92 bytes SRAM**.
   * **Stellar-XL3N@:** 64 B (burst cache) + 50 B (`prev_line`) + state $\approx$ **126 bytes SRAM**.

---

## 6. Empirical Benchmarks & Compression Ratios

Compression efficiency was benchmarked against real images and standard ESL layouts.

### 6.1 Benchmark Results Table

| Test Image / Content Type | Target Model | Raw Size | Compressed Size | Compression Ratio | Space Reduction |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Occupied Sign** (`toilet_occupied_250x122_1bit.bmp`) | M3Na (250×122) | 4,000 B | **1,350 B** | **2.96 : 1** | **66.3%** |
| **Retail Shelf Tag** (price, barcode, banner, border) | M3Na (250×122) | 4,000 B | **484 B** | **8.26 : 1** | **87.9%** |
| **Simple QR Code & URL Text** | M3Na (250×122) | 4,000 B | **780 B** | **5.13 : 1** | **80.5%** |
| **Solid / Blank Plane** (e.g. absent Red plane) | M3Na (250×122) | 4,000 B | **64 B** | **62.50 : 1** | **98.4%** |
| **Retail Price Tag** (large bold price, barcode) | XL3Na (400×300) | 15,000 B | **276 B** | **54.35 : 1** | **98.2%** |
| **Detailed Warehouse Tag** (tables, text, 2 barcodes) | XL3Na (400×300) | 15,000 B | **1,613 B** | **9.30 : 1** | **89.2%** |
| **Solid / Blank Plane** | XL3Na (400×300) | 15,000 B | **236 B** | **63.56 : 1** | **98.4%** |
| **Continuous-Tone Photo** (Floyd-Steinberg dither) | M3Na (250×122) | 4,000 B | **3,920 B** | **1.02 : 1** | **2.0%** |

### 6.2 Total Slot Package Sizes (Header + BW + Red)

| Real-World Scenario | Model | Slot Capacity | Total Package | Slot Utilization |
| :--- | :---: | :---: | :---: | :---: |
| **Black/White Price Tag** (no red) | M3Na | 4,096 B | $16 + 484 = \mathbf{500\text{ B}}$ | **12.2%** |
| **BWR Sale Tag** (BW artwork + red price highlight) | M3Na | 4,096 B | $16 + 520 + 260 = \mathbf{796\text{ B}}$ | **19.4%** |
| **Complex Signage** (`toilet_occupied` BWR) | M3Na | 4,096 B | $16 + 1,350 + 420 = \mathbf{1,786\text{ B}}$ | **43.6%** |
| **XL3Na Large Retail Tag** (BW + Red banner) | XL3Na | 8,192 B | $16 + 276 + 180 = \mathbf{472\text{ B}}$ | **5.8%** |
| **XL3Na Full Graphic BWR Dashboard** | XL3Na | 8,192 B | $16 + 1,850 + 920 = \mathbf{2,786\text{ B}}$ | **34.0%** |

In all standard ESL use cases, total package size remains **well below 50% of the slot limit**.

---

## 7. Image Optimization Guidelines for E-Paper

Because 2D Delta + PackBits relies on spatial correlation, image design directly impacts compression:

### 7.1 Best Practices (High Compression: 3:1 to 50:1)
* **Use Clean Vector Graphics & Solid Fills:** Crisp edges and solid blocks of black, white, or red produce long runs of `0x00` in delta space.
* **Align Elements to Grid Lines:** Horizontal and vertical divider lines compress exceptionally well.
* **Use Threshold Dithering Rather than Error Diffusion:**
  * When preparing images from grayscale, use **Threshold / Posterization** or coarse **Bayer Ordered Dithering** instead of Floyd-Steinberg error diffusion.
* **Use Native Resolutions:**
  * Stellar-M3N@: Exactly **250 × 122** pixels.
  * Stellar-XL3N@: Exactly **400 × 300** pixels.

### 7.2 What to Avoid (Poor Compression: ~1:1)
* **Stochastic Error-Diffusion Dithering (Floyd-Steinberg):**
  * Error diffusion produces high-frequency, alternating pixel noise. Adjacent lines have almost zero matching bits, causing 2D Delta XOR to emit pseudo-random data that PackBits cannot compress.
* **Unfiltered Photos / Camera Snapshots:**
  * Grayscale photos should have backgrounds removed or replaced with solid white before uploading.

---

## 8. BLE Upload & Fetch Protocol Reference

All slot management can be performed wirelessly over Web Bluetooth using [`web/index.html`](../web/index.html).

### 8.1 BLE Slot Upload Protocol

```
Web Browser (web/index.html)                          TLSR8258 Tag
     │                                                     │
     │ ── 1. CMD_PREPARE: [0x14, slot, 0] ───────────────>│ (Erases slot sector(s))
     │                                                     │ (Sets 10ms BLE interval)
     │                                                     │
     │ ── 2. Pipelined 16-Byte Blocks (epdDataChar) ─────>│
     │       [b_lo, b_hi, 16_bytes, crc_lo, crc_hi]       │ (Buffered into RAM s_ota_cache, zero flash latency)
     │                                                     │
     │ <── 3. Flow Sync Telemetry (every 16 blocks) ──────│ (Reads confirmed_offset & error_code)
     │       [confirmed_off_lo, confirmed_off_hi, err]    │
     │                                                     │
     │ ── 4. CMD_COMMIT: [0x15, auto_display, has_red] ──>│ (Flushes RAM cache to flash in page writes,
     │                                                     │  updates active_slot in EEPROM, and renders)
```

1. **Prepare Upload:** Send `[0x14, slot, 0]` to `UUID_EPD_CMD` (`0x1315`).
   * Firmware erases 4 KB (M3Na) or 8 KB (XL3Na) starting at `epd_get_slot_flash_address(slot)`.
   * BLE connection interval drops to 10 ms (0 latency) for high throughput. Client waits ~1600ms for flash erase and L2CAP negotiation to settle.
2. **Stream Package:** Send 20-byte packets to `UUID_EPD_DATA` (`0x1316`):
   * Bytes 0..1: Block sequence number `b` (little-endian).
   * Bytes 2..17: 16-byte payload chunk.
   * Bytes 18..19: Modbus CRC-16 of bytes 0..17.
   * Chunks are buffered directly in RAM (`s_ota_cache`) without disabling interrupts.
   * Central supports two modes: **Turbo Stream** (pipelined `writeValueWithoutResponse` with flow sync barriers) and **Safe Mode** (per-packet ATT acknowledged `writeValue` with zero packet loss).
3. **Flow Control:** In Turbo mode, every 16 blocks (and on the final block), the client reads `UUID_EPD_DATA` to confirm `confirmed_offset` and verify `error_code == 0`. If a sync mismatch is detected, the client rewinds to `confirmed_offset / 16` and automatically falls back to Safe Mode.
4. **Commit Upload:** Send `[0x15, auto_display, has_red]` to `UUID_EPD_CMD`.
   * Firmware flushes `s_ota_cache` from RAM into SPI flash via fast 256-byte page writes (`flash_write_page`), commits `active_slot` to wear-leveled EEPROM, restores 50ms connection interval, and initiates display refresh if `auto_display == 1`.

### 8.2 BLE Slot Fetch Protocol

1. **Request Stream:** Send `[0x16, slot, 0]` to `UUID_EPD_CMD`.
2. **Autonomous Stream Notifications:** Firmware reads the slot's 16-byte header, determines `total_fetch_size = 16 + bw_comp_len + red_comp_len`, and emits GATT notifications on `UUID_EPD_CMD`:
   * Byte 0: `0x86` (Chunk marker)
   * Byte 1: Slot index
   * Byte 2: Plane index (`0`)
   * Bytes 3..4: Offset (little-endian)
   * Byte 5: Chunk length $L$ ($1 \dots 20$)
   * Bytes 6..$6+L$: Chunk payload
3. **End of File (EOF):** Firmware emits `[0x86, slot, 0, total_lo, total_hi, 0]` (length 0).
4. **Decompression:** Client checks magic `0x5A42`, extracts compressed BW and Red streams, decompresses them with `decompressPlane2DDelta()`, and renders to canvas.

---

## 9. Remote Slot Switching via NFC and Zigbee

Once images are uploaded to flash slots, slots can be displayed without computer or BLE connectivity:

### 9.1 Passive NFC Commands (FM11NC08)
Touch any NFC-enabled smartphone (using NFC Tools or WebNFC) to the tag:
* `s0` .. `s9`: Switch to Slot 0 through Slot 9 (M3Na).
* `s0` .. `s5`: Switch to Slot 0 through Slot 5 (XL3Na).
* `s2 rw`: Switch to Slot 2 and apply Style 3 (Red & White).
* `2,3`: Switch to Slot 2, Style 3.

### 9.2 Zigbee 3.0 / Zigbee2MQTT
In Zigbee2MQTT, select the tag device:
* Set `active_slot` to any slot (0..9 on M3Na, 0..5 on XL3Na).
* Set `render_style` to any of the 5 runtime styles (`0`..`4`).
* On the next 2.0s poll interval, the tag downloads the attribute, commits it to wear-leveled flash, and refreshes the screen.
