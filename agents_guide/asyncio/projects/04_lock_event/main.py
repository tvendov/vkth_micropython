# Project 04: Lock + Event (producer/consumer)
# Цел: споделено състояние без race conditions

import asyncio

lock = asyncio.Lock()
event = asyncio.Event()
shared = {"value": 0}


async def producer():
    while True:
        await asyncio.sleep_ms(700)
        async with lock:
            shared["value"] += 1
            v = shared["value"]
        print("producer: new value =", v)
        event.set()


async def consumer():
    while True:
        await event.wait()
        event.clear()
        async with lock:
            v = shared["value"]
        print("consumer: observed =", v)


async def main():
    print("Lock+Event demo. Ctrl+C to stop.\n")
    t1 = asyncio.create_task(producer())
    t2 = asyncio.create_task(consumer())
    await asyncio.gather(t1, t2)


asyncio.run(main())

