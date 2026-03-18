.. _renesas-ra_vk-ra4m2_validation_matrix_bg:

VK_RA4M2 Hardware Validation Matrix (BG)
========================================

Тази страница събира на едно място текущия, консервативен статус на
валидиране за ``VK_RA4M2``.

Тук умишлено не се приема, че нещо е "доказано", само защото има примерен
файл. Маркираме даден path като хардуерно потвърден само когато има реално
изпълнение на борд или изрично записана проверка в текущата работа по порта.

Свързани страници:

- :ref:`renesas-ra_vk-ra4m2_learning_paths_bg`
- :ref:`renesas-ra_vk-ra4m2_practical_guide_bg`
- :ref:`renesas-ra_vk-ra4m2_book_bg`
- :ref:`renesas-ra_audio_synth`

Status Legend
-------------

Използваните статуси са:

- ``Hardware-verified``: потвърдено е на реален ``VK_RA4M2`` в тази работа
- ``Example-backed``: има пример и docs покритие, но тук няма записано реално хардуерно потвърждение
- ``Needs external setup``: за смислена проверка е нужен външен модул, loopback, инструмент или специална постановка
- ``Caution``: има пример, но не бива да се приема за затворена тема без допълнителен тест

Current Matrix
--------------

.. list-table::
   :header-rows: 1
   :widths: 16 28 16 20 20

   * - Area
     - Primary examples
     - Current status
     - Hardware evidence
     - Next check
   * - Digital output and input
     - ``examples/21_digital_io/01_digital_output.py``, ``examples/21_digital_io/02_digital_input.py``, ``examples/23_timing/03_button_debounce.py``
     - ``Example-backed``
     - No board run recorded here yet
     - Quick on-board pass with ``LED1`` and ``SW1``
   * - ADC
     - ``examples/22_analog/01_analog_input_adc.py``, ``examples/23_timing/07_adc_timing_measure.py``
     - ``Example-backed``
     - No board run recorded here yet
     - Potentiometer or fixed known voltage on an ADC pin
   * - PWM
     - ``examples/22_analog/02_pwm_output.py``, ``examples/03_pwm_ab_same_channel.py``
     - ``Example-backed``
     - No board run recorded here yet
     - Scope, LED, buzzer, or duty sweep observation
   * - Timers, IRQ, button events, RTC
     - ``examples/23_timing/01_delays_and_timers.py``, ``examples/23_timing/02_interrupts.py``, ``examples/23_timing/06_rtc_basic.py``, ``examples/23_timing/17_fast_irq_two_buttons_queue.py``
     - ``Example-backed``
     - No board run recorded here yet
     - Start with ``Timer(-1)``, hardware timer, then RTC, then external IRQ pins
   * - I2C master, runtime pins, SoftI2C, I2CTarget
     - ``examples/24_i2c/01_i2c_master_basic.py``, ``examples/24_i2c/02_i2c_runtime_pins.py``, ``examples/24_i2c/03_softi2c_basic.py``, ``examples/24_i2c/04_i2ctarget_memory.py``
     - ``Needs external setup``
     - No external-device validation recorded here yet
     - External I2C device or second board for target-mode tests
   * - SPI and SoftSPI
     - ``examples/25_spi/01_spi_basic.py``, ``examples/25_spi/02_softspi_basic.py``, ``examples/34_external_modules/02_spi_module_transaction.py``
     - ``Needs external setup``
     - No loopback or module validation recorded here yet
     - SPI loopback first, then real module
   * - UART
     - ``examples/32_uart/01_uart_basic.py``, ``examples/34_external_modules/03_uart_module_request_response.py``
     - ``Needs external setup``
     - No UART loopback or external-module validation recorded here yet
     - USB-UART bridge, loopback, or text protocol module
   * - TouchPad
     - ``examples/26_touchpad/01_touchpad_basic.py``, ``examples/26_touchpad/02_touchpad_diagnostics.py``, ``examples/04_touchpad_async.py``
     - ``Caution``
     - No physical touch/electrode validation recorded here yet
     - Real pad or electrode, then cached and async path
   * - Flash, dataflash, and storage
     - ``examples/27_storage/01_flash_blockdev.py``, ``examples/27_storage/02_dataflash_basic.py``, ``examples/27_storage/03_dataflash_state_led.py``
     - ``Example-backed``
     - No dedicated validation log recorded here yet
     - Read/write persistence check across reset
   * - Sleep modes
     - ``examples/28_machine_misc/03_sleep_modes.py``, ``examples/30_fsm/08_tickless_sleep.py``
     - ``Caution``
     - Documented conservatively; no validated scenario recorded here
     - Short ``lightsleep`` measurement before any aggressive path
   * - NeoPixel
     - ``examples/neopixel.py``, ``examples/neopixel_test.py``
     - ``Needs external setup``
     - No explicit hardware confirmation recorded here
     - One-pixel smoke test with known power and data line
   * - WS2812 via ``machine.WS2812``
     - ``examples/ws2812_sci.py``, ``examples/ws2812_sci_test.py``, ``examples/ws2812_sci_p101.py``, ``examples/ws2812_sci_p109.py``
     - ``Needs external setup``
     - No current hardware confirmation recorded here
     - One-pixel smoke test on a known-good SCI TX/MOSI pin
   * - DAC timed playback and retro synth
     - ``examples/dac_firmware_probe.py``, ``examples/dac_retro_sfx.py``, ``examples/dac_retro_music.py``, ``examples/retro_synth.py``
     - ``Hardware-verified``
     - Confirmed on real ``VK_RA4M2`` for ``DTC`` circular, ``DTC`` one-shot, and ``DMAC`` one-shot
     - Keep ``P014/P015`` as baseline reference path

What Is Already Confirmed
-------------------------

Към момента консервативно потвърденото на реален ``VK_RA4M2`` е:

- timed DAC playback през ``DTC`` circular
- timed DAC playback през ``DTC`` one-shot
- timed DAC playback през ``DMAC`` one-shot
- ``dac_retro_sfx.py``
- ``dac_retro_music.py``

Това прави audio path-а най-зрелия хардуерно потвърден блок в текущата работа.

What Is Still Open
------------------

Най-важните незатворени теми са:

- външни ``I2C`` устройства
- ``SPI`` loopback и външни ``SPI`` модули
- ``UART`` loopback и text/binary request-response модул
- физическа TouchPad постановка
- ``WS2812`` smoke test на реален диоден модул
- кратък и безопасен ``lightsleep`` validation pass

Suggested Next Hardware Passes
------------------------------

Ако трябва да подредя следващите реални бордови проверки по стойност, това е
най-разумният ред:

1. ``UART`` loopback или външен USB-UART path
2. ``SPI`` loopback
3. ``I2C`` scan и базов външен модул
4. ``WS2812`` еднопикселен smoke test
5. ``TouchPad`` с реален електрод
6. кратък ``lightsleep`` timing check

Why This Page Matters
---------------------

Тази матрица е полезна по две причини:

- показва ясно какво вече е доказано и какво още не е
- пази книгата и guide-а честни, без да смесва "има пример" с "хардуерно е затворено"

Explicit Exclusion For Now
--------------------------

``Encoder`` умишлено не е включен като готов path в тази матрица.
Кодът и примерите вече съществуват в дървото, но feature-ът не трябва да се
смята за завършен, докато не мине собствена отделна валидация и документация.
