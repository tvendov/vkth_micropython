# Пример: Хардуерни таймери periodic и one-shot на VK_RA4M2.
# Ресурси на VK_RA4M2: LED = 1 брой -> LED1=P204.
# Ресурси на VK_RA4M2: Хардуерни таймери = 6 броя -> Timer(1), Timer(2), Timer(3), Timer(4), Timer(5), Timer(6).
# Ресурси на VK_RA4M2: Софтуерен таймер = Timer(-1).
# Бележка: В този пример използваме Timer(1) и Timer(2), но същият API стил важи и за Timer(3)..Timer(6).

from machine import Pin, Timer  # Импортираме Pin и Timer, за да покажем периодичен и еднократен хардуерен таймер.
import time  # Импортираме time, за да чакаме кратко между проверките.

led = Pin("LED1", Pin.OUT, value=1)  # Вземаме LED1 като видим индикатор за callback събитията.
periodic_timer = Timer(1)  # Избираме първия хардуерен таймер за периодичния режим.
oneshot_timer = Timer(2)  # Избираме втори хардуерен таймер за one-shot демонстрацията.

periodic_state = {"count": 0}  # Пазим броя на периодичните callback събития в mutable структура.
oneshot_state = {"fired": False}  # Пазим флаг дали one-shot таймерът вече е сработил.


def periodic_callback(timer_object):  # Тази функция се вика на всеки период от Timer(1).
    periodic_state["count"] = periodic_state["count"] + 1  # Увеличаваме брояча при всяко периодично събитие.
    led.value(0 if led.value() else 1)  # Превключваме LED1, за да има видима индикация.


def oneshot_callback(timer_object):  # Тази функция се вика само веднъж от Timer(2).
    oneshot_state["fired"] = True  # Отбелязваме, че еднократното събитие вече е дошло.
    led.value(1)  # Оставяме LED1 изгасен след еднократното събитие.


print("=== Хардуерен Timer.PERIODIC ===")  # Печатаме заглавие за първата част на примера.
periodic_timer.init(mode=Timer.PERIODIC, freq=2, callback=periodic_callback, hard=False)  # Стартираме Timer(1) на 2 Hz.
print("Timer(1).period() =", periodic_timer.period())  # Показваме периода в сурови AGT counts.

start_ms = time.ticks_ms()  # Запомняме началния момент, за да не чакаме безкрайно при проблем.
while periodic_state["count"] < 5 and time.ticks_diff(time.ticks_ms(), start_ms) < 4000:  # Чакаме до 5 callback събития.
    print("Timer(1).counter() =", periodic_timer.counter(), "callback count =", periodic_state["count"])  # Показваме текущия counter и броя събития.
    time.sleep_ms(250)  # Чакаме кратко, за да не flood-ваме конзолата.

periodic_timer.deinit()  # Спираме периодичния таймер след края на демонстрацията.
led.value(1)  # Оставяме LED1 изгасен между двете части на примера.
print("Периодичният таймер сработи", periodic_state["count"], "пъти.")  # Печатаме крайния брой периодични събития.

print("=== Хардуерен Timer.ONE_SHOT ===")  # Печатаме заглавие за one-shot режима.
oneshot_timer.init(mode=Timer.ONE_SHOT, freq=2, callback=oneshot_callback, hard=False)  # Стартираме Timer(2), който трябва да сработи само веднъж след около 500 ms.
print("Timer(2).period() =", oneshot_timer.period())  # Показваме и тук периода в сурови AGT counts.
time.sleep_ms(700)  # Чакаме достатъчно, за да има време one-shot таймерът да сработи.
oneshot_timer.deinit()  # Спираме таймера изрично, въпреки че в one-shot режим callback-ът е вече приключил.

print("One-shot fired =", oneshot_state["fired"])  # Печатаме дали еднократното събитие е дошло.
print("Примерът за periodic и one-shot хардуерни таймери приключи.")  # Завършваме примера.
