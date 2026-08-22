set -o pipefail
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra || exit 99
make BOARD=VK_RA6M3 -j16 2>&1 | tail -12
echo "EXIT=${PIPESTATUS[0]}"
ls -la --time-style=+%H:%M:%S build-VK_RA6M3/firmware.bin 2>&1
