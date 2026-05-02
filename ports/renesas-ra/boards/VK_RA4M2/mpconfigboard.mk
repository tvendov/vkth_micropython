CMSIS_MCU = RA4M2
MCU_SERIES = m33
LD_FILES = boards/VK_RA4M2/ra4m2ac3cfm.ld

# MicroPython settings
MICROPY_VFS_LFS2 = 0
MICROPY_VFS_FAT = 1
USE_FSP_LPM = 0
MICROPY_HW_ENABLE_TOUCHPAD = 1
MICROPY_HW_ENABLE_DSP = 1
# Hardware AES via SCE9 instead of software axTLS.
# MICROPY_HW_ENABLE_RNG=1 pulls in the r_sce_*.c FSP files (Makefile guard)
# and the SCE9 AES wrapper (ra/ra_sce_aes.c) replaces lib/axtls/crypto/aes.c
# at the AES_set_key/AES_encrypt/AES_decrypt API boundary.
MICROPY_HW_ENABLE_RNG = 1
MICROPY_SSL_AXTLS = 1                    # AES за cryptolib (LoRaWAN MIC/CTR) — backed by SCE9

CFLAGS+=-DDEFAULT_DBG_CH=0 \
          -DCFG_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED \
          -DCFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE \
          -DBOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED \
	          -DCFG_TUH_ENABLED=0 \
          -DCFG_TUSB_MCU=OPT_MCU_RAXXX \
          -DBOARD_TUD_RHPORT=0

# Don't include default frozen modules
FROZEN_MANIFEST ?= $(BOARD_DIR)/manifest.py
