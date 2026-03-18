.. _renesas-ra_vk-ra4m2_practical_guide_bg:

VK_RA4M2 Practical Guide (BG)
=============================

Това е по-официалната docs версия на практическия курс за ``VK_RA4M2``.
Тя е предназначена да ви даде ясен, подреден маршрут през реалните
примерни файлове в ``ports/renesas-ra/boards/VK_RA4M2/examples/``, без да
повтаря дословно вътрешния учебен ръкопис.

Ако искате още по-бърз избор на маршрут според целта си, вижте и
:ref:`renesas-ra_vk-ra4m2_learning_paths_bg`.

Ако искате по-разгърнат учебен текст с повече упражнения и каталожен стил,
вижте :ref:`renesas-ra_vk-ra4m2_book_bg`.

Scope
-----

Тази страница е за:

- първи стъпки с ``VK_RA4M2`` под MicroPython
- ориентиране в примерите по теми
- хардуерно важните особености на борда
- бърз преход от езикови примери към периферии, FSM и audio

Тази страница не е пълният справочник на порта. За API справки използвайте:

- :ref:`machine.DAC <machine.DAC>`
- :ref:`renesas-ra_audio_synth`
- :ref:`renesas-ra_tutorial`

How To Use This Guide
---------------------

Най-добрият ритъм е:

1. Прочетете краткото обяснение в съответната секция.
2. Пуснете минималния пример от директорията ``examples/``.
3. Променете само една стойност.
4. Наблюдавайте какво се променя на реалната платка.
5. Едва след това преминете към следващата тема.

Този подход е важен, защото курсът е изграден върху реални файлове, а не върху
измислени примери.

Board-Specific Notes
--------------------

Преди да започнете, запомнете няколко важни детайла:

- ``LED1`` е active-low: ``0`` го светва, ``1`` го гаси
- ``SW1`` е върху ``P400`` и влиза в конфликт с ``I2C(0).SCL``
- ``P014`` и ``P015`` са DAC изходи
- ``J16`` е boot-mode jumper, не е DAC изход
- ``Timer(-1)`` е софтуерен таймер и не е равен на хардуерен ``Timer(n)``
- ``lightsleep`` и ``deepsleep`` са полезни, но трябва да се упражняват предпазливо

Quick Resource Map
------------------

Най-често ползваните ресурси в примерите са:

- ``LED1 = P204``
- ``SW1 = P400``
- ADC външни пинове: ``P000`` до ``P008``, ``P013``, ``P014``, ``P015``, ``P500``
- DAC пинове: ``P014`` и ``P015``
- I2C: ``I2C(0)=P400/P401``, ``I2C(1)=P100/P101``
- SPI: ``SCK=P102``, ``MISO=P100``, ``MOSI=P101``, ``CS=P103``
- хардуерни таймери: ``Timer(1)`` до ``Timer(6)``

Learning Path
-------------

Препоръчителната последователност е:

1. Езиковите основи в ``examples/20_language_basics/``
2. GPIO и аналогови примери в ``examples/21_digital_io/`` и ``examples/22_analog/``
3. Таймери, прекъсвания и време в ``examples/23_timing/``
4. Серийни интерфейси в ``examples/24_i2c/``, ``examples/25_spi/`` и ``examples/32_uart/``
5. TouchPad, памет и ``machine`` в ``examples/26_touchpad/``, ``examples/27_storage/`` и ``examples/28_machine_misc/``
6. FSM и реален case study в ``examples/30_fsm/`` и ``examples/31_case_study/``
7. ``asyncio`` в ``examples/33_asyncio/``
8. Светлинни модули и audio/synth в top-level примерите и ``examples/ws2812_*``

Start Here
----------

Ако започвате от нулата, това е добър минимален ред:

- ``examples/20_language_basics/01_sample_program.py``
- ``examples/01_blink_async.py``
- ``examples/21_digital_io/01_digital_output.py``
- ``examples/21_digital_io/02_digital_input.py``
- ``examples/22_analog/01_analog_input_adc.py``
- ``examples/23_timing/01_delays_and_timers.py``

Тези примери дават най-бързата връзка между Python синтаксис, GPIO, време и
реален ефект върху борда.

Analog, Timing, And Events
--------------------------

След базовите примери най-полезният преход е към:

- ``examples/22_analog/02_pwm_output.py``
- ``examples/23_timing/05_timer_compare_hw_sw.py``
- ``examples/23_timing/13_hardware_timer_periodic_oneshot.py``
- ``examples/23_timing/14_hardware_timer_output_compare.py``
- ``examples/23_timing/15_hardware_timer_input_capture.py``

Точно тук започва разликата между „работещ пример“ и „контрол върху времето“.
Ако проектът ви ще има управление, интерфейс или sound effects, това е една от
най-важните части на целия набор.

Buttons And Robust Input
------------------------

За надеждно четене на бутон не спирайте на едно ``button.value()``.
Преминете през:

- ``examples/23_timing/03_button_debounce.py``
- ``examples/23_timing/09_button_polling_events.py``
- ``examples/23_timing/10_button_irq_deferred.py``
- ``examples/23_timing/11_button_soft_timer_hold.py``
- ``examples/23_timing/12_button_shift_debounce.py``

Тези примери показват как от шумен active-low вход се стига до стабилни
събития като ``PRESS``, ``RELEASE``, ``CLICK`` и ``LONG_PRESS``.

Interfaces And External Modules
-------------------------------

За работа с външни устройства използвайте последователно:

- ``examples/24_i2c/01_i2c_master_basic.py``
- ``examples/24_i2c/04_i2ctarget_memory.py``
- ``examples/25_spi/01_spi_basic.py``
- ``examples/32_uart/01_uart_basic.py``
- ``examples/34_external_modules/01_i2c_module_probe.py``
- ``examples/34_external_modules/02_spi_module_transaction.py``
- ``examples/34_external_modules/03_uart_module_request_response.py``

Тук окабеляването е толкова важно, колкото и Python кодът. Ако нещо не работи,
първо проверявайте pin mapping, обща маса, режим и скорост.

FSM And System Thinking
-----------------------

Курсът става по-силен, когато минете от единични примери към организирана
логика. Затова ``examples/30_fsm/`` и ``examples/31_case_study/`` са ключови:

- ``01_fsm_lamp_button.py``
- ``02_fsm_table.py``
- ``03_fsm_timeout.py``
- ``07_event_queue.py``
- ``01_bathroom_control.py``
- ``02_two_fsms.py``

Този блок е естествена основа за по-сериозни бордови приложения.

Audio And Retro Synth
---------------------

``VK_RA4M2`` вече има потвърден timed DAC path върху реален хардуер.
За подробностите вижте :ref:`renesas-ra_audio_synth`.

Най-полезните файлове са:

- ``examples/retro_synth.py``
- ``examples/dac_retro_sfx.py``
- ``examples/dac_retro_music.py``
- ``examples/dac_firmware_probe.py``

Хардуерно потвърдено е:

- ``DTC`` circular playback
- ``DTC`` one-shot playback
- ``DMAC`` one-shot playback

Този блок е особено подходящ за ретро звукови ефекти, кратки мелодии и
малки wavetable цикли.

Suggested Milestones
--------------------

Ако искате реален практически прогрес, ползвайте тези междинни цели:

1. Да пуснете LED, бутон и ADC без помощ.
2. Да различавате ``sleep_ms()``, хардуерен таймер и ``Timer(-1)``.
3. Да направите надеждно бутонно събитие с debounce.
4. Да подкарате поне един външен интерфейс със стабилен тест.
5. Да организирате логика чрез FSM.
6. Да пуснете DAC audio пример или firmware probe.

Hardware-Verified Areas
-----------------------

Към момента са потвърдени на реална платка:

- timed DAC playback чрез ``DTC`` circular
- timed DAC playback чрез ``DTC`` one-shot
- timed DAC playback чрез ``DMAC`` one-shot
- ``dac_retro_sfx.py``
- ``dac_retro_music.py``

Още остават чувствителни за допълнителна хардуерна валидация:

- външни I2C модули
- SPI loopback и външни SPI устройства
- външни UART модули
- по-дълги NeoPixel и WS2812 setup-и
- ``lightsleep`` и ``deepsleep`` в по-реални сценарии

Where To Go Next
----------------

След тази страница най-полезните следващи документи са:

- :ref:`renesas-ra_audio_synth`
- :ref:`machine.DAC <machine.DAC>`
- :ref:`renesas-ra_tutorial`

Ако целта ви е обучение с повече упражнения и подробно тематично разгръщане,
ползвайте :ref:`renesas-ra_vk-ra4m2_book_bg` като пълната docs книга,
а тази страница като кратък маршрут.
