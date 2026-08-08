CMSIS_MCU = RA4M2
MCU_SERIES = m33
LD_FILES = boards/VK_RA4M2/ra4m2ac3cfm.ld

# MicroPython settings
MICROPY_VFS_LFS2 = 0
MICROPY_VFS_FAT = 1
USE_FSP_LPM = 1                          # enable r_lpm.c -> machine.lightsleep/deepsleep work
MICROPY_HW_ENABLE_TOUCHPAD = 1
MICROPY_HW_ENABLE_SCI_I2C = 1
MICROPY_HW_ENABLE_DSP = 0
# Hardware AES via SCE9 instead of software axTLS.
# MICROPY_HW_ENABLE_RNG=1 pulls in the r_sce_*.c FSP files (Makefile guard)
# and the SCE9 AES wrapper (ra/ra_sce_aes.c) replaces lib/axtls/crypto/aes.c
# at the AES_set_key/AES_encrypt/AES_decrypt API boundary.
MICROPY_HW_ENABLE_RNG = 1
CFLAGS += -DMICROPY_HW_DATAFLASH_PARTITIONED=1
# LoRa/LoRaWAN — single selector switch.
#   MICROPY_HW_LORA_STACK = python   → frozen Python micropySX126X + LoRaWAN
#                                       + modaes_cmac.c + axTLS AES backend.
#                                       (legacy default, ~44 KB flash)
#   MICROPY_HW_LORA_STACK = renesas  → Renesas C LoRaMac-node stack from
#                                       ports/renesas-ra/lorawan/ (Phase 0
#                                       skeleton — see lorawan/README.md).
#   MICROPY_HW_LORA_STACK = none     → no LoRa.
#
# Legacy MICROPY_HW_ENABLE_LORA=1 with MICROPY_HW_LORA_STACK unset keeps the
# old "python" behaviour. The two are wired in ports/renesas-ra/Makefile.
MICROPY_HW_ENABLE_LORA = 1
MICROPY_HW_LORA_STACK ?= renesas

CFLAGS+=-DDEFAULT_DBG_CH=0 \
          -DCFG_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED \
          -DCFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE \
          -DBOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED \
	          -DCFG_TUH_ENABLED=0 \
          -DCFG_TUSB_MCU=OPT_MCU_RAXXX \
          -DBOARD_TUD_RHPORT=0

# Don't include default frozen modules
FROZEN_MANIFEST ?= $(BOARD_DIR)/manifest.py
