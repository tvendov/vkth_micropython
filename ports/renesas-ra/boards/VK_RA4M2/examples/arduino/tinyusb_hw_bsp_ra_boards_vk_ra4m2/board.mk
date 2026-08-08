CPU_CORE = cortex-m33
MCU_VARIANT = ra4m2

# Use the Renesas RA family linker script from TinyUSB/FSP.
LD_FILE = $(FAMILY_PATH)/linker/gcc/$(MCU_VARIANT).ld

# For TinyUSB's flash-jlink target. Bootloader should be placed at 0x00000000.
JLINK_DEVICE = R7FA4M2AC

flash: flash-jlink
