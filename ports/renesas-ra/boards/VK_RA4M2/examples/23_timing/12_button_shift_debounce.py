# Пример: Дебаунс на бутон чрез битово изместване на VK_RA4M2.
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
# Бележка: История 0x00 означава осем поредни натиснати проби, а история 0xFF означава осем поредни отпуснати проби.
# Бележка: Този подход е удобен, когато искаме компактен debounce филтър с фиксиран прозорец от проби.

from machine import Pin  # Импортираме Pin, за да четем SW1 и да управляваме LED1.
import time  # Импортираме time, за да правим периодично пробовземане на бутона.

SAMPLE_MS = 10  # Вземаме по една проба на всеки 10 ms.
LONG_PRESS_MS = 800  # Ползваме 800 ms като праг за long press след стабилен PRESS.
DEMO_MS = 12000  # Оставяме примера да работи 12 секунди.

button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # Настройваме SW1 като вход с pull-up резистор.
led = Pin("LED1", Pin.OUT, value=1)  # Настройваме LED1 като изход и започваме изгасени.

history = 0xFF  # Започваме с история от осем единици, защото бутонът нормално е отпуснат.
stable_value = button.value()  # Запомняме началното стабилно състояние на бутона.
press_start_ms = None  # Тук ще пазим момента на последното стабилно натискане.
long_reported = False  # С този флаг ще следим дали вече е излъчен LONG_PRESS за текущото задържане.

print("=== Дебаунс чрез битово изместване ===")  # Печатаме заглавие на примера.
print("Натиснете, задръжте и отпуснете SW1 в следващите 12 секунди.")  # Даваме инструкция към потребителя.

demo_start_ms = time.ticks_ms()  # Запомняме началния момент на демонстрацията.

while time.ticks_diff(time.ticks_ms(), demo_start_ms) < DEMO_MS:  # Работим само за ограничено време, за да е удобен примерът.
    now_ms = time.ticks_ms()  # Вземаме текущия момент в началото на итерацията.
    history = ((history << 1) | button.value()) & 0xFF  # Преместваме историята наляво и добавяме новата проба като най-млад бит.
    if history == 0x00 and stable_value != 0:  # Ако последните осем проби са 0, приемаме стабилно натискане.
        stable_value = 0  # Обновяваме стабилното състояние към натиснато.
        press_start_ms = now_ms  # Запомняме момента на стабилното натискане.
        long_reported = False  # Нулираме флага за long press за новото задържане.
        led.value(0)  # Светваме LED1, за да покажем PRESS събитие.
        print("EVENT: PRESS, history = 0x%02X" % history)  # Печатаме логическото събитие с текущата история.
    elif history == 0xFF and stable_value != 1:  # Ако последните осем проби са 1, приемаме стабилно отпускане.
        stable_value = 1  # Обновяваме стабилното състояние към отпуснато.
        release_duration_ms = 0 if press_start_ms is None else time.ticks_diff(now_ms, press_start_ms)  # Изчисляваме продължителността на натискането.
        led.value(1)  # Гасим LED1, за да покажем RELEASE събитие.
        print("EVENT: RELEASE, history = 0x%02X, duration_ms = %d" % (history, release_duration_ms))  # Печатаме логическото събитие RELEASE.
        press_start_ms = None  # Нулираме началния момент на натискането.
    elif stable_value == 0 and press_start_ms is not None and not long_reported and time.ticks_diff(now_ms, press_start_ms) >= LONG_PRESS_MS:  # Ако бутонът е стабилно натиснат достатъчно дълго, излъчваме long press.
        long_reported = True  # Маркираме, че long press вече е отчетен за това задържане.
        print("EVENT: LONG_PRESS, history = 0x%02X, held_ms = %d" % (history, time.ticks_diff(now_ms, press_start_ms)))  # Печатаме събитието за дълго задържане.
    time.sleep_ms(SAMPLE_MS)  # Изчакваме до следващата проба в shift-register филтъра.

led.value(1)  # Оставяме LED1 изгасен след края на примера.
print("Примерът за shift-register debounce приключи.")  # Завършваме примера.
