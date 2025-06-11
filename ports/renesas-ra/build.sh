#!/usr/bin/env bash
set -euo pipefail          # <- exits the script as soon as the pipeline is non-zero

BOARD="VK_RA6M5"
STAMP=$(date +%Y%m%d%H%M)
LOG="${BOARD}_build_${STAMP}.log"

make -j"$(nproc)" DEBUG=0 BOARD="$BOARD" |& tee "$LOG"