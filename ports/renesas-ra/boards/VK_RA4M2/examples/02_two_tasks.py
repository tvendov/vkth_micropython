# Пример: Две асинхронни задачи едновременно на VK_RA4M2.
# Ресурси на VK_RA4M2: ADC външни входове = 13 броя -> P000, P001, P002, P003, P004, P005, P006, P007, P008, P013, P014, P015, P500.
# Ресурси на VK_RA4M2: ADC вътрешни източници = 3 броя -> ADC.CORE_TEMP, ADC.CORE_VREF, ADC.VREF.
# Ресурси на VK_RA4M2: DAC изходи = 2 броя -> P014, P015.
# Ресурси на VK_RA4M2: PWM изходи = 14 броя -> P107, P106, P105, P104, P113, P114, P112, P115, P608, P409, P408, P600, P304, P303.
# Ресурси на VK_RA4M2: UART инстанции = 4 броя -> UART(0), UART(2), UART(7), UART(9).
# Ресурси на VK_RA4M2: I2C master = 2 броя -> I2C(0)=P400/P401 и I2C(1)=P100/P101.
# Ресурси на VK_RA4M2: I2CTarget = 2 броя -> I2CTarget(0)=P400/P401 и I2CTarget(1)=P100/P101.
# Ресурси на VK_RA4M2: SPI канали = 1 брой -> SPI=P103/P102/P100/P101.
# Ресурси на VK_RA4M2: TouchPad входове = 12 броя -> P205, P206, P407, P408, P409, P410, P411, P412, P413, P414, P415, P708.
# Ресурси на VK_RA4M2: LED = 1 брой -> LED1=P204, бутон = 1 брой -> SW1=P400.
# Ресурси на VK_RA4M2: Хардуерни Timer = 2 броя -> Timer(1), Timer(2), софтуерен Timer = Timer(-1), RTC = 1 брой, Data Flash = 8 KB, /flash = около 94 KB.

import asyncio  # Импортираме asyncio, за да можем да изпълняваме повече от една задача кооперативно.
from machine import Pin  # Импортираме Pin, за да управляваме LED1 като цифров изход.

LED_PIN_NAME = "LED1"  # Използваме board alias-а на LED1.
LED_ON_LEVEL = 0  # Светнатият LED1 е логическа нула, защото платката е active-low.
LED_OFF_LEVEL = 1  # Изгасеният LED1 е логическа единица.

led = Pin(LED_PIN_NAME, Pin.OUT, value=LED_OFF_LEVEL)  # Подготвяме LED1 като изход и го държим изгасен в началото.


async def blink_led_task():  # Създаваме задача, която ще мига с LED1.
    led_state = LED_OFF_LEVEL  # Пазим текущото състояние на светодиода в отделна променлива.
    blink_count = 0  # Броим колко пъти сме сменили състоянието на LED-а.
    while True:  # Повтаряме задачата безкрайно.
        led_state = LED_ON_LEVEL if led_state == LED_OFF_LEVEL else LED_OFF_LEVEL  # Обръщаме състоянието между ON и OFF.
        led.value(led_state)  # Записваме новото състояние на LED1 върху GPIO пина.
        blink_count = blink_count + 1  # Увеличаваме брояча след всяка промяна.
        print("LED промяна номер:", blink_count, "стойност:", led_state)  # Печатаме какво се е случило в задачата.
        await asyncio.sleep_ms(300)  # Отстъпваме управлението за 300 ms.


async def count_seconds_task():  # Създаваме втора задача, която брои секундите.
    seconds = 0  # Началната стойност на брояча е нула секунди.
    while True:  # Повтаряме и тази задача безкрайно.
        seconds = seconds + 1  # Увеличаваме брояча с една секунда.
        print("Изминали секунди:", seconds)  # Показваме колко време е минало от старта на примера.
        await asyncio.sleep(1)  # Чакаме точно една секунда без блокиране на event loop-а.


async def main():  # Дефинираме главна асинхронна функция, която ще стартира двете задачи.
    print("=== Две асинхронни задачи ===")  # Печатаме заглавие на примера.
    print("Натисни Ctrl+C за спиране.")  # Даваме кратка инструкция за спиране.
    task_led = asyncio.create_task(blink_led_task())  # Стартираме задачата за LED-а като отделен task.
    task_seconds = asyncio.create_task(count_seconds_task())  # Стартираме задачата за секундите като втори task.
    await asyncio.gather(task_led, task_seconds)  # Изчакваме и двете задачи, което тук означава безкрайна работа.


asyncio.run(main())  # Стартираме event loop-а и главната функция.
