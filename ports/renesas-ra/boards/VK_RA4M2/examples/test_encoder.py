# ASYNC TEST: Encoder IRQ -> flag.set() -> asyncio wakeup
# Checkpoint 40 firmware, no C changes needed
# Run: mpremote run test_encoder.py
# Stop: Ctrl+C

from machine import Encoder
import asyncio

flag = asyncio.ThreadSafeFlag()
cb_count = 0
cb_total = 0
last_val = 0

def on_move(e):
    global cb_count, cb_total, last_val
    cb_total += 1
    v = e.value()
    if v != last_val:
        last_val = v
        cb_count += 1
        flag.set()

enc = Encoder(mode=Encoder.X1, filter=Encoder.FILTER_64, value=0)
enc.irq(handler=on_move)
print(enc)
print("Rotate encoder. Ctrl+C to stop.")
print("cb = real moves, total = all callbacks (incl bounce)")

async def reader():
    prev = 0
    while True:
        await flag.wait()
        v = enc.value()
        print("  cb={} total={} pos={:5d} d={:+d}".format(cb_count, cb_total, v, v - prev))
        prev = v

try:
    asyncio.run(reader())
except KeyboardInterrupt:
    pass

print("\n--- FINAL ---")
print("real_cb:", cb_count, "total_cb:", cb_total, "pos:", enc.value())
enc.irq(handler=None)
enc.deinit()

