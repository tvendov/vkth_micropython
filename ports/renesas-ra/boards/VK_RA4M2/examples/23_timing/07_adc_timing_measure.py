# Пример: Измерване на ADC timing с time.ticks_us() на VK_RA4M2.
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
# Бележка: Тук мерим пълното време на ADC.read() през MicroPython, а не само времето на самото хардуерно A/D преобразуване.
# Бележка: Използваме вътрешния канал ADC.CORE_TEMP, за да не зависим от външен аналогов източник.

from machine import ADC  # Импортираме ADC, за да мерим read() в различни резолюции.
import time  # Импортираме time за time.ticks_us() и time.ticks_diff().

ADC_CHANNEL = ADC.CORE_TEMP  # Ползваме вътрешния температурен канал за стабилна демонстрация.
ADC_BITS_MODES = (8, 10, 12)  # Това са трите поддържани режима за RA4M2.
WARMUP_READS = 16  # Правим кратък warmup преди измерването.
MEASURED_READS = 500  # Мерим много последователни четения, за да има смислен интервал.
THEORETICAL_ADC_US = {8: "~0.31", 10: "~0.35", 12: "~0.39"}  # Ориентировъчни хардуерни времена при минимално валидно семплиране.


def measure_read_time(bits):  # Мерим общо и средно време за избраната ADC резолюция.
    adc = ADC(ADC_CHANNEL, bits=bits)  # Създаваме ADC обект и задаваме желания режим.

    for _ in range(WARMUP_READS):  # Загряваме с няколко четения извън измерването.
        adc.read()  # Правим реално четене, но не го включваме в измерения интервал.

    checksum = 0  # Пазим сбор, за да има реална работа във всяка итерация.
    start_us = time.ticks_us()  # Запомняме началния момент непосредствено преди цикъла.

    for _ in range(MEASURED_READS):  # Извършваме серия от ADC.read() извиквания.
        checksum = checksum + adc.read()  # Добавяме стойността към checksum.

    elapsed_us = time.ticks_diff(time.ticks_us(), start_us)  # Смятаме общото време на целия цикъл.
    average_us = elapsed_us / MEASURED_READS  # Пресмятаме средното време за едно read().
    return elapsed_us, average_us, checksum  # Връщаме резултатите за печат.


print("=== ADC timing с time.ticks_us() ===")  # Печатаме заглавие на примера.
print("Канал:", "ADC.CORE_TEMP")  # Показваме кой канал използваме.
print("Проби на режим:", MEASURED_READS)  # Показваме броя измерени read() извиквания.
print("Важно: bits е глобална настройка за ADC периферията, затова мерим режимите последователно.")  # Напомняме реалното поведение на порта.
print("Важно: числата по-долу са за целия Python път на ADC.read(), не само за ADC ядрото.")  # Даваме честна интерпретация на резултата.

for bits in ADC_BITS_MODES:  # Обхождаме трите поддържани режима на RA4M2.
    elapsed_us, average_us, checksum = measure_read_time(bits)  # Мерим текущия режим.
    print("bits =", bits, "общо =", elapsed_us, "us", "средно =", average_us, "us/read", "хардуер ADC ≈", THEORETICAL_ADC_US[bits], "us", "checksum =", checksum)  # Печатаме измереното и ориентировъчната теория.

print("Примерът за ADC timing приключи.")  # Завършваме примера.
