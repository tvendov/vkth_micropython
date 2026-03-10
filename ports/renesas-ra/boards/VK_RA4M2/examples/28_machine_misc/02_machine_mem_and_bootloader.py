# Пример: machine.disable_irq, machine.enable_irq, machine.mem32 и machine.bootloader в MicroPython на VK_RA4M2.
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
# Бележка: Примерът не пише по регистри и не влиза в bootloader, освен ако не включите DO_BOOTLOADER.

import machine  # Импортираме machine, за да покажем ниско ниво функции на системния модул.

DO_BOOTLOADER = False  # Оставяме bootloader скока изключен по подразбиране, за да не прекъснем сесията.

irq_state = machine.disable_irq()  # Временно забраняваме прекъсванията и пазим предишното състояние.
machine.enable_irq(irq_state)  # Възстановяваме прекъсванията веднага след демонстрацията.

print("=== machine low-level функции ===")  # Печатаме заглавие на примера.
print("Има ли machine.mem8:", hasattr(machine, "mem8"))  # Показваме дали портът излага 8-битов memory access.
print("Има ли machine.mem16:", hasattr(machine, "mem16"))  # Показваме дали портът излага 16-битов memory access.
print("Има ли machine.mem32:", hasattr(machine, "mem32"))  # Показваме дали портът излага 32-битов memory access.

print("Прекъсванията бяха изключени и после върнати безопасно.")  # Обясняваме какво демонстрирахме с disable_irq и enable_irq.

if DO_BOOTLOADER:  # Само ако потребителят умишлено включи, ще скочим към bootloader.
    machine.bootloader()  # Тази команда ще предаде управлението към bootloader на борда.
else:  # В стандартния безопасен режим само информираме.
    print("DO_BOOTLOADER е False и machine.bootloader() не се извиква.")  # Печатаме безопасното поведение.

print("Примерът за machine low-level функции приключи.")  # Завършваме примера.
