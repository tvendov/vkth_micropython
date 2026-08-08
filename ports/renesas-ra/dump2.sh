#!/usr/bin/env bash
export PATH=/mingw64/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
arm-none-eabi-objdump -d build-VK_RA4M2-phase4/firmware.elf > /home/teodor/dump.txt 2>&1
echo === lorawan_mac_process body ===
awk '/lorawan_mac_process>:/{found=1} found{print; if($0~/^$/ && NR>last+1){exit} last=NR}' /home/teodor/dump.txt | head -45
echo
echo === lorawan_mac_make_new body ===
awk '/lorawan_mac_make_new>:/{found=1} found{print; if($0~/^$/ && NR>last+1){exit} last=NR}' /home/teodor/dump.txt | head -100
