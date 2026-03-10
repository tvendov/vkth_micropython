# Учебна програма за демонстрационните MicroPython примери на VK_RA4M2

## Цел

Този документ описва началния пакет от демонстрационни файлове за платката `VK_RA4M2`, написани на MicroPython и насочени към начинаещи.

Всеки примерен `.py` файл трябва да спазва следните правила:

- Всеки ред код има български коментар за начинаещи.
- В началото на всеки файл има ресурсен header с броя и изброяване на наличните интерфейси на `VK_RA4M2`.
- Всеки пример използва реалните ресурси на порта `renesas-ra` за `VK_RA4M2`, а не абстрактни примери.
- Където има смисъл, се дават два подхода: блокиращ и асинхронен (`asyncio`).

## Базова информация за платката

- MCU: `RA4M2`
- Тактова честота на ядрото: `100 MHz`
- RAM: `128 KB`
- Code Flash: `384 KB`
- Data Flash: `8 KB`
- Вътрешна файлова система `/flash`: около `94 KB` според linker скрипта

## Ресурсна карта на VK_RA4M2

### GPIO и board aliases

- Потребителски LED: `1 брой` -> `LED1 = P204`
- Потребителски бутон: `1 брой` -> `SW1 = P400`
- USB device пинове: `3 броя` -> `USBDP = P914`, `USBDM = P915`, `USB_VBUS = P407`

### ADC

- Външни ADC входове: `13 броя`
- ADC пинове: `P000`, `P001`, `P002`, `P003`, `P004`, `P005`, `P006`, `P007`, `P008`, `P013`, `P014`, `P015`, `P500`
- Вътрешни ADC източници: `3 броя`
- Вътрешни ADC източници: `ADC.CORE_TEMP`, `ADC.CORE_VREF`, `ADC.VREF`

### DAC

- DAC изходи: `2 броя`
- DAC пинове: `P014`, `P015`

### PWM

- PWM изходи: `14 броя`
- PWM пинове: `P107`, `P106`, `P105`, `P104`, `P113`, `P114`, `P112`, `P115`, `P608`, `P409`, `P408`, `P600`, `P304`, `P303`
- Забележка: изходите `A` и `B` на един и същ GPT канал споделят честота

### UART

- Налични hardware UART инстанции: `4 броя`
- `UART(0)` -> `TX=P411`, `RX=P410`
- `UART(2)` -> `TX=P302`, `RX=P301`
- `UART(7)` -> `TX=P401`, `RX=P402`, `CTS=P403`
- `UART(9)` -> `TX=P602`, `RX=P601`, `CTS=P603`

### I2C master и I2CTarget

- Hardware I2C master инстанции: `2 броя`
- `I2C(0)` -> `SCL=P400`, `SDA=P401`
- `I2C(1)` -> `SCL=P100`, `SDA=P101`
- `I2CTarget` инстанции: `2 броя`
- `I2CTarget(0)` -> `SCL=P400`, `SDA=P401`
- `I2CTarget(1)` -> `SCL=P100`, `SDA=P101`
- Забележка: `I2C(1)` може да се пренасочи по runtime, но алтернативни touch-конфликтни пинове не се ползват в учебните примери

### SPI

- Hardware SPI канали: `1 брой`
- `SPI` -> `CS=P103`, `SCK=P102`, `MISO=P100`, `MOSI=P101`
- `SoftSPI`: наличен софтуерен вариант

### TouchPad

- Touch входове: `12 броя`
- Touch пинове: `P205`, `P206`, `P407`, `P408`, `P409`, `P410`, `P411`, `P412`, `P413`, `P414`, `P415`, `P708`
- Специален CTSU капацитивен пин: `P207 = TSCAP`
- Забележка: `P207` не се използва като TouchPad вход

### Timer, RTC и sleep

- Хардуерни `machine.Timer`: `2 броя` -> `Timer(1)` и `Timer(2)`
- Софтуерен `Timer(-1)`: наличен като софтуерен таймер
- За софтуерен timing в примерите ще се показват и `Timer(-1)`, и `uasyncio`, и `time.ticks_ms()`
- `machine.RTC`: `1 брой`
- `machine.idle`, `machine.sleep`, `machine.lightsleep`, `machine.deepsleep`: налични в API
- Забележка: за `lightsleep` и `deepsleep` има известни ограничения в текущия порт и това ще бъде отбелязвано в примерите

### Flash и Data Flash

- Вътрешна файлова система `/flash`: `1 брой`
- `renesas.Flash`: `1 block device` върху вътрешната flash памет
- `dataflash`: `1 data flash region` с размер `8 KB`
- `dataflash` API: `size()`, `block_size()`, `write_size()`, `read()`, `write()`, `erase()`, `erase_block()`

### Софтуерни интерфейси и бележки

- `asyncio`: включен във frozen modules на тази платка
- `SoftI2C`: наличен
- `SoftSPI`: наличен
- `Pin.PULL_DOWN`: не се поддържа в този порт и примерите ще използват `PULL_UP` или външни резистори

## Наличен пакет

### Преработени съществуващи примери

- `examples/01_blink_async.py`
- `examples/02_two_tasks.py`
- `examples/03_pwm_ab_same_channel.py`
- `examples/04_touchpad_async.py`

### Нови начални учебни примери

- `examples/20_language_basics/01_sample_program.py`
- `examples/20_language_basics/02_variables_and_constants.py`
- `examples/20_language_basics/03_operators_conditions_loops.py`
- `examples/20_language_basics/04_enums.py`
- `examples/20_language_basics/05_arrays.py`
- `examples/20_language_basics/06_structures.py`
- `examples/20_language_basics/07_pointers_equivalents.py`
- `examples/20_language_basics/08_functions.py`
- `examples/21_digital_io/01_digital_output.py`
- `examples/21_digital_io/02_digital_input.py`
- `examples/22_analog/01_analog_input_adc.py`
- `examples/22_analog/02_pwm_output.py`
- `examples/23_timing/01_delays_and_timers.py`
- `examples/23_timing/02_interrupts.py`
- `examples/23_timing/03_button_debounce.py`
- `examples/23_timing/04_software_timer_minus1.py`
- `examples/23_timing/05_timer_compare_hw_sw.py`
- `examples/23_timing/06_rtc_basic.py`
- `examples/24_i2c/01_i2c_master_basic.py`
- `examples/24_i2c/02_i2c_runtime_pins.py`
- `examples/24_i2c/03_softi2c_basic.py`
- `examples/24_i2c/04_i2ctarget_memory.py`
- `examples/25_spi/01_spi_basic.py`
- `examples/25_spi/02_softspi_basic.py`
- `examples/26_touchpad/01_touchpad_basic.py`
- `examples/26_touchpad/02_touchpad_diagnostics.py`
- `examples/27_storage/01_flash_blockdev.py`
- `examples/27_storage/02_dataflash_basic.py`
- `examples/28_machine_misc/01_machine_info_and_id.py`
- `examples/28_machine_misc/02_machine_mem_and_bootloader.py`
- `examples/28_machine_misc/03_sleep_modes.py`
- `examples/29_display_scan/01_dynamic_7seg_and_keypad.py`
- `examples/30_fsm/01_fsm_lamp_button.py`
- `examples/30_fsm/02_fsm_table.py`
- `examples/30_fsm/03_fsm_timeout.py`
- `examples/30_fsm/04_conditions_vs_events.py`
- `examples/30_fsm/05_guard_functions.py`
- `examples/30_fsm/06_priorities.py`
- `examples/30_fsm/07_event_queue.py`
- `examples/30_fsm/08_tickless_sleep.py`
- `examples/31_case_study/01_bathroom_control.py`
- `examples/31_case_study/02_two_fsms.py`
- `examples/32_uart/01_uart_basic.py`
- `examples/33_asyncio/01_asyncio_basics.py`
- `examples/33_asyncio/02_asyncio_event_flag.py`
- `examples/index.py`

### Инструмент за PDF генерация

- `examples/render_course_plan_pdf.py`
- Използва `reportlab` и Windows TrueType шрифтове за коректна кирилица в PDF

## Учебни глави, покрити в пакета

1. Примерна програма
2. Променливи и константи
3. Оператори и управление на изпълнението
4. Цикли
5. Цифров изход
6. Цифров вход
7. Аналогов вход
8. PWM
9. Енъми в MicroPython
10. Масиви и таблици
11. Структури в MicroPython
12. Еквиваленти на указатели в MicroPython
13. Функции, callbacks и таблици от функции
14. Таймери и закъснения
15. Прекъсвания
16. Бутон и дебаунс
17. Динамична индикация и сканиране
18. FSM идея
19. Таблична FSM
20. FSM с timeout
21. Conditions vs Events
22. Таблична FSM с guard функции
23. Реален пример за баня
24. Декомпозиция на две FSM
25. Приоритети между FSM
26. Event queue
27. Tickless timers и sleep режими
28. UART комуникация
29. asyncio основи
30. asyncio Event синхронизация

## Бележки за външен хардуер

- Главите за `I2C`, `SPI`, `I2CTarget`, `7-сегментен дисплей`, `матрично сканиране`, `PIR`, `сензор за влажност`, `сензор за осветеност` и `баня` изискват външни модули или поне loopback връзки.
- Главите за `renesas.Flash` и `dataflash` по подразбиране са направени безопасни и не записват памет, освен ако примерът изрично не бъде превключен в write режим.
- Главите за `machine.bootloader`, `lightsleep` и `deepsleep` са направени с предпазни флагове, за да не прекъсват сесията без намерение.

## Важни корекции спрямо старите примери

- `LED1` на тази платка е `P204`, а не `P011`
- `SW1` е върху `P400` и това влиза в конфликт с `I2C(0).SCL`
- `TouchPad` примерите трябва да използват само валидните CTSU пинове, описани по-горе
- Всеки нов пример ще описва не само използвания интерфейс, а и всички налични ресурси от същия клас на `VK_RA4M2`
