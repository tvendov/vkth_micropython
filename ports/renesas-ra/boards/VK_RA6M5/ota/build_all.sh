#!/usr/bin/env bash
# Build the OTA Option B artefacts:
#   1. bootloader  (304 B at 0x00000000)
#   2. Slot A app  (linked at 0x00010000, packed as firmware_v<version>_slotA.ota)
#   3. Slot B app  (linked at 0x00100000, packed as firmware_v<version>_slotB.ota)
#
# Run from .../ports/renesas-ra/boards/VK_RA6M5/ota/.
#
# Usage:   ./build_all.sh 1.2.3 [<git-short-hash>]
set -e

VERSION="${1:?usage: build_all.sh <version> [build-hash]}"
BUILDHASH="${2:-$(git -C ../../../.. rev-parse --short=8 HEAD 2>/dev/null || echo 0)}"

OTA_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT_DIR="$(cd "$OTA_DIR/../../.." && pwd)"     # ports/renesas-ra

cd "$PORT_DIR"

echo "================================================================"
echo "1/4  Building bootloader ..."
echo "================================================================"
make -C "$OTA_DIR/boot" clean
make -C "$OTA_DIR/boot"

echo "================================================================"
echo "2/4  Building Slot A firmware (linked at 0x00010000) ..."
echo "================================================================"
rm -rf build-VK_RA6M5_slotA
BUILD=build-VK_RA6M5_slotA make BOARD=VK_RA6M5 USE_OTA=1 \
    LD_FILES=boards/VK_RA6M5/ota/vk_ra6m5_app.ld -j8

echo "================================================================"
echo "3/4  Building Slot B firmware (linked at 0x00100000) ..."
echo "================================================================"
rm -rf build-VK_RA6M5_slotB
BUILD=build-VK_RA6M5_slotB make BOARD=VK_RA6M5 USE_OTA=1 \
    LD_FILES=boards/VK_RA6M5/ota/vk_ra6m5_app_slotb.ld -j8

echo "================================================================"
echo "4/4  Packing .ota files ..."
echo "================================================================"
python3 "$OTA_DIR/ota_pack.py" \
    build-VK_RA6M5_slotA/firmware.bin \
    "$OTA_DIR/firmware_v${VERSION}_slotA.ota" \
    --version "$VERSION" --build-hash "$BUILDHASH"
python3 "$OTA_DIR/ota_pack.py" \
    build-VK_RA6M5_slotB/firmware.bin \
    "$OTA_DIR/firmware_v${VERSION}_slotB.ota" \
    --version "$VERSION" --build-hash "$BUILDHASH"

echo
echo "================================================================"
echo "Done.  Artefacts:"
echo "  bootloader      : $OTA_DIR/boot/build/bootloader.{bin,hex}"
echo "  Slot A firmware : build-VK_RA6M5_slotA/firmware.{bin,hex}"
echo "  Slot B firmware : build-VK_RA6M5_slotB/firmware.{bin,hex}"
echo "  Slot A OTA pkg  : $OTA_DIR/firmware_v${VERSION}_slotA.ota"
echo "  Slot B OTA pkg  : $OTA_DIR/firmware_v${VERSION}_slotB.ota"
echo
echo "Provision (one-time, JLink):"
echo "  cd $OTA_DIR/boot"
echo "  JLink.exe -CommanderScript jlink_provision.txt"
echo
echo "OTA cycle (after provisioning):"
echo "  - device runs from Slot A; OTHER slot is Slot B"
echo "  - upload firmware_v<NEW>_slotB.ota to /flash/_ota_pending.ota"
echo "  - REPL: import ota; ota.flash_to_slot_b(); ota.mark_good_after_boot()"
echo "  - reset"
echo "================================================================"
