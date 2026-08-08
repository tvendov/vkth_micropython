from machine import Pin, Timer
import machine
import time

PIN = Pin("P302", Pin.OUT, value=0)
_state = bytearray([0])

# 100 us period square wave => 10 kHz output.
# We toggle the pin on every timer callback, so callback rate is 20 kHz.
CALLBACK_HZ = 20_000


def hard_cb(t):
    _state[0] ^= 1
    PIN.value(_state[0])


print("=== P302 100 us period proof ===")
print("machine.freq() =", machine.freq())
print("P302 = 10 kHz square wave (100 us period)")
print("Timer callback rate =", CALLBACK_HZ, "Hz")
print("Ctrl-C to stop")

TMR = Timer(1)
TMR.init(freq=CALLBACK_HZ, callback=hard_cb, hard=True)

try:
    while True:
        time.sleep_ms(1000)
except KeyboardInterrupt:
    TMR.deinit()
    PIN.value(0)
    print("Stopped.")
