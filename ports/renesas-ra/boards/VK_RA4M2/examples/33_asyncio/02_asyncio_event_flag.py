# Пример: asyncio Event и Flag синхронизация на VK_RA4M2.
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
# Ресурси на VK_RA4M2: Хардуерни Timer = 6 броя -> Timer(1), Timer(2), Timer(3), Timer(4), Timer(5), Timer(6), софтуерен Timer = Timer(-1), RTC = 1 брой, Data Flash = 8 KB, /flash = около 94 KB.

import asyncio  # Импортираме asyncio за кооперативна многозадачност.
from machine import Pin  # Импортираме Pin за реален хардуерен достъп.

LED_ON = 0  # Active-low: 0 = светва LED1.
LED_OFF = 1  # Active-low: 1 = гаси LED1.

led = Pin("LED1", Pin.OUT, value=LED_OFF)  # LED1 = P204, започваме изгасен.
button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # SW1 = P400, вътрешен pull-up.

button_event = asyncio.Event()  # Създаваме Event обект за синхронизация между задачи.


# --- Задача-производител: следи бутона и сигнализира ---
async def button_producer():  # Тази задача следи SW1 и сигнализира при натискане.
    last_value = 1  # Последна стойност на бутона (ненатиснат).

    while True:  # Безкрайно следене.
        current_value = button.value()  # Четем SW1.

        if current_value == 0 and last_value == 1:  # Нов натиск (falling edge).
            print("Производител: SW1 натиснат -> сигнализирам Event")  # Информираме.
            button_event.set()  # Сигнализираме Event-а.

        last_value = current_value  # Запомняме стойността.
        await asyncio.sleep_ms(50)  # Сканираме на 50 ms.


# --- Задача-консуматор: чака Event и реагира ---
async def led_consumer():  # Тази задача чака Event и мига LED1.
    blink_count = 0  # Брояч на мигания.

    while True:  # Безкрайно чакане.
        await button_event.wait()  # Чакаме Event-а (блокираме кооперативно).
        button_event.clear()  # Нулираме Event-а за следващо използване.

        blink_count += 1  # Увеличаваме брояча.
        print("Консуматор: получен Event #", blink_count, "-> мигам LED1")  # Информираме.

        for _ in range(3):  # Мигаме 3 пъти.
            led.value(LED_ON)  # Светваме.
            await asyncio.sleep_ms(100)  # Кратка пауза.
            led.value(LED_OFF)  # Гасим.
            await asyncio.sleep_ms(100)  # Кратка пауза.


# --- Задача-наблюдател: периодичен отчет ---
async def watchdog_task():  # Тази задача печата отчет на всеки 5 секунди.
    ticks = 0  # Брояч на отчети.

    while True:  # Безкрайно.
        await asyncio.sleep(5)  # Чакаме 5 секунди.
        ticks += 1  # Увеличаваме брояча.
        print("Наблюдател: отчет #", ticks, "- системата работи")  # Отчет.


# --- Главна функция ---
async def main():  # Стартираме всички задачи.
    print("=== asyncio Event синхронизация ===")  # Заглавие.
    print("Натисни SW1, за да мигне LED1 три пъти.")  # Указание.
    print("Натисни Ctrl+C за спиране.")  # Указание за спиране.

    await asyncio.gather(  # Стартираме трите задачи паралелно.
        button_producer(),  # Производител.
        led_consumer(),  # Консуматор.
        watchdog_task(),  # Наблюдател.
    )  # Край на gather.


asyncio.run(main())  # Стартираме event loop-а.

