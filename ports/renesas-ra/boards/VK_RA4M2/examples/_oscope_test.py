from machine import Pin, Timer
import machine
import time

SOFT_PIN = Pin("P100", Pin.OUT, value=0)
HARD_PIN = Pin("P302", Pin.OUT, value=0)

_sv = bytearray([0])
_hv = bytearray([0])

def soft_cb(t):
    _sv[0] ^= 1
    SOFT_PIN.value(_sv[0])

def hard_cb(t):
    _hv[0] ^= 1
    HARD_PIN.value(_hv[0])

print("=== P100/P302 timer proof ===")
print("machine.freq() =", machine.freq())
print("P100 = 50 Hz square from Timer(-1) soft")
print("P302 = 50 Hz square from Timer(1)  hard")
print("Ctrl-C to stop")

SOFT_TIMER = Timer(-1)
HARD_TIMER = Timer(1)
SOFT_TIMER.init(period=10, mode=Timer.PERIODIC, callback=soft_cb)
HARD_TIMER.init(freq=100, callback=hard_cb, hard=False)

try:
    while True:
        time.sleep_ms(1)
except KeyboardInterrupt:
    SOFT_TIMER.deinit()
    HARD_TIMER.deinit()
    SOFT_PIN.value(0)
    HARD_PIN.value(0)
    print("Stopped.")
