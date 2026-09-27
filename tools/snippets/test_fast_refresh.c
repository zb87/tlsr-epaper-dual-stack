#include "epd_prototype.h"

// ============================================================================
// Fast E-Paper Refresh Prototype Experiment for TLSR8258
// Target Board: Hanshow Stellar-M3N@ / E31HA (2.13" UC8151 250x122 BWR)
//
// Select Refresh Mode to Test:
//   MODE_FAST_OTP_BW  (0): Factory OTP Monochrome (~2.5s) - 6x faster than BWR, clean contrast
//   MODE_CUSTOM_LUT   (1): Custom Register LUT (~0.8s) - 18x faster partial refresh
//   MODE_FULL_BWR_OTP (2): Full 3-Color Factory OTP (~15s) - Baseline reference
// ============================================================================
#define MODE_FAST_OTP_BW   0
#define MODE_CUSTOM_LUT    1
#define MODE_FULL_BWR_OTP  2

#ifndef REFRESH_MODE
#define REFRESH_MODE MODE_FAST_OTP_BW
#endif

// Custom partial refresh waveform tables for UC8151 2.13" panel (~800 ms)
static const uint8_t lut_bw_20_vcom[] = {
    0x20, 0x00, 10, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t lut_bw_22_w2w_b2w[] = {
    0x22, 0x80, 10, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t lut_bw_23_b2b_w2b[] = {
    0x23, 0x40, 10, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00
};

static void send_lut(const epd_test_api_t *api, const uint8_t *lut, int len) {
    int i;
    api->write_cmd(lut[0]);
    for (i = 1; i < len; i++) {
        api->write_data(lut[i]);
    }
}

static void send_empty_lut(const epd_test_api_t *api, uint8_t cmd, int len) {
    int i;
    api->write_cmd(cmd);
    for (i = 0; i < len; i++) {
        api->write_data(0x00);
    }
}

// Generates high-contrast test pattern: thick vertical bars + outer frame border
// Toggles pattern inversion between executions for immediate physical visual confirmation
static uint8_t generate_test_pixel(uint16_t idx, uint8_t invert) {
    uint8_t col = idx / 16;      // 0..249 columns
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
        // Subtle checker texture in white bar
        pixel = (row_byte % 2 == 0) ? 0xFF : 0xAA;
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

    // Track toggle state across executions using shared scratch RAM
    if (api->render_buffer && api->buffer_size >= 4000) {
        toggle = (api->render_buffer[0] == 0xAA) ? 1 : 0;
        api->render_buffer[0] = toggle ? 0x55 : 0xAA;
    }

    const char *mode_name = "Unknown";
#if (REFRESH_MODE == MODE_FAST_OTP_BW)
    mode_name = "OTP B/W (~2.5s)";
#elif (REFRESH_MODE == MODE_CUSTOM_LUT)
    mode_name = "Custom LUT (<1s)";
#elif (REFRESH_MODE == MODE_FULL_BWR_OTP)
    mode_name = "Full BWR OTP (~15s)";
#endif

    api->log("SNIPPET", "=== Fast EPD Refresh Experiment ===");
    api->log("SNIPPET", "Board: %s (Type %u) | Mode: %s | Toggle: %u",
             api->board_type ? "4.2 XL3Na" : "2.13 M3Na",
             api->board_type,
             mode_name,
             toggle);

    // 1. Initialize EPD GPIO pins & gate MOSFET power ON
    api->epd_init_pins();
    api->epd_power_on();
    api->sleep_ms(5);

    // 2. Hardware reset pulse on EPD_PIN_RESET (PD4 = 0x0310)
    api->gpio_write(EPD_PIN_RESET, 0);
    api->sleep_ms(10);
    api->gpio_write(EPD_PIN_RESET, 1);
    api->sleep_ms(20);

    api->log("SNIPPET", "Post-reset BUSY pin: %u", api->gpio_read(EPD_PIN_BUSY));

    // 3. Booster Soft Start (0x06)
    api->write_cmd(0x06);
    api->write_data(0x17);
    api->write_data(0x17);
    api->write_data(0x17);

    // 4. Power On display controller (0x04)
    api->write_cmd(0x04);
    api->sleep_ms(1);

    // Wait for BUSY (PA1) to rise HIGH (controller ready)
    t = 0;
    while (epd_proto_is_busy(api) && ++t < 250) {
        api->sleep_ms(1);
    }
    api->log("SNIPPET", "Power on took %lu ms (BUSY pin=%u)", t, api->gpio_read(EPD_PIN_BUSY));

    // 5. Configure Panel Settings & Waveform LUT
#if (REFRESH_MODE == MODE_FAST_OTP_BW)
    // Factory OTP B/W Mode (PSR 0x00): bit 5 = 0 (OTP LUT), B/W only (~12.7s OTP)
    api->write_cmd(0x00);
    api->write_data(0x1F);

    // VCOM and data interval setting (0x50): 0x97
    api->write_cmd(0x50);
    api->write_data(0x97);

    // DTM1 (0x10): Black/White image plane
    api->write_cmd(0x10);
    for (i = 0; i < 4000; i++) {
        api->write_data(generate_test_pixel(i, toggle));
    }

    // DTM2 (0x13): Red plane cleared (0 = non-red)
    api->write_cmd(0x13);
    for (i = 0; i < 4000; i++) {
        uint8_t b = 0x00;
        if ((i & 0x0F) == 15) b &= 0xC0;
        api->write_data(b);
    }

#elif (REFRESH_MODE == MODE_CUSTOM_LUT)
    // Custom Register LUT Mode: bit 5 = 1 (LUT from registers 0x20..0x24)
    api->write_cmd(0x00);
    api->write_data(0x3F);
    api->write_data(0x0F);

    // VCOM and data interval setting (0x50): 0x97
    api->write_cmd(0x50);
    api->write_data(0x97);

    // Upload custom waveform tables
    // Note: UC8151 requires exactly 260 bytes for registers 0x21 and 0x24
    send_lut(api, lut_bw_20_vcom, sizeof(lut_bw_20_vcom));
    send_empty_lut(api, 0x21, 260);
    send_lut(api, lut_bw_22_w2w_b2w, sizeof(lut_bw_22_w2w_b2w));
    send_lut(api, lut_bw_23_b2b_w2b, sizeof(lut_bw_23_b2b_w2b));
    send_empty_lut(api, 0x24, 260);

    // DTM1 (0x10): Old image state (drives differential transitions)
    api->write_cmd(0x10);
    for (i = 0; i < 4000; i++) {
        api->write_data(generate_test_pixel(i, !toggle));
    }

    // DTM2 (0x13): New image state
    api->write_cmd(0x13);
    for (i = 0; i < 4000; i++) {
        api->write_data(generate_test_pixel(i, toggle));
    }

#elif (REFRESH_MODE == MODE_FULL_BWR_OTP)
    // Full 3-Color BWR OTP Mode: PSR = 0x0F (~15s)
    api->write_cmd(0x00);
    api->write_data(0x0F);

    api->write_cmd(0x50);
    api->write_data(0x97);

    // DTM1 (0x10): Black/White image plane
    api->write_cmd(0x10);
    for (i = 0; i < 4000; i++) {
        api->write_data(generate_test_pixel(i, toggle));
    }

    // DTM2 (0x13): Red plane (accent pattern)
    api->write_cmd(0x13);
    for (i = 0; i < 4000; i++) {
        uint8_t col = i / 16;
        uint8_t row = i % 16;
        uint8_t red = 0x00;
        if (col >= 100 && col <= 150 && row >= 4 && row <= 11) {
            red = 0xFF; // Solid red box in screen center
        }
        if (row == 15) red &= 0xC0;
        api->write_data(red);
    }
#endif

    // 6. Trigger Display Refresh (DRF 0x12)
    api->log("SNIPPET", "Triggering DRF (0x12)...");
    api->write_cmd(0x12);

    // Wait until BUSY (PA1) drops LOW (confirming refresh has begun)
    t_start = 0;
    while (!epd_proto_is_busy(api) && ++t_start < 100) {
        api->sleep_ms(1);
    }

    // Wait while BUSY is LOW (panel is actively refreshing)
    t = 0;
    while (epd_proto_is_busy(api) && ++t < 25000) {
        api->sleep_ms(1);
        if (t % 500 == 0) {
            api->log("SNIPPET", "Refreshing... (%lu ms, BUSY=%u)", t, api->gpio_read(EPD_PIN_BUSY));
        }
    }
    api->log("SNIPPET", "Refresh DONE in %lu ms (BUSY pin=%u)", t, api->gpio_read(EPD_PIN_BUSY));

    // 7. Clean EPD shutdown sequence
    api->write_cmd(0x92); // PTOUT: Partial Out
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

    // 8. Isolate GPIOs & gate power
    api->epd_power_off();
    api->epd_isolate_pins();

    return 0; // Return code passed back via BLE notification 0x89
}
