# Пример: I2C с runtime зададени пинове в MicroPython на VK_RA4M2.
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

from machine import I2C, Pin  # Импортираме I2C и Pin, за да покажем runtime избиране на SCL и SDA.

scl_pin = Pin("P100")  # Избираме валиден SCL пин за I2C(1).
sda_pin = Pin("P101")  # Избираме валиден SDA пин за I2C(1).

i2c = I2C(1, freq=400000, scl=scl_pin, sda=sda_pin)  # Създаваме I2C(1) на 400 kHz с изрично зададени пинове.

print("=== I2C с runtime зададени пинове ===")  # Печатаме заглавие на примера.
print("Обектът е", i2c)  # Показваме как изглежда обектът след runtime конфигуриране.
print("Използваният SCL пин е", scl_pin)  # Печатаме избрания SCL пин.
print("Използваният SDA пин е", sda_pin)  # Печатаме избрания SDA пин.

print("Резултат от scan():", i2c.scan())  # Показваме, че и при runtime избрани пинове API-то работи нормално.

print("Примерът за runtime I2C пинове приключи.")  # Завършваме примера.
