# Пример: Tickless таймери и sleep идея в MicroPython на VK_RA4M2.
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
# Бележка: Това е учебна идея за tickless scheduler и не разчита на стабилен deepsleep в този порт.

import machine  # Импортираме machine за кратък lightsleep между събитията.
import time  # Импортираме time за работа с ticks_ms и ticks_diff.

tasks = [("lamp_timeout", 120), ("fan_timeout", 450), ("display_refresh", 40)]  # Подготвяме три задачи с различни крайни срокове в милисекунди.

start_ms = time.ticks_ms()  # Запомняме началния момент.
next_deadline_ms = min(deadline for _, deadline in tasks)  # Избираме най-близкия срок като tickless цел.
sleep_ms = max(0, next_deadline_ms - time.ticks_diff(time.ticks_ms(), start_ms))  # Изчисляваме колко време може да се спи до следващото събитие.

print("=== Tickless timers и sleep ===")  # Печатаме заглавие на примера.
print("Най-близкият срок е след", sleep_ms, "ms")  # Показваме изчисленото време за sleep.

machine.lightsleep(sleep_ms)  # Спим точно до най-близкото планирано събитие с lightsleep.

elapsed_ms = time.ticks_diff(time.ticks_ms(), start_ms)  # Изчисляваме изминалото време след съня.
ready_tasks = [task_name for task_name, deadline in tasks if deadline <= elapsed_ms]  # Избираме всички задачи, които вече са изтекли.

print("Изтекли задачи:", ready_tasks)  # Печатаме кои задачи са готови за обработка.

print("Примерът за tickless идея приключи.")  # Завършваме примера.
