#!/usr/bin/env bash
# Run from MSYS2. No flash, reset, commit or publication is performed.
set -uo pipefail
export PATH=/mingw64/bin:/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython
export MICROPY_MPYCROSS=C:/msys_64/home/teodor/renesas_micropython/mpy-cross/build-i2c-20260923/mpy-cross.exe
export MICROPY_GIT_TAG=1c6357b5d1-dirty
export MICROPY_GIT_HASH=1c6357b5d
cd ports/renesas-ra
out=tests/rtc/artifacts
mkdir -p "$out"
python3 tests/test_rtc_optional.py 2>&1 | tee "$out/host.txt"
if [ "${PIPESTATUS[0]}" != 0 ]; then exit 1; fi
printf 'BOARD\tEXIT\n' > "$out/builds.tsv"
failed=0
for board in VK_RA4M2 VK_RA6M3 VK_RA6M5; do
    build="build-${board}-rtc-20260923"
    printf '\nBUILD %s -> %s\n' "$board" "$build"
    make "BOARD=$board" "BUILD=$build" -j8 > "$out/$board.txt" 2>&1
    result=$?
    printf '%s\t%s\n' "$board" "$result" | tee -a "$out/builds.tsv"
    tail -n 12 "$out/$board.txt"
    if [ "$result" != 0 ]; then failed=1; fi
done
exit "$failed"
