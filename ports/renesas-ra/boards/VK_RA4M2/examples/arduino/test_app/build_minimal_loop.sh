#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p build

arm-none-eabi-gcc \
  -mcpu=cortex-m33 \
  -mthumb \
  -nostdlib \
  -Wl,--build-id=none \
  -Wl,-Tminimal_loop.ld \
  -o build/minimal_loop.elf \
  minimal_loop.S

arm-none-eabi-objcopy -O binary build/minimal_loop.elf build/minimal_loop.bin
arm-none-eabi-objcopy -O ihex build/minimal_loop.elf build/minimal_loop.hex
arm-none-eabi-size build/minimal_loop.elf
