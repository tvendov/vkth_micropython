# Dual DAC 5 kHz sine: CH0 DTC circular vs CH1 DMAC circular
# Both hardware-looped, zero CPU involvement during playback
import gc
import math
from array import array
from machine import DAC, Pin

MID = 2048
AMP = 1800
FREQ = 5000
TBL = 32
SR = FREQ * TBL  # 160 kHz

buf = array("H", [0] * TBL)
for i in range(TBL):
    v = MID + int(AMP * math.sin(2.0 * math.pi * i / TBL))
    buf[i] = max(0, min(4095, v))

print("5 kHz sine, {} samples @ {} Hz".format(TBL, SR))

dac0 = DAC(Pin("P014"))
dac1 = DAC(Pin("P015"))
dac0.write(MID)
dac1.write(MID)

gc.collect()
gc.disable()

print("CH0=DTC circular | CH1=DMAC circular (Ctrl-C to stop)")

# CH0 — DTC circular (repeat ≤256, hardware infinite)
dac0.write_timed(buf, SR, mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DTC)

# CH1 — DMAC circular (repeat ≤1024, ISR re-arms for infinite)
dac1.write_timed(buf, SR, mode=DAC.CIRCULAR, transfer=DAC.TRANSFER_DMAC)

try:
    while True:
        pass
except KeyboardInterrupt:
    pass

dac0.stop()
dac1.stop()
dac0.write(MID)
dac1.write(MID)
gc.enable()
print("Stopped.")

