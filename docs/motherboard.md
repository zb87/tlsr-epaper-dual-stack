# Motherboard Hardware, Chipset & Pinout Specification

This document provides hardware specifications, component details, pinout tables, and electrical characteristics for the electronic shelf label (ESL) motherboards supported by the firmware.

---

## 1. Supported Hardware Models

| Specification | Hanshow Stellar-M3N@ (`E31HA`) | Hanshow Stellar-XL3N@ (`E31PA`) |
| :--- | :--- | :--- |
| **Model Number** | `E31HA` / `E31H` | `E31PA` / `STELLARP-420` |
| **Firmware Target** | `make m3na` (`tlsr-epaper-m3na.bin`) | `make xl3na` (`tlsr-epaper-xl3na.bin`) |
| **Display Size & Type** | 2.13-inch Active Matrix BWR E-Paper | 4.2-inch Active Matrix BWR E-Paper |
| **Resolution** | 250 × 122 pixels (120 DPI) | 400 × 300 pixels (120 DPI) |
| **Display Controller** | UltraChip **UC8151** (IL0373 compatible) | UltraChip **UC8176** (GoodDisplay compatible) |
| **Display Colors** | 3-Color: Black, White, Red | 3-Color: Black, White, Red |
| **Refresh Duration** | ~15.15 s (Full 3-Color BWR OTP Waveform) | ~15.15 s (Full 3-Color BWR OTP Waveform) |
| **Battery Configuration**| 2× CR2450 Lithium coin cells in parallel (3.0V, ~1,100 mAh) | 4× CR2450 / custom pack (3.0V, ~2,400 mAh) |
| **Status LEDs** | Red (`PD2`), Green (`PD3`), Blue (`PA7`) | Red (`PD2`), Green (`PD3`), Blue (`PA7`) |

---

## 2. Silicon Platform & Chipset Architecture

### 2.1 Telink TLSR8258 / TLSR8359 Multi-Standard Wireless SoC
- **MCU Core:** 32-bit TC32 proprietary RISC core @ 24 MHz (48 MHz PLL).
- **Instruction Set:** Mixed 16-bit and 32-bit compact instructions.
- **SRAM (64 KB Total):**
  - **32 KB Retention SRAM (`0x840000`–`0x847FFF`):** Retains system state, variables, and network contexts during deep sleep (`DEEPSLEEP_MODE_RET_SRAM_LOW32K`). Quiescent current: ~1.8 µA.
  - **32 KB Extended SRAM (`0x848000`–`0x84FFFF`):** High-speed scratchpad for display buffers, decompressors, and CPU stack. Powered down during deep sleep.
- **RF Subsystem:** 2.4 GHz multi-standard transceiver supporting IEEE 802.15.4 (Zigbee 3.0), Bluetooth 5.0 Low Energy (1M/2M PHY), and proprietary modes.
- **Analog:** SAR ADC with internal reference for VDD battery voltage monitoring.
- **Hardware Debug / Flashing:** Telink Single-Wire Slave (SWS) on `PA7`.

### 2.2 Internal SPI NOR Flash (512 KB)
- **Manufacturer & Part:** Puya Semiconductor **P25Q40H / P25D40H** (JEDEC ID `0x856013`).
- **Capacity:** 512 KB (128 sectors of 4 KB each, address range `0x00000`–`0x7FFFF`).
- **Interface:** Internal multi-IO SPI (MSPI) bus dedicated to flash memory execution and storage.
- **Factory Protection:** Status register `0x2C` (Block Protection BP=3); must be unprotected prior to flashing custom firmware.

### 2.3 Fudan Microelectronics FM11NC08 NFC Tag IC
- **Protocol:** ISO/IEC 14443 Type 4 A contactless interface.
- **NDEF Support:** Hardware contactless emulation with standard Type 4 NDEF records.
- **Interface:** Fast-mode I2C slave interface (up to 400 kHz) with configurable interrupt line (`IRQ` on `PC4`) and chip select (`PC6`).
- **Power:** Directly powered by 3.0V battery rail; maintains register state during MCU retention sleep cycles.

---

## 3. Hardware Pinout & Peripheral Mapping

| Function | Stellar-M3N@ (`E31HA`, 2.13") | Stellar-XL3N@ (`E31PA`, 4.2") | Net / Symbol | Electrical Characteristics |
| :--- | :---: | :---: | :--- | :--- |
| **SWS / Blue LED** | `PA7` | `PA7` | `GPIO_LED_BLUE` | SWS debug interface & Blue status LED (Active Low) |
| **Green LED** | `PD3` | `PD3` | `GPIO_LED_GREEN` | Zigbee mode / network status LED (Active Low) |
| **Red LED** | `PD2` | `PD2` | `GPIO_LED_RED` | Refresh in progress & error LED (Active Low) |
| **Serial Debug TX** | `PB1` | `PB1` | `GPIO_UART_TX` | Non-DMA hardware UART TX @ 115,200 baud (`DEBUG=1`) |
| **EPD Busy** | `PA1` | `PA1` | `GPIO_EPD_BUSY` | Active Low during refresh (M3Na: 1M pull-up; XL3Na: Float) |
| **EPD Reset** | `PD4` | `PD4` | `GPIO_EPD_RESET` | Active Low hardware reset |
| **EPD DC** | `PD7` | `PD7` | `GPIO_EPD_DC` | LOW = Command opcode, HIGH = Data payload |
| **EPD CS** | `PB4` | `PB4` | `GPIO_EPD_CS` | SPI Chip Select (Active Low) |
| **EPD CLK** | `PB5` | `PB5` | `GPIO_EPD_CLK` | SPI Master Clock |
| **EPD MOSI** | `PB6` | `PB6` | `GPIO_EPD_MOSI` | SPI Master Out |
| **EPD Power Switch**| **`PC5`** | **`PB7`** | `GPIO_EPD_PWR_ENABLE` | P-MOSFET gate: Low = 3.3V VDD ON, High = Power Isolated |
| **NFC I2C SDA** | `PC0` | `PC0` | `GPIO_NFC_SDA` | FM11NC08 Hardware I2C Data (400 kHz) |
| **NFC I2C SCL** | `PC1` | `PC1` | `GPIO_NFC_SCL` | FM11NC08 Hardware I2C Clock (400 kHz) |
| **NFC IRQ** | `PC4` | `PC4` | `GPIO_NFC_IRQ` | Active Low interrupt / RF field wake-up |
| **NFC CS** | `PC6` | `PC6` | `GPIO_NFC_CS` | Active Low chip select / contact power gate |
| **Battery ADC** | `PB0` | `PB0` | `GPIO_VBAT` | Internal SAR ADC millivolt sampling via VDD |

---

## 4. Key Architectural Differences (XL3Na vs M3Na)

### 4.1 Display Power Gate (`GPIO_PB7` vs `GPIO_PC5`)
- **Stellar-M3N@ (`E31HA` 2.13"):** The high-side P-MOSFET gate is controlled via **`PC5`**.
- **Stellar-XL3N@ (`E31PA` 4.2"):** The high-side P-MOSFET gate is routed to **`PB7`**. During active refresh, `PB7` is pulled LOW with floating resistor (`PM_PIN_UP_DOWN_FLOAT`) to supply 3.3V VDD to the UC8176 controller; during deep sleep, `PB7` is held HIGH with an internal 10K pull-up (`PM_PIN_PULLUP_10K`).

### 4.2 Red Plane Data Polarity
- **UC8151 (2.13"):** Command `0x13` (DTM2) uses active-high red polarity (`1` = Red, `0` = Non-red / follow DTM1).
- **UC8176 (4.2"):** Command `0x13` (DTM2) uses active-low red polarity (**`0` = Red**, **`1` = Non-red / follow DTM1**). The firmware inverts DTM2 data (`~dtm2`) when streaming to UC8176 to avoid flooding monochrome images with solid red.

### 4.3 Panel Orientation & Scan Direction (`PSR = 0x0F`)
- The UC8176 Panel Setting Register (`0x00`) Byte 1 is configured with `0x0F` (`UD = 1`, Gate scan up). Setting `UD = 0` (`0x07`) scans downwards, resulting in an upside-down display.

### 4.4 Hardware Reset Timing
- The 4.2" panel controller requires longer digital discharge: `GPIO_EPD_RESET` is held LOW for **200 ms** and HIGH for **200 ms** (followed by `EPD_CheckStatus(200)`), guaranteeing complete charge pump capacitor discharge and digital reset.

---

## 5. Programming Pads & Test Points

The motherboards provide solder test pads on the rear of the PCB for wire programming:

```text
       Hanshow PCB Edge Test Pads
┌──────────────────────────────────────────────┐
│  [ VCC / 3.3V ]   Positive Battery Terminal  │
│  [ GND ]          Ground Terminal            │
│  [ PA7 / SWS ]    Telink Single-Wire Slave   │
│  [ PB1 / TX ]     Debug UART Output (115200) │
│  [ PA0 ]          GPIO PA0 Test Pad (NC)     │
└──────────────────────────────────────────────┘
```

For wire flashing via USB-UART adapter:
1. Connect **GND** to programmer GND.
2. Connect **3.3V** to programmer 3.3V power (or install coin cells).
3. Connect programmer **TX** through a 1 kΩ resistor (or diode) and programmer **RX** directly to **PA7 (SWS)**.
4. *(Optional)* Connect programmer **RX** to **PB1** for serial log monitoring when compiled with `DEBUG=1`.
