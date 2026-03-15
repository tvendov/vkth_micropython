# Пример: Event count с хардуерен таймер на VK_RA4M2.
# Ресурси на VK_RA4M2: PWM тестов изход = P107.
# Ресурси на VK_RA4M2: AGT input capture pin за Timer(1) = P100.
# Ресурси на VK_RA4M2: Хардуерни таймери = 6 броя -> Timer(1), Timer(2), Timer(3), Timer(4), Timer(5), Timer(6).
# Бележка: За да работи примерът, свържете с джъмпер P107 към P100.
# Бележка: В event count режим няма channel callback; ако искате callback на всеки N импулса, задавате period=N и ползвате базовия Timer.callback().

from machine import Pin, PWM, Timer  # Импортираме Pin, PWM и Timer, за да генерираме и броим входни импулси.
import time  # Импортираме time, за да оставим брояча да работи кратко време.

signal_pwm = PWM(Pin("P107"), freq=200, duty=50)  # Генерираме тестов сигнал 200 Hz на P107, който ще броим на входа.
timer = Timer(1)  # Използваме Timer(1) като хардуерен брояч на събития.
state = {"windows": 0}  # Пазим колко пъти е завършил броячният прозорец от N импулса.


def every_n_events_callback(timer_object):  # Това е базовият callback, който идва след всеки завършен прозорец от N импулса.
    state["windows"] = state["windows"] + 1  # Увеличаваме брояча на завършените прозорци.


print("=== Хардуерен event count ===")  # Печатаме заглавието на примера.
print("Свържете P107 към P100 с джъмпер преди измерването.")  # Даваме ясна инструкция за необходимото окабеляване.

timer.init(mode=Timer.PERIODIC, period=20, callback=every_n_events_callback, hard=False)  # Настройваме прозорец от 20 нарастващи фронта на входа.
counter_channel = timer.channel(0, mode=Timer.IC, pin=Pin("P100"), measure=Timer.IC_EVENT_COUNT, edge=Timer.RISING)  # Пускаме channel(0) в event count режим.

time.sleep_ms(1100)  # Оставяме брояча да поработи малко над секунда.

partial_events = counter_channel.capture()  # Четем колко импулса има в текущия незавършен прозорец.
completed_windows = state["windows"]  # Вземаме колко пъти е дошъл базовият callback.
estimated_total = completed_windows * 20 + partial_events  # Изчисляваме приблизителния общ брой видени импулси.

timer.deinit()  # Спираме хардуерния таймер след края на примера.
signal_pwm.deinit()  # Спираме и тестовия PWM сигнал.

print("Завършени прозорци по 20 импулса =", completed_windows)  # Печатаме колко пълни групи по 20 импулса са минали.
print("Частичен брой в текущия прозорец =", partial_events)  # Печатаме колко импулса има в недовършения прозорец.
print("Приблизителен общ брой импулси =", estimated_total)  # Печатаме оценката за общия брой импулси.
print("Този пример показва кога е нужен callback: когато искате обработка на всеки N импулса, а не polling на всяко ребро.")  # Обясняваме смисъла на базовия callback.
print("Примерът за event count приключи.")  # Завършваме примера.
