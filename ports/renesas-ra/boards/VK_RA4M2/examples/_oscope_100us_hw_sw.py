from machine import Pin, Timer
import machine
import time

SOFT_PIN = Pin("P100", Pin.OUT, value=0)
HARD_PIN = Pin("P302", Pin.OUT, value=0)

_sv = bytearray([0])
_hv = bytearray([0])

# 100 us period square wave => 10 kHz output.
# Toggle on each callback => callback rate 20 kHz.
CALLBACK_HZ = 20_000


def soft_cb(t):
    _sv[0] ^= 1
    SOFT_PIN.value(_sv[0])


def hard_cb(t):
    _hv[0] ^= 1
    HARD_PIN.value(_hv[0])


print("=== 100 us hard vs soft ===")
print("machine.freq() =", machine.freq())
print("P100 = Timer(-1) soft, target 10 kHz square")
print("P302 = Timer(1) hard, target 10 kHz square")
print("Ctrl-C to stop")

soft_timer = Timer(-1)
hard_timer = Timer(1)
soft_started = False

try:
    soft_timer.init(freq=CALLBACK_HZ, callback=soft_cb)
    soft_started = True
    print("soft timer started")
except Exception as exc:
    print("soft timer failed:", exc)

hard_timer.init(freq=CALLBACK_HZ, callback=hard_cb, hard=True)
print("hard timer started")

try:
    while True:
        time.sleep_ms(1000)
except KeyboardInterrupt:
    if soft_started:
        soft_timer.deinit()
    hard_timer.deinit()
    SOFT_PIN.value(0)
    HARD_PIN.value(0)
    print("Stopped.")
