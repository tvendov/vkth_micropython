# lorawan/lorawan.mk - Makefile fragment for the Renesas C LoRaWAN stack.
#
# Included from ports/renesas-ra/Makefile only when:
#   MICROPY_HW_LORA_STACK = renesas
#
# This file must be additive - it appends to SRC_C / INC / CFLAGS without
# touching anything outside the lorawan/ directory.

LORAWAN_DIR := lorawan
LORAWAN_BOARD_DIR := $(LORAWAN_DIR)/boards/vk_ra4m2_sx126x
LORAWAN_VENDOR_TIMER_BOARD := $(LORAWAN_DIR)/boards/ra2l1ek_sx126x/timer-board.c

# ---- Active VK_RA4M2 board layer -----------------------------------------
# Do not compile any inactive tree or UART board source. All VK hardware-specific
# code lives in boards/vk_ra4m2_sx126x/ and vendor core files stay pristine.
# Timer board layer (Commit 2 of timer-board refactor): the VK adapter at
# $(LORAWAN_BOARD_DIR)/timer-board.c is retired. We now reuse the pristine
# RA2L1EK vendor timer-board.c verbatim, paired with VK-specific FSP HAL
# data ($(LORAWAN_BOARD_DIR)/lorawan_hal_data.c) and vector-number aliases
# ($(LORAWAN_BOARD_DIR)/lorawan_vector_aliases.h, force-included on the
# vendor compile only).
SRC_C += \
	$(LORAWAN_BOARD_DIR)/board.c \
	$(LORAWAN_BOARD_DIR)/delay-board.c \
	$(LORAWAN_BOARD_DIR)/gpio-board.c \
    $(LORAWAN_BOARD_DIR)/sx126x-board.c \
    $(LORAWAN_BOARD_DIR)/lorawan_hal_data.c \
    $(LORAWAN_BOARD_DIR)/lorawan_softreset.c \
    $(LORAWAN_VENDOR_TIMER_BOARD) \
    $(LORAWAN_DIR)/system/flash/nvm_board.c \
    $(LORAWAN_DIR)/system/flash/dflash_lwnvm.c

# Vendor timer-board.c is compiled from lorawan/boards/ra2l1ek_sx126x/, so its
# `#include "board.h"` resolves to the RA2L1EK board.h next to it instead of
# the active VK_RA4M2 board.h. Two collateral effects to fix:
#   1) ra2l1ek board.h unconditionally `#define RP_USE_RF_SWITCH/_DCDC_FOR_RADIO`,
#      which are already on the command line — redefinition under -Werror.
#   2) The vendor file refers to VECTOR_NUMBER_AGT0_INT/_AGT1_INT/_AGT1_COMPARE_A,
#      which on VK_RA4M2 belong to the port's Timer(1..2). They must be remapped
#      to the LoRaWAN-reserved AGT4/AGT5 vectors.
# Both are handled by force-including the active VK_RA4M2 board.h FIRST (so its
# #if !defined guards swallow the CLI macros, suppressing the redefinitions in
# the RA2L1EK header) and then the alias header (overriding the AGT0/AGT1
# vector-number macros pulled in via the VK board.h → vector_data.h chain).
#
# The active VK_RA4M2 board pulls boards/VK_RA4M2/ra_gen/hal_data.h via
# vk_ra4m2_sx126x/board.h. That header declares the port's GPT encoder as
#   extern gpt_instance_ctrl_t g_timer0_ctrl;
# which collides with the vendor LoRaWAN expectation that g_timer0_ctrl
# is an agt_instance_ctrl_t. Vendor timer-board.c does not consume any
# port-side hal_data symbols (it uses g_timer0/1 from our LoRaWAN
# hal_data only), so we suppress port hal_data.h on the vendor compile
# by pre-defining its include guard (HAL_DATA_H_).
$(BUILD)/$(LORAWAN_VENDOR_TIMER_BOARD:.c=.o): CFLAGS += \
    -DHAL_DATA_H_ \
    -include bsp_api.h \
    -include r_agt.h \
    -include $(LORAWAN_BOARD_DIR)/board.h \
    -include $(LORAWAN_BOARD_DIR)/lorawan_vector_aliases.h \
    -include $(LORAWAN_BOARD_DIR)/lorawan_hal_data.h

# Note: the vendor timer-board.c is also filtered out of SRC_QSTR in the
# port-level Makefile (after SRC_QSTR is finalised), because the bulk
# qstr preprocess does not honour per-file CFLAGS.

# Build the VK_RA4M2 MicroPython binding.
LORAWAN_BUILD_BINDING ?= 1
ifeq ($(LORAWAN_BUILD_BINDING),1)
SRC_C += $(LORAWAN_DIR)/mod_lorawan.c
endif

# ---- Phase 4+: imported Renesas tree -------------------------------------
# Set LORAWAN_BUILD_PHASE to 4 (or higher) to compile the imported
# mac/, radio/, peripherals/soft-se/, system/ tree.
LORAWAN_BUILD_PHASE ?= 4

ifeq ($(shell test $(LORAWAN_BUILD_PHASE) -ge 4 && echo y),y)
CFLAGS += -DLORAWAN_BUILD_PHASE=$(LORAWAN_BUILD_PHASE)
# Active VK SX126x transport follows the Renesas board sources: opcode,
# address, and data are shifted byte-by-byte while CS stays asserted.
SRC_C += $(wildcard $(LORAWAN_DIR)/mac/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/mac/region/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/radio/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/radio/region/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/peripherals/soft-se/*.c)
SRC_C += $(LORAWAN_DIR)/boards/mcu/utilities.c
LORAWAN_SYSTEM_SRC := $(filter-out $(LORAWAN_DIR)/system/uart.c $(LORAWAN_DIR)/system/gpio.c,$(wildcard $(LORAWAN_DIR)/system/*.c))
SRC_C += $(LORAWAN_SYSTEM_SRC)
# iter20+: r_icu.c removed from this fragment. DIO1 enters through the active
# VK board callback, records a pending edge, and requests foreground LoRaMac
# service; radio IRQ work is dispatched from foreground context, not directly
# from the IRQ callback.
# nvm/ copied upstream nvm.c + lorawan_nvmdata_table.c for reference only
# (per-field approach, heavy + tied to FUOTA sample app's data model). Keep
# disabled unless a VK board-layer NVM adapter is added later.
# SRC_C += $(wildcard $(LORAWAN_DIR)/nvm/*.c)
endif

# ---- Includes -------------------------------------------------------------
# Prepended to the global INC so our system/timer.h wins over the
# port-root ./timer.h, which is the MicroPython-private timer header
# referencing mp_obj_type_t.
LORAWAN_INC := \
    -I$(LORAWAN_DIR) \
    -I$(LORAWAN_BOARD_DIR) \
    -I$(LORAWAN_DIR)/boards \
    -I$(LORAWAN_DIR)/mac \
    -I$(LORAWAN_DIR)/mac/region \
    -I$(LORAWAN_DIR)/radio \
    -I$(LORAWAN_DIR)/radio/sx126x \
    -I$(LORAWAN_DIR)/radio/region \
    -I$(LORAWAN_DIR)/peripherals/soft-se \
    -I$(LORAWAN_DIR)/system/flash \
    -I$(LORAWAN_DIR)/system
INC := $(LORAWAN_INC) $(INC)

# ---- Compile-time configuration ------------------------------------------
# Source-side guard for any code that wants to know which stack is active.
CFLAGS += -DMICROPY_HW_LORA_STACK_RENESAS=1

# Region selection - start with EU868 only (smallest footprint).
# Add more by appending: -DREGION_AS923 -DREGION_US915 ...
CFLAGS += -DREGION_EU868

# Wio-SX1262 board parameters mirroring the working Python driver
# (boards/.../examples/LoRa/lorawan_upstream/lorawan_app.py:1505-1510):
#   useRegulatorLDO=False -> SX1262 DCDC mode (lower power, full TX range)
#   DIO2 = RF switch control -> without this TX never reaches the antenna
# The active configuration asserts setDio2AsRfSwitch(True) and uses DCDC;
# we mirror those settings here.
CFLAGS += -DRP_USE_RF_SWITCH
CFLAGS += -DRP_USE_DCDC_FOR_RADIO
# Note: LORAMAC_RXC_CONTINUOUS_ENABLED is already defined in
# lorawan/mac/LoRaMacConfig.h (line 130) - Class C continuous RX2 is
# supported out-of-the-box by this fork. No flag needed in lorawan.mk.

# Phase 8 - Class B support. Requires a gateway broadcasting LoRaWAN
# beacons every 128 s on 869.525 MHz SF12BW125 (gateway must have GPS
# lock). End-device boots in Class A, runs MLME_BEACON_ACQUISITION,
# configures ping-slot periodicity via MLME_PING_SLOT_INFO, then
# transitions to Class B with MIB_DEVICE_CLASS=CLASS_B. Server can
# push downlinks on any ping slot (~32x per beacon period at default
# periodicity 0).
CFLAGS += -DLORAMAC_CLASSB_ENABLED

# RP_CPU_CLK is a sentinel constant the upstream LoRaMacConfig.h gates
# on (line 75-77). It does not actually scale with our 100 MHz PCLK.
# Setting it to 8 satisfies the gate; we accept the conservative
# RA2E1/RA2L1 RX processing times (9 ms) since RA4M2 is faster anyway.
CFLAGS += -DRP_CPU_CLK=8

# LoRaMac fork enables C99/POSIX features that conflict with -Werror in some
# files. Relaxed warnings only for the imported Renesas tree.
LORAWAN_VENDOR_CFLAGS := \
    -Wno-unused-parameter \
    -Wno-unused-variable \
    -Wno-unused-function \
    -Wno-unused-but-set-variable \
    -Wno-sign-compare \
    -Wno-old-style-definition \
    -Wno-missing-prototypes \
    -Wno-float-conversion \
    -Wno-double-promotion \
    -Wno-implicit-fallthrough \
    -Wno-shift-negative-value

# Apply relaxed warnings only to mac/ + radio/ + peripherals/soft-se/ + system/
# object files. The VK board layer keeps the strict project-wide -Werror.
$(BUILD)/$(LORAWAN_DIR)/mac/%.o:        CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/mac/region/%.o: CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/radio/%.o:      CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/radio/region/%.o: CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/peripherals/soft-se/%.o: CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/system/%.o:     CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/boards/mcu/%.o: CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
