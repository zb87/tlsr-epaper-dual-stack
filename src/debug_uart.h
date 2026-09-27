#ifndef _DEBUG_UART_H_
#define _DEBUG_UART_H_

#include <stdint.h>
#include "tl_common.h"
#include "app_config.h"

#ifdef DEBUG_MODE
#undef DEBUG
#define DEBUG DEBUG_MODE
#endif

#define LOG_RING_CAPACITY       40
#define LOG_ENTRY_TEXT_LEN      54

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;              // System uptime in milliseconds
    uint16_t seq;                       // 1-based monotonic sequence number
    char     text[LOG_ENTRY_TEXT_LEN];  // Null-terminated message "[TAG] Text"
} ble_log_entry_t;                      // Exactly 60 bytes

#if DEBUG
void debug_uart_init(void);
void debug_uart_putc(char c);
void debug_uart_puts(const char *s);
void debug_uart_flush(void);
void debug_uart_sync(void);

void debug_log(const char *tag, const char *fmt, ...);
int  debug_printf(const char *fmt, ...);
void debug_dump_hex(const char *tag, const void *data, unsigned int len);

#define DEBUG_LOG(tag, fmt, ...) debug_log(tag, fmt, ##__VA_ARGS__)
#define DEBUG_PRINT(fmt, ...)    debug_printf(fmt, ##__VA_ARGS__)
#define DEBUG_HEX(tag, data, len) debug_dump_hex(tag, data, len)

// In-Memory Debug Log Ring Buffer APIs
void log_ring_init(void);
void log_ring_push(const char *tag, const char *msg);
uint16_t log_ring_get_latest_seq(void);
uint16_t log_ring_get_oldest_seq(void);
bool log_ring_get_entry(uint16_t seq, ble_log_entry_t *out_entry);
void log_ring_clear(void);
#else // !DEBUG
#define debug_uart_init()         do {} while(0)
#define debug_uart_putc(c)        ((void)0)
#define debug_uart_puts(s)        ((void)0)
#define debug_uart_flush()        do {} while(0)
#define debug_uart_sync()         do {} while(0)

void debug_log(const char *tag, const char *fmt, ...);
int  debug_printf(const char *fmt, ...);
void debug_dump_hex(const char *tag, const void *data, unsigned int len);

#define DEBUG_LOG(tag, fmt, ...)  do {} while(0)
#define DEBUG_PRINT(fmt, ...)     do {} while(0)
#define DEBUG_HEX(tag, data, len) do {} while(0)

#define log_ring_init()           do {} while(0)
#define log_ring_push(tag, msg)   do {} while(0)
#define log_ring_get_latest_seq() ((uint16_t)0)
#define log_ring_get_oldest_seq() ((uint16_t)0)
#define log_ring_get_entry(seq, out_entry) (false)
#define log_ring_clear()          do {} while(0)
#endif

#endif // _DEBUG_UART_H_
