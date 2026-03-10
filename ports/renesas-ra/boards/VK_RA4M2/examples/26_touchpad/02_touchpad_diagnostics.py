# Пример: Диагностика и cached четене на TouchPad в MicroPython на VK_RA4M2.
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

from machine import Pin, TouchPad  # Импортираме Pin и TouchPad за cached и diagnostic API.

touch = TouchPad(Pin("P205"))  # Създаваме TouchPad върху канал P205.
touch.config(500)  # Задаваме праг за логическата оценка на докосване.

print("=== Диагностика на TouchPad ===")  # Печатаме заглавие на примера.

TouchPad.sample_rate(20)  # Стартираме глобалния cooperative sampler на 20 пълни scan-а в секунда.
touch.start()  # Стартираме non-blocking scan, ако не е в ход такъв.

for service_step in range(5):  # Изпълняваме няколко service стъпки, за да съберем cached проби.
    TouchPad.service()  # Придвижваме вътрешния sampler една стъпка от VM контекст.
    print("Стъпка", service_step + 1, "ready() =", touch.ready(),
          "cached =", touch.read_cached(),
          "value_cached =", touch.value_cached())  # Показваме cached състоянието след всяка стъпка.

print("age_ms() =", touch.age_ms())  # Печатаме възрастта на последната cached проба.
print("diagnose(8) =", touch.diagnose(8))  # Печатаме резултат от кратък диагностичен цикъл.
print("offset_tune(8) =", touch.offset_tune(8))  # Печатаме резултат от кратък offset tuning цикъл.
print("offsets() =", touch.offsets())  # Печатаме списъка с активните CTSU offset стойности.

TouchPad.sample_rate(0)  # Спираме глобалния sampler, за да не оставяме фонова активност.

print("Примерът за диагностика на TouchPad приключи.")  # Завършваме примера.
