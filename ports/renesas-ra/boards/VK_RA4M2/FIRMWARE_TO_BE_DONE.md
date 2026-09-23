# RA4M2 Firmware - To Be Done

Натрупващ се регистър на проблемите, установени при документацията на VK_RA4M2.
Начало: 2026-09-18. Последна актуализация: 2026-09-22.
Завършени промени: [FIRMWARE_DONE.md](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA4M2/FIRMWARE_DONE.md).

## Обхват и правила

- Основно дърво: `C:\msys_64\home\teodor\renesas_micropython`.
- Проверен HEAD: `1c6357b5d11d7a3d2d3697202027655a67d7f33f`. Работното дърво като цяло има локални промени; ревизията сама не е идентичност на целия firmware.
- Основен обхват: VK_RA4M2. Изрично поисканите междуплаткови задачи се записват с отделен board ID, включително M5-CV2-001 за VK_RA6M5. Част от файловете са общи за други RA платки или всички MicroPython портове; преди поправка се определя общото въздействие.
- Това е backlog, не инструкция за незабавно качване или хардуерен тест. До 2026-09-20 няма firmware поправки. На 2026-09-22 е реализиран кандидат за RA-IRQ-001 и отделният build prerequisite RA-BUILD-001; host/build резултатите са в DONE. Flash/HIL няма.
- Идентификаторите са постоянни. Новите находки се добавят с дата; съществуващите се актуализират, без да се губят първоначалното доказателство и историята.
- Статуси: `OPEN-CONFIRMED` = дефект по изходния код; `OPEN-DESIGN` = необходимо решение за API; `OPEN-PORT` = поискан, но неизпълнен пренос към друга платка; `OPEN-DOC` = проблем само в документацията/тестовете; `IN-PROGRESS`; `CLOSED` с връзка към DONE.
- При изпълнение записвайте отделно: source review, host тест, ARM/build, upload, HIL. `NOT RUN` не означава PASS. За CLOSED трябва да са покрити предварително посочените критерии; липсващото хардуерно доказателство остава явно.
- Не преименувайте различие в API като firmware дефект без доказателство. Например raw RTC weekday и AudioADC singleton първо изискват точна документация и решение за съвместимост.
- По-старият [общ TODO](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/TODO.md) е отделен регистър. Неговите исторически твърдения не са преудостоверени тук.

## Обобщение

| ID | Приоритет | Статус | Проблем |
| --- | --- | --- | --- |
| M2-FW-001 | P1 | OPEN-CONFIRMED | RTC.wakeup чете липсващ args[2] |
| M2-FW-002 | P1 | OPEN-CONFIRMED | I2CTarget.readinto не изисква writable buffer |
| M2-FW-003 / RA-IRQ-001 | P2 | IN-PROGRESS | Soft dispatch е поправен локално; остава хардуерно приемане |
| M2-FW-004 | P2 | OPEN-CONFIRMED | CTSU cap параметри се стесняват преди проверка |
| M2-FW-005 | P1 | OPEN-CONFIRMED | RIIC pin таблицата допуска SCI0 пинове P100/P101 на RA4M2 |
| M2-FW-006 | P1 | OPEN-CONFIRMED | RSPI таблиците за RA4M2 използват несъответстващи SCI0 pins/channel mappings |
| M2-FW-007 | P1 | OPEN-CONFIRMED | RA4M2 P408/P409 са SCI3, но UART таблицата ги задава като SCI9; FEMTO UART3 не е конфигуриран |
| M2-API-001 | P2 | OPEN-DESIGN | CTSU: разграничаване на кеш и нов кадър |
| M2-API-002 | P2 | OPEN-DESIGN | CTSU: ownership и жизнен цикъл на diagnose/sampler |
| M2-API-003 | P2 | OPEN-DESIGN | AudioADC: явно правило за singleton/reconfigure |
| M5-CV2-001 | P2 | OPEN-PORT | OpenCV QSPI профил за VK_RA6M5, която има QSPI конфигурация |

P1 е приоритет за поправка поради нарушение на аргументен/паметен договор или несъответствие на pin/backend с хардуерния manual, не твърдение за възпроизведен crash. P2 е поведенчески/API проблем или планирано разширение към друга платка. Всички хардуерни последствия остават NOT VERIFIED без HIL.

## M2-FW-001 - RTC.wakeup: достъп извън аргументите

- Открит/проверен: 2026-09-18. Статус: OPEN-CONFIRMED.
- Доказателство: [machine_rtc_wakeup](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_rtc.c:334) чете `args[2]` при `n_args >= 2`. При bound `rtc.wakeup(ms)` или `rtc.wakeup(None)` има само два C аргумента, включително self.
- Засегнати вътрешни пътища: [lightsleep/deepsleep](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/modmachine.c:323) подават масив от два аргумента към същата функция.
- Нужна промяна: правилна проверка за наличието на callback; липсващият callback остава None. Одит на вътрешните извиквания, без промяна на публичния договор по предположение.
- Временно ограничение: явното `rtc.wakeup(ms, None)` избягва този конкретен out-of-bounds достъп. То НЕ поправя timed sleep вътрешния път и не доказва успешно събуждане.
- Приемане: регресионни тестове за wakeup(ms), wakeup(ms,None), wakeup(ms,handler), wakeup(None), wakeup(None,None), както и двата sleep caller-а; липса на достъп извън масива; M2 build; отделно HIL за wakeup/stop/standby след разрешение и идентификация на firmware.
- Проверка на граници: отделно уточнете договора за ms извън 4..2000; не променяйте квантуването като страничен ефект на поправката.
- Изпълнение: source review PASS; host test/ARM build/upload/HIL NOT RUN; няма patch.

## M2-FW-002 - I2CTarget.readinto: writable buffer

- Открит/проверен: 2026-09-18. Статус: OPEN-CONFIRMED.
- Доказателство: [machine_i2c_target_readinto](C:/msys_64/home/teodor/renesas_micropython/extmod/machine_i2c_target.c:275) заявява `MP_BUFFER_READ`, след което предава указателя на функция за запис в буфера. [RA backend](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_i2c_target.c:121) записва получените байтове.
- Риск: API не отхвърля read-only буфер на правилната граница. Реален запис/повреда с immutable bytes не е изпитван на платка.
- Нужна промяна: изискване за `MP_BUFFER_WRITE` в readinto; `write(data)` продължава да приема read-only вход. Файлът е общ extmod, затова проверката не трябва да е само за RA.
- Приемане: bytearray и writable memoryview се приемат; bytes и read-only memoryview се отказват преди извикване на backend; проверка на нулева дължина и частичен резултат; общи тестове за I2CTarget и M2 build; HIL за реален прием при разрешение.
- Изпълнение: source review PASS; host test/ARM build/upload/HIL NOT RUN; няма patch.

## M2-FW-003 - Pin.irq(hard=False) остава директен IRQ callback

- Открит/проверен: 2026-09-18. Актуален статус 2026-09-22: IN-PROGRESS. Историческите доказателства по-долу са за кода преди поправката.
- Доказателство: [extint_register_pin](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/extint.c:251) записва `pyb_extint_hard_irq[line]`. [extint_callback](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/extint.c:129) не го проверява; non-fast пътят заключва scheduler/heap и извиква Python директно.
- Нужна промяна: разграничаване на soft/hard dispatch, съобразено с текущите scheduler helpers. Да се запазят fast ASM, аргументът Pin, прекратяването при грешка и освобождаването на callback references.
- Особено внимание: shared extint_callback обслужва и не-GPIO потребители, включително RTC; да се одитира началното състояние на hard/fast флаговете и техните аргументи.
- Приемане: hard=False се изпълнява извън ISR; hard=True и fast=True запазват договорите; тестове за unregister/re-register, pending callback, пълна scheduler опашка, изключение и soft reset; M2 build и HIL с управляван входен сигнал.
- Изпълнение 2026-09-22: локален patch в extint.c/extint.h/machine_pin.c; host регресия върху реални C функции и scheduler при -O0/-Os PASS; ARM build за VK_RA4M2 с/без кварц и VK_RA6M5 PASS. Flash/HIL NOT RUN. Затварянето остава зависимо от RA-IRQ-HIL-001.
- Договорът за новия soft път е като scheduler: пълна опашка губи новото събитие без ISR fallback; стар queued handler може да се изпълни след unregister/re-register; soft exception не изключва GPIO. Старото автоматично изключване при изключение остава за hard пътя. Подробности и тестове: `../../tests/irq/README.md`.
- Сървърен проект: https://stemkids.bg/index.html#projects/a360fe00-35d2-4a20-9e4c-0e923ab00001?tab=tasks&task=RA-IRQ-001

## M2-FW-004 - CTSU cap setters: проверка преди cast

- Открит/проверен: 2026-09-18. Статус: OPEN-CONFIRMED.
- Доказателство: [cap_config](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_touchpad.c:587) и [cap_global_config](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_touchpad.c:646) преобразуват Python integer към uint8_t/uint16_t преди диапазонните проверки в [ra_ctsu_set_cap_element](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_ctsu.c:973) и [ra_ctsu_set_cap_global](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_ctsu.c:1027).
- Пример по C преобразуването, не хардуерен тест: `sdpa=256` става 0 преди проверката; `so=65536` става 0.
- Нужна промяна: проверка на оригиналните стойности преди стесняване и преди хардуерна промяна. Да се запазят None semantics и проверката `ssdiv`/`auto_ssdiv`.
- Допълнително: `_sst_debug` се валидира след прилагането на глобалната конфигурация. Валидирайте всички подадени аргументи предварително; runtime FSP отказът е отделен въпрос за rollback, не автоматично решен с валидация.
- Приемане: отрицателни, гранични, max+1 и wraparound стойности за всяко поле; невалидният аргумент не променя други настройки; тестове за None и ssdiv конфликт; M2 build; HIL за валидни настройки след разрешение.
- Изпълнение: source review PASS; host test/ARM build/upload/HIL NOT RUN; няма patch.

## M2-FW-005 - RA4M2 RIIC pin таблица и погрешен IIC0 коментар

- Открит/проверен: 2026-09-19. Приоритет P1, статус OPEN-CONFIRMED по source/manual, без HIL.
- [ra_i2c.c:84](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_i2c.c:84) допуска P100/P101 за RIIC1 в RA4M2 клона. [machine_i2c.c:282](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_i2c.c:282) и [machine_i2c_target.c:254](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_i2c_target.c:254) използват тази таблица за валидация. RIIC init задава AF_I2C=7, не SCI AF.
- Manual Rev.1.40, Table 19.6, p.441: P100/P101 са SCI0 SCL0/SDA0 при PSEL=00100b; не RIIC. В Table 19.7 p.442 IIC1 е P205/P206 при PSEL=00111b. Проверени са package pin list и PFS таблица; [визуално проверена страница 441](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/ra4m2-port1-functions.png).
- [mpconfigboard.h:134](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA4M2/mpconfigboard.h:134) погрешно нарича P100/P101 алтернативен IIC0. Това е коментар, не доказателство за runtime mapping. Предходното следване на този коментар в сайта се оттегля.
- Default I2C(1) остава P205/P206. Явен I2C(1, P100/P101) минава текущата RIIC таблица, но не съответства на hardware mux; смяна само на номера с 0 също не поправя проблема. Не се обещава готов SCI0 вариант без изрична реализация/config.
- Нужна промяна: одит на целия RA4M2 RIIC pin set спрямо manual/package, корекция на таблицата и коментара; запазване на валидните mappings за другите MCU; едновременно актуализиране на засегнатите I2C/I2CTarget примери и сайта.
- Приемане: положителни/отрицателни pin-pair проверки за master и target; несъответстващите SCI/RIIC pins се отказват преди конфигуриране на hardware; board build; отделно master/target HIL с разрешение, проверена схема/pull-ups и отчет за BLE/UART pin conflicts. Регресионна проверка на SCI I2C(2).
- Source/manual review: PASS за конкретното несъответствие. Host regression/ARM build/upload/HIL: NOT RUN. Няма firmware patch. Свързано: M2-DOC-007.

## M2-FW-006 - RA4M2 RSPI таблици, наследени от RA4M1

- Открит/проверен: 2026-09-19. Приоритет P1, статус OPEN-CONFIRMED по source/manual, без HIL.
- [machine_spi.c:80](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_spi.c:80) избира RSPI backend за SPI(0)/SPI(1). [ra_spi.c:45](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_spi.c:45) споделя RA4M1/RA4M2 таблици и свързва RSPI0 с P101/P100/P102.
- Table 19.6 p.441 показва на P101/P100/P102 съответно SCI0 MOSI0/MISO0/SCK0 при PSEL=00100b; не RSPI. Отделният SPI ред при PSEL=00110b дава MOSIA/MISOA/RSPCKA на P109/P110/P111. В текущата C таблица тези пинове са channel 1; нужната поправка не е само преместване на един pin.
- Нужна промяна: отделен проверен RA4M2 RSPI mapping; одит на брой/идентичност на RSPI контролерите, pin mux, SSL, IRQ/clock и публичните ID; запазване на RA4M1; синхронизация с board macros, machine validator, документация и примери. SCI SPI(2/3) да останат отделно проверени backend-и, без механично преномериране.
- Приемане: всички разрешени pins и канали имат manual източник; невалидните се отказват преди init; M2 и засегнатите shared-board build/regression проверки; разрешен HIL за SCK/MOSI/MISO/CS и loopback/реален target при известни mode/baudrate. Наличие на Python обект не е PASS за физически SPI.
- Source/manual review: PASS за конкретното несъответствие. Host regression/ARM build/upload/HIL: NOT RUN. Няма firmware patch. Свързано: M2-DOC-007.

## M2-FW-007 - FEMTO TP4/TP6: UART3 вместо погрешно SCI9

- Открит/проверен: 2026-09-20. Статус: OPEN-CONFIRMED по source/manual, без хардуерен тест.
- RA4M2 Hardware Manual R01UH0892EJ0140, Table 19.9: P408 = RXD3/MISO3/SCL3; P409 = TXD3/MOSI3/SDA3 (PSEL 00101). FEMTO TP4 е P408, TP6 е P409. Надписите RX9/TX9 в схемата на FEMTO не са правилните RA4M2 периферни функции.
- `ra/ra_sci.c`, RA4M2 TX и общата RA4M1/RA4M2 RX таблица, погрешно свързват P409/P408 с channel 9. Не се прави обща замяна за други MCU без техния manual.
- `boards/VK_RA4M2/mpconfigboard.h`: UART3 TX/RX са изключени; `ra_gen/vector_data.h/.c` няма SCI3 RXI/TXI/TEI/ERI. `uart.c` включва UART3 само при съответните board macros. `machine_uart.c` няма tx/rx keyword override.
- LoRaWAN `SPI(3)` е отделен SCI9: P111 SCK, P109 MOSI, P110 MISO. Радиото остава на същите пинове; няма хардуерен SCI конфликт с UART3. Предложението за SoftSPI се оттегля като основано на грешното UART означение.
- Приемане: RA4M2-specific pin-table корекция; UART3 TX=P409/RX=P408; пълна SCI3 IRQ/clock конфигурация; проверки за правилно приемане/отказ на pin/channel двойки; ARM build; отделен UART3 loopback и едновременно GNSS + LoRaWAN SPI3 изпитване с разрешение. Да се провери, че SCI9 остава собственост на радиото.
- Сайтът описва правилния mapping и блокира примерния UART3 helper по подразбиране до проверен build. Това не поправя firmware и не затваря задачата. Няма build, upload или HIL.

## M2-API-001 - CTSU: свежест на резултатите

- Статус: OPEN-DESIGN. Няма доказателство, че документираният в C смисъл на ready е дефектен.
- [ready](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_touchpad.c:318) означава наличен кеш. [scan_start](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_ctsu.c:830) не нулира g_cache_valid; [cached_ready](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_ctsu.c:882) връща този флаг.
- Нужда: решете дали да се добави sequence/generation за нов резултат или отделен completion API. Не променяйте ready тихо, защото това би било несъвместимост.
- Приемане на евентуално разширение: стар кеш, незавършил/провален scan, повторно start, няколко електрода, wraparound и глобални промени; никакъв стар кадър, обозначен като нов.
- Свързани документални задачи: M2-DOC-001 и M2-DOC-002.

## M2-API-002 - CTSU: управление на sampler и diagnose

- Статус: OPEN-DESIGN. Поведението е проверено по код; последствията при конкурентна употреба са NOT VERIFIED на платка.
- [service](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_touchpad.c:93) започва scan при свободен CTSU, независимо от sample_rate. sample_rate(0) премахва периодичния timer, но не е общ cancel на scan/вече насрочена работа.
- [diagnose](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_ctsu.c:1528) затваря нормалната инстанция, отваря diagnosis режим и после възстановява нормалния режим. Това засяга общия CTSU, не само Python обекта.
- Нужда: решение за pause/drain/resume или EBUSY при активен sampler/scan; ясно описани ownership и error/restore правила. Не се твърди универсална безопасна последователност без проследяване и тест.
- Приемане: pending scan, queued sampler node, timeout, FSP отказ при open/restore, повече от един електрод; състоянието и грешките остават наблюдаеми, без неверен ready резултат.
- Свързана документална задача: M2-DOC-003.

## M2-API-003 - AudioADC singleton и пренастройка

- Статус: OPEN-DESIGN, не автоматично признат firmware дефект.
- [machine_audioadc_make_new](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/machine_audioadc.c:90) връща общ статичен обект; при active първо деинициализира текущия поток. Вторият конструктор променя същия обект, към който сочи първата променлива.
- Нужда: първо документирайте един собственик. След решение за съвместимост: запазване на явна singleton семантика, EBUSY при втори конструктор или изричен reconfigure API.
- Приемане на промяна: две references, различни frame/fs/pin, отказ при reinit, стар буфер, stop/deinit/soft reset; няма недокументирано прекъсване на чужд поток.
- Свързана документална задача: M2-DOC-005.

## M5-CV2-001 - OpenCV QSPI за VK_RA6M5

- Добавена: 2026-09-20, по изрично искане на автора. Приоритет P2, статус OPEN-PORT. Цел: използване на QSPI на M5 за OpenCV профила; това е задача за firmware пренос, не ограничение на видимостта в сайта и не твърдение, че M5 няма QSPI.
- Потвърдено по конфигурация: [VK_RA6M5/mpconfigboard.h:28](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA6M5/mpconfigboard.h:28) задава `MICROPY_HW_HAS_QSPI_FLASH (1)`. [vk_ra6m5.ld:13](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA6M5/vk_ra6m5.ld:13) описва `QSPI_FLASH` от `0x60000000`; това е linker конфигурация, не ново хардуерно измерване.
- Текуща софтуерна пречка: [cv2_qspi.mk:3](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/cv2_qspi.mk:3) допуска само `VK_RA6M3`; същият файл използва `vk_ra6m3.ld` и инструментите от `platforms/ra6m3/`. Следователно наличието на QSPI на M5 само по себе си още не прави този build профил пренесен.
- Нужна работа: отделна M5 конфигурация за native OpenCV/ulab/Prepared, проверени CPU/FPU/ABI флагове, адаптация на linker генератора и пакетирането, QSPI инициализация/достъп до native кода и C++ startup. Общите части да се споделят с M3, без механично преименуване на неговата memory map.
- Разположение на паметта: проверка на реалния QSPI чип и картата на конкретната M5 платка; изрично разделяне на OpenCV кода/константите от файловата система и останалите данни. Текущите [external storage символи](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA6M5/vk_ra6m5.ld:323) обхващат `QSPI_FLASH`. Да няма застъпване; OSPI RAM е отделен ресурс и не се приравнява на QSPI flash.
- Приемане: чист M5 build с профила включен и изключен; проверени map/ELF и граници на областите; регресионен build на M3; след отделно разрешение за хардуерна работа и запазване на данните - M5 boot, `import cv2`, Prepared режими 0..7, числовите вектори, граници/буфери, allocator counters, deinit и soft reset. Записват се точни платка и firmware build за всеки HIL резултат.
- Изпълнение: проверени са само горните source/config зависимости. Firmware patch, host/ARM build, upload и HIL за M5 OpenCV: NOT RUN. Не се маркира CLOSED и не се прехвърля M3 тестов резултат към M5.

## Отделно: отворени задачи в сайта, не firmware поправки

Сайт: `C:\Users\teodor\Desktop\stem\mpy_drivers\examples\renesas_micropython_site`.

| ID | Статус | Нужна корекция и доказателство |
| --- | --- | --- |
| M2-DOC-001 | CLOSED | Исторически: touch-2.py смесваше sample_rate(20) с service през 10 ms. Коригирано 2026-09-19: отделни периодичен и ръчен примери; host regression потвърждава 0 manual service calls в периодичния и cleanup при KeyboardInterrupt. Не е измерена хардуерна честота. Виж DONE-SITE-001. |
| M2-DOC-002 | CLOSED | Исторически: touch-4.py прекратяваше по ready и можеше да прочете стар кеш. Коригирано 2026-09-19: няма early exit по ready или обещание за нов кадър; запазен host regression със стар кеш проверява 20 service calls. Виж DONE-SITE-001; firmware API не е променен. |
| M2-DOC-003 | CLOSED | Исторически advanced.cjs препоръчваше diagnose за наблюдение. corrections.cjs заменя съвета преди build и отделя диагностиката от обикновено четене; описва общото преконфигуриране и изключителното владение. Текстов regression PASS; виж DONE-SITE-001. |
| M2-DOC-004 | CLOSED | Историческите примери даваха weekday=4 за 2026-09-18. corrections.cjs и генерираните файлове вече използват raw weekday=5. Regression проверява двата примера. Това е поправка на документация, не RTC setter; виж DONE-SITE-001. |
| M2-DOC-005 | CLOSED | Добавено е изрично описание на AudioADC singleton, повторния конструктор и единствения собственик. Текстов regression PASS; виж DONE-SITE-001. M2-API-003 остава OPEN-DESIGN. |
| M2-DOC-006 | CLOSED | Безусловният AST_CALL_CHECK е премахнат. Build присвоява статуса само след успешен checker; qa/examples-static.json пази hashes на примерите, авторските входове, source/catalog manifests и checker-а. Ограничението за receiver/аргументи е изрично. Виж DONE-SITE-001. |
| M2-DOC-007 | IN-PROGRESS | 2026-09-19: 67 board-example headers съдържат I2C(1)=P100/P101, default вече е P205/P206. Сайтът и неговите downloads са коригирани чрез corrections.cjs, с предупреждения M2-FW-005/006 и default RIIC0 / SCI / SoftSPI примери. Оригиналните headers и firmware таблици НЕ са променени. Приемане: всеки публикуван mapping има manual + backend evidence; коригирани source comments, site source и regenerated downloads. |
| M2-DOC-008 | OPEN-DOC | 2026-09-19: board examples/27_storage/01_flash_blockdev.py:21 използва 16-byte readblocks без offset, а storage.c:309 брои len/512 = 0 блока. Приемане: пълен block buffer с размер от ioctl и проверен return; показаните 16 байта са от реално четене, а не от нулев initial buffer. |
| M2-DOC-009 | OPEN-DOC | 2026-09-19: 28_machine_misc/03_sleep_modes.py:23 извиква lightsleep(20) въпреки DO_LIGHTSLEEP=False; 30_fsm/08_tickless_sleep.py:27 също достига M2-FW-001. Приемане: no-sleep режимът не извиква sleep по нито един път; timed sleep не се предлага преди поправка/доказан договор. |
| M2-DOC-010 | OPEN-DOC | 2026-09-19: 23_timing/15_hardware_timer_input_capture.py:17 приравнява timer freq=1 MHz на 1 count=1 us. Приемане: разграничени cycle frequency, counter clock, capture counts и проверена конверсия; физическите очаквания не се представят като измерени. |
| M2-DOC-011 | OPEN-DOC | 2026-09-19: 23_timing/01..04 завършват с led.off(), наречено изгасване, но Pin.off=low и LED1 е active-low. Приемане: правилно финално ниво и cleanup при прекъсване/грешка; контрол-flow проверка плюс отделно HIL при разрешение. |
| M2-DOC-012 | IN-PROGRESS | 2026-09-20: всичките 278 Python оригинала вече имат видим код и download, отделно от 20-те адаптации. В 12 site копия AppKey стойностите са заменени с нули; оригиналите са запазени. Host/архив/dependency/fixtures и рисковете остават разграничени; хардуерен PASS не се извежда от AST. Това заменя предходното ограничение до metadata за 258 файла. Остава индивидуалният семантичен преглед и адаптация по EX-007..012: Touch/IRQ/build variants, NeoPixel naming/GPIO-power, protocol templates, destructive/RF операции. Авторът потвърди право за публикуване; няма лицензионна пречка. |
| M2-DOC-013 | OPEN-DOC | 2026-09-20: examples/DAC8830_DAC8571/test_adc_dac.py:187 създава button_timer, :188 стартира периодичен callback, :311 влиза в главния цикъл без общ try/finally и без button_timer.deinit(). Това е проблем на примера, не нов дефект в Timer C кода. Приемане: изрично освобождаване при KeyboardInterrupt/грешка, проверени нормален и аварийни пътища; отделно HIL след разрешение. Оригиналният файл не е променян. |

Първичен източник за weekday: [RA4M2 manual, RWKCNT](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA4M2/examples/r01uh0892ej0140-ra4m2.txt:39318). Записът е директен в [ra_rtc_set_time](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/ra/ra_rtc.c:279).

Host моделите проверяват Python control flow, а не C/FSP/хардуера. Към 2026-09-19 вече има запазен regression suite [check-flows.py](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/check-flows.py), изпълняван от [check-contracts.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/check-contracts.cjs); [отчет](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/qa/contracts-results.json). Browser отчетът е отделен и сам по себе си не доказва семантика. CLOSED M2-DOC-001..006 се отнася само за посочените документални корекции; M2-DOC-007..013 остават незавършени.

## Отложено: общият STEMKIDS сайт

- STEMKIDS-SITE-001: онлайн коментари и GitHub issues. Авторът изрично остави
  избора на приемащото хранилище за по-късно. Локалният компонент има Wiki,
  коментари, проблеми с build/статус и JSON износ/внос; няма публично изпращане.
  След избора са нужни отделни правила за достъп, публикуване и модерация.
  Това е отложена сайт интеграция, не firmware дефект или обещание за автоматичен follow-up.

- STEMKIDS-SITE-002 (OPEN-DOC, 2026-09-20): непълно покритие с 3D изображения.
  За VK NANO R4 / RA4M1 няма потвърдено изображение или модел; точният файл
  е поискан от автора. Няма подменена платка или изображение по предположение.
  За VK-RA6M3 и VK-RA6M5 сайтът има официални перспективни снимки и локални
  3D PDF в ZIP архиви. Самите PDF съдържат U3D без статичен preview; снимките
  са означени като снимки, не като рендери. Остават действителни статични
  рендери на техните модели. Cmod S7 е означен като учебен 3D модел.
  Приемане: потвърдена идентичност на M1 файла, рендерирани действителни
  M3/M5 модели без подмяна, всички изображения вътре в сайта, визуален
  преглед и desktop/mobile/offline QA. Това не е firmware дефект.

## Отделно: интеграция на FEMTO проектите

- STEMKIDS-PROJ-001 (OPEN-INTEGRATION, 2026-09-20): двата конкретни проекта
  използват само FEMTO. Балонът има модели за BMP581 SEN0667, SHT31 SEN0385,
  GNSS TEL0157 и LoRa телеметрия; мечът има BMI160 SEN0250 и кратък RGB
  опит през LEDOUT. Блоковите/breadboard схеми и host тестовете са готови,
  но не доказват работа на физическите сензори, радио или аудио.
  При първоначалното публикуване не беше установен нов firmware дефект.
  Последващата проверка на долния UART откри M2-FW-007 (виж по-горе);
  няма избран изпитан build.
- Нужно за приемане: свързване на действителните драйвери и протоколи,
  точен MicroPython build и FEMTO ревизия, измерен токов бюджет и тест
  на краткия LED сегмент, проверка на P500/LEDOUT при грешка и рестарт.
  За балона: реални измервания, свежест/CRC, GNSS fix и единици, избран
  Wio-SX1262 Header Board със същите LoRaWAN SPI(3)/SCI9 пинове и GNSS
  на UART3/SCI3 TP4/P408 и TP6/P409 след M2-FW-007; SoftI2C P302/P301,
  наземен RF тест и отделно решение
  за условията на полет. За меча: измерени IMU честота/закъснение, калибрация,
  два външни бутона P015/P100, отделен DAC/DFR0119-O усилвателен монтаж
  и едновременен LED/IMU/audio тест. Модулът PAM8403 е предложение,
  не хардуерно потвърдена реализация.
  SCI2 не се споделя едновременно между WS2812 и UART(2)/SPI(2).
- Дълга RGB лента и силовото й захранване не се приемат по breadboard
  модела. VLED остава изход, без външно захранване към него. Тестовият
  маркер се променя на „Да“ само след действителен тест с точна платка/build.
  ARM build/upload/HIL: NOT RUN; записът не е CLOSED.

## Идентичност на проверените източници

SHA-256 на файловите байтове към 2026-09-18, без нормализиране на новите редове. Това се различава от нормализираните hashes в source-manifest.json на сайта.

| Път спрямо основното дърво | SHA-256 |
| --- | --- |
| ports/renesas-ra/extint.c | cacb5d882326ee6963dca0a1d2454f641855117eeb379b07cc817acfed856e8e |
| ports/renesas-ra/machine_rtc.c | a4da52e96d398d4a162542d3dd84996517118effde0e43873a05f2d552b08a2a |
| extmod/machine_i2c_target.c | 74d74aeec4111b6abe26225ebe844b92f7b70002f77a47fa1fae01fee9931108 |
| ports/renesas-ra/machine_touchpad.c | 426f5060fee440572a24789e1a910eb977888207cd812c6e139eba4c2f3524a9 |
| ports/renesas-ra/ra/ra_ctsu.c | 02254eebb5953eab4453048921b8f806de77cd3401f4915ca856d773b5c1a71f |
| ports/renesas-ra/machine_audioadc.c | 33f585080323f4074de07f865d11ec56b3cb4ed7991e44a925ec2faab6924f17 |

## История

- 2026-09-20, FEMTO проекти: добавена M2-FW-007. Table 19.9 определя
  TP4/P408 и TP6/P409 като SCI3, не SCI9. Оттеглено погрешното предложение
  за SoftSPI поради несъществуващ SCI конфликт. Сайтът/проектите са коригирани;
  firmware pin таблиците, board конфигурацията и IRQ остават непроменени.

- 2026-09-20, общи Renesas материали: платката на примерната реализация вече
  не ограничава видимостта на библиотеката в STEMKIDS. Добавена M5-CV2-001
  (OPEN-PORT) по искане на автора: OpenCV QSPI за VK_RA6M5. QSPI е включен в
  M5 конфигурацията; текущият OpenCV build guard все още допуска само M3.
  Няма firmware patch/build/upload/HIL и няма затворен firmware/API запис.

- 2026-09-20, последващо уточнение: LoRa/LoRaWAN е под Библиотеки и Проекти,
  а не в главното меню. Публичният GitHub списък е ограничен до tvendov/Vekatech.
  Добавена е обща страница на пример със схема-картинка, KiCad връзка/файлове,
  диаграми, изображения, Wiki и локална обратна връзка. Firmware/API статусите
  не се променят; онлайн интеграцията е отделена като STEMKIDS-SITE-001.

- 2026-09-20: STEMKIDS обединява публикувани M2/LVGL копия, три книги, камерни/чипови драйвери, OpenCV и двата посочени SDR проекта. LoRa / LoRaWAN е главен раздел; добавени проверени GitHub препратки. M2-DOC-012 остава IN-PROGRESS въпреки пълния видим код; нов M2-DOC-013 е само за cleanup в примера. Шестте firmware и трите API задачи не са затваряни. Няма firmware build/upload/HIL; подробният сайт отчет е DONE-SITE-003.

- 2026-09-18: създаден регистърът. Внесени 4 потвърдени firmware дефекта, 3 API решения и 6 отделни документални задачи. Няма затворени firmware поправки. Създаването на регистъра не променя нито един от тези статуси.
- 2026-09-19: анализирани статично 278 board Python файла; добавени M2-FW-005/006 и M2-DOC-007..012. Общо 6 OPEN-CONFIRMED firmware, 3 OPEN-DESIGN API и 12 OPEN-DOC задачи. Старите находки не са повторно удостоверени чрез build/HIL и не са затворени. [Доклад с 12 групи находки и предложена структура](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/BOARD_EXAMPLES_SITE_PROPOSAL.md); актуалните hashes на новите evidence файлове са в [board-examples-inventory.json](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/board-examples-inventory.json), summary.evidence_sources. Няма промяна на firmware или оригиналните примери.
- 2026-09-19, интеграция на сайта: 278 каталогови записа, 20 адаптации, 37 API теми и 80 API примера. M2-DOC-001..006 са CLOSED с DONE-SITE-001; M2-DOC-007/012 са IN-PROGRESS, M2-DOC-008..011 остават OPEN-DOC. Шестте firmware и трите API задачи остават отворени. Няма firmware build/upload/HIL или промяна на оригиналните board examples.
