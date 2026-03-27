# Continuous 5 kHz sine — DTC circular, zero deviation
# One period in the lookup table, hardware loops it seamlessly

import gc
import math
from array import array
from machine import DAC, Pin

DAC_PIN = "P014"
MID = 2048
AMP = 1800
FREQ = 5000           # 5 kHz
TABLE_LEN = 16        # samples per period
SAMPLE_RATE = FREQ * TABLE_LEN  # 80 000 Hz

buf = array("H", [0] * TABLE_LEN)
for i in range(TABLE_LEN):
    v = MID + int(AMP * math.sin(2.0 * math.pi * i / TABLE_LEN))
    if v < 0:
        v = 0
    elif v > 4095:
        v = 4095
    buf[i] = v

dac = DAC(Pin(DAC_PIN))
dac.write(MID)

gc.collect()
gc.disable()
print("5 kHz sine, DTC circular (Ctrl-C to stop)...")
dac.write_timed(buf, SAMPLE_RATE, mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DTC)

try:
    while True:
        pass
except KeyboardInterrupt:
    pass

dac.stop()
dac.write(MID)
gc.enable()
print("Stopped.")

