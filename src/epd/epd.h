#ifndef _EPD_H_
#define _EPD_H_

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

extern RAM uint8_t epd_update_state;
extern RAM uint8_t epd_temperature;

extern uint8_t shared_scratch_ram[SHARED_SCRATCH_RAM_SIZE];
#define epd_render_buffer shared_scratch_ram

void epd_init(void);
uint8_t epd_read_temp(void);
void epd_set_sleep(void);
uint8_t epd_state_handler(void);
bool epd_is_busy(void);

// Displays the specified slot (0=Info, 1..=User Image, Blank) with selected style (0..4)
void epd_display_slot(uint8_t slot_idx, uint8_t style);
void epd_clear(void);

#endif // _EPD_H_
