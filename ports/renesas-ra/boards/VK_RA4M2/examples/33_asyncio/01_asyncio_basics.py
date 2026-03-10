# Пример: Основи на asyncio за VK_RA4M2.
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

import asyncio  # Импортираме asyncio за кооперативна многозадачност.
from machine import Pin  # Импортираме Pin за реален хардуерен достъп.

LED_ON = 0  # Active-low: 0 = светва LED1.
LED_OFF = 1  # Active-low: 1 = гаси LED1.

led = Pin("LED1", Pin.OUT, value=LED_OFF)  # LED1 = P204, започваме изгасен.
button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # SW1 = P400, вътрешен pull-up.


# --- Задача 1: Мигане на LED1 ---
async def blink_task(period_ms):  # Асинхронна задача за мигане с зададен период.
    while True:  # Безкрайно мигане.
        led.value(LED_ON)  # Светваме LED1.
        await asyncio.sleep_ms(period_ms)  # Отстъпваме управлението за period_ms.

        led.value(LED_OFF)  # Гасим LED1.
        await asyncio.sleep_ms(period_ms)  # Отстъпваме управлението отново.


# --- Задача 2: Следене на бутон ---
async def button_task():  # Асинхронна задача за четене на SW1.
    press_count = 0  # Брояч на натискания.
    last_value = 1  # Последна стойност на бутона (ненатиснат).

    while True:  # Безкрайно следене.
        current_value = button.value()  # Четем текущото състояние на SW1.

        if current_value == 0 and last_value == 1:  # Нов натиск (falling edge).
            press_count += 1  # Увеличаваме брояча.
            print("SW1 натиснат! Брой:", press_count)  # Отпечатваме.

        last_value = current_value  # Запомняме текущата стойност.
        await asyncio.sleep_ms(50)  # Сканираме на всеки 50 ms (прост дебаунс).


# --- Задача 3: Периодичен отчет ---
async def report_task(interval_s):  # Асинхронна задача за периодичен отчет.
    seconds = 0  # Брояч на секунди.

    while True:  # Безкрайно отчитане.
        seconds += interval_s  # Увеличаваме брояча.
        print("Отчет: изминали", seconds, "секунди")  # Печатаме отчет.
        await asyncio.sleep(interval_s)  # Изчакваме interval_s секунди.


# --- Главна функция: стартиране на всички задачи ---
async def main():  # Главна async функция, която стартира трите задачи.
    print("=== Основи на asyncio ===")  # Печатаме заглавие.
    print("LED1 мига, SW1 се следи, отчет на всеки 3 сек.")  # Обясняваме какво прави примерът.
    print("Натисни Ctrl+C за спиране.")  # Указание за спиране.

    task_blink = asyncio.create_task(blink_task(300))  # Стартираме мигане на 300 ms.
    task_button = asyncio.create_task(button_task())  # Стартираме следене на SW1.
    task_report = asyncio.create_task(report_task(3))  # Стартираме отчет на 3 сек.

    await asyncio.gather(task_blink, task_button, task_report)  # Изчакваме всички задачи.


asyncio.run(main())  # Стартираме event loop-а.

