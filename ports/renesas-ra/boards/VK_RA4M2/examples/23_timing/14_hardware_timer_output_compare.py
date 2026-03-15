# Пример: Output compare callback-и с хардуерен таймер на VK_RA4M2.
# Ресурси на VK_RA4M2: Хардуерни таймери = 6 броя -> Timer(1), Timer(2), Timer(3), Timer(4), Timer(5), Timer(6).
# Ресурси на VK_RA4M2: Timer(1) поддържа output compare channel(1) и channel(2).
# Бележка: Този пример показва compare callback-и без да изисква външен осцилоскоп или закачен изходен пин.
# Бележка: Ако искате и физически изход, за Timer(1) можете да добавите pin=Pin("P500") за channel(1) и pin=Pin("P501") за channel(2).

from machine import Timer  # Импортираме Timer, защото този пример работи изцяло през хардуерния AGT таймер.
import time  # Импортираме time, за да оставим compare събитията да се натрупат.

timer = Timer(1)  # Използваме Timer(1) за output compare демонстрацията.
state = {"cycle": 0, "compare_a": 0, "compare_b": 0}  # Пазим отделни броячи за края на периода и за двата compare канала.


def cycle_callback(timer_object):  # Това е базовият callback за края на всеки таймерен цикъл.
    state["cycle"] = state["cycle"] + 1  # Увеличаваме брояча при всяко cycle-end събитие.


def compare_a_callback(timer_object):  # Това е callback-ът за output compare channel(1).
    state["compare_a"] = state["compare_a"] + 1  # Увеличаваме брояча при всяко compare A събитие.


def compare_b_callback(timer_object):  # Това е callback-ът за output compare channel(2).
    state["compare_b"] = state["compare_b"] + 1  # Увеличаваме брояча при всяко compare B събитие.


print("=== Хардуерен output compare ===")  # Печатаме заглавието на примера.
timer.init(mode=Timer.PERIODIC, freq=10, callback=cycle_callback, hard=False)  # Стартираме Timer(1) на 10 Hz, за да имаме бавни и четими събития.

period_counts = timer.period()  # Вземаме суровия период в AGT counts, защото compare стойностите се задават в counts.
compare_a_value = period_counts // 4  # Избираме compare A на една четвърт от периода.
compare_b_value = (period_counts * 3) // 4  # Избираме compare B на три четвърти от периода.

channel_a = timer.channel(1, mode=Timer.OC, compare=compare_a_value, callback=compare_a_callback)  # Създаваме output compare channel(1).
channel_b = timer.channel(2, mode=Timer.OC, compare=compare_b_value, callback=compare_b_callback)  # Създаваме output compare channel(2).

print("Timer(1).period() =", period_counts)  # Печатаме общия период в counts.
print("channel(1).compare() =", channel_a.compare())  # Печатаме compare стойността на канал A.
print("channel(2).compare() =", channel_b.compare())  # Печатаме compare стойността на канал B.

time.sleep_ms(1200)  # Оставяме таймера да направи няколко цикъла и compare събития.

channel_a.callback(None)  # Спираме callback-а на compare A.
channel_b.callback(None)  # Спираме callback-а на compare B.
timer.deinit()  # Спираме и самия таймер.

print("Cycle-end callback count =", state["cycle"])  # Печатаме колко пъти е завършил цикълът.
print("Compare A callback count =", state["compare_a"])  # Печатаме броя compare A събития.
print("Compare B callback count =", state["compare_b"])  # Печатаме броя compare B събития.
print("Примерът за output compare callback-и приключи.")  # Завършваме примера.
