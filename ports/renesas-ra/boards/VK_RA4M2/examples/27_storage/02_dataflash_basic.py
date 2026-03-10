# Пример: dataflash модул в MicroPython на VK_RA4M2.
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
# Бележка: По подразбиране примерът само чете data flash и не прави запис или erase.

import dataflash  # Импортираме dataflash модула за достъп до отделната Data Flash област.

DO_WRITE = False  # Оставяме записа изключен по подразбиране, за да не променяме съдържанието без нужда.

print("=== Data Flash ===")  # Печатаме заглавие на примера.
print("size() =", dataflash.size())  # Печатаме общия размер на Data Flash областта.
print("block_size() =", dataflash.block_size())  # Печатаме размера на erase блока.
print("write_size() =", dataflash.write_size())  # Печатаме минималния размер за запис.

print("Първи 16 байта:", list(dataflash.read(0, 16)))  # Четем първите 16 байта от Data Flash.

if DO_WRITE:  # Само ако потребителят умишлено го включи, правим erase и write.
    dataflash.erase_block(0)  # Изтриваме първия блок, за да може после да се програмира.
    written = dataflash.write(0, b"RA4M2 DATA")  # Записваме кратък демонстрационен надпис в началото на Data Flash.
    print("Записани байтове:", written)  # Показваме колко байта са записани.
    print("След запис:", dataflash.read(0, 10))  # Четем обратно записаното съдържание.
else:  # В нормалния безопасен режим само обясняваме защо няма запис.
    print("DO_WRITE е False и примерът остава само за четене.")  # Печатаме безопасно съобщение.

print("Примерът за dataflash приключи.")  # Завършваме примера.
