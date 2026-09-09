#!/usr/bin/env bash
set -euo pipefail
unset PYTHONHOME PYTHONPATH
export PATH=/mingw64/bin:/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
exec make BOARD=VK_RA6M3 BUILD=build-VK_RA6M3 MICROPY_PY_CV2_QSPI=0 USER_C_MODULES= PYTHON=/mingw64/bin/python3.exe -j16
