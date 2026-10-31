#!/usr/bin/env bash
set -euo pipefail
export PATH=/mingw64/bin:/ucrt64/bin:/usr/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
export MICROPY_MPYCROSS=C:/msys_64/home/teodor/renesas_micropython/mpy-cross/build-i2c-20260923/mpy-cross.exe
cd /home/teodor/renesas_micropython/ports/renesas-ra
out=tests/rtc/artifacts/health-20261031
mkdir -p "$out"
for test in test_rtc_optional test_i2c_stop_state test_i2c_nonblocking test_i2c_softtimer; do
    python3 "tests/$test.py" 2>&1 | tee "$out/$test.txt"
done
make BOARD=VK_RA4M2 BUILD=build-VK_RA4M2 clean > "$out/clean.txt" 2>&1
make BOARD=VK_RA4M2 BUILD=build-VK_RA4M2 -j8 > "$out/build.txt" 2>&1 || { tail -n 70 "$out/build.txt"; exit 1; }
tail -n 15 "$out/build.txt"
arm-none-eabi-size build-VK_RA4M2/firmware.elf
sha256sum build-VK_RA4M2/firmware.hex
