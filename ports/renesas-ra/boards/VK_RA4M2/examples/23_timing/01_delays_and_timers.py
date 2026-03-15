# Пример: Закъснения и таймери в MicroPython на VK_RA4M2.
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
# Бележка: В тази глава показваме хардуерен Timer(1) и отделно асинхронен софтуерен timing с uasyncio.

from machine import Pin, Timer  # Импортираме Pin и Timer, за да покажем блокиращ и неблокиращ подход.
import time  # Импортираме time за блокиращото закъснение.
import uasyncio as asyncio  # Импортираме uasyncio за асинхронния вариант без задържане на главния поток.

led = Pin("LED1", Pin.OUT)  # Създаваме обект за потребителския LED на платката.
timer = Timer(1)  # Използваме първия от двата налични machine.Timer канала.
timer_state = {"count": 0}  # Пазим брояч в mutable структура, за да го променяме от callback.


def timer_callback(timer_object):  # Дефинираме callback, който Timer ще извиква периодично.
    timer_state["count"] = timer_state["count"] + 1  # Увеличаваме броя на таймерните събития.
    led.value(0 if led.value() else 1)  # Превключваме състоянието на LED, за да виждаме събитието.


print("=== Блокиращо закъснение ===")  # Печатаме заглавие за първата част.

for delay_index in range(3):  # Повтаряме три пъти прост блокиращ цикъл.
    led.value(0 if led.value() else 1)  # Превключваме LED и после чакаме със задържане на управлението.
    print("Блокираща стъпка", delay_index + 1)  # Показваме коя стъпка изпълняваме.
    time.sleep_ms(300)  # Това закъснение блокира изпълнението на останалия код.

print("=== Timer callback без busy wait ===")  # Печатаме заглавие за втората част.
timer.init(freq=4, callback=timer_callback, hard=False)  # Стартираме Timer(1) на 4 Hz с callback извън hard IRQ контекст.
time.sleep_ms(1200)  # Оставяме таймера да поработи малко време.
timer.deinit()  # Спираме таймера, за да не продължи да генерира събития.
print("Броят timer събития е", timer_state["count"])  # Печатаме колко пъти е извикан callback-ът.


async def async_blink_task():  # Дефинираме асинхронна задача за неблокиращо мигане.
    for async_index in range(4):  # Изпълняваме задачата четири пъти, за да остане примерът краен.
        led.value(0 if led.value() else 1)  # Превключваме LED и веднага отстъпваме управление след кратко време.
        print("Async стъпка", async_index + 1)  # Показваме текущата асинхронна стъпка.
        await asyncio.sleep_ms(150)  # Тук задачата спи неблокиращо и позволява други задачи да работят.


async def async_main():  # Дефинираме главна асинхронна функция.
    await async_blink_task()  # Изчакваме изпълнението на асинхронното мигане.


print("=== Asyncio вариант ===")  # Печатаме заглавие за асинхронния вариант.
asyncio.run(async_main())  # Стартираме асинхронната част на примера.
led.off()  # Оставяме LED изключен в края на примера.

print("Примерът за закъснения и таймери приключи.")  # Завършваме примера.
