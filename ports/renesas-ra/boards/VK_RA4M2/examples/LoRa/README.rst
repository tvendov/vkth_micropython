.. _renesas-ra_vk_ra4m2_lora_bg:

LoRa на VK-RA4M2 (Wio-SX1262 + SenseCAP)
=========================================

Тази страница описва поддръжката на LoRa радиокомуникация на ``VK_RA4M2``
с помощта на модула **Seeed Wio-SX1262 for XIAO** (Header Board вариант)
и регистрация на устройството в **SenseCAP Cloud** през LoRaWAN
gateway. Регионалният профил е **EU868** (Европа, 863–870 MHz).

Придружаващите примерни файлове се намират в
``ports/renesas-ra/boards/VK_RA4M2/examples/LoRa/``.

Общ преглед
-----------

VK_RA4M2 няма вграден радиочип, затова LoRa се добавя чрез външен модул на
**Semtech SX1262**, окачен на ``SPI(1)`` + 4 GPIO линии. На ниво софтуер
поддържаме **две алтернативни Python библиотеки** — по избор на потребителя:

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Драйвер
     - Източник
     - Когато е подходящ
   * - ``lora-sx126x``
     - официално от ``micropython-lib``
       (``mpremote mip install lora-sx126x``)
     - препоръчителен за нови проекти; чист API, поддържа
       ``dio2_rf_sw`` и ``dio3_tcxo_millivolts`` директно
   * - ``micropySX126X``
     - https://github.com/ehong-tl/micropySX126X
     - алтернатива; разширен ``begin()`` с повече параметри (FSK, IQ
       inversion, current limit), удобен при портиране от съществуващ
       код използващ този стил API

И двата драйвера ползват същия pin map (виж по-долу) и работят паралелно
— файловете не се припокриват и могат да съществуват едновременно в
``/flash/lib/``. Изборът се прави с ``import``-а.

.. important::

   **Никой от двата драйвера не е LoRaWAN стек** — те са само PHY. SenseCAP
   gateway-ите приемат само LoRaWAN frame-ове, така че за визуализация в
   SenseCAP Cloud се добавя LoRaWAN MAC слой (Class-A, OTAA) върху тях. Без
   MAC слоя е възможна само точка-точка комуникация между две VK_RA4M2
   платки или между VK_RA4M2 и друго SX126x устройство.

Подходящо е за:

- IoT сензорни възли с дълъг обхват (km) и ниска консумация
- Точка-точка телеметрия между VK_RA4M2 платки
- Регистрация на устройства в SenseCAP Cloud
- FSK комуникация съвместима с RFM69-подобни модули

Не е подходящо за: bulk прехвърляне на данни, реално-времеви аудио-стрийм,
high-rate телеметрия (> 5–10 kbps).

Хардуер
-------

Препоръчителен модул
~~~~~~~~~~~~~~~~~~~~~

**Seeed Wio-SX1262 for XIAO — Header Board** (с 14-pin XIAO header).

.. warning::

   Не бъркайте с ``Wio-SX1262 for XIAO ESP32-S3 Kit``, при който модулът
   се поставя върху XIAO ESP32-S3 чрез B2B конектор. За VK_RA4M2 ни
   трябва **Header Board** вариантът, чиито пинове са изведени на
   стандартен 0.1" XIAO header.

Спецификации:

- Чип: Semtech SX1262, EU868 вариант
- TCXO: **1.8 V**, захранван от ``DIO3`` на SX1262
- RF превключвател: автоматично управление през ``DIO2`` (вътрешно)
- Антенен съединител: U.FL (включена е малка SMA антена с pigtail)
- Изходна мощност: до +22 dBm (EU868 ограничено на +14 dBm)

Pin map (потвърден чрез loopback и линк-тестове)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Реалният hard-wired pin map на VK_RA4M2 ↔ Wio-SX1262 (валидиран чрез
T1/T3/T4/T5 тестове на 2026-05-01).

.. list-table::
   :header-rows: 1
   :widths: 18 22 18 42

   * - Wio-SX1262 / SX1262 сигнал
     - XIAO header (Seeed B2B kit)
     - VK_RA4M2 пин
     - Бележка
   * - SCK
     - ``D8 / SCK``
     - ``P111``
     - SoftSPI clock
   * - MOSI
     - ``D10 / MOSI``
     - ``P109``
     - SoftSPI data out
   * - MISO
     - ``D9 / MISO``
     - ``P110``
     - SoftSPI data in
   * - NSS (CS)
     - ``D4 / SDA``
     - ``P206``
     - ``Pin.OUT``, init ``1``
   * - RESET
     - ``D2 / A2``
     - ``P001``
     - ``Pin.OUT``, init ``1``, > 100 µs LOW
   * - BUSY
     - ``D3 / A3``
     - ``P002``
     - ``Pin.IN``, чете състоянието на чипа
   * - DIO1 (IRQ)
     - ``D1 / A1``
     - ``P015``
     - ``Pin.IN`` + ``IRQ_RISING``
   * - **RF_SW1** (PE4529 /CTRL)
     - ``D5 / SCL``
     - ``P100``
     - **``Pin.OUT``, винаги HIGH** (enable RF switch)
   * - DIO2 (RF switch CTRL)
     - вътрешен на Wio
     - не свързан
     - управлява се от SX1262 чрез ``SetDio2AsRfSwitchCtrl(True)``
   * - 3V3
     - 3V3
     - 3V3 рейл
     - стабилен (≥150 mA peak)
   * - GND
     - GND
     - GND
     - обща маса

.. important::

   **RF_SW1 (P100) ТРЯБВА да е HIGH постоянно**, докато LoRa работи. Без
   него RF switch на Wio-SX1262 (PE4529) остава в грешно състояние и
   нищо не предава/приема. Това е специфичен изискване за Wio модула
   (потвърдено от Seeed support и [D2] в ``TEST_PLAN.md``).

.. warning::

   **Hardware SPI(1) на VK_RA4M2 не работи** — `write_readinto` блокира
   безсрочно (firmware bug, виж ``TEST_PLAN.md`` дефект [D2]).
   Затова всички тестове и примери ползват ``SoftSPI`` на същите пинове
   P111/P109/P110.

.. note::

   **CS на P206** (не P006 както първоначално очаквахме). P206 е
   board-specific избор на потребителя за NSS на LoRa чипа.

Конфликти с други VK_RA4M2 примери:

- ``P001``, ``P002`` са ADC входове в стандартния ``ADC`` пример.
- ``P206`` е I2C1_SCL по подразбиране — не пускайте ``I2C(1)`` паралелно.
- ``P100`` е и SPI(0) MISO по подразбиране — не пускайте hardware
  ``SPI(0)`` паралелно с LoRa.

Пример за инициализация на SPI и GPIO
--------------------------------------

::

    from machine import SPI, Pin

    PIN_SCK    = "P111"
    PIN_MOSI   = "P109"
    PIN_MISO   = "P110"
    PIN_NSS    = "P206"
    PIN_RST    = "P001"
    PIN_BUSY   = "P002"
    PIN_DIO1   = "P015"
    PIN_RF_SW  = "P100"      # критично: drive HIGH постоянно

    # SoftSPI (hardware SPI(1) има firmware bug — виж TEST_PLAN [D2])
    from machine import SoftSPI
    spi = SoftSPI(baudrate=1_000_000, polarity=0, phase=0,
                  sck=Pin(PIN_SCK,  mode=Pin.OUT),
                  mosi=Pin(PIN_MOSI, mode=Pin.OUT),
                  miso=Pin(PIN_MISO, mode=Pin.IN))

    nss     = Pin(PIN_NSS,    Pin.OUT, value=1)
    reset   = Pin(PIN_RST,    Pin.OUT, value=1)
    busy    = Pin(PIN_BUSY,   Pin.IN)
    dio1    = Pin(PIN_DIO1,   Pin.IN)
    rf_sw1  = Pin(PIN_RF_SW,  Pin.OUT, value=1)   # ENABLE RF switch

Bring-up: проверка на хардуера
-------------------------------

Преди да опитате реален пакет, тествайте свързването с reset
последователност и четене на статусен регистър::

    import time
    from machine import Pin

    nss    = Pin("P206", Pin.OUT, value=1)
    reset  = Pin("P001", Pin.OUT, value=1)
    busy   = Pin("P002", Pin.IN)
    rf_sw1 = Pin("P100", Pin.OUT, value=1)   # ENABLE RF switch (Wio изисква)

    # Хардуерен reset
    reset.value(0)
    time.sleep_ms(20)
    reset.value(1)
    time.sleep_ms(20)

    # BUSY трябва да падне до 0 в първите 5 ms
    t0 = time.ticks_ms()
    while busy.value() and time.ticks_diff(time.ticks_ms(), t0) < 100:
        pass
    print("BUSY clear after", time.ticks_diff(time.ticks_ms(), t0), "ms")

Ако ``BUSY`` не пада в първите 100 ms, проверете захранването 3V3 и
свързванията на ``RESET`` и ``BUSY``.

Точка-точка LoRa
----------------

Вариант A: ``lora-sx126x`` (препоръчителен)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Инсталация през ``mpremote``::

    mpremote mip install lora-sx126x

Файлът се копира в ``/lib/lora/sx126x.py`` на VK_RA4M2 автоматично.

Пример за инициализация (виж ``01_lora_init.py``)::

    from machine import SoftSPI, Pin
    from lora import SX1262

    # ENABLE RF switch — задължително за Wio-SX1262
    rf_sw = Pin("P100", Pin.OUT, value=1)

    spi = SoftSPI(baudrate=1_000_000, polarity=0, phase=0,
                  sck=Pin("P111", mode=Pin.OUT),
                  mosi=Pin("P109", mode=Pin.OUT),
                  miso=Pin("P110", mode=Pin.IN))

    modem = SX1262(
        spi=spi,
        cs=Pin("P206", Pin.OUT, value=1),
        busy=Pin("P002", Pin.IN),
        dio1=Pin("P015", Pin.IN),
        reset=Pin("P001", Pin.OUT, value=1),
        dio2_rf_sw=True,
        dio3_tcxo_millivolts=1800,         # 1.8 V TCXO на Wio-SX1262
        lora_cfg={
            "freq_khz":     868100,         # EU868 канал 0
            "sf":           7,
            "bw":           "125",
            "coding_rate":  5,
            "output_power": 14,
            "preamble_len": 8,
            "crc_en":       True,
            "syncword":     0x12,           # 0x12 = частен LoRa
        },
    )

    modem.send(b"hello VK_RA4M2")
    modem.start_recv(continuous=True)
    rx = modem.poll_recv()                  # None или (data, meta)

    # ВНИМАНИЕ: lora-sx126x има bug в SNR scaling — RxPacket.snr връща
    # raw int8 от регистъра. Реалното SNR в dB = raw / 4. Виж TEST_PLAN [D1].

Вариант B: ``micropySX126X``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Сваляне на файловете от https://github.com/ehong-tl/micropySX126X:

   - ``lib/_sx126x.py``
   - ``lib/sx126x.py``
   - ``lib/sx1262.py``

Копиране в ``/flash/lib/`` на VK_RA4M2::

    mpremote cp _sx126x.py :lib/_sx126x.py
    mpremote cp sx126x.py  :lib/sx126x.py
    mpremote cp sx1262.py  :lib/sx1262.py

Пример за инициализация (виж ``02_lora_init_micropysx126x.py``)::

    from machine import Pin
    from sx1262 import SX1262

    # ENABLE RF switch — задължително за Wio-SX1262
    rf_sw = Pin("P100", Pin.OUT, value=1)

    # micropySX126X е локално патчнат (sx126x.py) да използва SoftSPI вместо
    # hardware SPI(1) — виж TEST_PLAN [D2].
    lora = SX1262(spi_bus=1,
                  clk="P111", mosi="P109", miso="P110",
                  cs="P206", irq="P015", rst="P001", gpio="P002")

    err = lora.begin(freq=868.1, bw=125.0, sf=7, cr=5,
                     syncWord=0x12,
                     power=14,
                     currentLimit=60.0,
                     preambleLength=8,
                     implicit=False, crcOn=True,
                     tcxoVoltage=1.8,        # 1.8 V TCXO на Wio-SX1262
                     useRegulatorLDO=False,
                     blocking=True)
    print("begin err =", err)                # 0 = ERR_NONE

    lora.send(b"hello VK_RA4M2")
    msg, rx_err = lora.recv(timeout_en=True, timeout_ms=2000)

.. tip::

   За продуктивна работа замразете избрания драйвер във firmware-а чрез
   ``boards/VK_RA4M2/manifest.py`` (виж раздел *Замразяване*), за да
   избегнете фрагментация на heap-а.

.. important::

   И при двата драйвера ``tcxoVoltage`` / ``dio3_tcxo_millivolts`` трябва
   да е **1.8 V** за Wio-SX1262. По подразбиране някои библиотеки подават
   1.6 V и радиочипът дрифти ±20 kHz.

Изпращане на пакет
~~~~~~~~~~~~~~~~~~

::

    import time

    counter = 0
    while True:
        msg = "hello %d" % counter
        lora.send(msg.encode())
        print("TX:", msg)
        counter += 1
        time.sleep(2)        # ETSI EU868 duty cycle ≤ 1% — не сваляйте под 1 s

Приемане на пакет (blocking)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    while True:
        payload, err = lora.recv()
        if err == 0 and payload:
            rssi = lora.getRSSI()
            snr  = lora.getSNR()
            print("RX:", payload, "RSSI=", rssi, "SNR=", snr)

Приемане през DIO1 IRQ (non-blocking)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    import micropython
    micropython.alloc_emergency_exception_buf(100)

    def on_rx(events):
        if events & SX1262.RX_DONE:
            payload, err = lora.recv()
            print("IRQ RX:", payload)

    lora.setBlockingCallback(False, on_rx)
    # main loop остава свободен за друга работа

LoRaWAN за SenseCAP Cloud
--------------------------

Защо отделна стъпка
~~~~~~~~~~~~~~~~~~~~

SenseCAP gateway-ите (M2 indoor / M4 outdoor) са **LoRaWAN
концентратори**. Те препредават само LoRaWAN frame-ове към LoRaWAN Network
Server (SenseCAP Cloud или TTN). Случайни LoRa пакети с ``syncWord=0x12``
не достигат до облака.

За да направим VK_RA4M2 виден в SenseCAP, са нужни:

1. ``syncWord = 0x34`` (LoRaWAN public)
2. EU868 канална таблица (8 канала + RX2 на 869.525 MHz, SF12)
3. AES-128 криптиране на payload + AES-CMAC за MIC
4. OTAA Join процедура (``DevEUI`` / ``JoinEUI`` / ``AppKey``)
5. Frame counter (``FCntUp`` / ``FCntDown``) с persistence в Data Flash
6. RX1/RX2 receive прозорци (5 s / 6 s след всеки uplink)

Тези функции **не са** в ``micropySX126X``. Реализират се като отделен
Python пакет ``lorawan/`` върху същия PHY.

Структура на минималния LoRaWAN MAC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    examples/LoRa/lorawan/
    ├── __init__.py
    ├── crypto.py        # AES-CMAC, AES-CTR (използва вградения cryptolib)
    ├── frame.py         # MHDR/MIC/encrypt/decrypt на uplink/join/downlink
    ├── eu868.py         # канална таблица, RX2, ETSI duty cycle
    ├── mac.py           # автомат: idle → tx → rx1 → rx2 → idle
    ├── radio.py         # обвивка над micropySX126X (sync 0x34)
    └── store.py         # persistence на DevAddr/FCnt в data flash

За първо включване в SenseCAP е достатъчен подмножеството:

- OTAA Join Request → Join Accept
- Unconfirmed Data Up (``MType = 0x40``, ``FPort = 1``)
- Игнор на downlink-овете (RX1/RX2 само за join accept)

Регистрация в SenseCAP Console
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Отворете https://sensecap.seeed.cc и създайте акаунт.
2. **Devices → Add Device → Custom LoRaWAN Device**.
3. Избор на frequency plan: **EU868**, Class A, OTAA, LoRaWAN 1.0.3.
4. Запишете ``DevEUI`` (8 байта), ``JoinEUI`` (8 байта), ``AppKey``
   (16 байта).
5. Регистрирайте SenseCAP gateway-а под същия акаунт; уверете се че е
   ``Online``.

Тези три ключа се поставят в ``boot.py`` или в защитен файл на VK_RA4M2 и
се подават като аргументи на LoRaWAN MAC обект.

Минимален пример (концептуален)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    from lorawan.mac import LoRaWAN

    lw = LoRaWAN(
        spi_bus=1,
        sck="P111", mosi="P109", miso="P110",
        cs="P006", irq="P000", rst="P001", busy="P002",
        tcxo_voltage=1.8,
        dev_eui  = b"\x00\x11\x22\x33\x44\x55\x66\x77",
        join_eui = b"\x00\x00\x00\x00\x00\x00\x00\x00",
        app_key  = b"\x01\x02\x03\x04\x05\x06\x07\x08"
                   b"\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10",
    )

    if lw.join_otaa(timeout_s=30):
        print("JOIN OK, DevAddr =", lw.dev_addr)
    else:
        raise RuntimeError("OTAA join failed")

    while True:
        lw.send_uplink(b"hello", port=1, confirmed=False)
        time.sleep(60)            # 60 s между uplink-ите

В SenseCAP Cloud ще видите:

- *Devices → <DevEUI> → Last Data*: payload-а декодиран като hex
- *Network Status*: RSSI / SNR от gateway-а
- *Frame Counter*: ``FCntUp`` се увеличава с всеки uplink

Замразяване във firmware-а
---------------------------

PHY и LoRaWAN модулите консумират значителна heap памет при ``import``
от файлова система. Препоръчва се замразяване в образа на **избрания**
(или и на двата) драйвера.

1. Копирайте ``.py`` файловете в
   ``ports/renesas-ra/boards/VK_RA4M2/modules/``.
2. Редактирайте ``boards/VK_RA4M2/manifest.py``::

       include("$(MPY_DIR)/extmod/asyncio")
       require("neopixel")

       # Вариант A — официалният lora-sx126x от micropython-lib
       require("lora-sx126x")

       # Вариант B — micropySX126X (паралелно или вместо A)
       freeze("modules", "_sx126x.py")
       freeze("modules", "sx126x.py")
       freeze("modules", "sx1262.py")

       # LoRaWAN MAC (когато е готов)
       freeze("modules", "lorawan/__init__.py")
       freeze("modules", "lorawan/crypto.py")
       freeze("modules", "lorawan/frame.py")
       freeze("modules", "lorawan/eu868.py")
       freeze("modules", "lorawan/mac.py")
       freeze("modules", "lorawan/radio.py")
       freeze("modules", "lorawan/store.py")

.. note::

   Двата драйвера могат да съществуват едновременно — namespace-ите им се
   различават: ``from lora import SX1262`` (вариант A) срещу
   ``from sx1262 import SX1262`` (вариант B).

3. Прекомпилирайте firmware-а::

       make BOARD=VK_RA4M2

4. След ``import sx1262`` проверете че ``sx1262.__file__ is None`` —
   модулът е в ROM.

Тестови резултати (валидирано 2026-05-01)
------------------------------------------

Изпълнен пълен набор тестове на 2× VK_RA4M2 + 2× Wio-SX1262 Header Board
по таблицата от ``TEST_PLAN.md``:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Тест
     - Параметри
     - Резултат
   * - **T1.smoke** lib A
     - import + init + send no-peer
     - ✅ PASS, init 66 ms, 14 KB heap
   * - **T1.smoke** lib B
     - import + init + send no-peer
     - ✅ PASS, init 213 ms, 50 KB heap
   * - **T3.tx + T3.rx**
     - 20 пакета, gap 2 s
     - ✅ 20/20, **PER 0%**, RSSI −13 dBm, SNR +13 dB
   * - **T3.pingpong**
     - 20 PING/PONG round-trips
     - ✅ 20/20, **RTT median 140 ms**
   * - **T5.stress**
     - 200 пакета, gap 500 ms
     - ✅ 200/200, **PER 0%**, leak < 1 KB
   * - **T4.parity** (lib A vs B)
     - 15 sample-а × 2 либи
     - ✅ ΔSNR 0.28 dB (с fix за lib A)

Заключение: **двете либи работят paritetno** на VK_RA4M2 + Wio-SX1262.
За пълни детайли и открити upstream bug-ове виж ``TEST_PLAN.md``.

Регулаторни ограничения (EU868)
--------------------------------

ETSI EN 300 220-2 за SRD ленти 863–870 MHz изисква:

- **Duty cycle ≤ 1%** в повечето подканали (g1, g2, g3)
- Максимална ERP **+14 dBm** (25 mW)
- Channel hopping за равномерно използване на канала

Примерите по подразбиране са настроени на **+14 dBm** и спазват duty
cycle с ``time.sleep(2)`` между предаванията. За продуктивно ползване
LoRaWAN MAC слоят следи time-on-air per канал и блокира TX при
изчерпване на бюджета.

Често срещани проблеми
-----------------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Симптом
     - Решение
   * - ``begin()`` връща грешка ``-705`` (CHIP_NOT_FOUND)
     - Проверете 3V3 и MOSI/MISO разменени; пуснете bring-up скрипта
   * - TX работи, RX не получава нищо
     - ``setDio2AsRfSwitchCtrl(True)`` не е извикан → RF switch остава
       в TX позиция
   * - Честотата дрифти ±20 kHz
     - ``tcxoVoltage`` подаден като 1.6 вместо **1.8**
   * - SenseCAP не вижда устройството
     - ``syncWord = 0x12`` (private) вместо **0x34** (LoRaWAN public)
   * - ``MemoryError`` при ``import sx1262``
     - Замразете модулите във firmware (виж раздел *Замразяване*)
   * - SenseCAP отказва Join след reboot
     - Frame counter не се пази в Data Flash → resyncнете през ``FCnt
       Reset`` бутона в SenseCAP Console
   * - Конфликт с ADC примера
     - ``P000``..``P002`` и ``P006`` са заети от LoRa — спрете ADC
       примера

Препратки
---------

- **micropySX126X** (PHY драйвер):
  https://github.com/ehong-tl/micropySX126X
- **Wio-SX1262 Header Board** pinout:
  https://github.com/meshtastic/firmware/issues/8409
- **XIAO RA4M1** референтна платка:
  https://wiki.seeedstudio.com/getting_started_xiao_ra4m1/
- **SX1262 datasheet** (Semtech DS.SX1261-2.W.APP):
  TCXO setup, DIO2 RF switch, status register
- **LoRaWAN 1.0.3 specification + Regional Parameters EU868**
- **SenseCAP Console**: https://sensecap.seeed.cc/
- **ETSI EN 300 220-2** (EU868 SRD регулация)
- **Подробен план**: ``boards/VK_RA4M2/examples/LoRa/PLAN.md``
