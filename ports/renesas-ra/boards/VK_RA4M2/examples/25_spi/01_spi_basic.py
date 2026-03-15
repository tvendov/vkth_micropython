# Пример: Базов hardware SPI в MicroPython на VK_RA4M2.
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
# Бележка: За loopback тест свържете MOSI към MISO, за да видите върнатите байтове.

from machine import SPI, Pin  # Импортираме SPI и Pin за достъп до хардуерния SPI канал.

spi = SPI(0, baudrate=500000, polarity=0, phase=0, bits=8, sck=Pin("P102"), mosi=Pin("P101"), miso=Pin("P100"), cs=Pin("P103"))  # Създаваме SPI обект върху валидните board пинове.

tx_buffer = bytes([0x11, 0x22, 0x33, 0x44])  # Това са тестовите байтове, които ще изпратим.
rx_buffer = bytearray(len(tx_buffer))  # Подготвяме приемен буфер със същата дължина.

print("=== Базов hardware SPI ===")  # Печатаме заглавие на примера.
print("Обектът е", spi)  # Показваме текущата конфигурация на SPI.

spi.write_readinto(tx_buffer, rx_buffer)  # Изпращаме байтовете и четем отговор едновременно във full-duplex режим.
print("Изпратени байтове:", list(tx_buffer))  # Печатаме изпратените байтове.
print("Получени байтове:", list(rx_buffer))  # Печатаме получените байтове.

print("Примерът за hardware SPI приключи.")  # Завършваме примера.
