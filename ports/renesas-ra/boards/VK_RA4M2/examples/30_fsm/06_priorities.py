# Пример: Приоритети между събития във FSM на VK_RA4M2.
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

pending_events = ["BUTTON", "ERROR", "TIMEOUT"]  # Симулираме едновременно чакащи събития.
priority_order = {"ERROR": 0, "BUTTON": 1, "TIMEOUT": 2}  # По-малкото число означава по-висок приоритет.

pending_events.sort(key=lambda event_name: priority_order[event_name])  # Подреждаме събитията по приоритет.

print("=== Приоритети между събития ===")  # Печатаме заглавие на примера.
print("Подредени събития:", pending_events)  # Показваме вече сортирания списък.
print("Първо ще се обработи:", pending_events[0])  # Показваме събитието с най-висок приоритет.

print("Примерът за приоритети приключи.")  # Завършваме примера.
