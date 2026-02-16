# Project 02: Two tasks running together
# Цел: create_task() + gather() + кооперативен паралелизъм

import asyncio


def _make_led():
    from machine import Pin
    for name in ("LED", "P011", "P409"):
        try:
            return Pin(name, Pin.OUT)
        except Exception:
            pass
    raise RuntimeError("No LED pin found. Edit _make_led() and set a valid Pin name.")


async def blink_led():
    led = _make_led()
    count = 0
    while True:
        try:
            led.toggle()
        except AttributeError:
            led.value(1 - led.value())
        count += 1
        print("LED toggles:", count)
        await asyncio.sleep_ms(300)


async def seconds_counter():
    sec = 0
    while True:
        sec += 1
        print("Seconds:", sec)
        await asyncio.sleep(1)


async def main():
    print("Starting 2 tasks. Ctrl+C to stop.\n")
    t1 = asyncio.create_task(blink_led())
    t2 = asyncio.create_task(seconds_counter())
    await asyncio.gather(t1, t2)


asyncio.run(main())

