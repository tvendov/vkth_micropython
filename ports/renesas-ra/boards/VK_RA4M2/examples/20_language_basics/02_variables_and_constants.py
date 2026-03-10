# Пример: Променливи и константи в MicroPython на VK_RA4M2.
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

from micropython import const  # Импортираме const, за да дефинираме истински MicroPython константи.

BOARD_NAME = "VK_RA4M2"  # Това е обикновена низова променлива с името на платката.
LED_ALIAS = "LED1"  # Това е още една обикновена променлива, която пази board alias на LED-а.
BLINK_COUNT = const(3)  # Това е константа, която няма да променяме по време на работа.
BLINK_DELAY_MS = const(200)  # Това е константа за закъснение в милисекунди.

adc_external_pin_count = 13  # В тази променлива пазим броя на външните ADC пинове на платката.
touch_input_count = 12  # В тази променлива пазим броя на TouchPad входовете.
pwm_output_count = 14  # В тази променлива пазим броя на PWM изходите.
uses_asyncio = True  # Булева променлива ни показва дали платката има поддръжка на asyncio.

print("=== Променливи и константи ===")  # Показваме заглавието на примера.
print("Платка:", BOARD_NAME)  # Печатаме стойността на обикновена низова променлива.
print("LED alias:", LED_ALIAS)  # Печатаме друга низова променлива.
print("Брой мигания:", BLINK_COUNT)  # Печатаме цялочислена константа.
print("Закъснение в ms:", BLINK_DELAY_MS)  # Печатаме втора константа.
print("ADC външни пинове:", adc_external_pin_count)  # Печатаме броя на ADC пиновете.
print("TouchPad входове:", touch_input_count)  # Печатаме броя на TouchPad входовете.
print("PWM изходи:", pwm_output_count)  # Печатаме броя на PWM изходите.
print("Има asyncio:", uses_asyncio)  # Печатаме булева стойност.

adc_external_pin_count = adc_external_pin_count + 1  # Променяме обикновена променлива, за да покажем че тя е изменяема.
print("ADC пинове след тестова промяна:", adc_external_pin_count)  # Показваме новата стойност след промяната.
print("Константите BLINK_COUNT и BLINK_DELAY_MS не ги променяме в този пример.")  # Обясняваме разликата между променлива и константа.
