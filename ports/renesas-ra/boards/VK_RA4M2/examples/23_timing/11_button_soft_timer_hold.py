# Пример: Бутон със софтуерен Timer(-1), debounce и hold събития на VK_RA4M2.
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
# Бележка: Timer(-1) тук играе ролята на периодичен sampler на бутона, а печатането на събитията става в основния цикъл.
# Бележка: Събитията са PRESS, RELEASE, HOLD_START и HOLD_REPEAT.

from machine import Pin, Timer, disable_irq, enable_irq  # Импортираме Pin, Timer и IRQ helper-и за таймерния sampler.
import time  # Импортираме time, за да мерим продължителността на натискането и времето между repeat събитията.

SAMPLE_MS = 10  # Вземаме проба на бутона през 10 ms от софтуерния таймер.
LONG_PRESS_MS = 800  # Отчитаме задържане след 800 ms непрекъснато натискане.
HOLD_REPEAT_MS = 300  # След HOLD_START повтаряме HOLD_REPEAT през 300 ms.
DEMO_MS = 12000  # Оставяме примера да работи 12 секунди.

EVENT_NONE = 0  # Кодът 0 означава, че няма чакащо логическо събитие.
EVENT_PRESS = 1  # Кодът 1 означава валидно натискане.
EVENT_RELEASE = 2  # Кодът 2 означава валидно отпускане.
EVENT_HOLD_START = 3  # Кодът 3 означава начало на задържане.
EVENT_HOLD_REPEAT = 4  # Кодът 4 означава повторно събитие по време на задържане.

button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # Настройваме SW1 като вход с pull-up резистор.
led = Pin("LED1", Pin.OUT, value=1)  # Настройваме LED1 като изход и започваме с изгасен светодиод.
soft_timer = Timer(-1)  # Създаваме софтуерен таймер чрез идентификатор -1.

timer_state = {  # Пазим споделеното състояние между callback-а и основния цикъл в малка mutable структура.
    "history": 0xFF,  # Започваме с история 0xFF, защото бутонът първоначално е отпуснат и чете 1.
    "stable_value": button.value(),  # Запомняме началното стабилно състояние на бутона.
    "event_code": EVENT_NONE,  # Започваме без чакащо логическо събитие.
    "event_data": 0,  # Тук ще пазим продължителност или брояч, свързан със събитието.
    "press_start_ms": 0,  # Тук ще пазим момента на последното валидно натискане.
    "hold_started": 0,  # Този флаг показва дали вече е излъчено HOLD_START.
    "next_repeat_ms": 0,  # Тук пазим следващия момент за HOLD_REPEAT.
}  # Завършваме началната структура за таймерния sampler.
soft_started = False  # С този флаг ще следим дали Timer(-1) е стартирал успешно.


def button_sample_callback(timer_object):  # Това е callback-ът, който периодично взема проба от бутона.
    raw_value = button.value()  # Четем суровото моментно ниво на SW1.
    timer_state["history"] = ((timer_state["history"] << 1) | raw_value) & 0xFF  # Преместваме историята наляво и вкарваме новата проба като най-млад бит.
    now_ms = time.ticks_ms()  # Вземаме текущия момент за hold логиката.
    if timer_state["history"] == 0x00 and timer_state["stable_value"] != 0:  # Ако последните осем проби са 0, приемаме стабилно натискане.
        timer_state["stable_value"] = 0  # Обновяваме стабилното състояние на бутона към натиснато.
        timer_state["event_code"] = EVENT_PRESS  # Записваме логическо събитие PRESS за основния цикъл.
        timer_state["event_data"] = 0  # Нулираме помощните данни за това събитие.
        timer_state["press_start_ms"] = now_ms  # Запомняме момента на натискането.
        timer_state["hold_started"] = 0  # Нулираме флага за hold, защото започва нов натиск.
        timer_state["next_repeat_ms"] = time.ticks_add(now_ms, LONG_PRESS_MS)  # Планираме първия hold праг на LONG_PRESS_MS след натискането.
    elif timer_state["history"] == 0xFF and timer_state["stable_value"] != 1:  # Ако последните осем проби са 1, приемаме стабилно отпускане.
        timer_state["stable_value"] = 1  # Обновяваме стабилното състояние на бутона към отпуснато.
        timer_state["event_code"] = EVENT_RELEASE  # Записваме логическо събитие RELEASE за основния цикъл.
        timer_state["event_data"] = time.ticks_diff(now_ms, timer_state["press_start_ms"])  # Пазим колко е продължило последното натискане.
        timer_state["hold_started"] = 0  # Нулираме флага за hold, защото задържането е приключило.
    elif timer_state["stable_value"] == 0 and not timer_state["hold_started"] and time.ticks_diff(now_ms, timer_state["press_start_ms"]) >= LONG_PRESS_MS:  # Ако бутонът е стабилно натиснат и минава прага за задържане, излъчваме HOLD_START.
        timer_state["hold_started"] = 1  # Маркираме, че HOLD_START вече е излъчено.
        timer_state["event_code"] = EVENT_HOLD_START  # Записваме логическо събитие HOLD_START.
        timer_state["event_data"] = time.ticks_diff(now_ms, timer_state["press_start_ms"])  # Пазим колко време е минало до началото на задържането.
        timer_state["next_repeat_ms"] = time.ticks_add(now_ms, HOLD_REPEAT_MS)  # Планираме първия HOLD_REPEAT след фиксиран интервал.
    elif timer_state["stable_value"] == 0 and timer_state["hold_started"] and time.ticks_diff(now_ms, timer_state["next_repeat_ms"]) >= 0:  # Ако сме в режим на задържане и е дошло време за repeat, излъчваме HOLD_REPEAT.
        timer_state["event_code"] = EVENT_HOLD_REPEAT  # Записваме логическо събитие HOLD_REPEAT.
        timer_state["event_data"] = time.ticks_diff(now_ms, timer_state["press_start_ms"])  # Пазим общото време на задържане до текущия repeat.
        timer_state["next_repeat_ms"] = time.ticks_add(timer_state["next_repeat_ms"], HOLD_REPEAT_MS)  # Преместваме следващия repeat още една стъпка напред.


print("=== Бутон със софтуерен Timer(-1) и hold събития ===")  # Печатаме заглавие на примера.

try:  # Пробваме първо style-а с period и mode, защото той е най-четим за sampler логика.
    if hasattr(Timer, "PERIODIC"):  # Проверяваме дали текущият Timer клас предлага PERIODIC режим.
        soft_timer.init(period=SAMPLE_MS, mode=Timer.PERIODIC, callback=button_sample_callback)  # Стартираме софтуерния таймер с период SAMPLE_MS.
        soft_started = True  # Отбелязваме, че таймерът е стартирал успешно.
except Exception as first_error:  # Ако първият стил не работи, отпечатваме причината и ще пробваме резервен стил.
    print("Timer(-1) period/mode API не тръгна:", first_error)  # Печатаме диагностична информация за първия неуспех.

if not soft_started:  # Ако още не сме стартирали таймера, пробваме style с честота.
    try:  # Влизаме във втори защитен блок.
        soft_timer.init(freq=1000 // SAMPLE_MS, callback=button_sample_callback, hard=False)  # Стартираме sampler-а със същата честота чрез freq API.
        soft_started = True  # Отбелязваме, че резервният стил е успял.
    except Exception as second_error:  # Ако и вторият стил не работи, примера ще приключи с ясно съобщение.
        print("Timer(-1) freq API не тръгна:", second_error)  # Печатаме и втората диагностична причина.

demo_start_ms = time.ticks_ms()  # Запомняме началото на демонстрацията.

while soft_started and time.ticks_diff(time.ticks_ms(), demo_start_ms) < DEMO_MS:  # Ако таймерът е стартирал, чакаме събития за ограничено време.
    irq_lock_state = disable_irq()  # Спираме прекъсванията за кратко, за да копираме event полетата консистентно.
    event_code = timer_state["event_code"]  # Вземаме локално копие на чакащото логическо събитие.
    event_data = timer_state["event_data"]  # Вземаме локално копие на данните към събитието.
    if event_code != EVENT_NONE:  # Ако има чакащо събитие, го изчистваме, за да не се отпечата два пъти.
        timer_state["event_code"] = EVENT_NONE  # Нулираме чакащото събитие след копиране.
    enable_irq(irq_lock_state)  # Възстановяваме прекъсванията веднага след краткото копиране.
    if event_code == EVENT_PRESS:  # Ако чакащото събитие е PRESS, печатаме и светваме LED.
        led.value(0)  # Светваме LED1 като видим индикатор за PRESS.
        print("EVENT: PRESS")  # Печатаме логическото събитие PRESS.
    elif event_code == EVENT_RELEASE:  # Ако чакащото събитие е RELEASE, печатаме и гасим LED.
        led.value(1)  # Гасим LED1 като индикатор за RELEASE.
        print("EVENT: RELEASE, duration_ms =", event_data)  # Печатаме отпускането и продължителността на натискането.
    elif event_code == EVENT_HOLD_START:  # Ако чакащото събитие е HOLD_START, отбелязваме началото на задържането.
        print("EVENT: HOLD_START, held_ms =", event_data)  # Печатаме, че е започнало задържане.
    elif event_code == EVENT_HOLD_REPEAT:  # Ако чакащото събитие е HOLD_REPEAT, отбелязваме повторение по време на задържането.
        print("EVENT: HOLD_REPEAT, held_ms =", event_data)  # Печатаме repeat събитието и натрупаното време.
    time.sleep_ms(20)  # Изчакваме кратко преди следващата проверка на event полето.

if soft_started:  # Ако таймерът е стартирал успешно, го спираме чисто в края на примера.
    soft_timer.deinit()  # Деинициализираме софтуерния таймер.
else:  # Ако таймерът не е тръгнал, информираме потребителя ясно.
    print("Примерът не можа да стартира Timer(-1) в този runtime.")  # Даваме диагностично съобщение.

led.value(1)  # Оставяме LED1 изгасен след края на демонстрацията.
print("Примерът за Timer(-1) бутон приключи.")  # Завършваме примера.
