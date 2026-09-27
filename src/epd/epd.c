#include <string.h>
#include "tl_common.h"
#include "app_config.h"
#include "flash_eep.h"
#include "epd.h"
#include "epd_spi.h"
#if (BOARD == BOARD_HANSHOW_E31PA)
#include "epd_bwr_420.h"
#else
#include "epd_bwr_213.h"
#endif
#include "epd_slots.h"
#include "led.h"
#include "debug_uart.h"

RAM uint8_t epd_update_state = 0;
RAM uint8_t epd_temperature = 22;
extern u32 pm_get_32k_tick(void);
RAM static uint32_t epd_refresh_start_32k_tick = 0;

void epd_init(void) {
    EPD_init_pins();
    EPD_POWER_OFF();
    EPD_isolate_pins();
}

uint8_t epd_read_temp(void) {
    if (epd_is_busy()) {
        return epd_temperature;
    }
#if (BOARD == BOARD_HANSHOW_E31PA)
    epd_temperature = EPD_BWR_420_read_temp();
#else
    epd_temperature = EPD_BWR_213_read_temp();
#endif
    return epd_temperature;
}

void epd_set_sleep(void) {
#if (BOARD == BOARD_HANSHOW_E31PA)
    EPD_BWR_420_set_sleep();
#else
    // Exit partial window mode if active
    EPD_WriteCmd(0x92); // PTOUT: Partial Out

    // Float VCOM before power off
    EPD_WriteCmd(0x50);
    EPD_WriteData(0xF7);

    // Power off panel and enter deep sleep
    EPD_WriteCmd(0x02); // Power off
    EPD_CheckStatus(100);
    EPD_WriteCmd(0x07); // Deep sleep
    EPD_WriteData(0xA5);
#endif

    DEBUG_LOG("EPD", "Panel deep sleep entered (POF + DSLP + Power Off)");
    debug_uart_flush();

    EPD_POWER_OFF();
    EPD_isolate_pins();
    cpu_set_gpio_wakeup(GPIO_EPD_BUSY, Level_High, 0);
    epd_update_state = 0;
    led_off(LED_RED); // Turn off Red LED when refresh completes
}

extern volatile u8 ota_is_working;

uint8_t epd_state_handler(void) {
    if (ota_is_working) {
        return 0; // Completely bypass EPD processing during OTA update to preserve BLE RF
    }
    if (epd_update_state == 1) {
        uint32_t elapsed_32k = pm_get_32k_tick() - epd_refresh_start_32k_tick;

        // BUSY (PA1) is LOW during refresh, goes HIGH when refresh completes.
        // Full BWR refresh takes 15-18 seconds. Require at least 10,000ms (10 * 32000 ticks)
        // before declaring refresh completion to avoid premature cutoff.
        // Note: gpio_read(GPIO_EPD_BUSY) returns 0 or 2 (BIT(1)), NEVER 1!
        if (elapsed_32k >= (10 * 32000) && gpio_read(GPIO_EPD_BUSY) != 0) {
            uint32_t elapsed_ms = (elapsed_32k * 1000) / 32000;
            DEBUG_LOG("EPD", "Refresh DONE in %u ms (BUSY high). Sleeping panel.", (unsigned int)elapsed_ms);
            debug_uart_flush();
            epd_set_sleep();
        } else if (elapsed_32k >= (35 * 32000)) {
            DEBUG_LOG("EPD", "Refresh TIMEOUT (>35s)! Forcing panel sleep.");
            debug_uart_flush();
            epd_set_sleep();
        }
    }
    return epd_update_state;
}

bool epd_is_busy(void) {
    return (epd_update_state == 1);
}

extern void flash_read(u32 addr, u32 len, u8 *buf);

// -------------------------------------------------------------------------
// 2D Delta + PackBits Flash Stream Decompressor (Ultra-Low RAM)
// -------------------------------------------------------------------------
typedef struct {
    uint32_t flash_addr;
    uint16_t stream_len;
    uint16_t stream_idx;
    uint8_t  buf[64];
    uint8_t  buf_idx;
    uint8_t  buf_len;
    uint8_t  run_rem;
    uint8_t  run_val;
    bool     is_repeat;
} packbits_flash_stream_t;

typedef struct {
    packbits_flash_stream_t stream;
    uint8_t  prev_line[EPD_LINE_BYTES];
    uint16_t line_bytes;
    uint16_t total_lines;
    uint16_t current_line;
} delta2d_line_decoder_t;

static void delta2d_decoder_init(delta2d_line_decoder_t *dec, uint32_t flash_addr, uint16_t comp_len) {
    dec->stream.flash_addr = flash_addr;
    dec->stream.stream_len = comp_len;
    dec->stream.stream_idx = 0;
    dec->stream.buf_idx = 0;
    dec->stream.buf_len = 0;
    dec->stream.run_rem = 0;
    dec->stream.run_val = 0;
    dec->stream.is_repeat = false;
    dec->line_bytes = EPD_LINE_BYTES;
    dec->total_lines = EPD_TOTAL_LINES;
    dec->current_line = 0;
    memset(dec->prev_line, 0, sizeof(dec->prev_line));
}

static bool delta2d_read_line(delta2d_line_decoder_t *dec, uint8_t *dst_line) {
    if (dec->current_line >= dec->total_lines) return false;
    packbits_flash_stream_t *s = &dec->stream;
    uint16_t d_idx = 0;
    uint16_t line_bytes = dec->line_bytes;

    while (d_idx < line_bytes) {
        if (s->run_rem > 0) {
            uint16_t take = line_bytes - d_idx;
            if (take > s->run_rem) take = s->run_rem;
            if (s->is_repeat) {
                memset(&dst_line[d_idx], s->run_val, take);
            } else {
                for (uint16_t i = 0; i < take; i++) {
                    if (s->buf_idx >= s->buf_len) {
                        uint16_t rem = s->stream_len - s->stream_idx;
                        if (rem == 0) { dst_line[d_idx + i] = 0; continue; }
                        uint8_t chunk = (rem > sizeof(s->buf)) ? sizeof(s->buf) : (uint8_t)rem;
                        flash_read(s->flash_addr + s->stream_idx, chunk, s->buf);
                        s->stream_idx += chunk;
                        s->buf_idx = 0;
                        s->buf_len = chunk;
                    }
                    dst_line[d_idx + i] = s->buf[s->buf_idx++];
                }
            }
            d_idx += take;
            s->run_rem -= take;
            continue;
        }

        // Need new header
        if (s->buf_idx >= s->buf_len) {
            uint16_t rem = s->stream_len - s->stream_idx;
            if (rem == 0) {
                memset(&dst_line[d_idx], 0, line_bytes - d_idx);
                break;
            }
            uint8_t chunk = (rem > sizeof(s->buf)) ? sizeof(s->buf) : (uint8_t)rem;
            flash_read(s->flash_addr + s->stream_idx, chunk, s->buf);
            s->stream_idx += chunk;
            s->buf_idx = 0;
            s->buf_len = chunk;
        }

        int8_t header = (int8_t)s->buf[s->buf_idx++];
        if (header >= 0) {
            s->is_repeat = false;
            s->run_rem = (uint8_t)header + 1;
        } else if (header != -128) {
            s->is_repeat = true;
            s->run_rem = (uint8_t)(1 - header);
            if (s->buf_idx >= s->buf_len) {
                uint16_t rem = s->stream_len - s->stream_idx;
                if (rem > 0) {
                    uint8_t chunk = (rem > sizeof(s->buf)) ? sizeof(s->buf) : (uint8_t)rem;
                    flash_read(s->flash_addr + s->stream_idx, chunk, s->buf);
                    s->stream_idx += chunk;
                    s->buf_idx = 0;
                    s->buf_len = chunk;
                    s->run_val = s->buf[s->buf_idx++];
                } else {
                    s->run_val = 0;
                }
            } else {
                s->run_val = s->buf[s->buf_idx++];
            }
        }
    }

    if (dec->current_line == 0) {
        for (uint16_t i = 0; i < line_bytes; i++) {
            dec->prev_line[i] = dst_line[i];
        }
    } else {
        for (uint16_t i = 0; i < line_bytes; i++) {
            dst_line[i] ^= dec->prev_line[i];
            dec->prev_line[i] = dst_line[i];
        }
    }

    dec->current_line++;
    return true;
}

void epd_display_slot(uint8_t slot_idx, uint8_t style) {
    if (slot_idx >= EPD_SLOT_COUNT) slot_idx = EPD_SLOT_INFO;
    if (style >= STYLE_COUNT) style = STYLE_STANDARD;

    const char *style_names[] = {"Standard", "B&W", "B&W inv", "R&W", "R&W inv"};
    DEBUG_LOG("EPD", "Display Slot %u | Style %u (%s) [%ux%u]",
              slot_idx, style, style_names[style], EPD_WIDTH, EPD_HEIGHT);

    epd_update_state = 1;
    epd_refresh_start_32k_tick = pm_get_32k_tick();

    // Force turn on Red LED indicator during screen refresh
    led_on(LED_RED);

    EPD_init_pins();
    EPD_POWER_ON();
    epd_delay_ms(5);

#if (BOARD == BOARD_HANSHOW_E31PA)
    epd_delay_ms(10);
    gpio_write(GPIO_EPD_RESET, 0);
    epd_delay_ms(200);
    gpio_write(GPIO_EPD_RESET, 1);
    epd_delay_ms(200);
    EPD_CheckStatus(200);

    EPD_BWR_420_init();
    if (slot_idx == EPD_SLOT_INFO) {
        epd_render_info_slot();
    }
    EPD_BWR_420_start_dtm1();
#else
    gpio_write(GPIO_EPD_RESET, 0);
    epd_delay_ms(10);
    gpio_write(GPIO_EPD_RESET, 1);
    epd_delay_ms(20);

    // UC8151 2.13" panel initialization
    EPD_WriteCmd(0x06); // Booster Soft Start
    EPD_WriteData(0x17);
    EPD_WriteData(0x17);
    EPD_WriteData(0x17);

    EPD_WriteCmd(0x04); // Power On
    EPD_CheckStatus(100);

    // Read internal temperature sensor (0x40) while panel is powered on
    EPD_WriteCmd(0x40);
    EPD_CheckStatus(100);
    uint8_t raw_temp = EPD_SPI_read();
    EPD_SPI_read();
    if (raw_temp > 0 && raw_temp <= 80) {
        epd_temperature = raw_temp;
    }

    if (slot_idx == EPD_SLOT_INFO) {
        epd_render_info_slot();
    }

    EPD_WriteCmd(0x00); // Panel Setting
    EPD_WriteData(0x0F);

    EPD_WriteCmd(0x50); // VCOM and Data Interval Setting
    EPD_WriteData(0x97);

    EPD_WriteCmd(0x10); // DTM1 (BW)
#endif

    epd_slot_header_t header;
    bool is_compressed = false;
    uint32_t slot_addr = 0;

    if (slot_idx >= EPD_USER_SLOT_START && slot_idx < (EPD_USER_SLOT_START + EPD_USER_SLOT_COUNT)) {
        slot_addr = epd_get_slot_flash_address(slot_idx);
        if (slot_addr != 0) {
            flash_read(slot_addr, sizeof(header), (uint8_t *)&header);
            if (header.magic == EPD_COMP_MAGIC) {
                is_compressed = true;
            }
        }
    }

    if (is_compressed) {
        // ---------------------------------------------------------------------
        // Compressed User Slot: Stream DTM1 line-by-line (Ultra-Low RAM)
        // ---------------------------------------------------------------------
        uint8_t line_bw[EPD_LINE_BYTES];
        uint8_t line_red[EPD_LINE_BYTES];
        delta2d_line_decoder_t dec_bw;
        delta2d_line_decoder_t dec_red;

        uint32_t bw_comp_addr = slot_addr + sizeof(epd_slot_header_t);
        uint32_t red_comp_addr = bw_comp_addr + header.bw_comp_len;
        bool has_red = (header.flags & 1) && (header.red_comp_len > 0);

        delta2d_decoder_init(&dec_bw, bw_comp_addr, header.bw_comp_len);
        if (has_red) {
            delta2d_decoder_init(&dec_red, red_comp_addr, header.red_comp_len);
        }

        for (uint16_t line = 0; line < EPD_TOTAL_LINES; line++) {
            if ((line % 10) == 0) {
                ble_display_poll();
            }
            delta2d_read_line(&dec_bw, line_bw);
            if (has_red) {
                delta2d_read_line(&dec_red, line_red);
            } else {
                memset(line_red, 0x00, EPD_LINE_BYTES);
            }

            for (uint16_t j = 0; j < EPD_LINE_BYTES; j++) {
                uint8_t bw = line_bw[j];
                uint8_t red = line_red[j];
                uint8_t dtm1;

                switch (style) {
                    case STYLE_STANDARD:    dtm1 = bw | red; break;
                    case STYLE_BW_STANDARD: dtm1 = (~red) & bw; break;
                    case STYLE_BW_INVERTED: dtm1 = ~((~red) & bw); break;
                    case STYLE_RW_STANDARD:
                    case STYLE_RW_INVERTED: dtm1 = 0xFF; break;
                    default:                dtm1 = bw | red; break;
                }
#if (BOARD == BOARD_HANSHOW_E31HA)
                if (j == 15) dtm1 |= 0x3F; // Pad bits high for 122 vertical lines
#endif
                EPD_WriteData(dtm1);
            }
        }

        // ---------------------------------------------------------------------
        // Compressed User Slot: Stream DTM2 line-by-line (Ultra-Low RAM)
        // ---------------------------------------------------------------------
#if (BOARD == BOARD_HANSHOW_E31PA)
        EPD_BWR_420_start_dtm2();
#else
        EPD_WriteCmd(0x13);
#endif
        delta2d_decoder_init(&dec_bw, bw_comp_addr, header.bw_comp_len);
        if (has_red) {
            delta2d_decoder_init(&dec_red, red_comp_addr, header.red_comp_len);
        }

        for (uint16_t line = 0; line < EPD_TOTAL_LINES; line++) {
            if ((line % 10) == 0) {
                ble_display_poll();
            }
            delta2d_read_line(&dec_bw, line_bw);
            if (has_red) {
                delta2d_read_line(&dec_red, line_red);
            } else {
                memset(line_red, 0x00, EPD_LINE_BYTES);
            }

            for (uint16_t j = 0; j < EPD_LINE_BYTES; j++) {
                uint8_t bw = line_bw[j];
                uint8_t red = line_red[j];
                uint8_t dtm2;

                switch (style) {
                    case STYLE_STANDARD:    dtm2 = red; break;
                    case STYLE_BW_STANDARD:
                    case STYLE_BW_INVERTED: dtm2 = 0x00; break;
                    case STYLE_RW_STANDARD: dtm2 = ~((~red) & bw); break;
                    case STYLE_RW_INVERTED: dtm2 = (~red) & bw; break;
                    default:                dtm2 = red; break;
                }
#if (BOARD == BOARD_HANSHOW_E31HA)
                if (j == 15) dtm2 &= 0xC0; // Pad bits cleared for 122 vertical lines
#elif (BOARD == BOARD_HANSHOW_E31PA)
                dtm2 = ~dtm2; // UC8176 4.2" BWR polarity: 0 = Red, 1 = Non-red
#endif
                EPD_WriteData(dtm2);
            }
        }
    } else {
        // ---------------------------------------------------------------------
        // Uncompressed / Dynamic Screen (Info or Blank): Stream in 256-byte chunks
        // ---------------------------------------------------------------------
        uint8_t chunk_bw[256];
        uint8_t chunk_red[256];

        for (uint16_t off = 0; off < EPD_PLANE_SIZE; off += sizeof(chunk_bw)) {
            ble_display_poll();
            uint16_t chunk_len = (off + sizeof(chunk_bw) > EPD_PLANE_SIZE) ? (EPD_PLANE_SIZE - off) : sizeof(chunk_bw);

            if (slot_idx == EPD_SLOT_INFO) {
                for (uint16_t j = 0; j < chunk_len; j++) {
                    chunk_bw[j] = epd_get_info_pixel_byte(off + j);
                }
                memset(chunk_red, 0x00, chunk_len);
            } else {
                // Blank or unprogrammed slot
                memset(chunk_bw, 0xFF, chunk_len);
                memset(chunk_red, 0x00, chunk_len);
            }

            for (uint16_t j = 0; j < chunk_len; j++) {
                uint8_t bw = chunk_bw[j];
                uint8_t red = chunk_red[j];
                uint8_t dtm1;

                switch (style) {
                    case STYLE_STANDARD:    dtm1 = bw | red; break;
                    case STYLE_BW_STANDARD: dtm1 = (~red) & bw; break;
                    case STYLE_BW_INVERTED: dtm1 = ~((~red) & bw); break;
                    case STYLE_RW_STANDARD:
                    case STYLE_RW_INVERTED: dtm1 = 0xFF; break;
                    default:                dtm1 = bw | red; break;
                }
#if (BOARD == BOARD_HANSHOW_E31HA)
                if (((off + j) & 0x0F) == 15) dtm1 |= 0x3F;
#endif
                EPD_WriteData(dtm1);
            }
        }

        // Stream DTM2 for Info / Blank
#if (BOARD == BOARD_HANSHOW_E31PA)
        EPD_BWR_420_start_dtm2();
#else
        EPD_WriteCmd(0x13);
#endif

        for (uint16_t off = 0; off < EPD_PLANE_SIZE; off += sizeof(chunk_bw)) {
            ble_display_poll();
            uint16_t chunk_len = (off + sizeof(chunk_bw) > EPD_PLANE_SIZE) ? (EPD_PLANE_SIZE - off) : sizeof(chunk_bw);

            if (slot_idx == EPD_SLOT_INFO) {
                for (uint16_t j = 0; j < chunk_len; j++) {
                    chunk_bw[j] = epd_get_info_pixel_byte(off + j);
                }
                memset(chunk_red, 0x00, chunk_len);
            } else {
                memset(chunk_bw, 0xFF, chunk_len);
                memset(chunk_red, 0x00, chunk_len);
            }

            for (uint16_t j = 0; j < chunk_len; j++) {
                uint8_t bw = chunk_bw[j];
                uint8_t red = chunk_red[j];
                uint8_t dtm2;

                switch (style) {
                    case STYLE_STANDARD:    dtm2 = red; break;
                    case STYLE_BW_STANDARD:
                    case STYLE_BW_INVERTED: dtm2 = 0x00; break;
                    case STYLE_RW_STANDARD: dtm2 = ~((~red) & bw); break;
                    case STYLE_RW_INVERTED: dtm2 = (~red) & bw; break;
                    default:                dtm2 = red; break;
                }
#if (BOARD == BOARD_HANSHOW_E31HA)
                if (((off + j) & 0x0F) == 15) dtm2 &= 0xC0;
#elif (BOARD == BOARD_HANSHOW_E31PA)
                dtm2 = ~dtm2;
#endif
                EPD_WriteData(dtm2);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Trigger Display Refresh
    // -------------------------------------------------------------------------
#if (BOARD == BOARD_HANSHOW_E31PA)
    EPD_BWR_420_refresh();
#else
    EPD_WriteCmd(0x12); // DRF
    unsigned long t0 = clock_time();
    while ((gpio_read(GPIO_EPD_BUSY) != 0) && (clock_time() - t0 < 100 * CLOCK_16M_SYS_TIMER_CLK_1MS)) {
        epd_delay_ms(1);
    }
#endif

    epd_update_state = 1;
    epd_refresh_start_32k_tick = pm_get_32k_tick();

    settings.screen_refresh_count++;
    if ((settings.screen_refresh_count % 50) == 0) {
        flash_eep_save();
    }
}
