#include <string.h>
#include "debug_uart.h"
#include "u_printf.h"
#include "tl_common.h"

#if DEBUG

// ---------------------------------------------------------------------------
// In-Memory Debug Log Ring Buffer (40 entries x 60 B = 2,400 B in retention .bss)
// ---------------------------------------------------------------------------
static ble_log_entry_t s_log_ring[LOG_RING_CAPACITY];
static uint16_t s_log_head = 0;
static uint16_t s_log_seq = 0;
static uint16_t s_log_count = 0;

void log_ring_init(void) {
    s_log_head = 0;
    s_log_seq = 0;
    s_log_count = 0;
}

void log_ring_clear(void) {
    s_log_head = 0;
    s_log_count = 0;
}

extern u32 pm_get_32k_tick(void);

void log_ring_push(const char *tag, const char *msg) {
    ble_log_entry_t *entry = &s_log_ring[s_log_head];
    // pm_get_32k_tick() runs continuously at 32,000 Hz across active, suspend, and deep sleep.
    // 32,000 ticks / 1,000 ms = 32 ticks/ms.
    entry->timestamp_ms = pm_get_32k_tick() / 32;
    entry->seq = ++s_log_seq;

    if (tag && tag[0]) {
        u_snprintf(entry->text, sizeof(entry->text), "[%s] %s", tag, msg ? msg : "");
    } else {
        u_snprintf(entry->text, sizeof(entry->text), "%s", msg ? msg : "");
    }
    entry->text[sizeof(entry->text) - 1] = '\0';

    s_log_head = (s_log_head + 1) % LOG_RING_CAPACITY;
    if (s_log_count < LOG_RING_CAPACITY) s_log_count++;
}

uint16_t log_ring_get_latest_seq(void) {
    return s_log_seq;
}

uint16_t log_ring_get_oldest_seq(void) {
    if (s_log_count == 0) return 0;
    if (s_log_seq <= s_log_count) return 1;
    return s_log_seq - s_log_count + 1;
}

bool log_ring_get_entry(uint16_t seq, ble_log_entry_t *out_entry) {
    if (!out_entry || seq == 0 || s_log_count == 0) return false;
    uint16_t oldest = log_ring_get_oldest_seq();
    if (seq < oldest || seq > s_log_seq) return false;

    int diff = (int)s_log_seq - (int)seq;
    if (diff < 0 || diff >= (int)s_log_count) return false;
    int idx = (int)s_log_head - 1 - diff;
    while (idx < 0) idx += LOG_RING_CAPACITY;
    idx = idx % LOG_RING_CAPACITY;

    memcpy(out_entry, &s_log_ring[idx], sizeof(ble_log_entry_t));
    return true;
}

// Hardware UART Non-DMA Mode on TLSR8258
// System clock = 24 MHz Crystal
// 115200 baud: div = 12, bwpc = 15 -> 24000000 / ((12+1)*(15+1)) = 115384.6 baud (0.16% error)

void debug_uart_init(void) {
    gpio_set_output_en(GPIO_UART_TX, 1);
    gpio_write(GPIO_UART_TX, 1);
    gpio_setup_up_down_resistor(GPIO_UART_TX, PM_PIN_PULLUP_10K);
    gpio_set_func(GPIO_UART_TX, AS_GPIO);

    reg_clk_en0 |= FLD_CLK0_UART_EN;

    uart_reset();
    uart_init(12, 15, PARITY_NONE, STOP_BIT_ONE);
    uart_dma_enable(0, 0); // Non-DMA mode

    uart_ndma_clear_tx_index();

    gpio_set_input_en(GPIO_UART_TX, 1);
    gpio_set_func(GPIO_UART_TX, AS_UART);
}

void debug_uart_putc(char c) {
    if (c == '\n') {
        uart_ndma_send_byte('\r');
    }
    uart_ndma_send_byte((unsigned char)c);
}

void debug_uart_puts(const char *s) {
    if (!s) return;
    while (*s) {
        debug_uart_putc(*s++);
    }
}

extern int device_in_connection_state;

_attribute_ram_code_
void debug_uart_flush(void) {
    if (((reg_uart_buf_cnt >> 4) != 0) || !(reg_uart_status1 & FLD_UART_TX_DONE)) {
        while (((reg_uart_buf_cnt >> 4) != 0) || !(reg_uart_status1 & FLD_UART_TX_DONE));
        WaitUs(100);
    }
}

_attribute_ram_code_
void debug_uart_sync(void) {
    if (!device_in_connection_state) {
        WaitUs(200);
    }
    uart_ndma_clear_tx_index();
}

void drv_putchar(unsigned char byte) {
    debug_uart_putc((char)byte);
}

void debug_log(const char *tag, const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = u_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) buf[0] = '\0';

    log_ring_push(tag, buf);

    if (tag) {
        debug_uart_putc('[');
        debug_uart_puts(tag);
        debug_uart_puts("] ");
    }
    if (len > 0) {
        debug_uart_puts(buf);
    }
    debug_uart_puts("\r\n");
    debug_uart_flush();
}

int debug_printf(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = u_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) return 0;

    log_ring_push(NULL, buf);

    debug_uart_puts(buf);
    debug_uart_flush();
    return len;
}

void debug_dump_hex(const char *tag, const void *data, unsigned int len) {
    if (!data || len == 0) return;
    const unsigned char *p = (const unsigned char *)data;
    char hex_str[64];
    uint16_t pos = 0;
    for (unsigned int i = 0; i < len && pos + 3 < sizeof(hex_str); i++) {
        u_snprintf(&hex_str[pos], sizeof(hex_str) - pos, "%02X ", p[i]);
        pos += 3;
    }
    debug_log(tag ? tag : "HEX", "%s(%u B)", hex_str, len);
}

#else // !DEBUG

void drv_putchar(unsigned char byte) {
    (void)byte;
}

void debug_log(const char *tag, const char *fmt, ...) {
    (void)tag;
    (void)fmt;
}

int debug_printf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

void debug_dump_hex(const char *tag, const void *data, unsigned int len) {
    (void)tag;
    (void)data;
    (void)len;
}

#endif // DEBUG
