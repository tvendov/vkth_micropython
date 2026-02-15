# Пример 2: Две задачи едновременно
# За деца: Компютърът прави ДВЕ неща наведнъж!

import asyncio
from machine import Pin

led = Pin('P011', Pin.OUT)

async def blink_led():
    """Задача 1: LED мига"""
    count = 0
    while True:
        led.toggle()
        count += 1
        print(f"  LED мигна {count} пъти")
        await asyncio.sleep(0.3)

async def count_seconds():
    """Задача 2: Брои секунди"""
    seconds = 0
    while True:
        seconds += 1
        print(f">>> Минаха {seconds} секунди")
        await asyncio.sleep(1)

async def main():
    """Стартира двете задачи заедно"""
    print("=== Две задачи работят заедно! ===")
    print("Натисни Ctrl+C за стоп\n")
    
    # Създай двете задачи
    task1 = asyncio.create_task(blink_led())
    task2 = asyncio.create_task(count_seconds())
    
    # Чакай и двете (безкрайно)
    await asyncio.gather(task1, task2)

asyncio.run(main())

