# RA4M2 Firmware - Done

Натрупващ се отчет за реално извършеното по VK_RA4M2.
Начало: 2026-09-18. Последна актуализация: 2026-09-22.
Отворени задачи: [FIRMWARE_TO_BE_DONE.md](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA4M2/FIRMWARE_TO_BE_DONE.md).

## 2026-09-22: RA-IRQ-001 кандидат, без хардуерно приемане

- Локална промяна: GPIO hard=False използва mp_sched_schedule; hard/fast/legacy ExtInt/RTC direct dispatch е запазен. GPIO teardown спира ICU източниците и чисти callback/ASM references преди GC sweep.
- База: `1c6357b5d11d7a3d2d3697202027655a67d7f33f` плюс приложен patch. Няма нов commit/tag/push.
- `tests/irq/run_host_tests.py`: старият dispatcher възпроизвежда дефекта; поправеният минава -O0 и -Os. Проверени soft/hard/fast, аргументи, queue saturation, exceptions, unregister/re-register, cleanup и RTC slot. Периферните регистри и Python/GC са host stubs, не HIL.
- Независим RA-BUILD-001: `machine_dac.c` огражда was_iq_stream със същия IQ_ADC guard като използването. Реалното тяло се компилира и изпълнява с IQ_ADC=0/1, -Werror; failed stop и запазването на IQ snapshot са проверени.
- ARM GCC 13.3.0: VK_RA4M2 с кварц, VK_RA4M2 без кварц и VK_RA6M5 са компилирани в нови `build-*-irq-*-20260922` директории. Build `1c6357b5d1-dirty`, дата 2026-09-22. Крайният ELF съдържа extint_callback -> mp_sched_schedule и extint_deinit.
- M2 linker издава warning за RWX LOAD segment; не е потиснат. Няма compile/link грешки в завършените builds.
- `tests/irq/hil_pin_irq.py` и README описват следващото приемане; mpy-cross ARM syntax проверката минава. Скриптът не е изпълнен върху платка.
- Старите публикувани HEX не са заменени. Flash/HIL/реална IRQ latency: NOT RUN. RA-IRQ-001 не е затворен.

## Исторически статус към 2026-09-20

НЯМА в обхвата на тази задача. Това не е твърдение за историята на целия порт.

- Променен C/firmware код: няма.
- Компилация на firmware: NOT RUN.
- Качване/достъп до платката: NOT RUN.
- HIL и физически измервания: NOT RUN.
- Commit/tag за firmware поправка: няма.

Нито M2-FW-001..007, нито M2-API-001..003 са приключени. Поправка на предупреждение или пример в сайта не затваря firmware дефект.

## Извършено: документация и инструменти

Този раздел е отделен от firmware. Описва извършени действия, не твърди пълна или безгрешна документация. M2-DOC-001..006 са приключени с DONE-SITE-001; M2-DOC-007..013 остават незавършени в TO_BE_DONE.

### DONE-DOC-001 - Създаден локален справочник

- Дата: 2026-09-18.
- Артефакт: [renesas_micropython_site](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/index.html).
- Извършено: отделен сайт за M2 с коментирани Python примери и източници; празни раздели за RA6M3, RA6M5 и RA4M1. Оригиналният LVGL сайт не е редактиран в тази задача.
- Последен записан browser отчет: 36 теми, 78 примера, desktop 1440x1000 и mobile 390x844. Това са размер и UI покритие, не доказателство за семантична пълнота.
- Доказателство: [qa/results.json](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/qa/results.json), timestamp `2026-09-18T20:02:42.813Z`.
- Firmware/build/upload/HIL: NOT RUN.

### DONE-DOC-002 - Допълнени API описания

- Дата: 2026-09-18.
- Извършено: коригирано memaddr като последен избран memory адрес; добавени I2CTarget IRQ изисквания и примери, UART flush/txdone, TouchPad cap параметри/валидация, IRQ приоритети и маскиране.
- Доказателство: [contracts.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/contracts.cjs).
- Ограничение: това не поправя C реализациите. По-дълбокият последващ анализ откри допълнителните M2-DOC-001..006 и M2-API-001..003; те не са затворени с този запис.
- Firmware/build/upload/HIL: NOT RUN.

### DONE-TOOL-001 - Коректно съобщение при отказ на копиране

- Дата: 2026-09-18.
- Извършено: copyText връща реален success/failure; отказ или exception в fallback не показват успех. Временният textarea се премахва.
- Доказателство: [app.js](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/app.js:19), [check-site.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/check-site.cjs).
- Проверено: реалното clipboard съдържание и принудителни fallback success/false/exception; тестовете са в browser отчета от `2026-09-18T20:02:42.813Z`.
- Firmware/build/upload/HIL: не се отнася; не е изпълнявано.

### DONE-TOOL-002 - Добавени структурни проверки на примерите

- Дата: 2026-09-18.
- Извършено: сравнение на изтегляния Python файл с показания код; регистър за UART/I2CTarget/TouchPad; AST проверка за Python синтаксис и наличие на методно извикване.
- Доказателство: [coverage.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/coverage.cjs), [check-examples.py](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/check-examples.py).
- Исторически изпълнен резултат в тази задача: 78 syntax files, 37 covered methods. Scope: трите класа; hardware и ASM compilation NOT RUN. Отчетът тогава е stdout, не отделен подписан/хеширан файл.
- Исторически към 2026-09-18 M2-DOC-006 остава OPEN; последващата корекция е в DONE-SITE-001. Тези проверки не доказват receiver тип, аргументен договор, правилна последователност, нов кадър или хардуерно изпълнение.

### DONE-TRACK-001 - Създадени постоянни регистри

- Дата: 2026-09-18.
- Извършено: FIRMWARE_TO_BE_DONE.md и FIRMWARE_DONE.md с постоянни ID, доказателства, приоритети, критерии и отделни нива на верификация.
- Основа: HEAD `1c6357b5d11d7a3d2d3697202027655a67d7f33f`, конкретни source hashes в TO_BE_DONE. Старият общ TODO.md е запазен.
- Проверка на регистрите: 28 локални файлови връзки съществуват; няма trailing whitespace; преброени 4 firmware дефекта, 3 API решения и 6 OPEN-DOC задачи. Това е проверка на файловете, не на firmware.
- Няма автоматизация или разрешение за бъдещо качване. При следваща работа по тези находки се обновяват двата файла.

### DONE-ANALYSIS-001 - Инвентар и структура за board examples

- Дата: 2026-09-19.
- Извършено: рекурсивен инвентар на 1864 файла; прочетени и статично анализирани всички 278 Python файла (55 670 реда), SHA-256, imports, calls, pins, сигнали за риск, предложено място в сайта и 9 двойки byte-identical дубликати. Останалите 1586 файла имат inventory-only записи.
- [Инвентар по файл](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/BOARD_EXAMPLES_INVENTORY.md), [JSON](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/board-examples-inventory.json), [доклад и предложение](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/BOARD_EXAMPLES_SITE_PROPOSAL.md).
- Команда: bundled Python изпълнява [audit_board_examples.py](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/analysis/audit_board_examples.py). Не import-ва и не стартира анализираните примери. CPython AST: 277 PASS, 1 FAIL (старият Python 2 host файл codeless-existing.py:281); отделно warning за escape в codeless.py.
- Извършен е ръчен source/manual преглед за 12 групи находки; таблици 19.6/19.7 от локалния examples PDF са визуално проверени. Установени са новите M2-FW-005/006, а не поправени. Твърдението за P100/P101 като RIIC0 се оттегля; manual ги определя като SCI0 SCL0/SDA0.
- Предложение: един каталог и пет раздела (учебен път, рецепти, проекти, лаборатории, API); отделни зависимости/host/архив; metadata, коментари, source hashes, status gates и първи 12 кандидата за адаптация. М3/М5/М1 остават празни.
- Ограничения: няма exhaustive API семантичен одит на всички извиквания, няма разпаковане на RAR или изпълнение на vendor tools. Статичната класификация е предложение, не одобрение. Ключове не се публикуват в отчета. Оригиналните примери, firmware C и runtime файловете на сайта не са редактирани.
- Firmware build/upload/HIL: NOT RUN. Приключен е анализът и предложението, не бъдещата интеграция и не отворените корекции.
- Уточнение от автора: кодът е негов и няма ограничение за публикуване. Премахната е предложената лицензионна пречка; техническите проверки остават отделни.
- Финална host проверка: съвпадат 278 Python hashes и 14 evidence hashes; инвентарът отчита уникално всички 1864 файла; 339 локални връзки в двата отчета и двата регистъра съществуват. Това е проверка на анализа и артефактите, не изпълнение на примерите. Няма лицензионни publication gates.

### DONE-SITE-001 - Учебен сайт, каталог и документални регресии

- Дата: 2026-09-19. Изпълнено след изричното искане за създаване на сайта, върху съществуващия локален справочник. [Начален файл](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/index.html), маршрут `#m2/learn`; статичен `file://`, без сървър.
- Създадени са учебен път с пет модула, каталог с 278 Python записа, рецепти, проекти, лаборатории и API справочник. Има търсене в двете колекции, комбинирани филтри, страници, постоянни URL адреси, локално отбелязване на прочетеното, копиране и изтегляне. М3/М5/М1 остават празни.
- [lessons.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/lessons.cjs) съдържа 20 отделни адаптации: коментари на български, подготовка, пинове, очакван резултат, упражнение и край/освобождаване. Останалите 258 записа са metadata, не публикуван изпълним код. Оригиналните файлове не са променени; build съпоставя всичките 278 source hashes с анализа.
- [catalog.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/catalog.cjs) запазва роли, рискове, оригинален hash и отделен hash за адаптацията. Конфигурационни ключове от оригиналите не се пренасят като код. Няма лицензионна пречка след уточнението на автора.
- Приключени M2-DOC-001/002: периодичният TouchPad пример няма ръчен service; ръчният не прекратява по ready и не обещава нов кадър. Fake-peripheral regression проверява 0 manual service calls в периодичния, 20 в ръчния при непроменен стар кеш, и прекратяване на периодичния timer при KeyboardInterrupt. Това не е scan completion или измерена честота.
- Приключени M2-DOC-003/004/005: [corrections.cjs](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/corrections.cjs) отделя diagnose от обикновено четене, описва CTSU reconfiguration/ownership, поправя raw RTC weekday на 5 за петък и документира AudioADC singleton. Текстовите regression проверки минават. M2-API-001..003 не са затворени.
- Приключен M2-DOC-006: build присвоява AST статуса след успешен checker. [qa/examples-static.json](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/qa/examples-static.json) съдържа hashes на checker, примерите, авторските входове и source/catalog manifests; методният договор има отделен hash. Обхватът е syntax/method-name, не receiver/argument/runtime проверка.
- Частично M2-DOC-007/012: коригирани са публикуваните I2C/RSPI насоки, добавени M2-FW-005/006 предупреждения и разграничение на host/fixtures/dependencies. Board headers, firmware mappings и останалите адаптации не са поправени; задачите остават IN-PROGRESS.
- Команди от директорията на сайта с bundled Node/Python: `node build.cjs`, `node check-contracts.cjs`, `node check-site.cjs`, `node check-catalog.cjs`. Build изпълнява `check-examples.py` и минава с 37 теми, 80 API примера, 20 адаптации и 78 source snapshots. Syntax: 80 + 20 файла; регистър: 37 метода от три класа.
- [Host regression](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/qa/contracts-results.json), `2026-09-19T20:52:53.088Z`: седем control-flow сценария PASS, плюс текстови и hash проверки. Fake machine/time, без C/FSP.
- [API browser QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/qa/results.json), `2026-09-19T20:59:26.892Z`: 37 маршрута, 80 displayed/downloaded примера, source anchors, clipboard и трите fallback изхода; 1440x1000 и 390x844. [Catalog browser QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/renesas_micropython_site/qa/catalog-results.json), `2026-09-19T20:59:09.346Z`: всички 278 detail routes, 20 downloads/hashes, филтри/URL/pagination, reading progress, clipboard, skip focus, изображение, mobile menu, празни платки; ширини 1440/768/390/320. Няма browser exceptions или document horizontal overflow в проверените изгледи.
- Реалното изтегляне през file:// първоначално не стартираше в Edge. Поправено е чрез Blob от вграденото съдържание; browser download events за led-blink.py и touch-2.py завършват успешно и байтовете съвпадат с файловете. Поправено е и междинното smooth-scroll движение при смяна на маршрута. Повторната проверка на 14 evidence hashes потвърждава непроменени source/manual входове; 52 връзки в регистрите сочат съществуващи файлове.
- Прегледани са desktop/mobile screenshots на учебния път, каталога и урока, включително dark тема. Успешният UI/host тест не удостоверява работа на платката. Firmware C, оригинални board examples и LVGL сайт: непроменени. Firmware build/upload/HIL: NOT RUN. Commit/tag: няма.

### DONE-SITE-002 - Премахнати контролни суми от учебния интерфейс

- Дата: 2026-09-20. По изрично искане на автора са премахнати SHA-256 полетата от всички 278 каталогови страници и заглавните части на 78 source HTML страници. Данните за тези суми вече не се включват и в публичния catalog payload в data.js; няма скрит или разгъващ се блок за тях.
- Промени: catalog-ui.js, build.cjs, coverage.cjs, README.md и браузърните регресионни проверки. Статичните проверки за съответствие на файловете остават отделени от интерфейса. Python примерите и firmware не са променени.
- Проверки PASS: build.cjs, check-contracts.cjs, check-site.cjs и check-catalog.cjs; 278 каталожни и 37 API маршрута, липса на показвани контролни суми, копиране и реално изтегляне. Прегледани са desktop/mobile изображения на states. Browser отчети: qa/catalog-results.json (`2026-09-19T21:07:37.089Z`) и qa/results.json (`2026-09-19T21:07:39.760Z`). Firmware build/upload/HIL: NOT RUN. Отворените firmware задачи не променят статуса си.

### DONE-SITE-003 - STEMKIDS, книги, видими файлове и проектна галерия

- Дата: 2026-09-20. Публикувано общо начало: [STEMKIDS](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/index.html). Авторските M2/LVGL директории и оригиналните книги/примери са запазени. `package.cjs` прави runtime копия вътре в STEMKIDS.
- Книги: Cortex-M R021, Часовник R022 и Nixie R013; 219 раздела, 183 изображения, 146 code блока и 74 примерни файла. Clock използва посочения пакет R020; Nixie архивът е извлечен от R013 PDF. `qa/books-import.json` отчита абзаците и изображенията; Word/PDF оригиналите не са редактирани.
- Всичките 278 M2 Python оригинала вече се показват и изтеглят. 20 адаптации са отделни; в 12 публикувани копия AppKey стойностите са нулирани. Премахнати са повтарящите се provenance/AST блокове от detail страниците. Краткият маркер за тестване/build не превръща host проверка в HIL.
- Общият каталог има 732 материала и 48 проекта, избор по платка, среда, библиотека и вид. Проектната галерия има визуални прегледи, филтри, сортиране и списък. Всички Renesas платки имат MicroPython/Arduino избор; това не е ново доказателство за конкретен build. Host инструментите и архивният ESP пример не са означени като работещ M2 код.
- Допълнени 16 resource страници: BMP581, AMG8833, MLX90640, DAC8571/DAC8830, дисплей и I2C буфери, двукамерни демота, OpenCV Prepared/вектори, SDR TRANCEIVER и SDR Lab. 35 Python копия минават AST, без import/изпълнение. Файловете са видими с copy/download; добавени са наличните лицензи. RA6M3, LAB, RadioOnly и OpenCV профилите са отделени.
- LoRa / LoRaWAN е в главната навигация с отделни филтри. OpenCV, камери, драйвери, LVGL, CMSIS-DSP, SDR и аудио са постоянно видими. Добавени 11 GitHub хранилища; произходът е проверен по локални metadata/remotes и публичните страници. Не е измислен отделен GitHub адрес за локалните AMG8833/SDR приложения.
- Регресии PASS: `check-contracts.cjs` (7 fake-peripheral control-flow сценария); M2 `check-site.cjs` (37 API теми/80 примера); `check-catalog.cjs` (278 original code страници); `check-books.cjs` (657 посещения, 74 actual downloads, 11 вътрешни anchors); LVGL `test_site.cjs` (162 official + 12 project, 0 browser errors). Няма нов firmware, COM/J-Link или HIL.
- Общите последни browser/resource/portable отчети са в [STEMKIDS qa](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/results.json), [resource QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/resources-results.json) и [portable QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/portable-results.json). Те описват конкретното проверено издание, не поведение на платката.
- M2-DOC-013 е нова отворена source находка: липсва общ cleanup за button_timer в DAC8830_DAC8571 примера. Няма промяна на оригиналния код. Предишните firmware/API backlog записи остават отворени. Commit/tag: няма.

### DONE-SITE-004 - Единен пример: схема, KiCad, Wiki и обратна връзка

- Дата: 2026-09-20. Общият компонент `stemkids_site/workspace.js` и CSS е
  включен в M2 примерите/API, LVGL, проектните ресурси и примерите към книгите.
  Има раздели за описание, код, схема-картинка, KiCad връзка и файлове,
  диаграми, изображения, Wiki, коментари и проблеми с build/статус.
  Липсващите схеми/проекти са означени като липсващи; не са подменени с измислени материали.
- `#examples` има галерия/списък, филтри и сортиране за 381 примера.
  M2 и LVGL каталозите имат визуални прегледи; използват се действителен код
  и наличните изображения. Кодът остава копируем/изтегляем. Дългите M2 блокове
  имат ограничена височина на екрана; CSS премахва това ограничение при печат.
- Допълненията са локални в IndexedDB, с JSON износ/внос и проверка за правилния
  пример. Няма автоматично публикуване или записване в файловете на сайта.
  Проверени са редакция/изтриване, reload, KiCad файлове/връзка, изображения,
  увеличение, export/import, XSS като текст, отказ на SVG, quota/отказ на
  storage и конфликт между табове. При отказ формата се запазва и няма фалшив успех.
- По последното уточнение на автора LoRa/LoRaWAN е под Библиотеки/Проекти,
  не отделна точка в главното меню. Старият `#radio` адрес води към библиотеките.
  Публичните GitHub референции вече са само tvendov/Vekatech:
  vkth_micropython, Arduino и uPy. Оригиналните лицензи/авторски файлове остават.
  Това заменя първоначалната навигация и списък от DONE-SITE-003.
- [Workspace QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/workspace-results.json):
  PASS, 498 посещения, ширини 1440/1024/768/390/320; `2026-09-19T22:35:58Z`.
  [Hub QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/results.json):
  PASS, 260 посещения. [Resource QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/resources-results.json):
  16 страници, 66 byte-identical реални изтегляния, 120 изгледа.
- Повторно PASS: M2 37 API теми/80 примера и реален clipboard fallback;
  278 каталожни страници; книги 657 посещения/74 downloads; LVGL 174 примера,
  0 browser errors; 35 resource Python файла с CPython AST. Прегледани са
  desktop/mobile галерията, допълненията/коментарите и тъмният LVGL изглед.
- [Portable QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/portable-results.json):
  PASS, отделно копие и изключен интернет, 753 посещения, 1156 локални ресурса,
  0 липсващи, 0 локални препратки извън копието и 0 мрежови заявки;
  `2026-09-19T22:36:38Z`. Външните справочни връзки не са runtime зависимости.
- Онлайн comments/issues са изрично отложени до избор на хранилище:
  STEMKIDS-SITE-001 в TO_BE_DONE. Няма нов firmware patch/build/upload/HIL,
  няма затворен firmware/API дефект, няма commit/tag.

### DONE-SITE-005 - Общи материали за всички Renesas платки

- Дата: 2026-09-20. RA4M2, FEMTO, NANO, RA6M3, RA6M5 и RA4M1 вече
  използват един и същ набор от 578 материала и 14 библиотечни раздела.
  Общо в каталога остават 732: 578 общи Renesas материала + 119 само за
  FPGA + 34 host инструмента + 1 архивен ESP пример. Това е подредба на
  съдържанието, не 578 потвърдени изпълнения на всяка платка.
- `stemkids_site/build.cjs` отделя `catalogBoards` (видимост) от `boards`
  и `target` (конкретна реализация). `app.js` използва новата видимост във
  всички board/library/environment филтри. LVGL не е ограничен до M3,
  machine/радио не са ограничени до M2; всички Renesas входове имат общите
  библиотеки и MicroPython/Arduino. Езикът и оригиналният код са запазени.
- Страниците показват „Реализация“, а положителният маркер „Тествано“ включва
  конкретна платка и build. LVGL `build_examples_data.py` взема платката
  от съществуващия `HIL_RESULTS.md`, без да създава нов HIL резултат.
  Пинове, pRGB/LCD, OpenCV QSPI, RadioOnly и LAB изискванията остават явни.
- Проверени са импортите в AMG8833/example.py и MLX90640 55 degre/example.py:
  началното четене използва machine.I2C и сензорния драйвер, не LCD/LVGL.
  `resources.cjs` вече отделя тези изисквания от графичните демонстрации.
  M3/M5/M1 и FEMTO/NANO изборите в M2 reader-а водят към общия каталог,
  вместо към празна страница. Board-specific API код не е измислян или дублиран.
- Команди: bundled Python `examples/lvgl_micropython_site/build_examples_data.py`,
  bundled Node `stemkids_site/build.cjs`, `check-boards.cjs`, `check-site.cjs`,
  `check-resources.cjs`, `check-workspace.cjs`, `check-portable.cjs`;
  Renesas `check-site.cjs` / `check-catalog.cjs`; LVGL `test_site.cjs`.
  Старите QA очаквания за празни платки и Arduino guide на първа страница
  са заменени с проверки за общ каталог, правилен филтър и намиране на guide.
- [Board QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/boards-results.json):
  PASS, 474 посещения, 6 Renesas варианта, всички 14 библиотеки, ширини
  1440/768/390/320, `2026-09-19T22:49:28Z`. Проверени са оригиналната
  реализация/test board/build, gallery/list, reload, FPGA/host/ESP границите
  и пренасочванията в пакета. Няма мрежови заявки или browser exceptions.
- [Hub QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/results.json):
  PASS, 260 посещения, `2026-09-19T22:51:21Z`.
  [Resource QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/resources-results.json):
  PASS, 16 страници, 66 действителни byte-identical изтегляния, 120 изгледа,
  `2026-09-19T22:51:42Z`. M2: 37 API теми/80 примера и 278 catalog routes
  PASS; LVGL: 174 примера, 0 browser errors. Прегледани desktop/mobile снимки.
- [Workspace QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/workspace-results.json):
  PASS, 498 посещения, `2026-09-19T22:53:22Z`; Wiki, коментари, проблеми,
  KiCad/изображения и локалните записи запазват идентичността на примера.
  [Portable QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/portable-results.json):
  PASS, 753 посещения, 1156 локални ресурса, 0 липсващи, 0 локални връзки
  извън копието, 0 мрежови заявки; `2026-09-19T22:53:57Z`.
- По новото искане на автора е добавена M5-CV2-001 (OPEN-PORT) в
  [TO_BE_DONE](C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA4M2/FIRMWARE_TO_BE_DONE.md).
  VK_RA6M5 има QSPI конфигурация; преносът на OpenCV QSPI остава неизпълнен.
  Не е отбелязан като DONE и не е прехвърлено M3 хардуерно доказателство към M5.
  Firmware patch/ARM build/upload/HIL: NOT RUN. Няма затворен firmware/API
  дефект, commit или tag. Онлайн GitHub обратната връзка остава отложена.

### DONE-SITE-006 - Изображения на платките и име „Разширения“

- Дата: 2026-09-20. `stemkids_site/board-media.cjs`, `app.js` и `styles.css`
  добавят собствени изображения над съдържанието на платката и в общия избор.
  RA4M2 показва отделно FEMTO и NANO. Рендерите са от съществуващите GLB
  модели, извлечени от KiCad STEP, с Three.js, не измислена геометрия.
- VK-RA6M3 и VK-RA6M5 използват официални Vekatech перспективни снимки,
  означени като снимки. Четирите Vekatech платки имат локални 3D PDF в ZIP
  архиви. ZIP е нужен за действително изтегляне при `file://`; директният PDF
  се отваря в браузъра. Проверени са точните байтове в архивите и downloads.
  Cmod S7 използва съществуващия учебен 3D модел от книгата с явен етикет.
- Всички нови изображения и архиви са в `assets/boards/`, а произходът е в
  `sources/board-media.json`, извън учебния интерфейс. За M1 не е поставена
  чужда платка: състоянието е „снимка предстои“. Липсващото M1 изображение
  и статичните M3/M5 3D рендери остават OPEN-DOC като STEMKIDS-SITE-002.
  Следователно пълното искане за 3D рендер на всяка платка НЕ е затворено.
- По последното уточнение публичното име „Renesas разширения“ е сменено с
  „Разширения“ в `build.cjs` и генерираните данни. Заглавията, филтрите,
  изборът на библиотеки и връзките от платките използват общите данни.
  ID `renesas` е запазен за съвместимост на адресите. Търсене в трите сайта
  не открива стария публичен надпис.
- Команди: bundled Node `build.cjs`, `check-board-media.cjs`, `check-site.cjs`,
  `check-boards.cjs`, `check-portable.cjs`. Python stdlib `zipfile` потвърди
  че четирите архива съдържат точния оригинален PDF, без промяна.
- [Media QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/board-media-results.json):
  PASS, 44 посещения, 4 действителни изтегляния, 5 непразни изображения,
  1440/768/390/320 px, `2026-09-19T23:08:10Z`. Първи екран, object-fit,
  точен asset/alt, очаквано липсващ M1 файл, стар адрес с ново заглавие,
  0 мрежови заявки; desktop/mobile и dark screenshots са прегледани.
- Hub QA: PASS 260 посещения, `2026-09-19T23:08:26Z`.
  Board QA: PASS 474 посещения, `2026-09-19T23:08:45Z`.
  [Portable QA](C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site/qa/portable-results.json):
  PASS 760 посещения, 1165 локални ресурса, 0 липсващи, 0 локални връзки
  извън копието, 0 мрежови заявки; `2026-09-19T23:09:25Z`.
- Firmware patch/build/upload/HIL: NOT RUN. M5-CV2-001 остава OPEN-PORT.
  Не е затворен firmware/API дефект; няма commit/tag.

### DONE-SITE-007 - FEMTO и NANO, без трети вход RA4M2

- Дата: 2026-09-20. Последователните уточнения на автора заменят
  междинната групировка под основен раздел RA4M2. Основните имена са
  FEMTO и NANO. Няма трета основна карта RA4M2 или допълнителен слой
  за избор на тип чип. Останалите платки са с имената си: VK-RA6M3,
  VK-RA6M5, NANO R4, Cmod S7. Учебното съдържание не се разделя по чип.
- В `stemkids_site/build.cjs` старият `m2` запис е `catalogOnly` и се
  нарича „FEMTO / NANO“ за старите адреси. `app.js` не го показва като
  отделна карта или избираема опция. Ако стар адрес съдържа `board=m2`,
  скритата selected опция запазва филтъра и точния брой резултати.
  Не е заменена по предположение с конкретна платка и не е разширена
  до host/FPGA материали. ID-тата на съдържанието и тестовете са запазени.
- MicroPython reader `app.js` / `index.html` използва същите видими имена
  и скрит стар `m2` избор. Публикуваното копие е обновено с `build.cjs`.
  FEMTO/NANO водят към общия каталог с избраната MicroPython среда.
  Изображенията и точните target имена остават към конкретните платки.
- Host/browser проверки: `check-board-media.cjs` PASS, 45 посещения,
  4 downloads, 5 изображения, 4 ширини, `2026-09-19T23:19:06Z`;
  `check-site.cjs` PASS 260 посещения, `2026-09-19T23:19:22Z`;
  `check-boards.cjs` PASS 474 посещения, `2026-09-19T23:19:42Z`.
  Renesas `check-site.cjs`: PASS 37 API теми, 80 примера, desktop/mobile,
  `2026-09-19T23:19:48Z`. Проверени са списъкът без третата опция,
  точните заглавия, общият каталог, старите URL и мобилното меню.
- `check-portable.cjs`: PASS 760 посещения, 1165 локални ресурса,
  0 липсващи, 0 локални връзки извън копието, 0 мрежови заявки;
  `2026-09-19T23:20:25Z`. Прегледана е окончателната галерия с имената.
- STEMKIDS-SITE-002 остава отворен за липсващото M1 изображение и
  статичните M3/M5 3D рендери. M5-CV2-001 остава OPEN-PORT.
  Firmware patch/build/upload/HIL: NOT RUN; няма commit/tag.

### DONE-SITE-008 - Два FEMTO проекта, DFRobot, схеми и GitHub

- Дата: 2026-09-20. В STEMKIDS са добавени локално създаване/редакция на
  проекти и избираемо GitHub публикуване: отделно хранилище или PR към общо.
  Първоначалният собственик е tvendov. Токенът не се записва в проектите
  или browser storage. Общите онлайн коментари/issues остават отложени
  като STEMKIDS-SITE-001; това публикуване не ги затваря.
- Авторските „Детски метеобалон“ и „Меч на джедай“ са версия 2, и двата
  конкретно с FEMTO по последното указание на автора. Общата видимост на
  библиотеките за останалите платки не е ограничавана. LoRa е само за
  балона. DFRobot: SEN0667 BMP581, SEN0385 SHT31 и TEL0157 GNSS за балона;
  SEN0250 BMI160 за меча. Описани са единици, свежест/валидност и границите
  на моделите; тези модели не са готови драйвери или хардуерно измерване.
- `projects/*/` съдържат блокови и breadboard PNG схеми, коментиран код,
  инструкции и непроменени локални FEMTO pinout/schematic PDF файлове.
  Схемите използват SoftI2C P001/P002. Мечът използва LEDOUT през вградения
  преобразувател и първия бордови RGB пиксел, управлявани с P112/P500.
  Бордовият пиксел е индекс 0; външните са 1...5 в началния опит.
  VLED е изход, не вход за външно захранване. SCI2 конфликтът с UART(2)
  и SPI(2) е описан. Дълга лента и аудио силов монтаж не се представят
  като проверени. Изображенията могат да се разглеждат увеличени в сайта.
- `build-project-wiring.cjs`: 18 връзки за балона и 13 за меча; проверки
  на модела за заети отвори, свързаност и липса на смесени мрежи. Това
  не е ERC или тест на реален breadboard. Прегледани desktop/mobile PNG.
- Host проверки: Python unittest 9 за балона и 14 за меча, PASS; общо 23.
  `check-projects-core.cjs` PASS, 27 проверки; `check-projects.cjs` PASS,
  40 mock заявки, включително публикуване, persistence, XSS и конфликти.
  `check-authored-projects.cjs` PASS след публикуването: 6 responsive
  посещения и 2 byte-identical изтегляния; и двата target записа са FEMTO.
- `build.cjs` PASS: 839 записа. `check-site.cjs` PASS, 260 посещения,
  `2026-09-20T00:17:42Z`. `check-portable.cjs` PASS, 868 посещения,
  1366 локални ресурса, 0 липсващи, 0 локални връзки извън копието,
  0 мрежови заявки, `2026-09-20T00:20:13Z`. Отчети: `stemkids_site/qa/`.
- Публикувано с `publish-project-seeds.cjs --publish --public` в
  [балон](https://github.com/tvendov/stemkids-weather-balloon) и
  [меч](https://github.com/tvendov/stemkids-jedi-lightsaber).
  `verify-project-publications.cjs` независимо потвърди всички 19 и 23
  проектни файла съответно чрез обратно изтегляне и сравнение на байтовете,
  както и public достъп без удостоверяване. Запазените publication receipts
  са в `stemkids_site/sources/project-publications.json`. Това не е
  публикуване на целия статичен сайт на интернет домейн.
- Firmware patch/ARM build/upload/HIL: NOT RUN. Маркерът остава „Тествано:
  Не“, без избран изпитан MicroPython build. Следващите физически интеграции
  са отделени като STEMKIDS-PROJ-001, не като нови доказани firmware дефекти.
  Не са затваряни M2 firmware/API и M5-CV2-001 задачи.

### DONE-SITE-009 - Редактируеми проекти, изисквания и коригиран FEMTO UART

- Дата: 2026-09-20. Авторските проекти са версия 3. „Редакция“ създава
  локално работно копие със същия ID и publication receipt, без дублиране
  в каталога. Съществуващите локални редакции не се презаписват от seed.
  Импортът и копирането остават с нов ID и без наследено remote ownership.
- Редактируеми са описанието, Wiki, кодът и Markdown документите;
  изображенията/PDF могат да се заменят. „Запази локално“ не прави мрежова
  заявка. GitHub публикуването остава отделно, с удостоверяване, repository
  identity/head проверки и защита от конкурентна редакция.
- Изискванията имат REQ ID, приоритет, критерий за приемане, решение,
  история и филтър. Статуси: Предложено/Потвърдено/Отказано/В работа/Изпълнено.
  Отказът изисква причина. Статусът на изискване не променя маркера за
  хардуерен тест. Двата проекта имат по шест начални изисквания.
- Коригиран балон: TP4/P408 = RXD3 и TP6/P409 = TXD3 по RA4M2 Table 19.9,
  следователно UART(3)/SCI3. Wio-SX1262 запазва LoRaWAN SPI(3)/SCI9:
  SCK P111, MOSI P109, MISO P110, CS P206, NRST P001, BUSY P002,
  DIO1 P015 и RF_SW P100. Няма хардуерен конфликт SCI3/SCI9 и няма
  SoftSPI замяна. SoftI2C на датчиците вече е P302/P301, не P001/P002.
  Това заменя картата на балона от историческия DONE-SITE-008.
- Установена, но НЕ поправена M2-FW-007: погрешни UART P408/P409 таблици,
  липсваща UART3 board/IRQ конфигурация. femto_radio.py блокира UART3
  helper-а по подразбиране до проверен build; няма измислени tx/rx kwargs.
  Radio helper-ът не извършва join/send или factory reset на LoRaWAN NVM.
- Меч: два външни бутона P015/P100 към GND с pull-up, независим debounce
  и отпускане при старт. BOOT/RESET не се използват за управление.
  Предложен аналогов DFR0119-O/PAM8403 с входно разделяне/затихване,
  отделен 5 V клон и говорител между мостовите L+/L-. Схемите и
  AUDIO_BUTTONS_BG.md разграничават това предложение от изпитан монтаж.
- Обновени блокови и основни breadboard PNG; добавени breadboard-radio.png
  и breadboard-controls.png. Проверени са моделирани връзки, заети отвори
  и липса на смесени jumper мрежи. Визуално прегледани PNG и responsive UI.
  Проверката на моделите не е ERC, проверка на footprint или хардуерен тест.
- Python unittest: 15 за балона и 19 за меча, общо 34 PASS.
  check-projects-core.cjs: 29 основни assertions плюс validation/rejection
  случаи PASS; check-projects.cjs: 40 mock API заявки PASS.
  check-project-management.cjs PASS, повторен след публикуване: same-ID
  checkout, история/отказ, редакция на код/Markdown, замяна на изображение,
  offline save, mock publish в същото repo, XSS и конкурентна редакция;
  viewports 1440/390/320. Няма реални GitHub writes от тези тестове.
- check-authored-projects.cjs PASS след публикуване: 6 responsive посещения
  и 2 точни byte downloads. build.cjs: 839 записа. check-site.cjs PASS:
  260 посещения; check-workspace.cjs PASS: 500; check-portable.cjs PASS:
  868 посещения, 1367 локални ресурса, нула липсващи, извън сайта или
  мрежови заявки. Отчети и screenshots: stemkids_site/qa/.
- Публичните съществуващи tvendov/stemkids-weather-balloon и
  tvendov/stemkids-jedi-lightsaber са обновени с версия 3.
  verify-project-publications.cjs потвърди всички 23/27 пакетни файла
  чрез обратно изтегляне и byte сравнение, както и анонимен public достъп.
  Това не е интернет hosting на целия статичен сайт.
- Firmware patch/ARM build/upload/HIL: NOT RUN. Маркерът остава
  „Тествано: Не“. M2-FW-007 и STEMKIDS-PROJ-001 остават отворени;
  останалите firmware/API задачи не са затваряни.

## Правила за следващ завършен запис

1. Използвайте същия ID от TO_BE_DONE; добавете дата, точния обхват и описание на действителната промяна.
2. Посочете patch/файлове и commit/tag само ако съществуват; не създавайте commit по подразбиране.
3. Запишете точните команди и резултати, build конфигурацията, идентичността на входовете и firmware артефакта. При HIL добавете идентичност на платката и лог.
4. Отделете source/host/ARM build/upload/HIL; липсващото се маркира NOT RUN или NOT VERIFIED с причина.
5. Маркирайте първоначалния запис CLOSED едва след приемане на критериите и добавете връзка към този отчет. Запазете историята; при регресия отворете повторно ID с дата.

Шаблон за firmware запис:

```text
ID:
Дата:
Обхват/платка:
Промяна и източници:
Commit/tag (ако има):
Source/host tests:
ARM/build и hash на firmware:
Upload:
HIL/измерване:
Оставащи ограничения:
Покрити критерии за CLOSED:
```
