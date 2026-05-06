# lorawan/lorawan.mk — Makefile fragment for the Renesas C LoRaWAN stack.
#
# Included from ports/renesas-ra/Makefile only when:
#   MICROPY_HW_LORA_STACK = renesas
#
# This file MUST be additive — it appends to SRC_C / INC / CFLAGS without
# touching anything outside the lorawan/ directory.

LORAWAN_DIR := lorawan

# ---- Phase 0 skeleton sources -------------------------------------------
# Top-level binding + glue stubs. Real Renesas C stack files
# (mac/, radio/, soft_se/, system/) are added in later phases via
# wildcard expansion below — empty directories are safe.
SRC_C += \
    $(LORAWAN_DIR)/mod_lorawan.c \
    $(LORAWAN_DIR)/glue/sx126x_board.c \
    $(LORAWAN_DIR)/glue/timer_board.c \
    $(LORAWAN_DIR)/glue/dma_board.c \
    $(LORAWAN_DIR)/glue/nvm_board.c \
    $(LORAWAN_DIR)/glue/dflash.c \
    $(LORAWAN_DIR)/glue/utilities.c

# ---- Phase 4+: imported Renesas tree -----------------------------------
# Set LORAWAN_BUILD_PHASE to 4 (or higher) to compile the imported
# mac/, radio/, soft_se/, system/ tree. Default 3 keeps the Phase 3
# v1 timer + glue build untouched while we land Phase 4 incrementally.
LORAWAN_BUILD_PHASE ?= 3
ifeq ($(shell test $(LORAWAN_BUILD_PHASE) -ge 4 && echo y),y)
CFLAGS += -DLORAWAN_BUILD_PHASE=$(LORAWAN_BUILD_PHASE)
SRC_C += $(wildcard $(LORAWAN_DIR)/mac/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/mac/region/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/radio/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/radio/region/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/soft_se/*.c)
SRC_C += $(wildcard $(LORAWAN_DIR)/system/*.c)
# nvm/ — copied upstream nvm.c + lorawan_nvmdata_table.c for reference only
# (per-field approach, heavy + tied to FUOTA sample app's data model). Phase 6a
# uses the modern MIB_NVM_CTXS blob API instead, implemented in glue/nvm_board.c.
# SRC_C += $(wildcard $(LORAWAN_DIR)/nvm/*.c)
endif

# ---- Includes -----------------------------------------------------------
# Prepended to the global INC so our `system/timer.h` wins over the
# port-root `./timer.h` (which is the MicroPython-private timer header
# referencing `mp_obj_type_t`).
LORAWAN_INC := \
    -I$(LORAWAN_DIR) \
    -I$(LORAWAN_DIR)/glue \
    -I$(LORAWAN_DIR)/mac \
    -I$(LORAWAN_DIR)/mac/region \
    -I$(LORAWAN_DIR)/radio \
    -I$(LORAWAN_DIR)/radio/region \
    -I$(LORAWAN_DIR)/soft_se \
    -I$(LORAWAN_DIR)/system \
    -I$(LORAWAN_DIR)/nvm
INC := $(LORAWAN_INC) $(INC)

# ---- Compile-time configuration ----------------------------------------
# Source-side guard for any code that wants to know which stack is active.
CFLAGS += -DMICROPY_HW_LORA_STACK_RENESAS=1

# Region selection — start with EU868 only (smallest footprint).
# Add more by appending: -DREGION_AS923 -DREGION_US915 ...
CFLAGS += -DREGION_EU868

# Wio-SX1262 board parameters mirroring the working Python driver
# (boards/.../examples/LoRa/lorawan_upstream/lorawan_app.py:1505-1510):
#   useRegulatorLDO=False → SX1262 DCDC mode (lower power, full TX range)
#   DIO2 = RF switch control → without this TX never reaches the antenna
# The legacy Python driver always asserts setDio2AsRfSwitch(True)
# (sx126x.py:173,236) and uses DCDC; we mirror those here.
CFLAGS += -DRP_USE_RF_SWITCH
CFLAGS += -DRP_USE_DCDC_FOR_RADIO
# Note: LORAMAC_RXC_CONTINUOUS_ENABLED is already defined in
# lorawan/mac/LoRaMacConfig.h (line 130) — Class C continuous RX2 is
# supported out-of-the-box by this fork. No flag needed in lorawan.mk.

# Phase 8 — Class B support. Requires a gateway broadcasting LoRaWAN
# beacons every 128 s on 869.525 MHz SF12BW125 (gateway must have GPS
# lock). End-device boots in Class A, runs MLME_BEACON_ACQUISITION,
# configures ping-slot periodicity via MLME_PING_SLOT_INFO, then
# transitions to Class B with MIB_DEVICE_CLASS=CLASS_B. Server can
# push downlinks on any ping slot (~32× per beacon period at default
# periodicity 0).
CFLAGS += -DLORAMAC_CLASSB_ENABLED

# RP_CPU_CLK is a sentinel constant the upstream LoRaMacConfig.h gates
# on (line 75-77). It does not actually scale with our 100 MHz PCLK.
# Setting it to 8 satisfies the gate; we accept the conservative
# RA2E1/RA2L1 RX processing times (9 ms) since RA4M2 is faster anyway.
CFLAGS += -DRP_CPU_CLK=8

# LoRaMac fork enables C99/POSIX features that conflict with -Werror in some
# files. Relaxed warnings only for the imported Renesas tree, not the glue.
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

# Apply relaxed warnings only to mac/ + radio/ + soft_se/ + system/ object
# files. Glue and mod_lorawan keep the strict project-wide -Werror.
# (No-op in Phase 0 — wildcards expand to empty.)
$(BUILD)/$(LORAWAN_DIR)/mac/%.o:        CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/mac/region/%.o: CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/radio/%.o:      CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/radio/region/%.o: CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/soft_se/%.o:    CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
$(BUILD)/$(LORAWAN_DIR)/system/%.o:     CFLAGS += $(LORAWAN_VENDOR_CFLAGS)
