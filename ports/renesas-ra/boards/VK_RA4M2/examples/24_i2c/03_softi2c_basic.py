# Пример: SoftI2C в MicroPython на VK_RA4M2.
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
# Бележка: SoftI2C е полезен, когато искаме I2C върху произволни GPIO пинове вместо върху хардуерните I2C линии.

from machine import SoftI2C, Pin  # Импортираме SoftI2C и Pin за софтуерна I2C шина върху GPIO.

scl_pin = Pin("P105", Pin.OPEN_DRAIN)  # Избираме P105 като софтуерен SCL пин.
sda_pin = Pin("P104", Pin.OPEN_DRAIN)  # Избираме P104 като софтуерен SDA пин.

soft_i2c = SoftI2C(scl=scl_pin, sda=sda_pin, freq=100000)  # Създаваме SoftI2C на 100 kHz.

print("=== SoftI2C ===")  # Печатаме заглавие на примера.
print("SoftI2C е създаден върху", scl_pin, "и", sda_pin)  # Показваме кои GPIO пинове използва софтуерната шина.

print("Резултат от scan():", soft_i2c.scan())  # Показваме как се прави сканиране и при SoftI2C.

print("Примерът за SoftI2C приключи.")  # Завършваме примера.
