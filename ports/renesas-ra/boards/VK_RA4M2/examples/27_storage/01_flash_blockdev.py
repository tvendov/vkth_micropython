# Пример: renesas.Flash и /flash в MicroPython на VK_RA4M2.
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
# Бележка: Този пример е само за четене и не променя вътрешната flash памет.

import os  # Импортираме os, за да покажем съдържанието на файловата система /flash.
import renesas  # Импортираме renesas модула, който излага Flash block device.

flash = renesas.Flash()  # Създаваме block device обект върху вътрешната flash памет.

sector = bytearray(16)  # Подготвяме малък буфер за четене на първите байтове от блок 0.
result = flash.readblocks(0, sector)  # Четем първите 16 байта от блок 0 в буфера.

print("=== renesas.Flash и /flash ===")  # Печатаме заглавие на примера.
print("Flash обектът е", flash)  # Показваме как изглежда block device обектът.
print("readblocks(0, sector) върна", result)  # Показваме кода за резултат от block четенето.
print("Първи 16 байта от block 0:", list(sector))  # Печатаме първите байтове в четим вид.

print("Съдържание на /flash:", os.listdir("/flash"))  # Показваме кои файлове има във вътрешната файлова система.

print("Примерът за renesas.Flash приключи.")  # Завършваме примера.
