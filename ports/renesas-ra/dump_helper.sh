#!/usr/bin/env bash
cd /home/teodor/renesas_micropython/ports/renesas-ra
arm-none-eabi-objdump -d build-VK_RA4M2-phase4/firmware.elf > dump.txt
echo "===== lorawan_mac_process body ====="
sed -n '/<lorawan_mac_process>:/,/^$/p' dump.txt | head -40
echo ""
echo "===== lorawan_mac_make_new body (first 80) ====="
sed -n '/<lorawan_mac_make_new>:/,/^$/p' dump.txt | head -80
rm -f dump.txt
