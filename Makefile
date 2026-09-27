PROJECT_NAME ?= tlsr-epaper-m3na
BOARD ?= BOARD_HANSHOW_E31HA
VERSION_BIN ?=
ZNAME ?= "Hanshow:E31HA-dual"

TEL_CHIP := -DMCU_CORE_8258=1 -DEND_DEVICE=1 -DMCU_STARTUP_8258=1 -D__PROJECT_TL_SWITCH__=1 -DBLE_CONCURRENT_MODE=1 -DBOOT_LOADER_MODE=0

LIBS := -lble_8258 -ldrivers_8258 -lzb_ed

PROJECT_PATH ?= .
SRC_DIR ?= /src
SRC_PATH ?= $(PROJECT_PATH)$(SRC_DIR)
TEL_PATH ?= .
SDK_PATH ?= ./SDK
SDK_FLAGS := $(SDK_PATH)/stack
MAKE_PATH ?= ./make

LS_FLAGS := $(SRC_PATH)/boot.link

OUT_PATH ?= ./build/$(PROJECT_NAME)
BIN_PATH ?= ./bin

OBJ_SRCS :=
S_SRCS :=
ASM_SRCS :=
C_SRCS :=
S_UPPER_SRCS :=
O_SRCS :=
FLASH_IMAGE :=
ELFS :=
OBJS :=
LST :=
SIZEDUMMY :=
OUT_DIR :=

PYTHON ?= python3

ZCL_VERSION_FILE := $(shell git log -1 --format=%cd --date=format:%Y%m%d -- $(SRC_PATH) | sed -e "s/./\'&\',/g" -e "s/,$$//")

ifeq ($(strip $(ZCL_VERSION_FILE)),)
GCC_FLAGS += -DBUILD_DATE="{8,'2','0','2','6','0','9','2','2'}"
else
GCC_FLAGS += -DBUILD_DATE="{8,$(ZCL_VERSION_FILE)}"
endif

ifneq ($(TC32PATH),)
	TC32_PATH := $(TC32PATH)/
else ifneq ($(TC32_BIN),)
	TC32_PATH := $(dir $(TC32_BIN))
else ifneq ($(wildcard ../reference/ATC_TLSR_Paper/Firmware/tc32_linux/bin/tc32-elf-gcc),)
	TC32_PATH := ../reference/ATC_TLSR_Paper/Firmware/tc32_linux/bin/
else ifneq ($(wildcard /opt/tc32/bin/tc32-elf-gcc),)
	TC32_PATH := /opt/tc32/bin/
else
	TC32_PATH := tc32/bin/
endif

LNK_FLAGS := --gc-sections -nostartfiles

GCC_FLAGS := \
	-O2 \
	-ffunction-sections \
	-fdata-sections \
	-Wall \
	-fpack-struct \
	-fshort-enums \
	-finline-small-functions \
	-std=gnu99 \
	-funsigned-char \
	-fshort-wchar \
	-fms-extensions \
	-nostartfiles \
	-nostdlib \
	-Wno-unused-variable \
	-Wno-unused-function

ASM_FLAGS := \
	-fomit-frame-pointer \
	-fshort-enums \
	-Wall \
	-Wpacked \
	-Wcast-align \
	-fdata-sections \
	-ffunction-sections \
	-fno-use-cxa-atexit \
	-fno-rtti \
	-fno-threadsafe-statics

INCLUDE_PATHS := -I$(SRC_PATH) -I$(SRC_PATH)/epd -I$(SRC_PATH)/zigbee -I$(SRC_PATH)/ble \
	-I$(SDK_PATH) \
	-I$(SDK_PATH)/proj \
	-I$(SDK_PATH)/proj/common \
	-I$(SDK_PATH)/platform \
	-I$(SDK_PATH)/platform/chip_8258 \
	-I$(SDK_PATH)/stack/ble \
	-I$(SDK_PATH)/stack/ble/ble_8258 \
	-I$(SDK_PATH)/stack/zigbee/af \
	-I$(SDK_PATH)/stack/zigbee/include \
	-I$(SDK_PATH)/stack/zigbee/bdb/includes \
	-I$(SDK_PATH)/stack/zigbee/common/includes \
	-I$(SDK_PATH)/stack/zigbee/ota \
	-I$(SDK_PATH)/stack/zigbee/zbapi \
	-I$(SDK_PATH)/stack/zigbee/zcl \
	-I$(SDK_PATH)/stack/zigbee/zdo

GCC_FLAGS += $(TEL_CHIP) -DBOARD=$(BOARD)

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	GCC_FLAGS += -DDEBUG=1 -DDEBUG_MODE=1 -DUART_PRINTF_MODE=1 -DDEBUG_INFO_TX_PIN=GPIO_PB1
else
	GCC_FLAGS += -DDEBUG=0 -DDEBUG_MODE=0 -DUART_PRINTF_MODE=0
endif



LS_INCLUDE := -L$(SDK_PATH)/platform/lib -L$(SDK_PATH)/stack/zigbee/lib/tc32 -L$(SDK_PATH)/stack/ble/lib -L$(OUT_PATH)

-include $(MAKE_PATH)/src.mk
-include $(MAKE_PATH)/platform.mk
-include $(MAKE_PATH)/proj.mk
-include $(MAKE_PATH)/zigbee.mk

LST_FILE := $(OUT_PATH)/$(PROJECT_NAME).lst
BIN_FILE := $(BIN_PATH)/$(PROJECT_NAME)$(VERSION_BIN).bin
OTA_FILE := $(BIN_PATH)/$(PROJECT_NAME).zigbee
ELF_FILE := $(OUT_PATH)/$(PROJECT_NAME).elf

SIZEDUMMY := sizedummy

default: all

all: m3na xl3na

m3na:
	@$(MAKE) build-target BOARD=BOARD_HANSHOW_E31HA PROJECT_NAME=tlsr-epaper-m3na ZNAME="Hanshow:E31HA-dual"

xl3na:
	@$(MAKE) build-target BOARD=BOARD_HANSHOW_E31PA PROJECT_NAME=tlsr-epaper-xl3na ZNAME="Hanshow:E31PA-dual"

build-target: pre-build main-build

main-build: $(ELF_FILE) secondary-outputs

OBJ_LIST := $(OBJS) $(USER_OBJS)

$(ELF_FILE): $(OBJ_LIST)
	@echo 'Building Standard target: $@'
	@$(TC32_PATH)tc32-elf-ld $(LNK_FLAGS) -Map $(OUT_PATH)/$(PROJECT_NAME).map $(LS_INCLUDE) -T$(LS_FLAGS) -o $(ELF_FILE) $(OBJ_LIST) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

$(LST_FILE): $(ELF_FILE)
	@echo 'Invoking: TC32 Create Extended Listing'
	@$(TC32_PATH)tc32-elf-objdump -x -D -l -S $(ELF_FILE) > $(LST_FILE)
	@echo 'Finished building: $@'
	@echo ' '

$(BIN_FILE): $(ELF_FILE)
	@echo 'Create Flash image (binary format)'
	@$(TC32_PATH)tc32-elf-objcopy -v -O binary $(ELF_FILE) $(BIN_FILE)
	@$(PYTHON) $(MAKE_PATH)/tl_check_fw.py $(BIN_FILE)
	@echo 'Finished building: $@'
	@echo ' '

$(OTA_FILE): $(BIN_FILE)
	@echo 'Create OTA image'
	@$(PYTHON) $(MAKE_PATH)/zigbee_ota.py $(BIN_FILE) -p $(BIN_PATH) -n $(PROJECT_NAME) -s $(ZNAME)
	@cp -f $(BIN_PATH)/1141-*-$(PROJECT_NAME).zigbee $(OTA_FILE) 2>/dev/null || true
	@echo 'Finished building: $@'
	@echo ' '

sizedummy: $(ELF_FILE)
	@$(PYTHON) $(MAKE_PATH)/TlsrMemInfo.py -t $(TC32_PATH)tc32-elf-nm $(ELF_FILE)
	@echo ' '

clean:
	@rm -rf ./build $(BIN_PATH)
	@echo 'Clean complete.'

pre-build:
	@mkdir -p $(foreach s,$(OUT_DIR),$(OUT_PATH)$(s))
	@mkdir -p $(BIN_PATH)

secondary-outputs: $(BIN_FILE) $(OTA_FILE) $(LST_FILE) $(SIZEDUMMY)

.PHONY: all default m3na xl3na build-target clean pre-build secondary-outputs
.SECONDARY: main-build
