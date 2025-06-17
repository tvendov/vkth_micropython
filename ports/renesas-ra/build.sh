#!/usr/bin/env bash
set -euo pipefail          # <- exits the script as soon as the pipeline is non-zero

BOARD="VK_RA6M5"
STAMP=$(date +%Y%m%d%H%M)
LOG="${BOARD}_build_${STAMP}.log"

# Enable OSPI GC support for VK_RA6M5
make -j"$(nproc)" DEBUG=0 BOARD="$BOARD" MICROPY_PORT_RA6M5_OSPI=1 |& tee "$LOG"