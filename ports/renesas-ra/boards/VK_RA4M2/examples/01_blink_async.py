# Пример 1: Мигащ LED с asyncio
# За деца: LED-ът мига като светофар!

import asyncio
from machine import Pin

# LED на платката (P011)
led = Pin('P011', Pin.OUT)

async def blink():
    """LED мига - светва и гасне"""
    while True:
        led.value(0)  # Светва (active low)
        print("LED: ON ★")
        await asyncio.sleep(0.5)  # Чакай 0.5 секунди
        
        led.value(1)  # Гасне
        print("LED: OFF")
        await asyncio.sleep(0.5)

# Стартирай програмата
print("=== Мигащ LED ===")
print("Натисни Ctrl+C за стоп")
asyncio.run(blink())

