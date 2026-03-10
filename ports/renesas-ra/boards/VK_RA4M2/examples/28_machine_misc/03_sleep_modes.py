# Пример: machine.sleep, machine.lightsleep и machine.deepsleep в MicroPython на VK_RA4M2.
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
# Бележка: lightsleep и deepsleep имат известни ограничения в текущия порт, затова тук ги пазим зад флагове.

import machine  # Импортираме machine за достъп до sleep режимите.

DO_LIGHTSLEEP = False  # Оставяме lightsleep изключен по подразбиране.
DO_DEEPSLEEP = False  # Оставяме deepsleep изключен по подразбиране.

print("=== Sleep режими ===")  # Печатаме заглавие на примера.

print("Извикваме кратък machine.lightsleep(20).")  # Подготвяме потребителя за кратък sleep.
machine.lightsleep(20)  # Извикваме кратък lightsleep за 20 ms.
print("machine.lightsleep(20) приключи.")  # Потвърждаваме, че краткият sleep е завършил.

if DO_LIGHTSLEEP:  # Само ако е включено, пробваме по-дълъг lightsleep.
    machine.lightsleep(100)  # Извикваме lightsleep за 100 ms.
else:  # В нормалния режим само печатаме бележка.
    print("DO_LIGHTSLEEP е False и по-дълъг lightsleep не се извиква.")  # Печатаме безопасно съобщение.

if DO_DEEPSLEEP:  # Само ако е включено, пробваме deepsleep.
    machine.deepsleep(1000)  # Извикваме deepsleep за 1 секунда.
else:  # В нормалния режим не пипаме deepsleep.
    print("DO_DEEPSLEEP е False и deepsleep не се извиква.")  # Печатаме безопасно съобщение.

print("Примерът за sleep режими приключи.")  # Завършваме примера.
