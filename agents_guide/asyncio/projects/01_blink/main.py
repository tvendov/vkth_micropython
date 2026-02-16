# Project 01: Blink LED with MicroPython asyncio
# Цел: минимален пример за asyncio.run() + sleep_ms()

import asyncio


def _make_led():
    # Опит за често срещани имена. Смени/добави твоя пин при нужда.
    from machine import Pin

    for name in ("LED", "P011", "P409"):
        try:
            return Pin(name, Pin.OUT)
        except Exception:
            pass

    raise RuntimeError("No LED pin found. Edit _make_led() and set a valid Pin name.")


async def blink():
    led = _make_led()
    print("Blink started. Ctrl+C to stop.")

    while True:
        # toggle() е удобен и го има на много портове
        try:
            led.toggle()
        except AttributeError:
            led.value(1 - led.value())

        await asyncio.sleep_ms(500)


asyncio.run(blink())

