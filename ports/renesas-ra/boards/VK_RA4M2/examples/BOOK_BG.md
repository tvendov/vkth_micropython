# Книга: MicroPython за VK_RA4M2 чрез демонстрационни примери

## Цел

Тази книга е направена върху реално наличния пакет от `50` примерни `.py` файла за `VK_RA4M2`.

Тук целта не е само да се изброят файловете.

Целта е да се покрият всички глави, теми и примери от плана, без пропуски и без измислени ресурси.

Затова книгата е организирана в три слоя:

1. Ресурсна карта на борда и порта.
2. Матрица за пълност спрямо учебния план.
3. Подробни глави с кодови откъси и препратки към всички реални сорс файлове.

## Как да се ползва книгата

Книгата се чете заедно със сорс файловете в `examples`.

Препоръчителният ритъм е:

1. Прочитате теорията в съответната глава.
2. Стартирате показания минипример.
3. Отваряте пълния `.py` файл.
4. Преглеждате ресурсния header в началото му.
5. Променяте само по една стойност и наблюдавате ефекта.

Когато примерът съдържа предпазен флаг като `DO_WRITE`, `DO_BOOTLOADER`, `DO_LIGHTSLEEP`, `DO_DEEPSLEEP` или `SET_DEMO_TIME`, започвайте с безопасната стойност.

## Ресурсна карта на VK_RA4M2

Книгата и примерите приемат следната карта на ресурси:

- LED: `1 брой` -> `LED1 = P204`
- Бутон: `1 брой` -> `SW1 = P400`
- USB device пинове: `USBDP = P914`, `USBDM = P915`, `USB_VBUS = P407`
- ADC външни входове: `13 броя` -> `P000`, `P001`, `P002`, `P003`, `P004`, `P005`, `P006`, `P007`, `P008`, `P013`, `P014`, `P015`, `P500`
- ADC вътрешни източници: `3 броя` -> `ADC.CORE_TEMP`, `ADC.CORE_VREF`, `ADC.VREF`
- DAC изходи: `2 броя` -> `P014`, `P015`
- PWM изходи: `14 броя` -> `P107`, `P106`, `P105`, `P104`, `P113`, `P114`, `P112`, `P115`, `P608`, `P409`, `P408`, `P600`, `P304`, `P303`
- UART: `4 инстанции` -> `UART(0)`, `UART(2)`, `UART(7)`, `UART(9)`
- I2C master: `2 инстанции` -> `I2C(0)=P400/P401`, `I2C(1)=P100/P101`
- I2CTarget: `2 инстанции` -> `I2CTarget(0)=P400/P401`, `I2CTarget(1)=P100/P101`
- SPI: `1 канал` -> `CS=P103`, `SCK=P102`, `MISO=P100`, `MOSI=P101`
- SoftI2C: наличен
- SoftSPI: наличен
- TouchPad входове: `12 броя` -> `P205`, `P206`, `P407`, `P408`, `P409`, `P410`, `P411`, `P412`, `P413`, `P414`, `P415`, `P708`
- CTSU специален пин: `P207 = TSCAP`
- Хардуерни таймери: `2 броя` -> `Timer(1)`, `Timer(2)`
- Софтуерен таймер: `Timer(-1)`
- RTC: `1 брой`
- Вътрешна файлова система: `/flash`
- `renesas.Flash`: наличен block device
- `dataflash`: `8 KB`

Важни уточнения:

- `SPI` тук се разглежда като `1` канал.
- `Timer(-1)` тук се разглежда като софтуерен таймер.
- `SW1` е върху `P400`, което влиза в конфликт с `I2C(0).SCL`.
- `LED1` е active-low: логическа `0` го светва, логическа `1` го гаси.
- `Pin.PULL_UP` се ползва в курса; `Pin.PULL_DOWN` не се разглежда като наличен ресурс в тези уроци.
- `lightsleep` и `deepsleep` са показани предпазливо.

## Матрица за пълност спрямо плана

Това е контролният списък, по който книгата е проверена.

- `Примерна програма` -> `examples/20_language_basics/01_sample_program.py`
- `Променливи и константи` -> `examples/20_language_basics/02_variables_and_constants.py`
- `Аритметични оператори` -> `examples/20_language_basics/03_operators_conditions_loops.py`
- `Логически оператори` -> `examples/20_language_basics/03_operators_conditions_loops.py`
- `if / if-else / if-elif-else` -> `examples/20_language_basics/03_operators_conditions_loops.py`
- `goto в MicroPython липсва` -> обяснено в главата за управление на изпълнението
- `switch-case в MicroPython липсва` -> обяснено в главата за управление на изпълнението
- `while` -> `examples/20_language_basics/03_operators_conditions_loops.py`
- `for` -> `examples/20_language_basics/03_operators_conditions_loops.py`
- `do-while еквивалент` -> `examples/20_language_basics/03_operators_conditions_loops.py`
- `Цифров изход` -> `examples/21_digital_io/01_digital_output.py`, `examples/01_blink_async.py`
- `Цифров вход` -> `examples/21_digital_io/02_digital_input.py`
- `Аналогов вход` -> `examples/22_analog/01_analog_input_adc.py`
- `Какво е ADC и как работи` -> обяснено в главата за ADC и показано в `examples/22_analog/01_analog_input_adc.py`
- `PWM изход` -> `examples/22_analog/02_pwm_output.py`, `examples/03_pwm_ab_same_channel.py`
- `Enums` -> `examples/20_language_basics/04_enums.py`
- `Масив` -> `examples/20_language_basics/05_arrays.py`
- `Масив от масиви` -> `examples/20_language_basics/05_arrays.py`
- `Структури` -> `examples/20_language_basics/06_structures.py`
- `Структури от структури` -> `examples/20_language_basics/06_structures.py`
- `Масив от структури` -> `examples/20_language_basics/06_structures.py`
- `Указатели като идея` -> `examples/20_language_basics/07_pointers_equivalents.py`
- `Указател към променлива/масив/структура` -> еквиваленти чрез references и mutable обекти в `examples/20_language_basics/07_pointers_equivalents.py`
- `Указател към указател` -> обяснено като верига от references и индирекция чрез mutable обекти
- `Масив от указатели` -> еквивалент чрез списък от функции в `examples/20_language_basics/08_functions.py`
- `Функция` -> `examples/20_language_basics/08_functions.py`
- `Входни и изходни параметри` -> `examples/20_language_basics/08_functions.py`
- `Параметри чрез указател` -> еквивалент чрез mutable обект в `examples/20_language_basics/08_functions.py`
- `Указател към функция` -> callback reference в `examples/20_language_basics/08_functions.py`
- `Масив от указатели към функции` -> `operation_table` в `examples/20_language_basics/08_functions.py`
- `Таймери и закъснения` -> `examples/23_timing/01_delays_and_timers.py`
- `Софтуерен таймер` -> `examples/23_timing/04_software_timer_minus1.py`
- `Хардуерен срещу софтуерен таймер` -> `examples/23_timing/05_timer_compare_hw_sw.py`
- `Прекъсвания` -> `examples/23_timing/02_interrupts.py`
- `Приоритети` -> `examples/30_fsm/06_priorities.py`
- `Забраняване/разрешаване на прекъсвания` -> `examples/23_timing/02_interrupts.py`, `examples/28_machine_misc/02_machine_mem_and_bootloader.py`
- `Четене на бутон и дебаунс` -> `examples/23_timing/03_button_debounce.py`
- `Динамична 7-сегментна индикация` -> `examples/29_display_scan/01_dynamic_7seg_and_keypad.py`
- `Прекъсвания по таймер / времево опресняване` -> обяснено в главата за display scan и timer главите
- `Матрично сканиране на бутони` -> `examples/29_display_scan/01_dynamic_7seg_and_keypad.py`
- `FSM идея` -> `examples/30_fsm/01_fsm_lamp_button.py`
- `Графично и таблично описание на FSM` -> обяснено в главата за FSM и показано в `examples/30_fsm/02_fsm_table.py`
- `FSM с timeout` -> `examples/30_fsm/03_fsm_timeout.py`
- `Conditions vs Events` -> `examples/30_fsm/04_conditions_vs_events.py`
- `Guard функции` -> `examples/30_fsm/05_guard_functions.py`
- `Реален пример: баня` -> `examples/31_case_study/01_bathroom_control.py`
- `Декомпозиция на 2 FSM` -> `examples/31_case_study/02_two_fsms.py`
- `Комуникация между стейт машини` -> `examples/31_case_study/02_two_fsms.py`
- `Event queue` -> `examples/30_fsm/07_event_queue.py`
- `Tickless timers + sleep` -> `examples/30_fsm/08_tickless_sleep.py`, `examples/28_machine_misc/03_sleep_modes.py`

## Част I: Първи стъпки с борда и с MicroPython

### Глава 1. Първа програма

Първата програма трябва да даде веднага видим резултат и да покаже как изглежда MicroPython файл за `VK_RA4M2`.

Ключова идея:

```python
from machine import Pin
import time

led = Pin("LED1", Pin.OUT, value=1)

for blink_index in range(3):
    led.value(0)
    time.sleep_ms(300)
    led.value(1)
    time.sleep_ms(300)
```

Тук се виждат наведнъж:

- `Pin`
- `for`
- `sleep_ms`
- active-low поведението на `LED1`

Файлове в главата:

- `examples/20_language_basics/01_sample_program.py`

### Глава 2. Първи асинхронни примери

Преди да влезем в детайлите на езика, полезно е да се види как изглежда неблокиращото мислене.

Минипример:

```python
import asyncio
from machine import Pin

led = Pin("LED1", Pin.OUT, value=1)

async def blink_task():
    while True:
        led.value(0)
        await asyncio.sleep_ms(500)
        led.value(1)
        await asyncio.sleep_ms(500)
```

Втори минипример:

```python
async def count_seconds_task():
    seconds = 0
    while True:
        seconds += 1
        print("Изминали секунди:", seconds)
        await asyncio.sleep(1)
```

Тези примери показват две идеи:

- `asyncio` като кооперативен scheduler
- разделяне на поведението на независими задачи

Файлове в главата:

- `examples/01_blink_async.py`
- `examples/02_two_tasks.py`

## Част II: Езикови основи на MicroPython

### Глава 3. Променливи и константи

Пример:

```python
from micropython import const

BOARD_NAME = "VK_RA4M2"
LED_ALIAS = "LED1"
BLINK_COUNT = const(3)
BLINK_DELAY_MS = const(200)
uses_asyncio = True
```

Тук разграничението е просто:

- променлива е нещо, което променяме
- `const()` е именувана константа, с която описваме числа и режими

Файл:

- `examples/20_language_basics/02_variables_and_constants.py`

### Глава 4. Оператори, условия и цикли

Тази глава покрива аритметика, сравнения, логика и контрол на потока.

Пример:

```python
left_value = 7
right_value = 3

sum_value = left_value + right_value
logic_result = (left_value > right_value) and (left_value != right_value)

if left_value < right_value:
    print("По-малка")
elif left_value == right_value:
    print("Равна")
else:
    print("По-голяма")
```

Циклите са показани и в трите практически форми:

```python
for index in range(3):
    print(index)

counter = 0
while counter < 3:
    print(counter)
    counter += 1

attempt = 0
while True:
    print(attempt)
    attempt += 1
    if attempt >= 2:
        break
```

Важни уточнения за начинаещи:

- `goto` няма в Python и в MicroPython
- `switch-case` няма в класическия MicroPython синтаксис на тези примери
- еквивалентите са `if/elif/else`, таблици, `break`, `continue`, `return`
- `do-while` се показва като `while True` + `break`

Файл:

- `examples/20_language_basics/03_operators_conditions_loops.py`

### Глава 5. Enums в MicroPython

Пример:

```python
from micropython import const

STATE_OFF = const(0)
STATE_ON = const(1)
STATE_BLINK = const(2)
```

Това е правилният стил за тези уроци:

- именувани константи
- таблица за превод към четимо име

Файл:

- `examples/20_language_basics/04_enums.py`

### Глава 6. Масиви и таблици

Пример:

```python
from array import array

adc_samples = [1200, 1215, 1198, 1222]
led_table = [["LED1", "P204"], ["SW1", "P400"]]
pwm_duty_values = array("H", [0, 16384, 32768, 49152, 65535])
raw_bytes = bytearray([0x10, 0x20, 0x30, 0x40])
```

Тук има четири различни вида колекции:

- `list`
- вложен `list`, тоест масив от масиви
- `array`
- `bytearray`

Файл:

- `examples/20_language_basics/05_arrays.py`

### Глава 7. Структури в MicroPython

Вместо `struct` от C, в MicroPython се ползват:

- `dict`
- `namedtuple`
- клас
- списък от такива обекти

Пример:

```python
from collections import namedtuple

SensorRecord = namedtuple("SensorRecord", ("name", "pin", "unit"))
light_sensor = {"name": "Light", "adc_pin": "P000", "limits": {"dark": 800, "bright": 3000}}
devices = [{"name": "LED1", "pin": "P204", "kind": "output"}]
```

Тук има и:

- структура от структури чрез вложен `dict`
- масив от структури чрез списък от `dict`

Файл:

- `examples/20_language_basics/06_structures.py`

### Глава 8. Указатели и еквивалентите им в MicroPython

Тази глава е умишлено преведена към MicroPython, а не към C синтаксис.

Пример:

```python
adc_list = [100, 200, 300]
alias_list = adc_list
alias_list[1] = 999

tx_buffer = bytearray([1, 2, 3, 4])
tx_view = memoryview(tx_buffer)
tx_view[2] = 77
```

Тук се показват:

- две имена към един и същ обект
- промяна на буфер „на място“
- `memoryview` като евтин прозорец към същите данни
- ниско ниво чрез `machine.mem8`, `mem16`, `mem32`

Файлове:

- `examples/20_language_basics/07_pointers_equivalents.py`
- `examples/28_machine_misc/02_machine_mem_and_bootloader.py`

### Глава 9. Функции, callbacks и таблици от функции

Пример:

```python
def add_values(left_value, right_value):
    return left_value + right_value

def run_callback(callback, value):
    callback(value)

def square(value):
    return value * value

operation_table = [square]
```

Тази глава покрива:

- входни параметри
- връщане на стойности
- multiple return values
- mutable параметри вместо „предаване с указател“
- функция като стойност
- списък от функции като таблица от „function pointers“

Файл:

- `examples/20_language_basics/08_functions.py`

## Част III: Цифрови и аналогови входове и изходи

### Глава 10. Цифров изход

Пример:

```python
from machine import Pin
import time

led = Pin("LED1", Pin.OUT, value=1)
led.value(0)
time.sleep_ms(700)
led.value(1)
```

Тук се обясняват:

- логическа `0`
- логическа `1`
- защо при active-low LED те не означават директно „изключено“ и „включено“
- че допустимият ток и товароспособността се гледат по hardware manual и схемата на борда

Файлове:

- `examples/21_digital_io/01_digital_output.py`
- `examples/01_blink_async.py`

### Глава 11. Цифров вход

Пример:

```python
from machine import Pin

button = Pin("SW1", Pin.IN, Pin.PULL_UP)
print(button.value())
```

Тук се обясняват:

- входно ниво
- активен нисък бутон
- вътрешен `PULL_UP`
- защо `PULL_DOWN` не е част от тези уроци за този порт

Файл:

- `examples/21_digital_io/02_digital_input.py`

### Глава 12. Аналогов вход и ADC

Пример:

```python
from machine import ADC

temperature_adc = ADC(ADC.CORE_TEMP)
vref_adc = ADC(ADC.CORE_VREF)

print(temperature_adc.read())
print(vref_adc.read())
```

Тук се учат:

- какво е ADC
- как напрежението става число
- разликата между сурово четене и `read_u16()`
- защо вътрешните канали са добър старт

Наличните външни ADC пинове в този курс са:

`P000`, `P001`, `P002`, `P003`, `P004`, `P005`, `P006`, `P007`, `P008`, `P013`, `P014`, `P015`, `P500`

Файл:

- `examples/22_analog/01_analog_input_adc.py`

### Глава 13. PWM

Базов пример:

```python
from machine import Pin, PWM

pwm = PWM(Pin("P107"), freq=1000, duty=50)
```

Разширен пример за общ GPT канал:

```python
pwm_a = PWM(Pin("P107"), freq=1000, duty=25)
pwm_b = PWM(Pin("P106"), freq=1000, duty=75)
pwm_a.freq(2000)
```

Тук се вижда, че:

- duty може да е различен
- честотата се споделя от двата изхода на общия GPT канал

Файлове:

- `examples/22_analog/02_pwm_output.py`
- `examples/03_pwm_ab_same_channel.py`

## Част IV: Таймери, прекъсвания и време

### Глава 14. Закъснения, хардуерни таймери и `Timer(-1)`

Блокиращо закъснение:

```python
import time
time.sleep_ms(300)
```

Хардуерен таймер:

```python
from machine import Timer

timer = Timer(1)
timer.init(freq=4, callback=timer_callback, hard=False)
```

Софтуерен таймер:

```python
soft_timer = Timer(-1)
soft_timer.init(freq=4, callback=soft_timer_callback, hard=False)
```

Тук трябва да остане ясно:

- `Timer(1)` и `Timer(2)` са хардуерни
- `Timer(-1)` е софтуерен
- `asyncio` е още една неблокираща алтернатива

Файлове:

- `examples/23_timing/01_delays_and_timers.py`
- `examples/23_timing/04_software_timer_minus1.py`
- `examples/23_timing/05_timer_compare_hw_sw.py`

### Глава 15. Прекъсвания

Пример:

```python
from machine import Pin, disable_irq, enable_irq

button = Pin("SW1", Pin.IN, Pin.PULL_UP)

def button_handler(pin_object):
    print("IRQ")

button.irq(handler=button_handler, trigger=Pin.IRQ_FALLING, hard=False)
```

Безопасно четене на споделени данни:

```python
irq_state = disable_irq()
snapshot = irq_state_data["count"]
enable_irq(irq_state)
```

Това е преходът към реално събитийно програмиране.

Файлове:

- `examples/23_timing/02_interrupts.py`
- `examples/28_machine_misc/02_machine_mem_and_bootloader.py`

### Глава 16. Дебаунс на бутон

Пример с polling:

```python
if current_value != last_stable_value:
    now_ms = time.ticks_ms()
    if time.ticks_diff(now_ms, last_change_ms) >= 40:
        last_change_ms = now_ms
        last_stable_value = current_value
```

Пример с `uasyncio`:

```python
if stable_value == 0 and stable_count == 4:
    async_presses += 1
```

Тук има два подхода:

- времеви прозорец в polling цикъл
- последователни стабилни проби в async задача

Файл:

- `examples/23_timing/03_button_debounce.py`

### Глава 17. RTC

Пример:

```python
from machine import RTC

rtc = RTC()
print(rtc.info())
print(rtc.calibration())
print(rtc.datetime())
```

Тази глава покрива:

- диагностика на RTC
- current datetime tuple
- безопасно задаване на време зад флаг

Файл:

- `examples/23_timing/06_rtc_basic.py`

## Част V: Серийни интерфейси

### Глава 18. I2C master и runtime избор на пинове

Базов пример:

```python
from machine import I2C

i2c = I2C(1, freq=100000)
print(i2c.scan())
```

Runtime пинове:

```python
from machine import I2C, Pin

i2c = I2C(1, freq=400000, scl=Pin("P100"), sda=Pin("P101"))
```

Тази глава е важна и заради конфликтите:

- `I2C(0)` ползва `P400/P401`
- `P400` е и `SW1`

Файлове:

- `examples/24_i2c/01_i2c_master_basic.py`
- `examples/24_i2c/02_i2c_runtime_pins.py`

### Глава 19. SoftI2C и I2CTarget

SoftI2C:

```python
from machine import SoftI2C, Pin

soft_i2c = SoftI2C(scl=Pin("P105", Pin.OPEN_DRAIN), sda=Pin("P104", Pin.OPEN_DRAIN), freq=100000)
```

I2CTarget:

```python
from machine import I2CTarget, Pin

memory_buffer = bytearray(range(16))
target = I2CTarget(1, addr=0x42, mem=memory_buffer, mem_addrsize=8, scl=Pin("P100"), sda=Pin("P101"))
```

Тук се вижда:

- софтуерна I2C шина върху GPIO
- целево устройство с memory-backed буфер

Файлове:

- `examples/24_i2c/03_softi2c_basic.py`
- `examples/24_i2c/04_i2ctarget_memory.py`

### Глава 20. Hardware SPI и SoftSPI

Hardware SPI:

```python
from machine import SPI, Pin

spi = SPI(0, baudrate=500000, polarity=0, phase=0, bits=8, sck=Pin("P102"), mosi=Pin("P101"), miso=Pin("P100"), cs=Pin("P103"))
```

SoftSPI:

```python
from machine import SoftSPI, Pin

soft_spi = SoftSPI(baudrate=100000, polarity=0, phase=0, sck=Pin("P105"), mosi=Pin("P104"), miso=Pin("P106"))
```

В тази книга `SPI` остава `1` канал.

Файлове:

- `examples/25_spi/01_spi_basic.py`
- `examples/25_spi/02_softspi_basic.py`

## Част VI: TouchPad, памет и системни функции

### Глава 21. TouchPad: базово четене

Пример:

```python
from machine import Pin, TouchPad

touch = TouchPad(Pin("P205"))
touch.config(500)
print(touch.read())
print(touch.value())
print(touch.read_value())
```

Тук се виждат:

- сурова стойност
- логическа стойност по праг
- комбинирано четене

Файл:

- `examples/26_touchpad/01_touchpad_basic.py`

### Глава 22. TouchPad: диагностика, cached API и асинхронно следене

Диагностичен пример:

```python
TouchPad.sample_rate(20)
touch.start()
TouchPad.service()
print(touch.ready(), touch.read_cached(), touch.value_cached())
print(touch.diagnose(8))
```

Асинхронен пример:

```python
TouchPad.sample_rate(50)

async def touch_task():
    while True:
        if tp.ready():
            print(tp.read_cached(), tp.value_cached(), tp.age_ms())
        await asyncio.sleep_ms(20)
```

Тази глава е важна, защото показва не само „натиснато/ненатиснато“, а и сервизно поведение на CTSU слоя.

Файлове:

- `examples/26_touchpad/02_touchpad_diagnostics.py`
- `examples/04_touchpad_async.py`

### Глава 23. `renesas.Flash`, `/flash` и `dataflash`

Пример с `renesas.Flash`:

```python
import os
import renesas

flash = renesas.Flash()
sector = bytearray(16)
flash.readblocks(0, sector)
print(os.listdir("/flash"))
```

Пример с `dataflash`:

```python
import dataflash

print(dataflash.size())
print(dataflash.block_size())
print(dataflash.write_size())
print(dataflash.read(0, 16))
```

Тук се разграничават:

- файлова система
- block device
- отделен data flash регион

Файлове:

- `examples/27_storage/01_flash_blockdev.py`
- `examples/27_storage/02_dataflash_basic.py`

### Глава 24. `machine` модулът и порт-специфично поведение

Информационен пример:

```python
import machine

uid = machine.unique_id()
print(machine.freq())
print(machine.reset_cause())
machine.info()
```

Ниско ниво:

```python
irq_state = machine.disable_irq()
machine.enable_irq(irq_state)
print(hasattr(machine, "mem32"))
```

Sleep режими:

```python
machine.sleep(20)

if DO_LIGHTSLEEP:
    machine.lightsleep(100)
```

Тази глава е мястото за:

- `machine.info`
- `machine.freq`
- `machine.unique_id`
- `machine.reset_cause`
- `disable_irq` и `enable_irq`
- `mem8`, `mem16`, `mem32`
- предпазен `machine.bootloader`
- `sleep`, `lightsleep`, `deepsleep`

Файлове:

- `examples/28_machine_misc/01_machine_info_and_id.py`
- `examples/28_machine_misc/02_machine_mem_and_bootloader.py`
- `examples/28_machine_misc/03_sleep_modes.py`

## Част VII: Индикация и входни матрици

### Глава 25. Динамична 7-сегментна индикация и матрично сканиране

Пример:

```python
segment_pins = [Pin(name, Pin.OUT) for name in ("P105", "P104", "P113", "P114", "P115", "P304", "P303")]
digit_pins = [Pin(name, Pin.OUT) for name in ("P608", "P409")]

for digit_index, symbol in enumerate(("1", "2")):
    for digit_pin in digit_pins:
        digit_pin.off()
    for segment_pin, level in zip(segment_pins, patterns[symbol]):
        segment_pin.value(level)
    digit_pins[digit_index].on()
    time.sleep_ms(5)
```

Клавиатурна матрица:

```python
for row_index, row_pin in enumerate(row_pins):
    for reset_row in row_pins:
        reset_row.off()
    row_pin.on()
    for col_index, col_pin in enumerate(col_pins):
        if col_pin.value() == 0:
            pressed_keys.append((row_index, col_index))
```

Този пример изисква външен хардуер, но е много важен за свързване на GPIO, време и логика.

Файл:

- `examples/29_display_scan/01_dynamic_7seg_and_keypad.py`

## Част VIII: FSM като начин на мислене

### Глава 26. Минимална FSM и таблична FSM

Минимална FSM:

```python
STATE_OFF = "OFF"
STATE_ON = "ON"
EVENT_BUTTON = "BUTTON"

def handle_event(current_state, event_name):
    if current_state == STATE_OFF and event_name == EVENT_BUTTON:
        return STATE_ON
    if current_state == STATE_ON and event_name == EVENT_BUTTON:
        return STATE_OFF
    return current_state
```

Таблична FSM:

```python
transition_table = {
    ("OFF", "BUTTON"): "ON",
    ("ON", "BUTTON"): "OFF",
}
```

Графично идеята може да се мисли така:

```text
OFF --BUTTON--> ON
ON  --BUTTON--> OFF
```

Файлове:

- `examples/30_fsm/01_fsm_lamp_button.py`
- `examples/30_fsm/02_fsm_table.py`

### Глава 27. FSM с timeout

Пример:

```python
transition_table = {
    ("OFF", "BUTTON"): "ON",
    ("ON", "TIMEOUT"): "OFF",
    ("ON", "BUTTON"): "OFF",
}
```

Тази глава е преходът от „лампа + бутон“ към „лампа + бутон + време“.

Файл:

- `examples/30_fsm/03_fsm_timeout.py`

### Глава 28. Conditions vs Events

Пример:

```python
light_level = 600
dark_threshold = 800
event_name = "BUTTON"
condition_is_dark = light_level < dark_threshold

if condition_is_dark and event_name == "BUTTON":
    print("Лампата може да се включи")
```

Тук разграничението е:

- condition може да е вярно дълго време
- event е дискретен факт

Файл:

- `examples/30_fsm/04_conditions_vs_events.py`

### Глава 29. Guard функции

Пример:

```python
def is_dark(data):
    return data["light"] < 800

def has_motion(data):
    return data["pir"] is True

if state == "OFF" and event_name == "MOTION" and is_dark(context) and has_motion(context):
    state = "ON"
```

Тук преходът не зависи само от state и event, а и от допълнителни условия.

Файл:

- `examples/30_fsm/05_guard_functions.py`

### Глава 30. Приоритети и event queue

Приоритети:

```python
pending_events = ["BUTTON", "ERROR", "TIMEOUT"]
priority_order = {"ERROR": 0, "BUTTON": 1, "TIMEOUT": 2}
pending_events.sort(key=lambda event_name: priority_order[event_name])
```

Event queue:

```python
event_queue = []
event_queue.append(("BUTTON", {"source": "SW1"}))
event_queue.append(("PIR", {"source": "pir_sensor"}))

while event_queue:
    event_name, payload = event_queue.pop(0)
    print(event_name, payload)
```

Това е мостът между:

- прекъсвания
- таймери
- сензори
- FSM логика

Файлове:

- `examples/30_fsm/06_priorities.py`
- `examples/30_fsm/07_event_queue.py`

### Глава 31. Tickless timers и sleep идея

Пример:

```python
tasks = [("lamp_timeout", 120), ("fan_timeout", 450), ("display_refresh", 40)]
next_deadline_ms = min(deadline for _, deadline in tasks)
machine.sleep(sleep_ms)
ready_tasks = [task_name for task_name, deadline in tasks if deadline <= elapsed_ms]
```

Тази глава не разчита на агресивни sleep режими, а показва начина на мислене:

- намираме най-близкия срок
- спим точно дотогава
- обработваме всички изтекли задачи

Файлове:

- `examples/30_fsm/08_tickless_sleep.py`
- `examples/28_machine_misc/03_sleep_modes.py`

## Част IX: Реален пример

### Глава 32. Управление на осветление и вентилация за баня

Пример:

```python
sensors = {
    "pir": True,
    "door_open": False,
    "light_level": 300,
    "humidity": 82,
}

outputs = {"lamp": False, "fan": False}

if sensors["pir"] and sensors["light_level"] < 800:
    outputs["lamp"] = True

if sensors["humidity"] > 75 or sensors["door_open"]:
    outputs["fan"] = True
```

Това е умишлено учебна симулация, не готов продукт.

Но тя събира:

- движение
- врата
- осветеност
- влажност
- два управлявани изхода

Файл:

- `examples/31_case_study/01_bathroom_control.py`

### Глава 33. Декомпозиция на две FSM

Пример:

```python
lamp_state = "OFF"
fan_state = "OFF"

for event_name in event_sequence:
    if event_name == "BUTTON":
        lamp_state = "ON" if lamp_state == "OFF" else "OFF"
    if event_name == "HUMIDITY_HIGH":
        fan_state = "ON"
```

Тук голямата идея е:

- една FSM да не поглъща цялата система
- отделните машини да имат отделни отговорности
- комуникацията да става през събития

Файл:

- `examples/31_case_study/02_two_fsms.py`

Забележка: И двата case study примера вече използват реален LED1 (P204) и SW1 (P400) за вход и изход, освен симулираните сензори.

## Част X: UART комуникация

### Глава 34. Базова UART комуникация

Пример:

```python
from machine import UART
import time

uart = UART(9, 115200)
uart.write("Здравей от VK_RA4M2!\r\n")

time.sleep_ms(100)
if uart.any() > 0:
    print(uart.read())
```

Налични UART канали:

- `UART(0)` -> `TX=P411`, `RX=P410`
- `UART(2)` -> `TX=P302`, `RX=P301`
- `UART(7)` -> `TX=P401`, `RX=P402`, `CTS=P403`
- `UART(9)` -> `TX=P602`, `RX=P601`, `CTS=P603`

Тук се учат:

- създаване на UART инстанция
- изпращане и четене на данни
- loopback тест (TX свързан към RX)

Файл:

- `examples/32_uart/01_uart_basic.py`

## Част XI: asyncio като учебна секция

### Глава 35. Основи на asyncio

Пример:

```python
import asyncio
from machine import Pin

led = Pin("LED1", Pin.OUT, value=1)

async def blink_task(period_ms):
    while True:
        led.value(0)
        await asyncio.sleep_ms(period_ms)
        led.value(1)
        await asyncio.sleep_ms(period_ms)
```

Тази глава покрива:

- `create_task` за стартиране на задачи
- `asyncio.gather` за паралелно изпълнение
- три задачи едновременно: мигане, бутон, отчет

Файл:

- `examples/33_asyncio/01_asyncio_basics.py`

### Глава 36. asyncio Event синхронизация

Пример:

```python
button_event = asyncio.Event()

async def button_producer():
    if button.value() == 0:
        button_event.set()

async def led_consumer():
    await button_event.wait()
    button_event.clear()
```

Тук се вижда:

- `asyncio.Event` за комуникация между задачи
- производител-консуматор модел
- кооперативно блокиране без busy-wait

Файл:

- `examples/33_asyncio/02_asyncio_event_flag.py`

## Каталог на всички примери

Този каталог е финалната проверка, че нито един пример не е пропуснат в книгата.

- `examples/01_blink_async.py` : асинхронно мигане на `LED1`
- `examples/02_two_tasks.py` : две едновременни `asyncio` задачи
- `examples/03_pwm_ab_same_channel.py` : два PWM изхода върху един GPT канал
- `examples/04_touchpad_async.py` : асинхронно следене на TouchPad с cached API
- `examples/20_language_basics/01_sample_program.py` : първа примерна програма
- `examples/20_language_basics/02_variables_and_constants.py` : променливи и константи
- `examples/20_language_basics/03_operators_conditions_loops.py` : оператори, условия и цикли
- `examples/20_language_basics/04_enums.py` : enum-подобни стойности с `const()`
- `examples/20_language_basics/05_arrays.py` : списъци, вложени таблици, `array`, `bytearray`
- `examples/20_language_basics/06_structures.py` : `dict`, `namedtuple`, вложени структури
- `examples/20_language_basics/07_pointers_equivalents.py` : references, `memoryview`, `machine.mem32`
- `examples/20_language_basics/08_functions.py` : функции, callbacks и таблици от функции
- `examples/21_digital_io/01_digital_output.py` : цифров изход с `LED1`
- `examples/21_digital_io/02_digital_input.py` : цифров вход с `SW1`
- `examples/22_analog/01_analog_input_adc.py` : ADC вътрешни и външни канали
- `examples/22_analog/02_pwm_output.py` : базов PWM изход
- `examples/23_timing/01_delays_and_timers.py` : блокиращо закъснение, хардуерен таймер, `uasyncio`
- `examples/23_timing/02_interrupts.py` : GPIO прекъсвания и защитено четене
- `examples/23_timing/03_button_debounce.py` : дебаунс с polling и с `uasyncio`
- `examples/23_timing/04_software_timer_minus1.py` : софтуерен `Timer(-1)`
- `examples/23_timing/05_timer_compare_hw_sw.py` : сравнение между `Timer(1)` и `Timer(-1)`
- `examples/23_timing/06_rtc_basic.py` : RTC основи
- `examples/24_i2c/01_i2c_master_basic.py` : базов I2C master
- `examples/24_i2c/02_i2c_runtime_pins.py` : I2C с runtime зададени пинове
- `examples/24_i2c/03_softi2c_basic.py` : SoftI2C
- `examples/24_i2c/04_i2ctarget_memory.py` : I2CTarget с memory-backed буфер
- `examples/25_spi/01_spi_basic.py` : hardware SPI
- `examples/25_spi/02_softspi_basic.py` : SoftSPI
- `examples/26_touchpad/01_touchpad_basic.py` : базов TouchPad
- `examples/26_touchpad/02_touchpad_diagnostics.py` : диагностика, tuning и cached API
- `examples/27_storage/01_flash_blockdev.py` : `renesas.Flash` и `/flash`
- `examples/27_storage/02_dataflash_basic.py` : `dataflash` модул
- `examples/28_machine_misc/01_machine_info_and_id.py` : `machine.info`, `freq`, `unique_id`
- `examples/28_machine_misc/02_machine_mem_and_bootloader.py` : `disable_irq`, `enable_irq`, `mem32`, `bootloader`
- `examples/28_machine_misc/03_sleep_modes.py` : `sleep`, `lightsleep`, `deepsleep`
- `examples/29_display_scan/01_dynamic_7seg_and_keypad.py` : динамична индикация и матрично сканиране
- `examples/30_fsm/01_fsm_lamp_button.py` : минимална FSM
- `examples/30_fsm/02_fsm_table.py` : таблична FSM
- `examples/30_fsm/03_fsm_timeout.py` : FSM с timeout
- `examples/30_fsm/04_conditions_vs_events.py` : conditions срещу events
- `examples/30_fsm/05_guard_functions.py` : guard функции
- `examples/30_fsm/06_priorities.py` : приоритети между събития
- `examples/30_fsm/07_event_queue.py` : event queue
- `examples/30_fsm/08_tickless_sleep.py` : tickless идея и sleep
- `examples/31_case_study/01_bathroom_control.py` : баня с няколко сензора и реален LED1/SW1
- `examples/31_case_study/02_two_fsms.py` : лампа и вентилатор като две FSM с реален LED1/SW1
- `examples/32_uart/01_uart_basic.py` : базова UART комуникация и loopback тест
- `examples/33_asyncio/01_asyncio_basics.py` : asyncio основи с три паралелни задачи
- `examples/33_asyncio/02_asyncio_event_flag.py` : asyncio Event синхронизация
- `examples/index.py` : скрипт за листване на всички примери на борда

## Какво още не е потвърдено на реален борд

Книгата и примерите са прегледани за пълност и са подредени спрямо плана.

Още не е потвърдено пълно runtime изпълнение на всички примери върху реален `VK_RA4M2`.

Най-чувствителните места остават:

- външните I2C устройства
- SPI loopback и външни SPI модули
- 7-сегментната индикация и матричната клавиатура
- write режимите на `Flash` и `dataflash`
- `lightsleep` и `deepsleep`

## Заключение

Тази версия на книгата е построена като проверка за пълност:

- всички теми от плана са отразени
- всички `50` примерни файла са споменати изрично
- всяка голяма тема има поне един реален кодов откъс
- порт-специфичните ограничения на `VK_RA4M2` са записани ясно

Следващата разумна стъпка вече не е „още съдържание“, а хардуерна валидация на примерите по групи върху реалната платка.
