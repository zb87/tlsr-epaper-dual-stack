#include "epd_prototype.h"

// ============================================================================
// Ultra-Fast Partial Differential Refresh Experiment for TLSR8258
// Target Board: Hanshow Stellar-M3N@ / E31HA (2.13" UC8151 250x122 BWR)
//
// Waveform Architecture:
//   - Uses GxEPD2_213_Z19c verified differential register LUTs (0x20..0x24)
//   - Frame rate: 100 Hz (10 ms per frame, PLL 0x30 = 0x3A)
//   - Pulse duration: 32 frames = 320 ms (~0.32s vs 15.1s standard BWR OTP)
//   - Controller Configuration: PSR = 0xBF (RES=128x296, REG=1, B/W=1)
//   - Partial Window Activation: PTIN (0x91) + PTL (0x90) (0, 0, 128, 250)
//   - True Differential State Tracking via shared RAM render_buffer:
//       DTM1 = physically active previous frame on screen
//       DTM2 = new inverted frame
// ============================================================================

#define LUT_PULSE_FRAMES 0x1F // 31 driving frames
#define LUT_DISCH_FRAMES 0x01 // 1 discharge frame
// Total: 32 frames @ 100 Hz = 320 ms

// 1. VCOM table: 44 bytes (6 active bytes + 38 zero bytes)
static const uint8_t lut_vcom[44] = {
    // Row 0: pat=0 (VCOM_DC), dur=[31, 1, 0, 0], rep=1
    0x00, LUT_PULSE_FRAMES, LUT_DISCH_FRAMES, 0x00, 0x00, 0x01,
    // Rows 1-6 + ST_XON/ST_CHV: all 0x00
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

// 2. WW (White to White): 42 bytes (6 active bytes + 36 zero bytes)
// Holds 0V (no pulse for pixels staying white, zero flicker)
static const uint8_t lut_ww[42] = {
    0x00, LUT_PULSE_FRAMES, LUT_DISCH_FRAMES, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 3. BW (Black to White): 42 bytes
// pat = 0b10_00_00_00 = 0x80 (VDL = -10V, pulls white pigment to front electrode)
static const uint8_t lut_bw[42] = {
    0x80, LUT_PULSE_FRAMES, LUT_DISCH_FRAMES, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 4. WB (White to Black): 42 bytes
// pat = 0b01_00_00_00 = 0x40 (VDH = +10V, pulls black pigment to front electrode)
static const uint8_t lut_wb[42] = {
    0x40, LUT_PULSE_FRAMES, LUT_DISCH_FRAMES, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 5. BB (Black to Black): 42 bytes
// Holds 0V (no pulse for pixels staying black, zero flicker)
static const uint8_t lut_bb[42] = {
    0x00, LUT_PULSE_FRAMES, LUT_DISCH_FRAMES, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void send_buf(const epd_test_api_t *api, uint8_t cmd, const uint8_t *buf, int len) {
    api->write_cmd(cmd);
    for (int i = 0; i < len; i++) {
        api->write_data(buf[i]);
    }
}

// Generates high-contrast vertical bars + outer frame border
// Toggles pattern inversion between executions for immediate physical visual confirmation
static uint8_t generate_test_pixel(uint16_t idx, uint8_t invert) {
    uint8_t col = idx / 16;       // 0..249 columns
    uint8_t row_byte = idx % 16;  // 0..15 bytes per column (122 pixels)

    // Outer solid border
    if (col < 4 || col >= 246 || row_byte == 0) {
        return 0x00; // Black
    }

    // Alternating vertical stripes (32 pixels wide each)
    uint8_t stripe = (col / 32) % 2;
    uint8_t pixel;
    if (stripe ^ invert) {
        pixel = 0x00; // Solid black
    } else {
        pixel = 0xFF; // Solid white
    }

    // UC8151 122-pixel column padding: byte 15 bits 5..0 are padding (set HIGH)
    if (row_byte == 15) {
        pixel |= 0x3F;
    }
    return pixel;
}

int snippet_main(const epd_test_api_t *api) {
    int i;
    uint32_t t = 0;
    uint32_t t_start = 0;
    uint8_t toggle = 0;
    uint8_t has_prev_frame = 0;

    // Check shared RAM buffer for previous frame state
    // api->render_buffer has 16KB of persistent retention RAM
    if (api->render_buffer && api->buffer_size >= 4001) {
        if (api->render_buffer[0] == 0x42) {
            has_prev_frame = 1;
            // Determine toggle state from first data byte of previous frame
            toggle = (api->render_buffer[1 + 16 * 10] == 0x00) ? 1 : 0;
        } else {
            // First execution: mark valid and start with toggle 0
            api->render_buffer[0] = 0x42;
            toggle = 0;
        }
    }

    api->log("SNIPPET", "=== Ultra-Fast Partial Refresh Experiment ===");
    api->log("SNIPPET", "Board: %s (Type %u) | Mode: GxEPD2 Partial LUT (~320 ms) | Toggle: %u | PrevFrame: %u",
             api->board_type ? "4.2 XL3Na" : "2.13 M3Na",
             api->board_type,
             toggle,
             has_prev_frame);

    // 1. Initialize EPD GPIO pins & gate MOSFET power ON
    api->epd_init_pins();
    api->epd_power_on();
    api->sleep_ms(5);

    // 2. Hardware reset pulse
    api->gpio_write(EPD_PIN_RESET, 0);
    api->sleep_ms(10);
    api->gpio_write(EPD_PIN_RESET, 1);
    api->sleep_ms(20);

    // 3. Booster Soft Start (0x06)
    api->write_cmd(0x06);
    api->write_data(0x17);
    api->write_data(0x17);
    api->write_data(0x17);

    // 4. Power On display controller (0x04)
    api->write_cmd(0x04);
    t = 0;
    while (epd_proto_is_busy(api) && ++t < 250) {
        api->sleep_ms(1);
    }
    api->log("SNIPPET", "Power on took %lu ms (BUSY pin=%u)", t, api->gpio_read(EPD_PIN_BUSY));

    // 5. Upload custom register waveform tables (0x20..0x24)
    send_buf(api, 0x20, lut_vcom, sizeof(lut_vcom));
    send_buf(api, 0x21, lut_ww,   sizeof(lut_ww));
    send_buf(api, 0x22, lut_bw,   sizeof(lut_bw));
    send_buf(api, 0x23, lut_wb,   sizeof(lut_wb));
    send_buf(api, 0x24, lut_bb,   sizeof(lut_bb));

    // 6. Panel Setting: PSR 0x00 -> 0xBF
    // 0xBF = RES_128x296 (0x80) | LUT_REG (0x20) | FORMAT_BW (0x10) | SCAN_UP (0x08) | SHIFT_RIGHT (0x04) | BOOSTER_ON (0x02) | RESET_NONE (0x01)
    api->write_cmd(0x00);
    api->write_data(0xBF);

    // 7. PLL Frequency (0x30): 100 Hz (10 ms per frame)
    api->write_cmd(0x30);
    api->write_data(0x3A);

    // 8. VCOM and data interval setting (0x50): 0xF7 (Floating border)
    api->write_cmd(0x50);
    api->write_data(0xF7);

    // 9. Partial Mode Enable (PTIN: 0x91)
    api->write_cmd(0x91);

    // 10. Partial Window Area (PTL: 0x90) for full panel (0, 0, 128, 250)
    api->write_cmd(0x90);
    api->write_data(0x00); // x start (0)
    api->write_data(0x7F); // x end (127)
    api->write_data(0x00); // y start MSB (0)
    api->write_data(0x00); // y start LSB (0)
    api->write_data(0x00); // y end MSB (0)
    api->write_data(0xF9); // y end LSB (249)
    api->write_data(0x01); // PT_SCAN

    // 11. Stream DTM1 (0x10): Old image (source state for differential transition)
    api->write_cmd(0x10);
    for (i = 0; i < 4000; i++) {
        uint8_t old_pixel = 0xFF; // Default to white if no previous frame
        if (has_prev_frame && api->render_buffer) {
            old_pixel = api->render_buffer[1 + i];
        }
        api->write_data(old_pixel);
    }

    // 12. Stream DTM2 (0x13): New image (target state) & record into shared RAM
    api->write_cmd(0x13);
    for (i = 0; i < 4000; i++) {
        uint8_t new_pixel = generate_test_pixel(i, toggle);
        if (api->render_buffer && api->buffer_size >= 4001) {
            api->render_buffer[1 + i] = new_pixel;
        }
        api->write_data(new_pixel);
    }

    // 13. Data Stop (DSP 0x11)
    api->write_cmd(0x11);

    // 14. Trigger Display Refresh (DRF 0x12)
    api->log("SNIPPET", "Triggering DRF (0x12) in Partial Mode [~320 ms LUT]...");
    api->write_cmd(0x12);

    // Wait until BUSY (PA1) drops LOW (confirming refresh has begun)
    t_start = 0;
    while (!epd_proto_is_busy(api) && ++t_start < 100) {
        api->sleep_ms(1);
    }

    // Wait while BUSY is LOW (panel is actively refreshing)
    t = 0;
    while (epd_proto_is_busy(api) && ++t < 15000) {
        api->sleep_ms(1);
        if (t % 100 == 0) {
            api->log("SNIPPET", "Refreshing... (%lu ms, BUSY=%u)", t, api->gpio_read(EPD_PIN_BUSY));
        }
    }
    api->log("SNIPPET", "Refresh DONE in %lu ms (BUSY pin=%u)", t, api->gpio_read(EPD_PIN_BUSY));

    // 15. Exit Partial Mode (PTOUT 0x92)
    api->write_cmd(0x92);

    // 16. Restore Panel Setting (PSR 0x00 -> 0x0F) to preserve factory OTP configuration
    api->write_cmd(0x00);
    api->write_data(0x0F);

    // 17. Clean EPD shutdown sequence
    api->write_cmd(0x50); // Float VCOM
    api->write_data(0xF7);

    api->write_cmd(0x02); // Power Off
    api->sleep_ms(1);
    uint32_t t_off = 0;
    while (epd_proto_is_busy(api) && ++t_off < 200) {
        api->sleep_ms(1);
    }

    api->write_cmd(0x07); // Deep Sleep
    api->write_data(0xA5);

    // 18. Isolate GPIOs & gate power
    api->epd_power_off();
    api->epd_isolate_pins();

    return 0;
}
