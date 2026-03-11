# Пример: Четене на бутон с polling и събития на VK_RA4M2.
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
# Бележка: SW1 е active-low бутон, тоест 0 означава натиснато, а 1 означава отпуснато.
# Бележка: Този пример показва PRESS, RELEASE, CLICK и LONG_PRESS чрез обикновен polling цикъл.

from machine import Pin  # Импортираме Pin, за да четем SW1 и да управляваме LED1.
import time  # Импортираме time, за да мерим debounce интервали и продължителност на натискането.

SAMPLE_MS = 10  # Вземаме проба от бутона на всеки 10 ms.
DEBOUNCE_MS = 40  # Приемаме промяна за валидна само ако е стабилна поне 40 ms.
LONG_PRESS_MS = 800  # Приемаме задържане за long press след 800 ms.
DEMO_MS = 12000  # Оставяме примера да работи 12 секунди.

button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # Настройваме SW1 като вход с вътрешен pull-up резистор.
led = Pin("LED1", Pin.OUT, value=1)  # Настройваме LED1 като изход и започваме с изгасен светодиод.

stable_value = button.value()  # Запомняме текущото стабилно състояние на бутона като начално.
last_raw_value = stable_value  # Започваме и суровата стойност от същото начално състояние.
raw_change_ms = time.ticks_ms()  # Запомняме момента на последната сурова промяна.
press_start_ms = None  # Тук ще пазим момента на последното валидно натискане.
long_reported = False  # С този флаг ще пазим дали вече сме отчели LONG_PRESS за текущото задържане.

print("=== Бутон с polling и събития ===")  # Печатаме заглавие на демонстрацията.
print("Натиснете, отпуснете и задръжте SW1 в следващите 12 секунди.")  # Даваме указания към потребителя.

demo_start_ms = time.ticks_ms()  # Запомняме началния момент на демонстрацията.

while time.ticks_diff(time.ticks_ms(), demo_start_ms) < DEMO_MS:  # Работим само за ограничено време, за да е удобен примерът.
    now_ms = time.ticks_ms()  # Вземаме текущия момент в началото на всяка итерация.
    raw_value = button.value()  # Четем суровата моментна стойност на бутона.
    if raw_value != last_raw_value:  # Ако суровото ниво се е променило, започваме нов debounce прозорец.
        last_raw_value = raw_value  # Обновяваме последната видяна сурова стойност.
        raw_change_ms = now_ms  # Запомняме кога е започнала новата сурова промяна.
    if raw_value != stable_value and time.ticks_diff(now_ms, raw_change_ms) >= DEBOUNCE_MS:  # Ако суровата стойност се различава и е стабилна достатъчно дълго, приемаме ново стабилно състояние.
        stable_value = raw_value  # Обновяваме стабилното състояние на бутона.
        if stable_value == 0:  # Ако новото стабилно състояние е 0, значи имаме валидно натискане.
            press_start_ms = now_ms  # Запомняме момента на натискането.
            long_reported = False  # Нулираме флага за long press за новото задържане.
            led.value(0)  # Светваме LED1, за да покажем събитие PRESS.
            print("EVENT: PRESS")  # Печатаме събитие за натискане.
        else:  # Ако новото стабилно състояние е 1, значи имаме валидно отпускане.
            press_duration_ms = 0 if press_start_ms is None else time.ticks_diff(now_ms, press_start_ms)  # Изчисляваме колко е продължило натискането.
            led.value(1)  # Гасим LED1, за да покажем събитие RELEASE.
            print("EVENT: RELEASE, duration_ms =", press_duration_ms)  # Печатаме събитие за отпускане с продължителност.
            if press_duration_ms < LONG_PRESS_MS:  # Ако натискането е било по-късо от прага, приемаме CLICK.
                print("EVENT: CLICK")  # Печатаме събитие за кратко натискане.
            else:  # Ако натискането е било по-дълго от прага, то вече е long press освобождаване.
                print("EVENT: LONG_PRESS_RELEASE")  # Печатаме, че освобождаваме след дълго задържане.
            press_start_ms = None  # Нулираме началния момент на натискането.
    if stable_value == 0 and press_start_ms is not None and not long_reported:  # Ако бутонът още е натиснат и не сме отчели long press, проверяваме прага.
        held_ms = time.ticks_diff(now_ms, press_start_ms)  # Изчисляваме текущата продължителност на задържането.
        if held_ms >= LONG_PRESS_MS:  # Ако вече сме достигнали прага за long press, генерираме събитие.
            long_reported = True  # Маркираме, че long press вече е отчетен за това задържане.
            print("EVENT: LONG_PRESS, held_ms =", held_ms)  # Печатаме събитието за задържане.
    time.sleep_ms(SAMPLE_MS)  # Изчакваме до следващата polling проба.

led.value(1)  # Оставяме LED1 изгасен след края на примера.
print("Примерът за polling събития приключи.")  # Завършваме демонстрацията с финално съобщение.
