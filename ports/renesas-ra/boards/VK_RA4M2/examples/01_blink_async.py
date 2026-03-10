# Пример: Асинхронно мигане на LED1 с asyncio на VK_RA4M2.
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

import asyncio  # Импортираме asyncio, за да правим неблокиращи задачи в MicroPython.
from machine import Pin  # Импортираме класа Pin, за да управляваме LED1 като цифров изход.

LED_PIN_NAME = "LED1"  # Използваме board alias-а на светодиода, за да е по-ясно за начинаещи.
LED_ON_LEVEL = 0  # При VK_RA4M2 светодиодът е active-low и свети при логическа нула.
LED_OFF_LEVEL = 1  # При VK_RA4M2 светодиодът изгасва при логическа единица.

led = Pin(LED_PIN_NAME, Pin.OUT, value=LED_OFF_LEVEL)  # Създаваме изход за LED1 и започваме от изгасено състояние.


async def blink_task():  # Дефинираме асинхронна задача, която ще мига без да блокира event loop-а.
    while True:  # Повтаряме мигането безкрайно, докато потребителят не спре програмата.
        led.value(LED_ON_LEVEL)  # Светваме LED1.
        print("LED1 = ON")  # Показваме в терминала, че LED1 е светнат.
        await asyncio.sleep_ms(500)  # Отстъпваме управлението за 500 ms, без да блокираме останалите задачи.
        led.value(LED_OFF_LEVEL)  # Гасим LED1.
        print("LED1 = OFF")  # Показваме в терминала, че LED1 е изгасен.
        await asyncio.sleep_ms(500)  # Отново отстъпваме управлението за 500 ms.


print("=== Асинхронно мигане на LED1 ===")  # Печатаме заглавие на примера.
print("Натисни Ctrl+C за спиране.")  # Обясняваме как се спира безкрайната програма.
asyncio.run(blink_task())  # Стартираме event loop-а и изпълняваме задачата.
