# Пример: Conditions vs Events във FSM на VK_RA4M2.
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

light_level = 600  # Това е моментното ниво от светлинния сензор в симулацията.
dark_threshold = 800  # Това е прагът, под който приемаме че е тъмно.
event_name = "BUTTON"  # Това е дискретното събитие от бутон.

condition_is_dark = light_level < dark_threshold  # Това е condition, която може да е вярна дълго време.

print("=== Conditions vs Events ===")  # Печатаме заглавие на примера.
print("Светлинното ниво е", light_level)  # Показваме входната стойност от сензора.
print("Condition dark =", condition_is_dark)  # Показваме condition резултата.
print("Event =", event_name)  # Показваме отделното дискретно събитие.

if condition_is_dark and event_name == "BUTTON":  # Комбинираме условие и събитие в общо решение.
    print("Лампата може да се включи, защото е тъмно и има бутонно събитие.")  # Печатаме положителния изход.
else:  # Ако комбинацията не е изпълнена, не включваме лампата.
    print("Няма достатъчно условия за включване.")  # Печатаме отрицателния изход.

print("Примерът за Conditions vs Events приключи.")  # Завършваме примера.
