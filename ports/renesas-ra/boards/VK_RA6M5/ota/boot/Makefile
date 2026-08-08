# VK_RA6M5 OTA bootloader build.
#
# Standalone — does NOT use the MicroPython build system.  Output goes
# to ./build/.  Invoke with:   make -C boot
#
# Prerequisites: arm-none-eabi-gcc on PATH (the same toolchain as the
# main port build).

CROSS    = arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size

OUT      = build
TARGET   = $(OUT)/bootloader

CFLAGS   = -mcpu=cortex-m33 -mthumb \
           -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
           -Os -ffunction-sections -fdata-sections \
           -Wall -Wextra \
           -std=gnu11 \
           -fno-builtin \
           -DuECC_PLATFORM=5 \
           -DuECC_SUPPORTS_secp160r1=0 \
           -DuECC_SUPPORTS_secp192r1=0 \
           -DuECC_SUPPORTS_secp224r1=0 \
           -DuECC_SUPPORTS_secp256r1=1 \
           -DuECC_SUPPORTS_secp256k1=0 \
           -DuECC_SUPPORT_COMPRESSED_POINT=0 \
           -DuECC_SQUARE_FUNC=1 \
           -DuECC_VLI_NATIVE_LITTLE_ENDIAN=1

LDFLAGS  = -mcpu=cortex-m33 -mthumb \
           -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
           -nostdlib -nostartfiles \
           -Wl,--gc-sections -Wl,--print-memory-usage \
           -T bootloader.ld

SRCS     = bootloader_startup.c bootloader_main.c bootloader_fcu.c \
           uECC.c WjCryptLib_Sha256.c
OBJS     = $(SRCS:%.c=$(OUT)/%.o)

# Rescue stub — newer single-slot architecture.
RESCUE_TARGET   = $(OUT)/rescue
RESCUE_SRCS     = rescue_startup.c rescue_main.c \
                  uECC.c WjCryptLib_Sha256.c
# rescue_fcu.c kept on disk for future revisit but excluded from build
RESCUE_OBJS     = $(RESCUE_SRCS:%.c=$(OUT)/rescue_%.o)
RESCUE_LDFLAGS  = -mcpu=cortex-m33 -mthumb \
                  -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
                  -nostdlib -nostartfiles \
                  -Wl,--gc-sections -Wl,--print-memory-usage \
                  -T rescue.ld

.PHONY: all rescue clean size
all: $(TARGET).bin $(TARGET).hex size

rescue: $(RESCUE_TARGET).bin $(RESCUE_TARGET).hex
	@$(SIZE) $(RESCUE_TARGET).elf

$(OUT)/rescue_%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(RESCUE_TARGET).elf: $(RESCUE_OBJS) rescue.ld | $(OUT)
	$(CC) $(RESCUE_LDFLAGS) $(RESCUE_OBJS) -o $@

$(RESCUE_TARGET).bin: $(RESCUE_TARGET).elf
	$(OBJCOPY) -O binary $< $@
$(RESCUE_TARGET).hex: $(RESCUE_TARGET).elf
	$(OBJCOPY) -O ihex   $< $@

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) bootloader.ld | $(OUT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

size: $(TARGET).elf
	@$(SIZE) $<

clean:
	rm -rf $(OUT)
