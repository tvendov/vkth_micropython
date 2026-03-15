# Пример: Сравнение между хардуерен Timer(1) и софтуерен Timer(-1) на VK_RA4M2.
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
# Бележка: Примерът не измерва прецизност с осцилоскоп, а само показва как и двата типа таймери се стартират и броят събития.

from machine import Timer  # Импортираме Timer, за да използваме едновременно хардуерен и софтуерен таймер.
import time  # Импортираме time, за да дадем време на таймерите да работят.

hardware_timer = Timer(1)  # Създаваме хардуерен таймер върху първия наличен hardware канал.
software_timer = Timer(-1)  # Създаваме софтуерен таймер с идентификатор -1.
counter_state = {"hardware": 0, "software": 0}  # Пазим броя събития от двата таймера в обща структура.
soft_started = False  # Този флаг показва дали Timer(-1) е успял да тръгне в текущия runtime.


def hardware_callback(timer_object):  # Това е обработчикът за хардуерния таймер.
    counter_state["hardware"] = counter_state["hardware"] + 1  # Увеличаваме брояча за хардуерния таймер.


def software_callback(timer_object):  # Това е обработчикът за софтуерния таймер.
    counter_state["software"] = counter_state["software"] + 1  # Увеличаваме брояча за софтуерния таймер.


print("=== Сравнение: hardware срещу software timer ===")  # Печатаме заглавие на примера.

hardware_timer.init(freq=5, callback=hardware_callback, hard=False)  # Стартираме хардуерния таймер на 5 Hz.

try:  # Първо пробваме style-а с period и mode за Timer(-1).
    if hasattr(Timer, "PERIODIC"):  # Проверяваме дали класът Timer предлага константа за периодичен режим.
        software_timer.init(period=200, mode=Timer.PERIODIC, callback=software_callback)  # Стартираме и софтуерния таймер на 200 ms период.
        soft_started = True  # Отбелязваме, че software timer е стартирал.
except Exception as first_error:  # Ако това не тръгне, минаваме към вариант с честота.
    print("Timer(-1) period/mode API не тръгна:", first_error)  # Печатаме диагностична информация.

if not soft_started:  # Само ако още не е стартиран software timer, пробваме резервния стил.
    try:  # Влизаме във втори защитен блок.
        software_timer.init(freq=5, callback=software_callback, hard=False)  # Пробваме стартиране с честота вместо с период.
        soft_started = True  # Отбелязваме, че резервният стил е успял.
    except Exception as second_error:  # Ако и вторият стил не тръгне, продължаваме само с hardware timer.
        print("Timer(-1) freq API не тръгна:", second_error)  # Печатаме и втората диагностична причина.

time.sleep_ms(1200)  # Оставяме таймерите да работят малко повече от една секунда.
hardware_timer.deinit()  # Спираме хардуерния таймер в края на примера.

if soft_started:  # Спираме software timer само ако е бил стартиран успешно.
    software_timer.deinit()  # Деинициализираме software timer, за да няма висящи callback-и.

print("Хардуерният таймер е сработил", counter_state["hardware"], "пъти.")  # Печатаме броя на събитията от hardware timer.
print("Софтуерният таймер е сработил", counter_state["software"], "пъти.")  # Печатаме броя на събитията от software timer.

print("Примерът за сравнение на таймери приключи.")  # Завършваме примера.
