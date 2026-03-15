# Пример: Input capture за период и pulse width с хардуерен таймер на VK_RA4M2.
# Ресурси на VK_RA4M2: PWM тестов изход = P107.
# Ресурси на VK_RA4M2: AGT input capture pin за Timer(1) = P100.
# Ресурси на VK_RA4M2: Хардуерни таймери = 6 броя -> Timer(1), Timer(2), Timer(3), Timer(4), Timer(5), Timer(6).
# Бележка: За да работи примерът, свържете с джъмпер P107 към P100.
# Бележка: PWM на P107 генерира тестов сигнал, а Timer(1) измерва периода и ширината на high импулса на P100.

from machine import Pin, PWM, Timer  # Импортираме Pin, PWM и Timer за генерация и измерване на тестов сигнал.
import time  # Импортираме time, за да чакаме capture логиката да натрупа стойности.

signal_pwm = PWM(Pin("P107"), freq=1000, duty=50)  # Генерираме правоъгълен сигнал 1 kHz с 50 процента duty на P107.
timer = Timer(1)  # Използваме Timer(1) за input capture измерването.

print("=== Хардуерен input capture ===")  # Печатаме заглавието на примера.
print("Свържете P107 към P100 с джъмпер преди измерването.")  # Даваме ясна инструкция за необходимото окабеляване.

timer.init(mode=Timer.PERIODIC, freq=1_000_000, hard=False)  # Стартираме Timer(1) на 1 MHz, за да имаме резолюция 1 count = 1 us.

period_channel = timer.channel(0, mode=Timer.IC, pin=Pin("P100"), measure=Timer.IC_PERIOD, edge=Timer.RISING)  # Настройваме channel(0) за измерване на пълния период.
time.sleep_ms(200)  # Изчакваме capture логиката да се синхронизира с входния сигнал.
period_counts = period_channel.capture()  # Четем последно измерения период в сурови counts.

high_width_channel = timer.channel(0, mode=Timer.IC, pin=Pin("P100"), measure=Timer.IC_PULSE_WIDTH_HIGH)  # Пренастройваме същия канал да мери high ширината.
time.sleep_ms(200)  # Изчакваме новото измерване да се стабилизира.
high_width_counts = high_width_channel.capture()  # Четем ширината на high импулса в сурови counts.

timer.deinit()  # Спираме хардуерния таймер след края на измерването.
signal_pwm.deinit()  # Спираме и тестовия PWM сигнал.

print("Измерен период =", period_counts, "counts (~us при 1 MHz)")  # Печатаме периода в counts, което тук е приблизително и в микросекунди.
print("Измерена high ширина =", high_width_counts, "counts (~us при 1 MHz)")  # Печатаме high ширината по същия начин.
print("Очакване при 1 kHz / 50% duty: период около 1000, high около 500.")  # Даваме ориентир какви числа трябва да видим.
print("Примерът за input capture приключи.")  # Завършваме примера.
