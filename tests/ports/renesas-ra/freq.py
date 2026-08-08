#
# definitions
#
MACHINE_RA4M1_CLICKER = "RA4M1 CLICKER with RA4M1"
MACHINE_EK_RA4M1 = "EK-RA4M1 with RA4M1"
MACHINE_EK_RA4W1 = "EK-RA4W1 with RA4W1"
MACHINE_VK_RA4M2 = "VK_RA4M2 with RA4M2"
MACHINE_VK_RA6M3 = "VK-RA6M3 with RA6M3"
MACHINE_VK_RA6M5 = "VK-RA6M5 with RA6M5"
SYSCLK_RA4M1_CLICKER = 48000000
SYSCLK_EK_RA4M1 = 48000000
SYSCLK_EK_RA4W1 = 48000000
SYSCLK_VK_RA4M2 = 100000000
SYSCLK_VK_RA6M3 = 120000000
SYSCLK_VK_RA6M5 = 200000000

#
# machine
#

import os

try:
    import machine
except ImportError:
    print("machine module is not found")
    raise SystemExit

m = os.uname().machine
f = machine.freq()

if m == MACHINE_RA4M1_CLICKER:
    if f == SYSCLK_RA4M1_CLICKER:
        print("freq: OK")
    else:
        print("freq: NG")


if m == MACHINE_EK_RA4M1:
    if f == SYSCLK_EK_RA4M1:
        print("freq: OK")
    else:
        print("freq: NG")

if m == MACHINE_EK_RA4W1:
    if f == SYSCLK_EK_RA4W1:
        print("freq: OK")
    else:
        print("freq: NG")

if m == MACHINE_VK_RA4M2:
    if f == SYSCLK_VK_RA4M2:
        print("freq: OK")
    else:
        print("freq: NG")

if m == MACHINE_VK_RA6M3:
    if f == SYSCLK_VK_RA6M3:
        print("freq: OK")
    else:
        print("freq: NG")

if m == MACHINE_VK_RA6M5:
    if f == SYSCLK_VK_RA6M5:
        print("freq: OK")
    else:
        print("freq: NG")
