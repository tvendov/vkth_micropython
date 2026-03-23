from machine import Encoder
import asyncio

enc = Encoder(mode=Encoder.X1, filter=4, value=0)

async def reader():
    prev = enc.value()
    n = 0
    while True:
        v = enc.value()
        if v != prev:
            n += 1
            print("#{} pos={} d={:+d}".format(n, v, v - prev))
            prev = v
        await asyncio.sleep_ms(100)

try:
    asyncio.run(reader())
except KeyboardInterrupt:
    pass

enc.deinit()