.. _renesas-ra_vk-ra4m2_learning_paths_bg:

VK_RA4M2 Learning Paths (BG)
============================

Тази страница е кратък вход към материалите за ``VK_RA4M2``.
Целта ѝ не е да замени книгата, а да помогне да изберете правилния маршрут
според вашата цел.

Полезните опорни страници са:

- :ref:`renesas-ra_vk-ra4m2_validation_matrix_bg` за консервативния хардуерен статус
- :ref:`renesas-ra_vk-ra4m2_practical_guide_bg` за по-кратък практически guide
- :ref:`renesas-ra_vk-ra4m2_book_bg` за пълната книга
- :ref:`renesas-ra_audio_synth` за DAC audio и retro synth

Choose Your Path
----------------

Най-полезните пътеки са три:

1. ``Начинаещ``: ако сега започвате с борда и с MicroPython
2. ``Периферии и логика``: ако искате GPIO, таймери, интерфейси и FSM
3. ``Audio и retro synth``: ако целта ви е звук, SFX и малки мелодии

Path 1: Beginner Route
----------------------

Този маршрут е за първи успешни стъпки и за изграждане на увереност.

Добър ред за минаване е:

1. ``examples/20_language_basics/01_sample_program.py``
2. ``examples/01_blink_async.py``
3. ``examples/21_digital_io/01_digital_output.py``
4. ``examples/21_digital_io/02_digital_input.py``
5. ``examples/22_analog/01_analog_input_adc.py``
6. ``examples/23_timing/01_delays_and_timers.py``
7. ``examples/23_timing/03_button_debounce.py``

Какво ще получите от този маршрут:

- ще видите веднага реален ефект на платката
- ще разберете active-low поведението на ``LED1``
- ще започнете да мислите в GPIO, време и събития
- ще имате база за почти всички следващи глави

След този блок преминете към:

- :ref:`renesas-ra_vk-ra4m2_practical_guide_bg`
- Части I-IV от :ref:`renesas-ra_vk-ra4m2_book_bg`

Path 2: Peripherals And System Logic
------------------------------------

Това е правилната пътека, ако целта ви е реално вградено приложение, а не само
отделни примерчета.

Препоръчителен ред:

1. ``examples/22_analog/02_pwm_output.py``
2. ``examples/23_timing/05_timer_compare_hw_sw.py``
3. ``examples/23_timing/13_hardware_timer_periodic_oneshot.py``
4. ``examples/24_i2c/01_i2c_master_basic.py``
5. ``examples/25_spi/01_spi_basic.py``
6. ``examples/32_uart/01_uart_basic.py``
7. ``examples/34_external_modules/01_i2c_module_probe.py``
8. ``examples/30_fsm/01_fsm_lamp_button.py``
9. ``examples/30_fsm/02_fsm_table.py``
10. ``examples/30_fsm/07_event_queue.py``
11. ``examples/31_case_study/01_bathroom_control.py``
12. ``examples/31_case_study/02_two_fsms.py``

Тази пътека учи на:

- работа с време като ресурс
- стабилни входове и събития
- комуникация с външни модули
- превръщане на логика в FSM
- преминаване от единичен пример към система

Ако тази пътека е основната ви цел, четете приоритетно:

- Части III-XI от :ref:`renesas-ra_vk-ra4m2_book_bg`

Path 3: Audio And Retro Synth
-----------------------------

Това е най-добрата пътека, ако искате game audio, retro SFX и кратки мелодии.

Препоръчителен ред:

1. :ref:`renesas-ra_audio_synth`
2. ``examples/dac_firmware_probe.py``
3. ``examples/retro_synth.py``
4. ``examples/dac_retro_sfx.py``
5. ``examples/dac_retro_music.py``

Този маршрут е особено полезен, ако проектът ви включва:

- coin / jump / laser / explosion звуци
- wavetable loop-ове
- арпежио и кратки мелодии
- аудио за ретро игри

Важно да се помни:

- DAC изходите са ``P014`` и ``P015``
- ``J16`` не е DAC изход
- хардуерно потвърдени са ``DTC`` circular, ``DTC`` one-shot и ``DMAC`` one-shot
- текущият ``CIRCULAR`` wavetable loop е най-подходящ за ретро synth, не за дълги PCM клипове

Recommended Milestones
----------------------

Ако искате да мерите напредъка си, ползвайте тези междинни цели:

1. Да подкарате LED, бутон и ADC без помощ.
2. Да различавате ``sleep_ms()``, ``Timer(-1)`` и хардуерен ``Timer(n)``.
3. Да направите надежден debounce и бутонни събития.
4. Да вдигнете поне един сериен интерфейс с работещ тест.
5. Да организирате логика през FSM.
6. Да подкарате DAC synth пример или firmware probe.

Which Page To Open Next
-----------------------

Използвайте това просто правило:

- ако искате бърза ориентация: :ref:`renesas-ra_vk-ra4m2_practical_guide_bg`
- ако искате пълния учебен текст: :ref:`renesas-ra_vk-ra4m2_book_bg`
- ако искате звук веднага: :ref:`renesas-ra_audio_synth`

Ако трябва да посоча един най-добър старт за повечето хора, това е:

1. тази страница
2. :ref:`renesas-ra_vk-ra4m2_practical_guide_bg`
3. :ref:`renesas-ra_vk-ra4m2_book_bg`
