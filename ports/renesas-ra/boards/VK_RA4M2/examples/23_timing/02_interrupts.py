# Пример: Прекъсвания в MicroPython на VK_RA4M2.
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
# Бележка: SW1 е active-low бутон, тоест падащ фронт означава натискане.
# Бележка: Pin.irq поддържа IRQ_FALLING и IRQ_RISING, а machine.disable_irq и machine.enable_irq управляват глобалното разрешаване на прекъсванията.

from machine import Pin, disable_irq, enable_irq  # Импортираме Pin, disable_irq и enable_irq за демонстрацията.
import time  # Импортираме time, за да следим събитията в продължение на няколко секунди.

button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # Настройваме SW1 като вход с pull-up.
led = Pin("LED1", Pin.OUT)  # Вземаме LED, за да имаме видима реакция при прекъсване.
irq_state_data = {"count": 0}  # Пазим броя натискания в mutable структура, достъпна и от IRQ обработчика.


def button_handler(pin_object):  # Това е функцията, която ще се вика при прекъсване от бутона.
    irq_state_data["count"] = irq_state_data["count"] + 1  # Увеличаваме броя на прекъсванията.
    led.value(0 if led.value() else 1)  # Превключваме LED при всяко прекъсване.


button.irq(handler=button_handler, trigger=Pin.IRQ_FALLING, hard=False)  # Регистрираме обработчик за падащ фронт на SW1.

print("=== Прекъсвания ===")  # Печатаме заглавие на примера.
print("Натиснете SW1 няколко пъти в следващите 5 секунди.")  # Даваме инструкция какво да направи потребителят.

start_ms = time.ticks_ms()  # Запомняме началния момент в милисекунди.

while time.ticks_diff(time.ticks_ms(), start_ms) < 5000:  # Наблюдаваме брояча в продължение на 5 секунди.
    irq_state = disable_irq()  # Временно забраняваме прекъсванията, за да вземем консистентно копие на брояча.
    snapshot = irq_state_data["count"]  # Копираме брояча, докато прекъсванията са спрени.
    enable_irq(irq_state)  # Възстановяваме предишното състояние на прекъсванията.
    print("Видени натискания досега:", snapshot)  # Печатаме текущия брой натискания.
    time.sleep_ms(500)  # Чакаме половин секунда между две проверки.

button.irq(handler=None)  # Премахваме IRQ обработчика в края на примера.
led.off()  # Оставяме LED изключен след края на примера.

print("Крайният брой прекъсвания е", irq_state_data["count"])  # Печатаме крайния брой обработени натискания.
print("Примерът за прекъсвания приключи.")  # Завършваме примера.
