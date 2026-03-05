from machine import Pin, TouchPad
import uasyncio as asyncio


TouchPad.sample_rate(50)

tp = TouchPad(Pin("P205"))
tp.config(600)


async def touch_task():
    while True:
        if tp.ready():
            raw = tp.read_cached()
            pressed = tp.value_cached()
            if pressed:
                print("touch", raw, tp.age_ms())
        await asyncio.sleep_ms(20)


asyncio.run(touch_task())
