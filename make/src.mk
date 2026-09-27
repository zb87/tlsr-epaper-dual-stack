OUT_DIR += $(SRC_DIR) \
	$(SRC_DIR)/patch_sdk \
	$(SRC_DIR)/epd \
	$(SRC_DIR)/zigbee \
	$(SRC_DIR)/ble

OBJS += \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/flash.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/flash_drv.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/adc_drv.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/random.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/i2c_drv.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/hw_drv.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/cstartup_8258.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/drv_nv.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/mac_pib.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/utility.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/irq_handler.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/ev.o \
	$(OUT_PATH)$(SRC_DIR)/patch_sdk/ev_buffer.o \
	$(OUT_PATH)$(SRC_DIR)/battery.o \
	$(OUT_PATH)$(SRC_DIR)/flash_eep.o \
	$(OUT_PATH)$(SRC_DIR)/led.o \
	$(OUT_PATH)$(SRC_DIR)/mode_switch.o \
	$(OUT_PATH)$(SRC_DIR)/nfc_fm11nc08.o \
	$(OUT_PATH)$(SRC_DIR)/zigbee_ble_switch.o \
	$(OUT_PATH)$(SRC_DIR)/main.o \
	$(OUT_PATH)$(SRC_DIR)/u_printf.o \
	$(OUT_PATH)$(SRC_DIR)/debug_uart.o \
	$(OUT_PATH)$(SRC_DIR)/power_tracker.o \
	$(OUT_PATH)$(SRC_DIR)/epd/epd.o \
	$(OUT_PATH)$(SRC_DIR)/epd/epd_slots.o \
	$(OUT_PATH)$(SRC_DIR)/epd/epd_spi.o \
	$(OUT_PATH)$(SRC_DIR)/epd/epd_prototype.o \
	$(OUT_PATH)$(SRC_DIR)/epd/one_bit_display.o \
	$(OUT_PATH)$(SRC_DIR)/zigbee/zb_app.o \
	$(OUT_PATH)$(SRC_DIR)/zigbee/zb_appCb.o \
	$(OUT_PATH)$(SRC_DIR)/zigbee/zb_endpoint_cfg.o \
	$(OUT_PATH)$(SRC_DIR)/zigbee/zcl_appCb.o \
	$(OUT_PATH)$(SRC_DIR)/ble/ble_app.o \
	$(OUT_PATH)$(SRC_DIR)/ble/bthome_beacon.o

ifeq ($(strip $(BOARD)), BOARD_HANSHOW_E31PA)
OBJS += $(OUT_PATH)$(SRC_DIR)/epd/epd_bwr_420.o
else
OBJS += $(OUT_PATH)$(SRC_DIR)/epd/epd_bwr_213.o
endif

# Each subdirectory must supply rules for building sources it contributes
$(OUT_PATH)$(SRC_DIR)/%.o: $(PROJECT_PATH)$(SRC_DIR)/%.c
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"

$(OUT_PATH)$(SRC_DIR)/%.o: $(PROJECT_PATH)$(SRC_DIR)/%.S
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(ASM_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"