# Пример: Приблизително измерване на interrupt latency с time.ticks_us() на VK_RA4M2.
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
# Бележка: Този пример иска jumper между OUTPUT_PIN_NAME и INPUT_PIN_NAME.
# Бележка: Резултатът включва GPIO пътя, IRQ доставката, soft callback scheduling и Python overhead.

from machine import Pin, disable_irq, enable_irq  # Импортираме Pin и IRQ helpers за измерването.
import time  # Импортираме time за time.ticks_us(), time.ticks_diff() и кратки паузи.

OUTPUT_PIN_NAME = "P105"  # Това е GPIO изходът, който ще превключваме софтуерно.
INPUT_PIN_NAME = "P104"  # Това е GPIO входът, на който чакаме прекъсване през jumper.
MEASUREMENT_COUNT = 10  # Правим няколко повторения, за да видим диапазон от стойности.
TIMEOUT_US = 50000  # Даваме разумен timeout, за да не висим безкрайно при липса на jumper.

signal_out = Pin(OUTPUT_PIN_NAME, Pin.OUT, value=0)  # Изходът започва от логическа нула.
signal_in = Pin(INPUT_PIN_NAME, Pin.IN)  # Входът очаква същото ниво през jumper.
irq_state_data = {"start_us": 0, "latency_us": None, "count": 0, "armed": False}  # Пазим общото състояние в mutable структура.
latencies_us = []  # Тук ще събираме всички успешни измервания.


def input_handler(pin_object):  # Това е Python callback-ът при входното прекъсване.
    _ = pin_object  # Не използваме директно pin обекта, но пазим стандартната сигнатура.

    if irq_state_data["armed"]:  # Реагираме само когато основният код е въоръжил измерване.
        irq_state_data["latency_us"] = time.ticks_diff(time.ticks_us(), irq_state_data["start_us"])  # Смятаме микросекундите от превключването до callback-а.
        irq_state_data["count"] = irq_state_data["count"] + 1  # Увеличаваме броя обработени събития.
        irq_state_data["armed"] = False  # Сваляме флага, за да приключи чакането в основния цикъл.


signal_in.irq(handler=input_handler, trigger=Pin.IRQ_RISING, hard=False)  # Слушаме за rising edge с soft callback в Python контекст.

print("=== Interrupt latency с time.ticks_us() ===")  # Печатаме заглавие на примера.
print("Свържете jumper:", OUTPUT_PIN_NAME, "->", INPUT_PIN_NAME)  # Даваме точната хардуерна инструкция.
print("Ще мерим rising edge от изходния към входния GPIO.")  # Обясняваме какво точно се измерва.

for measurement_index in range(MEASUREMENT_COUNT):  # Повтаряме измерването няколко пъти.
    signal_out.value(0)  # Връщаме изхода в нула, за да има следващ валиден rising edge.
    time.sleep_ms(20)  # Даваме кратко време нивото да се установи.

    irq_state_data["latency_us"] = None  # Изчистваме предишния резултат.
    irq_state_data["armed"] = True  # Въоръжаваме обработчика за следващото събитие.
    irq_state_data["start_us"] = time.ticks_us()  # Запомняме момента преди генериране на фронта.
    signal_out.value(1)  # Генерираме rising edge на изхода.

    wait_start_us = irq_state_data["start_us"]  # Пазим началото на чакането за timeout проверката.

    while irq_state_data["armed"] and time.ticks_diff(time.ticks_us(), wait_start_us) < TIMEOUT_US:  # Чакаме callback-а да запише резултат или да изтече timeout.
        pass  # Държим цикъла нарочно прост, за да не вкарваме допълнителна логика в измерването.

    irq_state = disable_irq()  # Временно спираме прекъсванията, за да копираме консистентно резултата.
    latency_us = irq_state_data["latency_us"]  # Вземаме текущата измерена латентност.
    irq_count = irq_state_data["count"]  # Вземаме и общия брой хванати IRQ събития.
    enable_irq(irq_state)  # Връщаме предишното IRQ състояние.

    if latency_us is None:  # Ако няма резултат, вероятно няма jumper или връзката е грешна.
        print("Няма IRQ в рамките на", TIMEOUT_US, "us. Проверете jumper връзката.")  # Печатаме честна диагностична причина.
        break  # Спираме примера, защото следващите повторения няма да са полезни.

    latencies_us.append(latency_us)  # Записваме успешното измерване в списъка.
    print("Измерване", measurement_index + 1, "latency =", latency_us, "us", "irq_count =", irq_count)  # Печатаме текущия резултат.

signal_in.irq(handler=None)  # Изключваме IRQ обработчика в края на примера.
signal_out.value(0)  # Оставяме изхода в ниско ниво.

if latencies_us:  # Само ако има поне един валиден резултат, печатаме обобщение.
    average_us = sum(latencies_us) / len(latencies_us)  # Пресмятаме средната измерена стойност.
    print("min/avg/max =", min(latencies_us), average_us, max(latencies_us), "us")  # Показваме диапазона на измерванията.

print("Примерът за interrupt latency приключи.")  # Завършваме примера.