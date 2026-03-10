# Пример: SoftSPI в MicroPython на VK_RA4M2.
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

from machine import SoftSPI, Pin  # Импортираме SoftSPI и Pin за софтуерен SPI върху GPIO.

soft_spi = SoftSPI(baudrate=100000, polarity=0, phase=0, sck=Pin("P105"), mosi=Pin("P104"), miso=Pin("P106"))  # Създаваме софтуерен SPI обект върху три GPIO пина.

tx_buffer = bytes([0xAA, 0x55, 0x0F, 0xF0])  # Подготвяме демонстрационен пакет от байтове.
rx_buffer = bytearray(len(tx_buffer))  # Подготвяме буфер за получените байтове.

print("=== SoftSPI ===")  # Печатаме заглавие на примера.

soft_spi.write_readinto(tx_buffer, rx_buffer)  # Изпращаме и четем едновременно със софтуерно генериран такт.
print("Изпратени байтове:", list(tx_buffer))  # Показваме какво сме изпратили.
print("Получени байтове:", list(rx_buffer))  # Показваме какво сме получили.

print("Примерът за SoftSPI приключи.")  # Завършваме примера.
