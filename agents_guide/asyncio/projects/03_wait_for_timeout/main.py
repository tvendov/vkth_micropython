# Project 03: wait_for_ms() timeout + cancellation
# Цел: как да сложиш timeout на операция и как се държи CancelledError

import asyncio


async def slow_operation(duration_ms):
    # Симулира I/O или бавна операция
    await asyncio.sleep_ms(duration_ms)
    return "OK after %d ms" % duration_ms


async def demo_timeout():
    print("1) Timeout demo")
    try:
        # Това ще тайм-аутира (1000ms работа, 200ms timeout)
        res = await asyncio.wait_for_ms(slow_operation(1000), 200)
        print("Result:", res)
    except asyncio.TimeoutError:
        print("TimeoutError: operation took too long")


async def demo_cancel():
    print("\n2) Cancel demo")

    async def worker():
        try:
            while True:
                print("worker: tick")
                await asyncio.sleep_ms(200)
        except asyncio.CancelledError:
            print("worker: cancelled -> cleanup here")
            raise

    t = asyncio.create_task(worker())
    await asyncio.sleep_ms(700)
    print("Cancelling task...")
    t.cancel()

    try:
        await t
    except asyncio.CancelledError:
        print("main: task finished as cancelled")


async def main():
    await demo_timeout()
    await demo_cancel()


asyncio.run(main())

