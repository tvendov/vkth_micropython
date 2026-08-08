#!/usr/bin/env bash
set -euo pipefail

BOARD="VK_RA4M2"
OUT_DIR="${OUT_DIR:-build-${BOARD}-hexes}"

NORMAL_BUILD="build-${BOARD}"
NO_RTC_BUILD="build-${BOARD}-NO-RTC_clck"

NO_RTC_FLAGS="-DMICROPY_HW_RTC_SOURCE=1 -DMICROPY_HW_SUBCLK_POPULATED=0 -DBSP_CLOCK_CFG_SUBCLOCK_POPULATED=0"

mkdir -p "${OUT_DIR}"

echo "Build normal ${BOARD} firmware..."
make BOARD="${BOARD}" BUILD="${NORMAL_BUILD}"
cp "${NORMAL_BUILD}/firmware.hex" "${OUT_DIR}/${BOARD}_RTC_SOSC.hex"

echo "Build ${BOARD} firmware without subclock oscillator..."
make BOARD="${BOARD}" BUILD="${NO_RTC_BUILD}" CFLAGS_EXTRA="${NO_RTC_FLAGS}"
cp "${NO_RTC_BUILD}/firmware.hex" "${OUT_DIR}/${BOARD}_NO-RTC_clck.hex"

{
    echo "board=${BOARD}"
    echo "normal_hex=${OUT_DIR}/${BOARD}_RTC_SOSC.hex"
    echo "no_rtc_hex=${OUT_DIR}/${BOARD}_NO-RTC_clck.hex"
    echo "normal_build=${NORMAL_BUILD}"
    echo "no_rtc_build=${NO_RTC_BUILD}"
    echo "no_rtc_flags=${NO_RTC_FLAGS}"
    echo "created=$(date '+%Y-%m-%d %H:%M:%S')"
} > "${OUT_DIR}/MANIFEST.txt"

echo "Done:"
echo "  ${OUT_DIR}/${BOARD}_RTC_SOSC.hex"
echo "  ${OUT_DIR}/${BOARD}_NO-RTC_clck.hex"
