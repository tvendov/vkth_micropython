# Select the board to build for:
ifdef BOARD_DIR
# Custom board path - remove trailing slash and get the final component of
# the path as the board name.
BOARD ?= $(notdir $(BOARD_DIR:/=))
else
# If not given on the command line, then default to VK_RA4M2.
BOARD ?= VK_RA4M2
BOARD_DIR ?= boards/$(BOARD)
endif

ifeq ($(wildcard $(BOARD_DIR)/.),)
$(error Invalid BOARD specified: $(BOARD_DIR))
endif

# If the build directory is not given, make it reflect the board name.
BUILD ?= build-$(BOARD)

# select use wrapper function of FSP library
USE_FSP_FLASH = 1

include ../../py/mkenv.mk
-include mpconfigport.mk
include $(BOARD_DIR)/mpconfigboard.mk

USE_FSP_LPM ?= 1
USE_FSP_QSPI ?= 0
USE_FSP_SDHI ?= 0
USE_FSP_ETH ?= 0
USE_FSP_LCD ?= 0
USE_FSP_CAM ?= 0
USE_FSP_DRW ?= 0
MICROPY_HW_ENABLE_SCI_I2C ?= 0
FSP_BOARD_NAME ?= $(shell echo $(BOARD) | tr '[:upper:]' '[:lower:]')

# qstr definitions (must come before including py.mk)
QSTR_DEFS += qstrdefsport.h

# Files that are generated and needed before the QSTR build.
QSTR_GLOBAL_DEPENDENCIES += mpconfigboard_common.h $(BOARD_DIR)/mpconfigboard.h

# LVGL integration for the parallel RGB display on VK_RA6M3.
ifeq ($(BOARD),VK_RA6M3)
ifeq ($(MICROPY_PY_LVGL),1)
GIT_SUBMODULES += lib/lv_bindings
LVGL_BINDING_DIR = $(TOP)/lib/lv_bindings
LVGL_DIR = $(LVGL_BINDING_DIR)/lib/lvgl
LVGL_BOARD_CONF = $(wildcard $(BOARD_DIR)/lv_conf_board.h)
LVGL_HEADERS = $(shell find $(LVGL_DIR) -type f -name '*.h') $(LVGL_BINDING_DIR)/lib/lv_conf.h $(LVGL_BOARD_CONF)
LVGL_GENERATOR = $(LVGL_BINDING_DIR)/gen/python_api_gen_mpy.py
LVGL_GENERATOR_LAUNCHER = lvgl_gen.py
LVGL_MPY_REL = lvgl/lv_mpy.c
LVGL_MPY = $(BUILD)/$(LVGL_MPY_REL)
LVGL_MPY_OBJ = $(BUILD)/lvgl/lv_mpy.o
LVGL_CFLAGS = -DMICROPY_PY_LVGL=1 -DMICROPY_COLOR_DEPTH=16 -DMICROPY_TINY_TTF=0 -DSTATIC=static -Wno-error=unused-function
ifneq ($(LVGL_BOARD_CONF),)
LVGL_CFLAGS += -include $(LVGL_BOARD_CONF)
endif

INC += -I$(LVGL_BINDING_DIR)/lib
CFLAGS_EXTMOD += $(LVGL_CFLAGS)
LVGL_SRC_C = $(subst $(TOP)/,,$(shell find $(LVGL_DIR)/src -type f -name '*.c'))
LVGL_DAVE2D_SRC_C = $(filter lib/lv_bindings/lib/lvgl/src/draw/renesas/dave2d/%.c,$(LVGL_SRC_C))
LVGL_DAVE2D_OBJ = $(addprefix $(BUILD)/,$(LVGL_DAVE2D_SRC_C:.c=.o))
ifeq ($(USE_FSP_DRW),1)
# VK_RA6M3 supplies a wrapper around this translation unit to add the
# MicroPython soft-reset lifecycle missing from the LVGL 9.4 implementation.
LVGL_SRC_C := $(filter-out lib/lv_bindings/lib/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c,$(LVGL_SRC_C))
$(LVGL_DAVE2D_OBJ): CFLAGS += -include $(BOARD_DIR)/dave2d_compat.h -Wno-unused-variable
endif
SRC_THIRDPARTY_C += $(LVGL_SRC_C)
SRC_THIRDPARTY_C += lib/lv_bindings/ext_mod/lvgl/mem_core.c
QSTR_GLOBAL_DEPENDENCIES += $(LVGL_MPY)

$(LVGL_MPY): $(LVGL_HEADERS) $(LVGL_GENERATOR) $(LVGL_GENERATOR_LAUNCHER)
	$(ECHO) "LVGL-GEN $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)PYTHONHASHSEED=0 $(PYTHON) $(LVGL_GENERATOR_LAUNCHER) $(LVGL_GENERATOR) $(LVGL_CFLAGS) --board=renesas-ra --output=$@ --include=$(LVGL_BINDING_DIR)/lib --include=$(LVGL_DIR) --module_name=lvgl --module_prefix=lv --metadata=$(BUILD)/lvgl/lv_mpy.json --header_file=$(LVGL_DIR)/lvgl.h

.PHONY: lvgl-submodules
lvgl-submodules:
	$(ECHO) "Updating LVGL binding and nested submodules"
	$(Q)cd $(TOP) && git submodule sync -- lib/lv_bindings
	$(Q)cd $(TOP) && git submodule update --init --filter=blob:none lib/lv_bindings
	$(Q)cd $(LVGL_BINDING_DIR) && git submodule sync -- lib/lvgl lib/pycparser
	$(Q)cd $(LVGL_BINDING_DIR) && git submodule update --init --filter=blob:none lib/lvgl lib/pycparser

submodules: lvgl-submodules
endif
endif

# MicroPython feature configurations
MICROPY_ROM_TEXT_COMPRESSION ?= 1

# File containing description of content to be frozen into firmware.
FROZEN_MANIFEST ?= boards/manifest.py

# include py core make definitions
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk
SRC_EXTMOD_C := $(filter-out extmod/machine_timer.c,$(SRC_EXTMOD_C))
PY_O := $(filter-out $(BUILD)/extmod/machine_timer.o,$(PY_O))
SRC_QSTR := $(filter-out extmod/machine_timer.c,$(SRC_QSTR))

ifeq ($(BOARD),VK_RA6M3)
ifeq ($(MICROPY_PY_LVGL),1)
PY_O += $(LVGL_MPY_OBJ)
SRC_QSTR += $(LVGL_MPY)
endif
endif

GIT_SUBMODULES += lib/fsp lib/tinyusb

MCU_SERIES_UPPER = $(shell echo $(MCU_SERIES) | tr '[:lower:]' '[:upper:]')
CMSIS_MCU_LOWER = $(shell echo $(CMSIS_MCU) | tr '[:upper:]' '[:lower:]')

LD_DIR=boards
CMSIS_DIR=lib/cmsis/inc
HAL_DIR=lib/fsp
STARTUP_FILE ?= lib/fsp/ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/startup.o
SYSTEM_FILE ?= lib/fsp/ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.o

# Select the cross compile prefix
CROSS_COMPILE ?= arm-none-eabi-

INC += -imacros boards/compiler_barrier.h
INC += -I.
INC += -Ifsp_cfg
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(TOP)/$(CMSIS_DIR)
INC += -I$(TOP)/$(HAL_DIR)
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/inc
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/inc/api
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/inc/instances
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/src/bsp/cmsis/Device/RENESAS/Include
INC += -I$(TOP)/lib/tinyusb/hw
INC += -I$(TOP)/lib/tinyusb/src
INC += -I$(TOP)/shared/tinyusb
INC += -Ilwip_inc
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M1 RA4M2 RA4W1 RA6M1 RA6M2 RA6M3 RA6M5))
INC += -Ira
endif
INC += -I$(BOARD_DIR)/ra_gen
INC += -I$(BOARD_DIR)/ra_cfg/driver
INC += -I$(BOARD_DIR)/ra_cfg/fsp_cfg
INC += -I$(BOARD_DIR)/ra_cfg/fsp_cfg/bsp
INC += -Idebug

CFLAGS += -D$(CMSIS_MCU)
CFLAGS += -DRA_HAL_H='<$(CMSIS_MCU)_hal.h>'
CFLAGS += -DRA_CFG_H='<$(FSP_BOARD_NAME)_conf.h>'
CFLAGS += -DCFG_TUSB_MCU=OPT_MCU_RAXXX

# Basic Cortex-M flags
CFLAGS_CORTEX_M = -mthumb

# Select hardware floating-point support
SUPPORTS_HARDWARE_FP_SINGLE = 0
SUPPORTS_HARDWARE_FP_DOUBLE = 0
ifeq ($(MCU_SERIES),$(filter $(MCU_SERIES),m4))
CFLAGS_CORTEX_M += -mfpu=fpv4-sp-d16 -mfloat-abi=hard
SUPPORTS_HARDWARE_FP_SINGLE = 1
endif

ifeq ($(MCU_SERIES),$(filter $(MCU_SERIES),m33))
CFLAGS_CORTEX_M += -mfpu=fpv5-sp-d16 -mfloat-abi=hard
SUPPORTS_HARDWARE_FP_SINGLE = 1
endif

# Options for particular MCU series
CFLAGS_MCU_RA4M1 = $(CFLAGS_CORTEX_M) -mtune=cortex-m4 -mcpu=cortex-m4
CFLAGS_MCU_RA4M2 = $(CFLAGS_CORTEX_M) -mtune=cortex-m33 -mcpu=cortex-m33
CFLAGS_MCU_RA4W1 = $(CFLAGS_CORTEX_M) -mtune=cortex-m4 -mcpu=cortex-m4
CFLAGS_MCU_RA6M1 = $(CFLAGS_CORTEX_M) -mtune=cortex-m4 -mcpu=cortex-m4
CFLAGS_MCU_RA6M2 = $(CFLAGS_CORTEX_M) -mtune=cortex-m4 -mcpu=cortex-m4
CFLAGS_MCU_RA6M3 = $(CFLAGS_CORTEX_M) -mtune=cortex-m4 -mcpu=cortex-m4 -DRA6M2
CFLAGS_MCU_RA6M5 = $(CFLAGS_CORTEX_M) -mtune=cortex-m33 -mcpu=cortex-m33

ASFLAGS += $(CFLAGS_CORTEX_M) -mcpu=cortex-$(MCU_SERIES)
CFLAGS += $(INC) -Wall -Wpointer-arith -Werror -Wdouble-promotion -Wfloat-conversion -std=gnu99 -nostdlib $(CFLAGS_EXTRA)
#CFLAGS += -D$(CMSIS_MCU)
CFLAGS += $(CFLAGS_MCU_$(CMSIS_MCU))
CFLAGS += $(COPT)
CFLAGS += -I$(BOARD_DIR)

# Configure floating point support
ifeq ($(MICROPY_FLOAT_IMPL),double)
CFLAGS += -DMICROPY_FLOAT_IMPL=MICROPY_FLOAT_IMPL_DOUBLE
else
ifeq ($(MICROPY_FLOAT_IMPL),none)
CFLAGS += -DMICROPY_FLOAT_IMPL=MICROPY_FLOAT_IMPL_NONE
else
CFLAGS += -DMICROPY_FLOAT_IMPL=MICROPY_FLOAT_IMPL_FLOAT
CFLAGS += -fsingle-precision-constant
endif
endif

LDFLAGS += -nostdlib -L $(LD_DIR) $(addprefix -T,$(LD_FILES)) -Map=$(@:.elf=.map) --cref
LDFLAGS += --defsym=_estack_reserve=8
LIBS += "$(shell $(CC) $(CFLAGS) -print-libgcc-file-name)"

# Remove uncalled code from the final image.
CFLAGS += -fdata-sections -ffunction-sections
LDFLAGS += --gc-sections

# Debugging/Optimization
CFLAGS += -g  # always include debug info in the ELF
ifeq ($(DEBUG), 1)
CFLAGS += -DPENDSV_DEBUG
#COPT = -Og
COPT = -Os
# Disable text compression in debug builds
MICROPY_ROM_TEXT_COMPRESSION = 0
else
COPT += -Os -DNDEBUG
endif

# Flags for optional C++ source code
CXXFLAGS += $(filter-out -Wmissing-prototypes -Wold-style-definition -std=gnu99,$(CFLAGS))

# TODO make this common -- shouldn't be using these "private" vars from py.mk
ifneq ($(SRC_CXX)$(SRC_USERMOD_CXX)$(SRC_USERMOD_LIB_CXX),)
LIBSTDCPP_FILE_NAME = "$(shell $(CXX) $(CXXFLAGS) -print-file-name=libstdc++.a)"
LDFLAGS += -L"$(shell dirname $(LIBSTDCPP_FILE_NAME))"
endif

# Options for mpy-cross
MPY_CROSS_FLAGS += -march=armv7m

SHARED_SRC_C += $(addprefix shared/,\
	libc/string0.c \
	netutils/dhcpserver.c \
	netutils/netutils.c \
	netutils/trace.c \
	readline/readline.c \
	runtime/gchelper_native.c \
	runtime/interrupt_char.c \
	runtime/mpirq.c \
	runtime/pyexec.c \
	runtime/softtimer.c \
	runtime/stdout_helpers.c \
	runtime/sys_stdio_mphal.c \
	timeutils/timeutils.c \
	tinyusb/mp_usbd.c \
	tinyusb/mp_usbd_cdc.c \
	tinyusb/mp_usbd_descriptor.c \
	)

# TinyUSB Stack source
TINYUSB_SRC_C += $(addprefix lib/tinyusb/,\
	src/class/cdc/cdc_device.c \
	src/class/dfu/dfu_rt_device.c \
	src/class/hid/hid_device.c \
	src/class/midi/midi_device.c \
	src/class/msc/msc_device.c \
	src/class/usbtmc/usbtmc_device.c \
	src/class/vendor/vendor_device.c \
	src/common/tusb_fifo.c \
	src/device/usbd.c \
	src/device/usbd_control.c \
	src/portable/renesas/rusb2/dcd_rusb2.c \
	src/portable/renesas/rusb2/hcd_rusb2.c \
	src/portable/renesas/rusb2/rusb2_common.c \
	src/tusb.c \
	)

ifeq ($(MICROPY_FLOAT_IMPL),double)
LIBM_SRC_C += $(SRC_LIB_LIBM_DBL_C)
ifeq ($(SUPPORTS_HARDWARE_FP_DOUBLE),1)
LIBM_SRC_C += $(SRC_LIB_LIBM_DBL_SQRT_HW_C)
else
LIBM_SRC_C += $(SRC_LIB_LIBM_DBL_SQRT_SW_C)
endif
else
LIBM_SRC_C += $(SRC_LIB_LIBM_C)
ifeq ($(SUPPORTS_HARDWARE_FP_SINGLE),1)
LIBM_SRC_C += $(SRC_LIB_LIBM_SQRT_HW_C)
else
LIBM_SRC_C += $(SRC_LIB_LIBM_SQRT_SW_C)
endif
endif

DRIVERS_SRC_C += $(addprefix drivers/,\
	bus/softspi.c \
	bus/softqspi.c \
	memory/spiflash.c \
	dht/dht.c \
	)

BOARD_SRC_C := $(wildcard $(BOARD_DIR)/*.c)
ifeq ($(CMSIS_MCU),RA6M3)
BOARD_SRC_C := $(filter-out $(BOARD_DIR)/ra_dac.c $(BOARD_DIR)/rng.c,$(BOARD_SRC_C))
endif

SRC_C += \
	boardctrl.c \
	main.c \
	ra_hal.c \
	ra_it.c \
	rng.c \
	mphalport.c \
	mpnetworkport.c \
	eth.c \
	network_lan.c \
	mpthreadport.c \
	irq.c \
	pendsv.c \
	systick.c  \
	powerctrl.c \
	powerctrlboot.c \
	pybthread.c \
	factoryreset.c \
	timer.c \
	led.c \
	uart.c \
	gccollect.c \
	help.c \
	machine_timer.c \
	machine_bitstream.c \
	machine_dac.c \
	machine_i2c.c \
	machine_opamp.c \
	machine_comparator.c \
	machine_spi.c \
	machine_ws2812.c \
	machine_encoder.c \
	machine_pin.c \
	machine_rtc.c \
	machine_sdcard.c \
	modaudiomixer.c \
	modrenesas.c \
		moddataflash.c \
	extint.c \
	usrsw.c \
	flash.c \
	flashbdev.c \
	storage.c \
	fatfs_port.c \
	usbd.c \
	$(BOARD_SRC_C)

SRC_C += $(addprefix $(BOARD_DIR)/ra_gen/,\
	common_data.c \
	hal_data.c \
	pin_data.c \
	vector_data.c \
	)

ifeq ($(MICROPY_SSL_MBEDTLS),1)
LIB_SRC_C += mbedtls/mbedtls_port.c
endif

# LoRa/LoRaWAN — single selector switch.
# Source of truth: MICROPY_HW_LORA_STACK in boards/<BOARD>/mpconfigboard.mk.
# Allowed values: python | renesas | none.
#   python  : freeze micropySX126X + GereZoltan/LoRaWAN modules,
#             modaes_cmac.c, axTLS AES backend (current default).
#   renesas : build the Renesas C LoRaMac-node stack from ports/renesas-ra/lorawan/
#             (skeleton in Phase 0). Mutually exclusive with python.
#   none    : no LoRa support at all.
#
# Backward compat: legacy MICROPY_HW_ENABLE_LORA=1 (no STACK set) maps to
#                  MICROPY_HW_LORA_STACK=python. Existing builds are
#                  unaffected when neither variable is changed.
MICROPY_HW_ENABLE_LORA ?= 0
ifeq ($(MICROPY_HW_LORA_STACK),)
ifeq ($(MICROPY_HW_ENABLE_LORA),1)
MICROPY_HW_LORA_STACK := python
else
MICROPY_HW_LORA_STACK := none
endif
endif
ifeq ($(filter $(MICROPY_HW_LORA_STACK),python renesas none),)
$(error Unknown MICROPY_HW_LORA_STACK '$(MICROPY_HW_LORA_STACK)' — must be python|renesas|none)
endif
export MICROPY_HW_ENABLE_LORA
export MICROPY_HW_LORA_STACK
CFLAGS += -DMICROPY_HW_ENABLE_LORA=$(MICROPY_HW_ENABLE_LORA)

# --- python stack (legacy frozen modules + AES-CMAC + axTLS) ---------------
ifeq ($(MICROPY_HW_LORA_STACK),python)
SRC_C += modaes_cmac.c
# axTLS AES backend за cryptolib (за LoRaWAN на VK_RA4M2).
# ra/os_port.h е минимален shim — препокрива стандартния axtls os_port.h
# чрез -Ira (търси се преди lib/axtls/ssl). Така избягваме зависимостите
# на пълния axtls_os_port.h (lwIP / arpa/inet.h / SSL stack).
LIB_SRC_C += lib/axtls/crypto/aes.c
CFLAGS += -DMICROPY_SSL_AXTLS=1
CFLAGS += -DMICROPY_PY_CRYPTOLIB=1 -DMICROPY_PY_CRYPTOLIB_CTR=1
CFLAGS += -Ira -I$(TOP)/lib/axtls/crypto
CFLAGS += -Wno-all -Wno-unused-parameter -Wno-uninitialized -Wno-sign-compare -Wno-old-style-definition
endif

# --- renesas stack (C LoRaMac-node fork, FSP 4.4 glue) --------------------
# All sources live under ports/renesas-ra/lorawan/. The fragment is fully
# additive — does not touch the python-stack block above.
ifeq ($(MICROPY_HW_LORA_STACK),renesas)
include lorawan/lorawan.mk
endif

ifeq ($(MICROPY_PY_BLUETOOTH),1)
SRC_C += mpbthciport.c
endif

ifeq ($(MICROPY_BLUETOOTH_NIMBLE),1)
SRC_C += mpnimbleport.c
endif



SRC_O += \
	$(STARTUP_FILE) \
	$(SYSTEM_FILE)

SRC_O += \
	shared/runtime/gchelper_thumb2.o

HAL_SRC_C += $(addprefix $(HAL_DIR)/ra/fsp/src/bsp/mcu/all/,\
	bsp_clocks.c \
	bsp_common.c \
	bsp_delay.c \
	bsp_group_irq.c \
	bsp_guard.c \
	bsp_io.c \
	bsp_irq.c \
	bsp_register_protection.c \
	bsp_rom_registers.c \
	bsp_sbrk.c \
	bsp_security.c \
	)

HAL_SRC_C += $(addprefix $(HAL_DIR)/ra/fsp/src/,\
	r_ioport/r_ioport.c \
	r_sci_uart/r_sci_uart.c \
	r_adc/r_adc.c \
	)

CFLAGS_FSP = -Wno-unused-variable -Wno-unused-function
$(BUILD)/lib/fsp/ra/fsp/src/r_sci_uart/r_sci_uart.o: CFLAGS += $(CFLAGS_FSP)

ifeq ($(USE_FSP_QSPI), 1)
CFLAGS += -DUSE_FSP_QSPI
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_qspi/r_qspi.c
endif

ifeq ($(USE_FSP_OSPI), 1)
CFLAGS += -DUSE_FSP_OSPI
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ospi/r_ospi.c
endif

ifeq ($(USE_FSP_ETH), 1)
CFLAGS += -DUSE_FSP_ETH
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ether/r_ether.c
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ether_phy/r_ether_phy.c
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ether_phy/targets/ICS1894/r_ether_phy_target_ics1894.c
endif

ifeq ($(USE_FSP_LCD), 1)
CFLAGS += -DUSE_FSP_LCD
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_glcdc/r_glcdc.c \
	$(HAL_DIR)/ra/fsp/src/r_sci_i2c/r_sci_i2c.c \
	$(HAL_DIR)/ra/fsp/src/r_icu/r_icu.c
endif

ifeq ($(USE_FSP_DRW), 1)
CFLAGS += -DUSE_FSP_DRW
INC += -I$(TOP)/$(HAL_DIR)/ra/tes/dave2d/inc
INC += -I$(TOP)/$(HAL_DIR)/ra/tes/dave2d/src
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/src/r_drw
HAL_SRC_C += $(subst $(TOP)/,,$(wildcard $(TOP)/$(HAL_DIR)/ra/fsp/src/r_drw/*.c))
HAL_SRC_C += $(subst $(TOP)/,,$(wildcard $(TOP)/$(HAL_DIR)/ra/tes/dave2d/src/*.c))
$(BUILD)/$(BOARD_DIR)/dave2d_port.o: CFLAGS += -Wno-unused-variable
endif

ifeq ($(USE_FSP_CAM), 1)
CFLAGS += -DUSE_FSP_CAM
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_pdc/r_pdc.c \
	$(HAL_DIR)/ra/fsp/src/r_jpeg/r_jpeg.c
endif

# SCE (Secure Crypto Engine) — hardware AES / GCM / CMAC / SHA / TRNG / RSA / ECC.
# Disabled by default.  To enable, set USE_FSP_SCE=1 in the board's
# mpconfigboard.mk and supply ra_cfg/fsp_cfg/r_sce_cfg.h plus a board-level
# call to R_SCE_Open() (see book chapter on hardware AES integration).
# Selects the per-MCU plain-key driver tree under r_sce/crypto_procedures/.
USE_FSP_SCE ?= 0
ifeq ($(USE_FSP_SCE), 1)
CFLAGS += -DUSE_FSP_SCE
SCE_DIR_BASE = $(HAL_DIR)/ra/fsp/src/r_sce
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M2))
SCE_PLAINKEY_DIR = $(SCE_DIR_BASE)/crypto_procedures/src/sce5/plainkey
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA6M5))
SCE_PLAINKEY_DIR = $(SCE_DIR_BASE)/crypto_procedures/src/sce9/plainkey
endif
HAL_SRC_C += $(wildcard $(SCE_PLAINKEY_DIR)/adaptors/*.c)
HAL_SRC_C += $(wildcard $(SCE_PLAINKEY_DIR)/primitive/*.c)
HAL_SRC_C += $(wildcard $(SCE_PLAINKEY_DIR)/private/*.c)
HAL_SRC_C += $(wildcard $(SCE_PLAINKEY_DIR)/public/*.c)
INC += -I$(TOP)/$(SCE_PLAINKEY_DIR)/private/inc
INC += -I$(TOP)/$(SCE_PLAINKEY_DIR)/public/inc
INC += -I$(TOP)/$(SCE_DIR_BASE)
endif

ifeq ($(USE_FSP_SDHI), 1)
CFLAGS += -DUSE_FSP_SDHI
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_sdhi/r_sdhi.c \
	$(HAL_DIR)/ra/fsp/src/r_dtc/r_dtc.c
endif

ifeq ($(USE_FSP_LPM), 1)
CFLAGS += -DUSE_FSP_LPM
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_lpm/r_lpm.c
endif

# DTC for I2C slave hardware buffering (add only if not already included via SDHI)
ifneq ($(USE_FSP_SDHI), 1)
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_dtc/r_dtc.c
endif

# DMAC for DAC streaming
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_dmac/r_dmac.c

# AGT FSP driver. Gated by MICROPY_HW_LORA_STACK_RENESAS — the LoRaWAN
# vendor timer-board.c uses R_AGT_Open/Close/Start/Stop. Symbol-collision
# avoidance with the port's own AGT path is handled by routing AGT4/AGT5
# slots in boards/<BOARD>/ra_gen/vector_data.c.
ifeq ($(MICROPY_HW_LORA_STACK),renesas)
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_agt/r_agt.c
endif

ifeq ($(USE_FSP_FLASH), 1)
CFLAGS += -DUSE_FSP_FLASH

# RA flash driver selection (FSP)
# - RA4M1/RA4W1 use FACI_LP -> r_flash_lp
# - RA4M2 uses FACI_HP     -> r_flash_hp
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M1 RA4W1))
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_flash_lp/r_flash_lp.c
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M2))
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_flash_hp/r_flash_hp.c
endif
endif

ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA6M1 RA6M2 RA6M3 RA6M5))
ifeq ($(USE_FSP_FLASH), 1)
CFLAGS += -DUSE_FSP_FLASH
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_flash_hp/r_flash_hp.c
endif
endif

ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M1 RA4M2 RA4W1 RA6M1 RA6M2 RA6M3 RA6M5))
HAL_SRC_C += $(addprefix ra/,\
	ra_adc.c \
	ra_dac.c \
	ra_flash.c \
	ra_gpio.c \
	ra_i2c.c \
	ra_i2c_slave.c \
	ra_icu.c \
	ra_init.c \
	ra_int.c \
	ra_opamp.c \
	ra_acmplp.c \
	ra_rtc.c \
	ra_sci.c \
	ra_sci_spi.c \
	ra_sci_ws2812.c \
	ra_spi.c \
	ra_timer.c \
	ra_gpt.c \
	ra_encoder.c \
	ra_utils.c \
	)

endif

CFLAGS += -DMICROPY_HW_ENABLE_SCI_I2C=$(MICROPY_HW_ENABLE_SCI_I2C)
ifeq ($(MICROPY_HW_ENABLE_SCI_I2C),1)
HAL_SRC_C += ra/ra_sci_i2c.c
endif

ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M2 RA6M3 RA6M5))
CFLAGS += -DMICROPY_HW_ENABLE_AUDIOADC=1
SRC_C += machine_audioadc.c
HAL_SRC_C += ra/ra_storm_adc.c ra/ra_storm_detect.c
else
CFLAGS += -DMICROPY_HW_ENABLE_AUDIOADC=0
endif

# Coherent I/Q capture (SDR receive path).  Needs the second ADC unit, the
# dedicated sample-and-hold channels and the PGA.  Gated on the board feature
# flag MICROPY_HW_ENABLE_RA_SDR (set in boards/<BOARD>/mpconfigboard.mk), NOT on
# the MCU: the RA6-family capability layer (ra_sdr_caps.c) supplies the portable
# facts so the same driver runs on register-compatible RA6 members.  The driver
# and Python wrapper still key off -DMICROPY_HW_ENABLE_IQ_ADC=1, kept below.
# NOTE: operator-owned Makefile block; leave for confirmation.
ifeq ($(MICROPY_HW_ENABLE_RA_SDR),1)
CFLAGS += -DMICROPY_HW_ENABLE_IQ_ADC=1
HAL_SRC_C += ra/ra_iq_adc.c
HAL_SRC_C += ra/ra_sdr_caps.c
SRC_C += machine_iq_adc.c
endif

# CTSU (Capacitive Touch Sensing Unit) support
ifeq ($(MICROPY_HW_ENABLE_TOUCHPAD),1)
CFLAGS += -DMICROPY_HW_ENABLE_TOUCHPAD=1
SRC_C += machine_touchpad.c
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ctsu/r_ctsu.c
HAL_SRC_C += $(addprefix ra/,ra_ctsu.c)
endif

# CMSIS-DSP support (FIR, IIR biquad, RMS) — requires lib/cmsis-dsp submodule.
# Single source of truth: MICROPY_HW_ENABLE_DSP in boards/<BOARD>/mpconfigboard.mk
# (default 0). The -D is always passed so .c sources see a defined macro.
MICROPY_HW_ENABLE_DSP ?= 0
CFLAGS += -DMICROPY_HW_ENABLE_DSP=$(MICROPY_HW_ENABLE_DSP)
ifeq ($(MICROPY_HW_ENABLE_DSP),1)
CMSIS_DSP_PATH = lib/cmsis-dsp
CMSIS_DSP_DIR = $(TOP)/$(CMSIS_DSP_PATH)
GIT_SUBMODULES += lib/cmsis-dsp
ifeq ($(MCU_SERIES),m33)
CMSIS_DSP_CORE = ARM_MATH_CM33
else ifeq ($(MCU_SERIES),m4)
CMSIS_DSP_CORE = ARM_MATH_CM4
else
$(error CMSIS-DSP is not configured for MCU_SERIES=$(MCU_SERIES))
endif
CFLAGS += -D$(CMSIS_DSP_CORE) -DARM_DSP_ATTRIBUTE= -DDISABLEFLOAT16
INC += -I$(CMSIS_DSP_DIR)/Include
INC += -I$(CMSIS_DSP_DIR)/PrivateInclude
# moddsp.c (the Python `dsp` module) is intentionally NOT built: its MP_QSTR_*
# tokens are not picked up by the qstr scan on this port, so it collides with the
# frozen-collected `run` qstr.  The CMSIS-DSP *library* below is still compiled and
# available to C code (e.g. ra_iq_adc AM demod); the Python wrapper is deferred.
# SRC_C += moddsp.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/FilteringFunctions/arm_fir_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/FilteringFunctions/arm_fir_init_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/StatisticsFunctions/arm_rms_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/StatisticsFunctions/arm_power_f32.c
# FFT (real forward, float32)
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_rfft_fast_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_rfft_fast_init_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_cfft_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_cfft_init_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_cfft_radix8_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_bitreversal.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/TransformFunctions/arm_bitreversal2.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/BasicMathFunctions/arm_mult_f32.c
# arm_cos_f32: Hann window generation for the ra_iq_adc spectrum path (control plane)
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/FastMathFunctions/arm_cos_f32.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/CommonTables/arm_common_tables.c
DSP_SRC_C += $(CMSIS_DSP_PATH)/Source/CommonTables/arm_const_structs.c
SRC_C += $(DSP_SRC_C)
endif

# BLE (Bluetooth Low Energy) support for RA4W1
ifeq ($(MICROPY_HW_ENABLE_BLE),1)
CFLAGS += -DMICROPY_HW_ENABLE_BLE=1
INC += -I$(TOP)/ports/renesas-ra/ble
SRC_C += ble/modra_ble.c ble/ra_ble.c ble/ra_ble_config.c ble/ra_ble_events.c ble/gatt_db.c
SRC_C += ble/profile_cmn/r_ble_serv_common.c ble/profile_cmn/r_ble_servs_if.c
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/rm_ble_abs/rm_ble_abs.c
# r_agt.c is added unconditionally for the LoRaWAN renesas stack at top of file;
# BLE also needs it but cannot duplicate — skipped here when LoRaWAN active.
ifneq ($(MICROPY_HW_LORA_STACK),renesas)
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_agt/r_agt.c
endif
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_ble/r_ble.c
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_gpt/r_gpt.c
HAL_SRC_C += $(HAL_DIR)/ra/fsp/src/r_icu/r_icu.c
# Prebuilt BLE library (compact variant for BLE_CFG_LIBRARY_TYPE=2)
LDFLAGS += -L$(BOARD_DIR)/lib
LIBS := -lr_ble $(LIBS)
endif

ifeq ($(MICROPY_HW_ENABLE_RNG),1)
ifeq ($(CMSIS_MCU),RA6M3)
CRYPTO_DIR = ra/fsp/src/r_sce/crypto_procedures/src/sce7/plainkey/

INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/private/inc
INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/public/inc
INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/primitive
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/src/r_sce/common
INC += -I$(TOP)/$(HAL_DIR)/ra/fsp/src/r_sce

HAL_SRC_C += $(addprefix $(HAL_DIR)/$(CRYPTO_DIR)/,\
	adaptors/r_sce_adapt.c \
	primitive/hw_sce_p_p00.c \
	primitive/hw_sce_p_p01.c \
	primitive/hw_sce_p_p02.c \
	primitive/hw_sce_p_p20.c \
	primitive/hw_sce_p_func050.c \
	primitive/hw_sce_p_func051.c \
	primitive/hw_sce_p_func052.c \
	primitive/hw_sce_p_func053.c \
	primitive/hw_sce_p_func054.c \
	primitive/hw_sce_p_func100.c \
	primitive/hw_sce_p_func101.c \
	primitive/hw_sce_p_func102.c \
	primitive/hw_sce_p_func103.c \
	primitive/hw_sce_p_subprc01.c \
	primitive/hw_sce_p_subprc02.c \
	)
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M2 RA6M5))
CRYPTO_DIR = ra/fsp/src/r_sce_protected/crypto_procedures_protected/src/sce9/

INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/inc
INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/inc/api
INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/private/inc
INC += -I$(TOP)/$(HAL_DIR)/$(CRYPTO_DIR)/inc/instances

# The following FSP files are required for the random number generator.
HAL_SRC_C += $(addprefix $(HAL_DIR)/$(CRYPTO_DIR)/,\
	public/r_sce.c \
	public/r_sce_ecc.c \
	public/r_sce_sha.c \
	public/r_sce_aes.c \
	private/r_sce_private.c \
	primitive/r_sce_p00.c \
	primitive/r_sce_p20.c \
	primitive/r_sce_p26.c \
	primitive/r_sce_p81.c \
	primitive/r_sce_p82.c \
	primitive/r_sce_p92.c \
	primitive/r_sce_p40.c \
	primitive/r_sce_func050.c \
	primitive/r_sce_func051.c \
	primitive/r_sce_func052.c \
	primitive/r_sce_func053.c \
	primitive/r_sce_func054.c \
	primitive/r_sce_func100.c \
	primitive/r_sce_func101.c \
	primitive/r_sce_func040.c \
	primitive/r_sce_func048.c \
	primitive/r_sce_func102.c \
	primitive/r_sce_func103.c \
	primitive/r_sce_subprc01.c\
	)
endif
endif

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(LIB_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(LIBM_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SHARED_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(DRIVERS_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(HAL_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(TINYUSB_SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_CXX:.cpp=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_O))
OBJ += $(BUILD)/pins_$(BOARD).o

ifeq ($(MCU_SERIES),m33)
# For Cortex-M33 parts we must assemble gchelper_thumb2 with the correct -mcpu,
# otherwise the linker can report conflicting CPU architectures.
$(BUILD)/shared/runtime/gchelper_thumb2.o: $(TOP)/shared/runtime/gchelper_thumb2.s
	$(ECHO) "AS $<"
	$(Q)$(AS) $(ASFLAGS) -o $@ $<
endif

# This file contains performance critical functions so turn up the optimisation
# level.  It doesn't add much to the code size and improves performance a bit.
# Don't use -O3 with this file because gcc tries to optimise memset in terms of itself.
$(BUILD)/shared/libc/string0.o: COPT += -O2

# We put several files into the first 16K section with the ISRs.
# If we compile these using -O0 then it won't fit. So if you really want these
# to be compiled with -O0, then edit boards/common.ld (in the .isr_vector section)
# and comment out the following lines.
$(BUILD)/$(OOFATFS_DIR)/ff.o: COPT += -Os
$(filter $(PY_BUILD)/../extmod/vfs_fat_%.o, $(PY_O)): COPT += -Os
$(PY_BUILD)/formatfloat.o: COPT += -Os
$(PY_BUILD)/parsenum.o: COPT += -Os
$(PY_BUILD)/mpprint.o: COPT += -Os

all: $(TOP)/lib/fsp/README.md $(BUILD)/firmware.hex $(BUILD)/firmware.bin

# For convenience, automatically fetch required submodules if they don't exist
$(TOP)/lib/fsp/README.md:
	$(ECHO) "fsp submodule not found, fetching it now..."
	(cd $(TOP) && git submodule update --init lib/fsp)

ifeq ($(BOARD),VK_RA6M3)
define GENERATE_ELF
	$(ECHO) "LINK $(1)"
	$(file >$(1).objects,$(2))
	$(Q)$(LD) $(LDFLAGS) -o $(1) @$(1).objects $(LDFLAGS_MOD) $(LIBS)
	$(Q)$(SIZE) $(1)
endef
else
define GENERATE_ELF
	$(ECHO) "LINK $(1)"
	$(Q)$(LD) $(LDFLAGS) -o $(1) $(2) $(LDFLAGS_MOD) $(LIBS)
	$(Q)$(SIZE) $(1)
endef
endif

define GENERATE_BIN
	$(ECHO) "GEN $(1)"
	$(Q)$(OBJCOPY) -O binary -j .id_code $(2) $(BUILD)/id_code.bin
	$(Q)$(OBJCOPY) -O binary --remove-section=.id_code $(2) $(1)
endef

define GENERATE_HEX
	$(ECHO) "GEN $(1)"
	$(Q)$(OBJCOPY) -O ihex $(2) $(1)
endef

.PHONY:

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(call GENERATE_BIN,$@,$^)

$(BUILD)/firmware.hex: $(BUILD)/firmware.elf
	$(call GENERATE_HEX,$@,$^)

$(BUILD)/firmware.elf: $(OBJ)
	$(call GENERATE_ELF,$@,$^)

MAKE_PINS = boards/make-pins.py
BOARD_PINS = $(BOARD_DIR)/pins.csv
PREFIX_FILE = boards/ra_pin_prefix.c
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M1))
AF_FILE = boards/ra4m1_af.csv
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4M2))
AF_FILE = boards/ra4m2_af.csv
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA4W1))
AF_FILE = boards/ra4w1_af.csv
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA6M1))
AF_FILE = boards/ra6m1_af.csv
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA6M2))
AF_FILE = boards/ra6m2_af.csv
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA6M3))
AF_FILE = boards/ra6m3_af.csv
endif
ifeq ($(CMSIS_MCU),$(filter $(CMSIS_MCU),RA6M5))
AF_FILE = boards/ra6m5_af.csv
endif
GEN_PINS_SRC = $(BUILD)/pins_$(BOARD).c
GEN_PINS_HDR = $(HEADER_BUILD)/pins.h

FILE2H = $(TOP)/tools/file2h.py

# List of sources for qstr extraction
SRC_QSTR += $(SRC_C) $(SRC_CXX) $(SHARED_SRC_C) $(GEN_PINS_SRC)

# Vendor LoRaWAN timer-board.c (pristine RA2L1EK source) lives in a
# different board's directory under lorawan/boards/ra2l1ek_sx126x/, so its
# relative `#include "board.h"` resolves to that board's header and
# collides with the CLI -DRP_USE_RF_SWITCH/-DRP_USE_DCDC_FOR_RADIO under
# -Werror. The per-file CFLAGS in lorawan.mk apply only to the compile
# step, not the bulk qstr preprocess. The file contains no MicroPython
# qstrs, so excluding it from the qstr pass is safe.
ifeq ($(MICROPY_HW_LORA_STACK),renesas)
SRC_QSTR := $(filter-out lorawan/boards/ra2l1ek_sx126x/timer-board.c,$(SRC_QSTR))
endif

# Making OBJ use an order-only dependency on the generated pins.h file
# has the side effect of making the pins.h file before we actually compile
# any of the objects. The normal dependency generation will deal with the
# case when pins.h is modified. But when it doesn't exist, we don't know
# which source files might need it.
$(OBJ): | $(GEN_PINS_HDR)

# With conditional pins, we may need to regenerate qstrdefs.h when config
# options change.
$(HEADER_BUILD)/qstrdefs.generated.h: $(BOARD_DIR)/mpconfigboard.h

# Use a pattern rule here so that make will only call make-pins.py once to make
# both pins_$(BOARD).c and pins.h
.PRECIOUS: $(GEN_PINS_SRC)
$(BUILD)/%_$(BOARD).c $(HEADER_BUILD)/%.h: $(BOARD_DIR)/%.csv $(MAKE_PINS) $(AF_FILE) $(PREFIX_FILE) | $(HEADER_BUILD)
	$(ECHO) "GEN $@"
	$(Q)$(PYTHON) $(MAKE_PINS) --board-csv $(BOARD_PINS) --af-csv $(AF_FILE) --prefix $(PREFIX_FILE) \
	    --output-source $(GEN_PINS_SRC) --output-header $(GEN_PINS_HDR)

CMSIS_MCU_HDR = $(CMSIS_DIR)/$(CMSIS_MCU_LOWER).h

include $(TOP)/py/mkrules.mk
