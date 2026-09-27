#ifndef _EPD_PROTOTYPE_H_
#define _EPD_PROTOTYPE_H_

#include <stdint.h>
#include <stdbool.h>

#define PROTOTYPE_MAGIC 0x54455354 // 'TEST'

// Hardware GPIO Pin Definitions (Hanshow Stellar-M3N@ 2.13" & Stellar-XL3N@ 4.2")
// Telink GPIO encoding: (Group << 8) | Bitmask
// Group A: 0x000, Group B: 0x100, Group C: 0x200, Group D: 0x300
#define EPD_PIN_RESET       0x0310 // PD4 (GPIO_GROUPD | BIT(4))
#define EPD_PIN_BUSY        0x0002 // PA1 (GPIO_GROUPA | BIT(1))
#define EPD_PIN_DC          0x0380 // PD7 (GPIO_GROUPD | BIT(7))
#define EPD_PIN_CS          0x0110 // PB4 (GPIO_GROUPB | BIT(4))
#define EPD_PIN_CLK         0x0120 // PB5 (GPIO_GROUPB | BIT(5))
#define EPD_PIN_MOSI        0x0140 // PB6 (GPIO_GROUPB | BIT(6))

typedef struct {
    uint16_t struct_version; // 1
    uint16_t board_type;     // 0 = M3Na (2.13"), 1 = XL3Na (4.2")
    // Hardware primitives
    void (*write_cmd)(uint8_t cmd);
    void (*write_data)(uint8_t data);
    uint8_t (*read_spi)(void);
    void (*sleep_us)(uint32_t us);
    void (*sleep_ms)(uint32_t ms);
    void (*gpio_write)(uint32_t pin, uint8_t val);
    uint8_t (*gpio_read)(uint32_t pin);
    void (*epd_power_on)(void);
    void (*epd_power_off)(void);
    void (*epd_init_pins)(void);
    void (*epd_isolate_pins)(void);
    // Logging primitive
    void (*log)(const char *tag, const char *fmt, ...);
    // Framebuffer access
    uint8_t *render_buffer;
    uint32_t buffer_size;
    // Current runtime settings
    uint8_t  active_slot;
    uint8_t  render_style;
    uint8_t  reserved[2];
} epd_test_api_t;

typedef int (*epd_snippet_entry_t)(const epd_test_api_t *api);

// Helper: check if display controller is BUSY
// On UC8151 (2.13" M3Na) and UC8176 (4.2" XL3Na), the BUSY pin is LOW (0) when active/busy,
// and HIGH (!= 0) when idle/ready.
static inline bool epd_proto_is_busy(const epd_test_api_t *api) {
    return api->gpio_read(EPD_PIN_BUSY) == 0;
}

// Commits code stored in s_ota_cache to flash at target_addr (default PROTOTYPE_CODE_ADDR) and invalidates cache
bool epd_prototype_commit(uint32_t flash_addr, const uint8_t *code_buf, uint16_t len);

// Runs snippet at flash_addr (default PROTOTYPE_CODE_ADDR) and returns exit code & duration
int epd_prototype_execute(uint32_t flash_addr, uint32_t *duration_ms);

#endif // _EPD_PROTOTYPE_H_
