#!/usr/bin/env bash
set -euo pipefail
export PATH=/mingw64/bin:/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
for name in test_rtc_optional test_i2c_stop_state test_i2c_nonblocking test_i2c_softtimer; do
    python3 "tests/$name.py" 2>&1 | tee "tests/rtc/artifacts/$name.txt"
done
