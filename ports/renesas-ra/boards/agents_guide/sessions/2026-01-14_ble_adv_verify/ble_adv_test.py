# BLE Advertising Test for EK_RA4W1
#
# FSP BLE Documentation:
# - FSP User Manual: https://renesas.github.io/fsp/
# - BLE Module (r_ble): https://renesas.github.io/fsp/group___b_l_e.html
# - BLE Abstraction (rm_ble_abs): https://renesas.github.io/fsp/group___r_m___b_l_e___a_b_s.html
# - RA4W1 Group User Manual: https://www.renesas.com/us/en/document/man/ra4w1-group-users-manual-hardware
# - EK-RA4W1 Kit Page: https://www.renesas.com/us/en/products/microcontrollers-microprocessors/ra-cortex-m-mcus/ek-ra4w1-evaluation-kit-ra4w1-mcu-group
#
# Local FSP Documentation (offline):
# - C:\msys64\home\teodor\agents_guide\fsp\fsp_documentation_v4.4.0\fsp_documentation\v4.4.0\fsp_user_manual_v4.4.0
#
# Local FSP BLE sources:
# - lib/fsp/ra/fsp/src/r_ble/           - BLE driver (uses prebuilt libr_ble.a)
# - lib/fsp/ra/fsp/src/rm_ble_abs/      - BLE abstraction layer
# - lib/fsp/ra/fsp/inc/api/r_ble_api.h  - BLE API definitions
# - lib/fsp/ra/fsp/lib/r_ble/cm4_gcc/compact/libr_ble.a - Prebuilt BLE library
#

import bluetooth
import time


NAME = "EK_RA4W1_MPY"
INTERVAL_US = 100000  # 100 ms


ble = bluetooth.BLE()
ble.active(True)
ble.config(gap_name=NAME)
ble.gap_advertise(INTERVAL_US)

print("BLE advertising started")
print("name:", NAME)
print("interval_us:", INTERVAL_US)

while True:
    time.sleep(1)
