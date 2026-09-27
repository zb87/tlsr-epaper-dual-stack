OUT_DIR += \
	/zigbee/af \
	/zigbee/aps \
	/zigbee/bdb \
	/zigbee/common \
	/zigbee/mac \
	/zigbee/ss \
	/zigbee/ota \
	/zigbee/zdo \
	/zigbee/zcl \
	/zigbee/zcl/general \
	/zigbee/zcl/ota_upgrading

OBJS += \
	$(OUT_PATH)/zigbee/bdb/bdb.o \
	$(OUT_PATH)/zigbee/aps/aps_group.o \
	$(OUT_PATH)/zigbee/mac/mac_phy.o \
	$(OUT_PATH)/zigbee/zdo/zdp.o \
	$(OUT_PATH)/zigbee/zcl/zcl.o \
	$(OUT_PATH)/zigbee/zcl/zcl_nv.o \
	$(OUT_PATH)/zigbee/zcl/zcl_reporting.o \
	$(OUT_PATH)/zigbee/zcl/general/zcl_basic.o \
	$(OUT_PATH)/zigbee/zcl/general/zcl_basic_attr.o \
	$(OUT_PATH)/zigbee/zcl/general/zcl_powerCfg.o \
	$(OUT_PATH)/zigbee/zcl/general/zcl_powerCfg_attr.o \
	$(OUT_PATH)/zigbee/zcl/general/zcl_identify.o \
	$(OUT_PATH)/zigbee/zcl/general/zcl_identify_attr.o \
	$(OUT_PATH)/zigbee/zcl/ota_upgrading/zcl_ota.o \
	$(OUT_PATH)/zigbee/zcl/ota_upgrading/zcl_ota_attr.o \
	$(OUT_PATH)/zigbee/common/zb_config.o \
	$(OUT_PATH)/zigbee/af/zb_af.o \
	$(OUT_PATH)/zigbee/ss/ss_nv.o \
	$(OUT_PATH)/zigbee/ota/ota.o \
	$(OUT_PATH)/zigbee/ota/otaEpCfg.o

# Each subdirectory must supply rules for building sources it contributes
$(OUT_PATH)/zigbee/%.o: $(SDK_PATH)/stack/zigbee/%.c
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"
