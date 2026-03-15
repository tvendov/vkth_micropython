# Пример: Бутон с прекъсване и отложена обработка на VK_RA4M2.
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
# Бележка: IRQ обработчикът тук не печата и не прави debounce директно, а само отлага работа към основния цикъл.
# Бележка: Използваме и IRQ_FALLING, и IRQ_RISING, за да можем да видим PRESS и RELEASE като отделни събития.

from machine import Pin, disable_irq, enable_irq  # Импортираме Pin и глобалните IRQ helper-и за безопасен достъп до споделено състояние.
import time  # Импортираме time, за да измерваме debounce прозореца след суров IRQ фронт.

DEBOUNCE_MS = 40  # Отлагаме окончателното решение с 40 ms след последния фронт.
MAIN_LOOP_SLEEP_MS = 5  # Основният цикъл ще проверява състоянието на всеки 5 ms.
DEMO_MS = 12000  # Оставяме примера да работи 12 секунди.

button = Pin("SW1", Pin.IN, Pin.PULL_UP)  # Настройваме SW1 като вход с pull-up резистор.
led = Pin("LED1", Pin.OUT, value=1)  # Настройваме LED1 като изход и започваме изгасени.

irq_state_data = {  # Пазим минималното споделено състояние между IRQ обработчика и главния цикъл.
    "irq_count": 0,  # Броим колко сурови фронта са минали през IRQ обработчика.
    "pending": 0,  # Този флаг показва, че има чакаща отложена обработка.
    "deadline_ms": 0,  # Тук пазим момента, след който може да вземем стабилно решение.
    "stable_value": button.value(),  # Започваме от текущото стабилно състояние на бутона.
}  # Завършваме началната структура за споделено състояние.
press_start_ms = None  # Тук ще пазим момента на последното валидно натискане.


def button_irq_handler(pin_object):  # Това е краткият IRQ обработчик, който само отбелязва, че има нов фронт.
    irq_state_data["irq_count"] = irq_state_data["irq_count"] + 1  # Увеличаваме брояча на суровите IRQ събития.
    irq_state_data["pending"] = 1  # Маркираме, че трябва да има отложена обработка в главния цикъл.
    irq_state_data["deadline_ms"] = time.ticks_add(time.ticks_ms(), DEBOUNCE_MS)  # Преместваме дедлайна за debounce след последния видян фронт.


button.irq(handler=button_irq_handler, trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING, hard=False)  # Регистрирaме IRQ обработчик и за натискане, и за отпускане.

print("=== Бутон с IRQ и deferred обработка ===")  # Печатаме заглавие на демонстрацията.
print("Натиснете и отпуснете SW1 няколко пъти в следващите 12 секунди.")  # Даваме инструкция към потребителя.

demo_start_ms = time.ticks_ms()  # Запомняме началния момент на демонстрацията.
last_reported_irq_count = 0  # Пазим последно отпечатания суров IRQ брояч за по-чист изход.

while time.ticks_diff(time.ticks_ms(), demo_start_ms) < DEMO_MS:  # Работим ограничено време, за да е удобен примерът.
    now_ms = time.ticks_ms()  # Вземаме текущото време в началото на итерацията.
    irq_lock_state = disable_irq()  # Спираме IRQ временно, за да копираме консистентно споделените полета.
    pending = irq_state_data["pending"]  # Вземаме локално копие на флага за чакаща обработка.
    deadline_ms = irq_state_data["deadline_ms"]  # Вземаме локално копие на debounce дедлайна.
    irq_count_snapshot = irq_state_data["irq_count"]  # Вземаме локално копие на броя сурови IRQ събития.
    enable_irq(irq_lock_state)  # Възстановяваме предишното IRQ състояние възможно най-бързо.
    if irq_count_snapshot != last_reported_irq_count:  # Ако броят сурови фронтове се е променил, отпечатваме диагностична информация.
        last_reported_irq_count = irq_count_snapshot  # Запомняме новата последно отпечатана стойност.
        print("RAW IRQ count =", irq_count_snapshot)  # Печатаме колко сурови фронта сме видели до момента.
    if pending and time.ticks_diff(now_ms, deadline_ms) >= 0:  # Ако има чакаща обработка и debounce прозорецът е изтекъл, вземаме стабилно решение.
        stable_candidate = button.value()  # Четем реалното ниво на бутона след изчакването за debounce.
        irq_lock_state = disable_irq()  # Отново спираме IRQ за кратко, за да обновим споделеното състояние последователно.
        irq_state_data["pending"] = 0  # Нулираме флага, защото обработваме текущия пакет фронтове.
        previous_stable_value = irq_state_data["stable_value"]  # Пазим предишното стабилно състояние за сравнение.
        if stable_candidate != previous_stable_value:  # Ако стабилното състояние реално е различно, приемаме ново събитие.
            irq_state_data["stable_value"] = stable_candidate  # Обновяваме споделеното стабилно състояние.
        enable_irq(irq_lock_state)  # Възстановяваме прекъсванията веднага след кратката критична секция.
        if stable_candidate != previous_stable_value:  # Само при реална промяна печатаме логическо събитие.
            if stable_candidate == 0:  # Ако стабилното ниво е паднало до 0, имаме валидно натискане.
                press_start_ms = now_ms  # Запомняме момента на натискането.
                led.value(0)  # Светваме LED1 като видим индикатор за PRESS събитие.
                print("EVENT: PRESS")  # Печатаме логическото събитие за натискане.
            else:  # Ако стабилното ниво се е върнало на 1, имаме валидно отпускане.
                release_duration_ms = 0 if press_start_ms is None else time.ticks_diff(now_ms, press_start_ms)  # Изчисляваме продължителността на натискането.
                led.value(1)  # Гасим LED1 като индикатор за RELEASE събитие.
                print("EVENT: RELEASE, duration_ms =", release_duration_ms)  # Печатаме събитието за отпускане.
                press_start_ms = None  # Нулираме началото на натискането за следващото събитие.
    time.sleep_ms(MAIN_LOOP_SLEEP_MS)  # Изчакваме кратко преди следващата проверка от основния цикъл.

button.irq(handler=None)  # Изключваме IRQ обработчика в края на примера.
led.value(1)  # Оставяме LED1 изгасен след края на демонстрацията.
print("Примерът за IRQ + deferred обработка приключи.")  # Завършваме примера.
