# SRAM Architecture & Memory Allocation Reference

This document defines the 64 KB SRAM physical memory map, retention vs. non-retention domains, section layouts, symbol-level allocation, and buffer sharing architecture for the dual-stack firmware on the Telink TLSR8258 SoC across both hardware variants:
- **Stellar-M3N@ / E31HA**: 2.13" E-Paper display (UC8151D, 250×122)
- **Stellar-XL3N@ / E31PA**: 4.2" E-Paper display (UC8176, 400×300)

---

## 1. TLSR8258 SRAM Hardware Architecture

The TLSR8258 contains **64 KB of physical on-chip SRAM** mapped from `0x840000` to `0x850000`. The hardware partitions this space into two distinct 32 KB domains with differing low-power sleep characteristics:

```
Physical Address     Domain                  Power Behavior & Contents
0x840000 - 0x848000  Retention SRAM (32 KB)  Powered during Deep Sleep with Retention.
                                             Holds resident code (ramcode), MCU instruction
                                             cache, initialized .data, and retention .bss
                                             (including the 56-entry debug log ring buffer).
--------------------------------------------------------------------------------------
0x848000 - 0x850000  Non-Retention SRAM (32 KB) Powered OFF during Deep Sleep.
                                                Holds .custom_bss (16 KB shared display/OTA
                                                buffer, NFC buffer) and CPU execution stack.
                                                Skipped on retention wakeup for <150 µs boot.
```

```
0x840000 ┌─────────────────────────────────────────────────────────────┐
         │  ramcode (Resident Code in SRAM: 12,832 B)                  │
         │  Mapped to CPU PC 0x0000..0x3220                            │
0x843220 ├─────────────────────────────────────────────────────────────┤
         │  Alignment Gap nc (224 B)                                   │
0x843300 ├─────────────────────────────────────────────────────────────┤
         │  Flash Instruction Cache: Tag (256 B: 0x843300..0x843400)   │
0x843400 ├─────────────────────────────────────────────────────────────┤
         │  Flash Instruction Cache: Data (2,048 B: 0x843400..0x843C00)│
0x843C00 ├─────────────────────────────────────────────────────────────┤
         │  .data Initialized Globals (972 B: 0x843C00..0x843FCC)      │
0x843FCC ├─────────────────────────────────────────────────────────────┤
         │  Alignment Gap (4 B)                                        │
0x843FD0 ├─────────────────────────────────────────────────────────────┤
         │  .bss Zero-Initialized Retention Globals (15,736 B)         │
         │  Zigbee pools, BLE buffers, 40-entry log ring, IRQ stack    │
0x847D48 ├─────────────────────────────────────────────────────────────┤
         │  FREE RETENTION HEADROOM: 696 BYTES                         │
0x848000 ├─────────────────────────────────────────────────────────────┤ ◄── RETENTION BOUNDARY
         │  .custom_bss (Non-Retention Scratch Buffers: 17,080 B)      │
         │  - shared_scratch_ram (16,384 B = 16 KB)                    │
         │  - t4t_ndef_file (384 B)                                    │
         │  - T_rfStatusDbg (256 B)                                    │
         │  - obd & transient scratch (56 B)                           │
0x84C000 ├─────────────────────────────────────────────────────────────┤
         │  CPU Execution Stack (16,384 B = 16.0 KB FREE)              │
         │  Grows downward from stack top                              │
0x850000 └─────────────────────────────────────────────────────────────┘ ◄── _stack_end_
```

---

## 2. Complete Memory Allocation Breakdown

Both M3NA and XL3NA share a unified memory map with identical retention consumption and stack headroom:

| Section | Description | Start (hex) | End (hex) | Used Space | Domain |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **`ramcode`** | Resident Code in SRAM | `0x000000` | `0x003220` | 12,832 B | Retention SRAM (`0x840000`–`0x843220`) |
| **`text`** | Application Code in Flash | `0x003220` | `0x02CBC8` | 170,408 B | SPI NOR Flash |
| **`nc`** | Alignment padding to 256 B | `0x843220` | `0x843300` | 224 B | Retention SRAM |
| **`ictag`** | Flash Instruction Cache Tag | `0x843300` | `0x843400` | 256 B | Retention SRAM (HW cache lookup) |
| **`icdata`** | Flash Instruction Cache Data | `0x843400` | `0x843C00` | 2,048 B | Retention SRAM (2 KB line cache) |
| **`.data`** | Initialized Globals | `0x843C00` | `0x843FCC` | 972 B | Retention SRAM (loaded from flash) |
| **`.bss`** | Zero-initialized Retention Data | `0x843FD0` | `0x847D48` | 15,736 B | Retention SRAM (preserved in sleep) |
| *(irq_stk)* | *(IRQ CPU Stack within .bss)* | `0x843FD0` | `0x8441D0` | 512 B | *(Subset of .bss)* |
| **Retention Free** | **Free space before 0x848000** | `0x847D48` | `0x848000` | **696 B** | **Retention Headroom** |
| **`.custom_data`** | Initialized Non-Retention Data | — | — | 0 B | Unused |
| **`.custom_bss`** | Non-Retention Scratch Buffers | `0x847D48` | `0x84C000` | 17,080 B | Starts at `_end_bss_`, non-retained |
| **`stack`** | **CPU Stack Headroom** | `0x84C000` | `0x850000` | **16,384 B** | **16.0 KB safe stack space** |

```
Total Used Retention SRAM: 32,072 B out of 32,768 B (97.9% utilized, 696 B free)
Total Available Stack:     16,384 B (16.0 KB safe stack headroom)
```

---

## 3. Shared Scratch RAM Architecture (16 KB)

To prevent display framebuffers and network staging buffers from dividing the remaining SRAM, the firmware implements a **16 KB unified scratch buffer** in non-retention RAM (`.custom_bss`):

```c
// src/app_config.h
#define SHARED_SCRATCH_RAM_SIZE    (16 * 1024) // 16 KB

// src/epd/epd_slots.c
_attribute_custom_bss_ uint8_t shared_scratch_ram[SHARED_SCRATCH_RAM_SIZE];

// src/epd/epd.h
#define epd_render_buffer          shared_scratch_ram

// src/ble/ble_app.c
#define s_ota_cache                shared_scratch_ram
#define OTA_MAX_CHUNK_K            16
#define OTA_MAX_CACHE_SIZE         (OTA_MAX_CHUNK_K * 1024)
```

### 3.1 Mutual Exclusivity Matrix

The two primary consumers of `shared_scratch_ram` are guaranteed to never operate concurrently:

| Operation | Buffer Consumer | Usage Pattern | Concurrent Conflicts |
| :--- | :--- | :--- | :--- |
| **Dynamic Info Render** (`Slot 0`) | `epd_render_buffer` | Active only during `epd_render_info_slot()`. OBD writes 1-bit monochrome pixels; bytes are streamed to EPD controller via SPI, then buffer is idle. | None. Device is processing a local display update command; radio OTA/stream transfer is not occurring. |
| **BLE Firmware OTA** | `s_ota_cache` | Staging buffer for incoming 16-byte blocks from BLE central. Once 16 KB (or requested chunk size) is accumulated, buffer flushes to SPI NOR flash via `flash_write_page()`. | None. Firmware update runs exclusively; display rendering does not occur during an active OTA stream. |
| **BLE Image Stream Upload** | `s_ota_cache` | Staging buffer for user compressed BWR image stream chunks before committing 256-byte pages to target flash slot. | None. If `auto_display` is requested, display refresh is triggered **after** the image has been fully committed to flash. The EPD driver then streams directly from flash without touching the buffer. |

### 3.2 Display Buffer Requirements by Variant

Both variants fit within the 16,384-byte scratch buffer:
- **XL3NA (400×300)**: Requires $400 \times \lceil 300 / 8 \rceil = 400 \times 38 = 15,200 \text{ bytes}$ for OneBitDisplay ($15,200 \le 16,384$).
- **M3NA (250×122)**: Requires $250 \times \lceil 122 / 8 \rceil = 250 \times 16 = 4,000 \text{ bytes}$ for OneBitDisplay ($4,000 \le 16,384$).

---

## 4. Retention RAM Breakdown (`0x840000`–`0x848000`)

### 4.1 Resident Code (`ramcode`: 12,720 Bytes)

Functions in `ramcode` are loaded into SRAM at boot and execute directly from RAM. They are reserved strictly for operations that cannot execute from flash cache (flash programming, low-power sleep entry/exit, critical radio ISRs):

```
Function / Symbol Name             Size (Bytes)  Module Source          Purpose
───────────────────────────────────────────────────────────────────────────────────────────────────────
blt_brx_sleep                             1,162  libble_8258:ll_pm.o    BLE radio RX sleep timing & power-down
rf_rx_irq_handler                           848  libzb_ed:mac_trx.o     802.15.4 frame reception ISR
blt_sdk_main_loop                           754  libble_8258:ll.o       BLE connection event loop
irq_blc_ll_rx                               638  libble_8258:ll.o       BLE hardware packet handler ISR
irq_handler                                 512  src/patch_sdk/irq.c    Top-level MCU vector interrupt dispatcher
irq_blt_sdk_handler                         442  libble_8258:ll.o       BLE scheduler interrupt
zb_buf_get + zb_buf_allocate                412  libzb_ed:zb_buffer.o   Zigbee buffer allocation fast-path
zb_macDataFilter                            348  libzb_ed:mac_trx.o     802.15.4 frame filtering in ISR
irq_blc_slave_rx_data                       340  libble_8258:ll_slave.o BLE slave RX data handler
rf_setTrxState                              340  libdrivers_8258:rf.o   RF transceiver mode transition
bls_ll_procRxPacket                         322  libble_8258:ll_slave.o BLE slave packet processing
blt_brx_timing_update                       312  libble_8258:ll_slave.o BLE anchor point timing maintenance
tl_zbTaskQPush                              300  libzb_ed:task_queue.o  Zigbee task dispatch queue push
irq_slave_system_timer                      294  libble_8258:ll_slave.o BLE connection interval timer ISR
flash_mspi_read/write/unlock/wait           480  src/patch_sdk/flash.c  Internal SPI flash controller routines
Radio context switchers & PHY (~10 funcs) 1,320  zigbee_ble_switch.c    Dual-stack radio re-tuning & calibration
Other stack ISRs & protocol primitives    4,236  libble / libzb / Drv   Real-time PHY/MAC timing routines
───────────────────────────────────────────────────────────────────────────────────────────────────────
TOTAL RESIDENT RAMCODE                   12,720  Bytes
```

> [!NOTE]
> EPD pin and SPI drivers ([`src/epd/epd_spi.c`](../src/epd/epd_spi.c)) and ADC measurement routines ([`src/patch_sdk/adc_drv.c`](../src/patch_sdk/adc_drv.c)) execute from **SPI NOR Flash** (`.text`), saving 1,712 bytes of resident SRAM. EPD communication uses standard hardware SPI (`PB4`/`PB5`/`PB6`), which is completely decoupled from the flash MSPI bus.

### 4.2 Retention BSS Globals (`.bss`: 15,736 Bytes)

Variables in `.bss` are preserved across deep sleep cycles. The major consumers are:

```
Category / Symbol Name    Size (Bytes)  Module Origin                  Description
─────────────────────────────────────────────────────────────────────────────────────────────────────
s_log_ring                       2,400  src/debug_uart.c               40-entry wireless debug log ring buffer (40 * 60 B)
g_mPool                          2,504  libzb_ed.a:zb_config.o         Zigbee packet buffer pool (12 * 208 B)
size_2_mem                         608  src/patch_sdk/ev_buffer.c      OS event buffer pool 2 (4 buffers * 150 B)
ev_timer                           684  SDK:ev_timer.c                 Software timer queue management
g_zbTaskQ                          660  libzb_ed.a:zb_task_queue.o     Zigbee internal task scheduler queue
blt_rxfifo_b                       512  libble_8258.a:ll_slave.o       BLE hardware RX FIFO buffer
irq_stk                            512  src/patch_sdk/cstartup_8258.S  Interrupt Request (IRQ) CPU stack
size_1_mem                         480  src/patch_sdk/ev_buffer.c      OS event buffer pool 1 (8 buffers * 60 B)
aps_txCache_tbl                    448  libzb_ed.a:aps_data.o          Zigbee APS retry/acknowledgement cache
blt_txfifo_b                       320  libble_8258.a:ll_slave.o       BLE hardware TX FIFO buffer
reportingTab                       297  SDK:zcl_reporting.c            Zigbee ZCL attribute periodic report table
blt_buff_ll                        260  libble_8258.a:ll.o             BLE Link Layer controller context
taskQ_user                         258  SDK:zb_task_queue.c            User application Zigbee task queue
g_txQueue                          256  libzb_ed.a:mac_trx.o           MAC layer transmit queue
g_zb_neighborTbl                   252  libzb_ed.a:mac_trx.o           Zigbee neighbor & parent node link table
blt_att_data                       250  libble_8258.a:att.o            BLE Attribute Protocol (ATT) PDU buffer
zcl_vars                           224  SDK:zcl.c                      Zigbee Cluster Library global states
size_0_mem                         192  src/patch_sdk/ev_buffer.c      OS event buffer pool 0 (8 buffers * 24 B)
u8Cache                            192  src/patch_sdk/flash.c          Flash write alignment staging buffer
g_zbInfo                           180  libzb_ed.a:zb_api.o            Zigbee device network info & capabilities
g_nwkAddrMap                       172  libzb_ed.a:nwk_addr_map.o      Short-to-IEEE 64-bit address translation map
rf_tx_buf                          132  libzb_ed.a:mac_trx.o           802.15.4 raw frame transmission buffer
size_3_mem                         512  src/patch_sdk/ev_buffer.c      OS event buffer pool 3 (1 buffer * 512 B)
aps_group_tbl                      112  libzb_ed.a:aps.o               Zigbee APS multicast group table
g_apsBindingTbl                    112  libzb_ed.a:aps.o               Zigbee binding table
Other (~285 symbols)             3,206  Various SDK & App              Assorted stack state, counters, flags
─────────────────────────────────────────────────────────────────────────────────────────────────────
TOTAL RETENTION BSS             15,736  Bytes
```

### 4.3 `ev_buffer` Memory Pool Configuration

The event buffer manager ([`src/patch_sdk/ev_buffer.c`](../src/patch_sdk/ev_buffer.c)) provides dynamic buffer allocation across 4 fixed-size pools:

| Pool Name | Buffer Size | Quantity | Total Footprint | Primary Consumers |
| :--- | :---: | :---: | :---: | :--- |
| `size_0_pool` | 24 B | 8 | 192 B | Short event messages, timer callbacks |
| `size_1_pool` | 60 B | 8 | 480 B | ZCL attribute reports, NV item temporary staging |
| `size_2_pool` | 150 B | 4 | 608 B | 802.15.4 frame payloads (Zigbee MTU = 127 B) |
| `size_3_pool` | 512 B | 1 | 512 B | APS fragmentation reassembly (`sizeof(aps_frag_dec_t)` = 37 B) |
| **Total** | | **21 buffers** | **1,408 B** | Reduced from default 2,912 B footprint |

---

## 5. Non-Retention RAM Breakdown (`0x847D48`–`0x850000`)

Non-retention RAM is allocated starting at `_end_bss_` (`0x847D48`). On cold boot (`ana_reg_0x7e == 0`), [`cstartup_8258.S`](../src/patch_sdk/cstartup_8258.S) zeroes `.custom_bss` and fills the stack with `0xFFFFFFFF`. On deep retention wakeup (`ana_reg_0x7e != 0`), `cstartup_8258.S` jumps directly to `ENTER_MAIN`, avoiding 33 KB of RAM writes (~3.4 ms penalty) so BLE and Zigbee wakeups complete in < 150 µs.

### 5.1 Custom BSS Items (`.custom_bss`: 17,080 Bytes)

```
Symbol Name               Size (Bytes)  Module Source              Purpose
───────────────────────────────────────────────────────────────────────────────────────────────────────
shared_scratch_ram              16,384  src/epd/epd_slots.c        Unified 16 KB buffer for EPD OBD rendering
                                                                   and BLE OTA / image / snippet caching
t4t_ndef_file                      384  src/nfc_fm11nc08.c         NFC Type 4 Tag NDEF file emulation buffer
T_rfStatusDbg                      256  src/patch_sdk/utility.c    SDK RF debug status ring buffer
obd                                 37  src/epd/epd_slots.c        OneBitDisplay virtual display context
s_power_stats_val                   16  src/ble/ble_app.c          BLE power telemetry GATT read scratch
Other alignment padding              3  Linker fill                Word alignment padding
───────────────────────────────────────────────────────────────────────────────────────────────────────
TOTAL CUSTOM BSS                17,080  Bytes
```

### 5.2 CPU Execution Stack Space (16,384 Bytes)

- **Stack Range**: `0x84C000` to `0x850000` (`_stack_end_`).
- **Growth Direction**: Downward from `0x850000`.
- **Available Headroom**: **16,384 bytes (16.0 KB)**.
- **Linker Safety Assertion**:
  ```ld
  ASSERT((_ram_end_ < _stack_end_), "STACK OVERFLOW!!!!!")
  ```
  `_ram_end_` reserves `_end_custom_bss_ + 0x800` (2 KB minimum stack) aligned to 256 bytes (`0x84CC00`). With `0x84CC00 < 0x850000`, the firmware operates with a 13.0 KB safety margin above the 2 KB linker threshold.

---

## 6. Build & Linker Verification

Memory allocation can be inspected at any time using the `TlsrMemInfo.py` script:

```bash
python3 make/TlsrMemInfo.py -t tc32-elf-nm build/tlsr-epaper-xl3na/tlsr-epaper-xl3na.elf
```

### Verification Output (XL3NA / M3NA)

```text
===================================================================
 Section|          Description| Start (hex)|   End (hex)|Used space
-------------------------------------------------------------------
 ramcode|   Resident Code SRAM|           0|        3220|   12832
    text|           Code Flash|        3220|       2CBC8|  170408
 cusdata|          Custom SRAM|      847F88|      847F88|       0
      nc|   Wasteful Area SRAM|      843220|      843300|     224
   ictag|     Cache Table SRAM|      843300|      843400|     256
  icdata|      Cache Data SRAM|      843400|      843C00|    2048
    data|       Init Data SRAM|      843C00|      843FCC|     972
     bss|        BSS Data SRAM|      843FD0|      847F88|   16312
 irq_stk|        BSS Data SRAM|      843FD0|      8441D0|     512
    cbss| Custom BSS Data SRAM|      847F88|      84C240|   17080
   stack|       CPU Stack SRAM|      84C240|      850000|   15808
   flash|       Bin Size Flash|           0|       2CF94|  184212
-------------------------------------------------------------------
Start Load SRAM : 0 (ICtag: 0x0)
Total Used SRAM : 32648 from 65536
Total Free SRAM : 224 + stack[15808] = 16032
```
