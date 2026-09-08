# VK_RA6M3 done changes

Постоянно правило от 2026-09-07: **след всяка работна итерация** се обновяват
този файл и `ra6_sdr_next_steps.md`, без напомняне. Записите разграничават
анализ, реализация, хост тест, build, качване и физическо измерване.

## 2026-09-08: реалният MIC AM TX отказ — 16383-B LUT алокация

Без reset е прочетено от работещото приложение: AM, `_inj_source=0`,
`_inj_on=False`, `TX start: MemoryError('memory allocation failed, allocating
16383 bytes',)`. След checked rollback състоянието е RX. Това е MIC AM, не
FILE AM от предишната проверка. Предишният FILE PASS не покрива този вход.
Опитът за общата свободна памет не даде валидна стойност поради изгубени
символи в UART командата; не се прави заключение за размер на фрагментация.

Причината за заявката е `machine_tx.c`: 8192 B таблица + 8191 B запас за
подравняване. Поправката премахва нуждата от таблицата за opt-in AM с
`audio_gain` (точно API-то на приложението), без нов постоянен RAM буфер:

- `ra_tx_hw.h`: MIC AM с audio_controls използва съществуващия C ADC callback.
- `ra_tx_core.c`: AM LUT се заявява само за legacy DTC варианта; MIC/FILE
  с новия интерфейс дават bytes=0, alignment=1. AM формулата не е променяна.
- `ra_tx_hw.c`: AM gain/depth/level се публикуват атомарно между семплите,
  както FILE/SSB; няма stop/rebuild/start за плъзгач. Коментарът различава legacy AM.
- `test_tx_audio.inc`: MIC без таблица, legacy DTC запазен, всички ADC кодове
  и 101 нива. Общият C набор: 18 групи / 8403968 проверки PASS.
- `test_audio_native_control.py`: реалният setter, MIC/FILE AM/SSB без restart,
  валидация и stopped/faulted състояния PASS.
- `test_am_mic_dispatch.py`: реалните C ADC callback и sample body с регистрови
  заместители; 4096 ADC кода, mute, FILE/legacy/stopped guards PASS.
- `README_IQTX_BG.md`: MIC и FILE използват C формула; старият API запазва
  LUT. Отчетена е цената: C работа на всеки MIC семпъл, а не нулев CPU път.

Общ RadioOnly build PASS, OpenCV OFF, без LAB, -j16, native MinGW Python.
Text1591101 B / BSS649784 B; heap281600 B (без промяна). BIN1591088 B,
SHA-256 `634e60a50e83a88952a88ca6584f8d8e24b4d717590d1e5eaa05a1b340d9b162`.
Предупрежденията за generated LVGL unused functions и RWX segment остават.
Качен е само вътрешният firmware; readback PASS. Приложението и QSPI не са
презаписвани, валидният dataflash запис и резервният internal tail са проверени.
Архив: `backups/am-mic-no-lut-1120000058-20260908-173300/`.

Цифров MIC HIL: два успешни AM старта при запазените 577500 Hz;
`file_source=False`, `cpu_dsp=True`, `lut_bytes=lut_allocation_bytes=0`,
`actual_rate=43988.27`, двата DAC enabled. Първият пълен контролен цикъл
мина LEVEL0/40, depth75%, AF gain120%, връщане RX. 239501 C семпъла,
109 AF кадъра; max283 / budget2728 цикъла; DSP clips/deadline/errors=0.
Това време е само C sample body, не целият ISR. Аналогова форма не е измерена.

Трите последователни цикъла НЕ са обявени за PASS: първи опит спря на
`wait for receiver tuning before TX`; други опити завършиха с UART EOF timeout,
включително след втория успешен MIC старт. И без menu stress се наблюдава
RX tuning guard >5 s. Причината за тези отделни откази остава непотвърдена;
няма доказателство, че всеки timeout означава замръзнал MCU. След всеки RAM
тест е изпълнен normal J-Link reset. Тестовете не се записват във flash.

Отделният FILE регресионен тест на новия firmware завърши PASS за
AM→USB→LSB→FM: LEVEL0/40, Si5351 ×1 регистри, неизменен FILE owner,
AF кадри, 0 DSP clips/deadline misses и 0 нови underruns в 2-s установени
интервали. Преходните FILE underruns са съответно 35/186/187/303 и остават
видими в отчета. Това не превръща незавършения MIC lifecycle тест в PASS.
След reset производственото приложение е стартирано: `PRODUCTION_HOME RX
rx True error None`; портът е освободен. Протоколът с действителните частични
MIC резултати, отказите и FILE резултатите е
`ports/renesas-ra/tests/tx/results/am-mic-no-lut-20260908.log`.

## 2026-09-08: AM/SSB TX аудиоконтроли — втори HOME етап

Реализация и 11 host команди PASS. Общ RadioOnly build PASS, OpenCV OFF,
USER_C_MODULES празен (без LAB), -j16, /mingw64/bin/python3.exe. Новите native
символи са в ELF. Text 1591109 B, BSS 649784 B; linker heap 281600 B.
BIN SHA-256: `5f5fd456459de045b2b19f7f443607b4d5844b6dc20fd291290443979c0270ba`.
Първият build спря за volatile указателя към AM clip брояча; корекцията
използва локален counter и публикува резултата в status. Повторният build PASS.
Има предупреждение за RWX LOAD segment; не е премахнато в този етап.

Промени по файлове:

- `sdr_single.py`: AM AF gain / depth / LEVEL, USB/LSB AF gain / LEVEL в
  HOME и BACKEND; FM запазен. Заявките към native setter са worker-side и
  се обединяват; FILE позицията не се нулира от плъзгач. RX VOL не се използва.
  AM-only контрола не остава активна в SSB; старият BACKEND се затваря при
  промяна на модулацията. txdepth се запазва с останалите TX настройки,
  отложено след връщане RX. R:USB/R:LSB задават FILE decoder gain 75%.
- `machine_tx.c`: opt-in audio_gain/am_depth, file_gain преди decoder limiter,
  audio_configure(), status audio_gain/am_depth. Старият native AM/SSB API
  без audio_gain запазва поведението си. UI изисква съответстващ firmware.
- `ra_tx_hw.h`: отделни AF gain/depth и opt-in флаг, декларация на setter.
- `ra_tx_core.h`: общ AM sample kernel за FILE и DTC LUT.
- `ra_tx_core.c`: нормализирана AM формула; LEVEL не променя depth; SSB gain
  преди общия I/Q limiter без reset на Hilbert историята. AM FILE няма LUT:
  заявката 8192+8191=16383 B е премахната. MIC AM запазва таблицата.
- `ra_tx_hw.c`: FILE AM използва C формулата; MIC AM спира проверено преди
  обновяване на таблицата и после стартира; FILE AM/SSB публикуват три скалара
  атомарно без рестарт. Отказ при MIC stop/start води към checked cleanup.
- `test_tx_audio.inc` / `test_tx_core.c`: нови AM/SSB проверки; общо 18 групи,
  8809464 C проверки PASS. Legacy AM/FM/CW/SSB тестовете остават PASS.
- `test_audio_native_control.py`: действителният C setter със заместени
  периферии — ред на операциите, откази, no-restart FILE и валидация PASS.
- `test_sdr_audio_controls.py`: действителни UI методи/callback-и — контекст,
  0/100%, обединяване, FILE owner, отложен save и rollback PASS.
- `test_sdr_fm_controls.py`: новите helper-и и persistence txdepth; старият
  firmware без audio_configure остава read-only. 512-B record граница PASS.
- `test_sdr_tx_home.py` / `test_sdr_tx_switch.py`: нови constructor параметри,
  отделен decoder gain/RF mode; ownership/failure/clock тестовете PASS.
- `README_IQTX_BG.md`: значения на AF gain/depth/LEVEL, диапазони, съвместимост,
  FILE запас, цифрово ниво срещу физическа мощност и кратък MIC AM преход.

Останалите host проверки: 8 UI договора, navigation/tester договори,
TX FILE AF 1465 проверки и RX DAC 127532407 assertions / 99200 моделирани
DMA периода — PASS. Това не доказва аналогови форми или ISR timing.

По последното „тествай моментално“ е извършено качване на запомнената цел
J-Link 1120000058 / COM25. Пълният текущ QSPI архив е в
`backups/tx-audio-1120000058-20260908-165554/`; boot/main и записите се пазят.
Firmware и приложение са записани и прочетени обратно успешно; 58 оригинални
файла са запазени, както и последните 4 MiB QSPI и валидният dataflash запис.
Първи HIL: AM FILE стартира, LUT allocation=0, двата DAC enabled, 0 deadline
misses; LEVEL0 дава I=Q=2048, LEVEL40 връща amplitude409. Първият тест спря на
растящия FILE underrun брояч след UI преход/контрола (4776 общо). Това не е
PASS за целия тракт. Следващият тест отчита отделно преходните и устойчивите
underruns; вече измереният отказ не се изтрива от историята. След теста е
изпълнен normal J-Link reset.

Последващо искане: старите честотни/спектрални бинове да не проблясват.
В `SdrUi._build_receiver` вече не се създават 27-те legacy LVGL bar widgets и
ключовете spectral-bin-*. Премахнат е и demo publish при startup/stop;
`paint_spectrum()` не генерира измислен спектър. Живите данни остават през
native `_paint_bars`. Host UI договорите, специалният startup тест и
mpy-cross PASS. Качено е само новото приложение, не втори firmware.
Два първи UART опита спряха при read-only команди с повреден текст
(ImportError `o`, SyntaxError). Преди тези откази няма rename/подмяна на файл.
Бавният paced upload е прекъснат преди rename; неговите `.new` staging файлове
са презаписани при успешния опит със safe boot и raw-paste. Старото приложение
е запазено като `sdr_single.pre-nobins.*`. SHA-256 readback преди rename PASS:

- `/flash/sdr_single.py.source`: 314869 B,
  `a16d0a639aa646134fca59b1931ec1da9eb24a528c73d77741a28a32022fbb61`.
- `/flash/sdr_single.mpy`: 88531 B,
  `6106a193fdb36db7d9546f95f757f61254094009071f97ded693a37cd73454d9`.

Повторен цифров HIL на каченото приложение: `NO_LEGACY_BINS_RUNTIME_PASS`
(bins=(), няма spectral-bin-*), AM → USB → LSB → FM, `TX_AUDIO_HOME_BOARD_PASS`.
За всеки режим: HOME gains/BACKEND контекст, Si5351 регистров план ×1 при
3.500 MHz, LEVEL 0 → 40 без смяна на FILE owner, напредващи AF кадри/семпли.
При LEVEL0 I=Q=2048; AM40 amplitude409, останалите amplitude819.
AM FILE status потвърди lut_allocation_bytes=0. Дълбочината и AF gain имат
host проверки; този HIL изменя LEVEL, не измерва аналогова AM дълбочина.

| Режим | Реален sample rate | AF кадри в контролния интервал | DSP clips / deadline misses | FILE underruns при прехода | Нови underruns в следващи 2 s | Най-дълъг DSP / бюджет, цикли |
| --- | --- | --- | --- | --- | --- | --- |
| AM | 24000 S/s | 90 | 0 / 0 | 208 | 0 | 288 / 5000 |
| USB | 12000 S/s | 86 | 0 / 0 | 270 | 0 | 4170 / 10000 |
| LSB | 12000 S/s | 83 | 0 / 0 | 381 | 0 | 4170 / 10000 |
| FM | 24000 S/s | 90 | 0 / 0 | 643 | 0 | 798 / 5000 |

Преходните броячи не са скрити от PASS: устойчивият интервал започва след
1200 ms изчакване след LEVEL40. Това е кратка цифрова проверка, не дълъг soak
и не доказателство за аналогов DAC/CLK/RF. Аудио отклоненията при меню остават.
Архив: `backups/tx-audio-1120000058-20260908-165554/audio-hil.log`,
`no-legacy-bins/deployment.json`, `production-home.log`; Git протоколът е
`ports/renesas-ra/tests/tx/results/tx-audio-board-20260908.log`.
Тестове не са инсталирани във flash. След HIL е изпълнен normal J-Link reset.
Първото последващо стартово UART предаване загуби текст и спря преди start;
след еднократен safe boot стартирането завърши: `PRODUCTION_HOME RX
legacy_bars 0 rx True`. Приложението е оставено в RX HOME; портът е освободен.

## 2026-09-08: LAB общ ADC/DAC адаптер, без синхронен такт

В `sdr_lab` е добавен `ra_lab_session_hw` към действителните ADC/DAC
драйвери: обща логическа резервация, една IQ фаза/таблица, общ отказ
към RMS и запазена ADC/DAC собственост при частично неуспешен стоп.
Използват се два независими AGT таймера, не общ фазов такт. ADC се
стартира преди DAC заради нулирането на DWT брояча; началният непълен
входен блок се отхвърля. Това не е калибрация на фазата/установяването.

Портови редакции: `ra/ra_iq_adc.c/.h`, нов `ra/ra_iq_capture.h`,
`ra/ra_dac.c/.h`. DAC двойката получава алтернативен `fill_pair`
callback след презареждане на двата DMA. Старият `fill` път и
самостоятелният Python IQGenerator са запазени. Няма LAB редакции
в текущите TX FILE/FM алгоритми или SDR интерфейса.

Проверки: 82 LAB C групи при `-O0/-O2`, 53 проверки върху реалните
функции за резервация/стоп, 291 върху реалния DMA обработчик,
43 статични договорни проверки, GCC анализ и осем ARM обекта PASS.
Периферните функции/регистри във всички host тестове са заместени.

LAB build в общия `build-VK_RA6M3`: PASS на 08.09, 12:04:07 UTC,
с повторна проверка на избраните източници/артефакти по SHA-256.
BIN=1 601 816 B, SHA-256
`651CE03DB2D342C1835E70041FE1E759589EDFEF72220D2D2FF09352F6A49912`.
Съществуващите Measurement/IQGenerator символи са запазени.
Новите `ra_lab_session_hw_init/poll` са в компилирания обект, но не
в крайния ELF: все още няма използващ ги Python/C вход. Това не е
готов общ инструмент във firmware. Подробности: `sdr_lab/done.md`,
`SESSION_HW_API.md`, `build/iq_firmware_audit.json`.

Няма нов RadioOnly контролен build, качване, COM/J-Link, reset,
HIL, физическо измерване или commit. Общата build директория вече
съдържа този LAB образ; това не променя образа на платката. Преди
следващ RadioOnly build/качване профилът се избира и проверява отново.

## 2026-09-07: LAB C сесия за IQ генератор и RMS

В `sdr_lab` общата C сесия вече приема наличното IQ ядро, а не само
едноканален генератор. Договорът е версия 2: една изходна заявка с I/Q
буфери и общ индекс. Използва съществуващия MeasurementEngine и TX
таблица/интерполация. Добавен е `ra_lab_session_output_fail` за общо
спиране и обезсилване на RMS при периферен отказ, включително при
затваряне. Това е C връзка с тестов периферен адаптер, не реализиран
общ физически ADC/DAC поток или нов Python обект.

Проверки: 72 LAB C групи при `-O0/-O2`, включително 10 стари MONO и
9 нови IQ Session групи; GCC анализ и седем Cortex-M4 обекта PASS.
34 статични IQ/Measurement/TX проверки PASS. Тестовете обхващат двата
изхода, RMS за I/Q, откази, два потребителя, стари извиквания и 100
старта. Размерът на общия C контекст е 1256 B в host теста, не измерена
RAM на платката. Подробности: `sdr_lab/done.md` и `SIGNAL_SESSION_API.md`.

Не са редактирани портовите C/Python източници. Няма firmware build,
COM/J-Link, flash, reset, UI или физическа проверка в тази LAB стъпка.
При повторен прочит общият `build-VK_RA6M3/firmware.elf` вече не съвпада
с хеша от предходния IQ отчет. Старият PASS по-долу се отнася за
тогавашния образ, не удостоверява текущите общи артефакти. Те не са
прекомпилирани или заменени от тази C итерация. Съпътстващият SDR
план вече отчита отделен RadioOnly/TX AF build; неговото deployment
не е проверявано от LAB.

## 2026-09-07: LAB IQGenerator, само код и компилация

В отделния `C:\Users\teodor\Desktop\stem\sdr\sdr_lab` са добавени
`machine.IQGenerator` и двуканален DAC адаптер. Използват общите TX
`ra_tx_core_build_iq_lut/iq_sample` и съществуващия DAC/AGT/DMA драйвер.
В `ra_dac.c/.h` има подготовка без автоматичен старт, общ старт със
запазени I/Q данни, общо презареждане на двата DMA преди генериране и
спиране на двойката при установен отказ/пропуснат срок. Старият SDR
`ra_dac_stream_sync_pair()` запазва началната тишина и 512-пробният RX
поток не е преработен от LAB. Добавени са защити за заетата двойка в
DAC, AudioMixer и IQTX, регистрация и проверено soft-reset почистване.

Проверки: 63 LAB C групи при `-O0/-O2`; 237 проверки върху действителния
общ DMA обработчик със заместени регистри/FSP; 11 IQ и 12 Measurement
статични проверки; 15 оригинални TX C групи (7 055 959 проверки),
11 актуализирани LAB TX проверки, 25 TxDemo теста и `mpy-cross` PASS.
RX `test_dac_chunks.py`: 127 532 407 проверки и 99 200 моделирани DMA
периода PASS, не времева или аналогова проверка на платката.

Крайният LAB build в общия `ports/renesas-ra/build-VK_RA6M3` приключи
на 2026-09-07 15:22:46 UTC: `IQ_FIRMWARE_AUDIT=PASS`, проверени крайни
IQGenerator/DAC символи и GC root, непроменени избрани източници,
повторна проверка на SHA-256 PASS. `firmware.bin`: 1 601 600 B,
SHA-256 `7E6AFBBB6B33A548ED7FED784958E3BB2E6C284F055576664B07E6F358E87E52`.
Това е LAB образ, не ново радио deployment доказателство. При следваща
смяна на LAB/Radio конфигурация да се прекомпилират зависимите обекти;
самата промяна на compiler flags не ги обезсилва всички.

Подробности и неуспешните междинни build опити:
`sdr_lab/done.md`, `sdr_lab/build/iq_firmware_audit.json`.
Няма COM/J-Link, flash, reset, изпълнение на платката, нов UI или
аналогово потвърждение на 90 градуса. ADC синхронизация, PWM и обща
измервателна сесия още не са реализирани. Чуждите FM/FILE/RX/UI промени
са запазени; техните хардуерни статуси не се променят с тази проверка.

## ADC/PGA for RA6M3

- Added RA6M3-only PGA support for ADC channels AN000..AN002 and AN100..AN102.
- Added PGA register control in `ports/renesas-ra/ra/ra_adc.c`:
  - `RA_ADC_PGA_OFF`
  - `RA_ADC_PGA_BYPASS`
  - `RA_ADC_PGA_SINGLE`
  - `RA_ADC_PGA_DIFFERENTIAL`
- Added gain tables and nominal gain reporting:
  - single-ended gain codes from x2.000 to x13.333
  - differential gain codes x1.500, x2.333, x4.000, x5.667
- PGA register writes are done as nibble read-modify-write operations so reserved/reset bits are preserved.
- PGA configuration is rejected while ADC scan is running.

## PGAVSS wiring

- Differential PGA now prepares the real negative input pin automatically:
  - AN000/AN001/AN002 use `P003 / PGAVSS000`
  - AN100/AN101/AN102 use `P007 / PGAVSS100`
- The driver enables analog mode (`PmnPFS.ASEL = 1`) on the proper PGAVSS pin before enabling differential PGA.
- There is no internal grounding of the second PGA input. The board must externally wire PGAVSS to the intended reference.

## machine.ADC Python API

- Added RA6M3 PGA constants to `machine.ADC`.
- Added methods:
  - `adc.pga_supported()`
  - `adc.pga()`
  - `adc.pga("bypass")`
  - `adc.pga("single", ADC.PGA_GAIN_4_000)`
  - `adc.pga("differential", ADC.PGA_DIFF_GAIN_1_500)`
  - `adc.set_gain(...)`
  - `adc.gain()`
- PGA-capable ADC pins default to `RA_ADC_PGA_BYPASS` when the channel was still in `RA_ADC_PGA_OFF`, so normal ADC reads do not leave the special PGA pin path unavailable.
- Added required qstr entries in `ports/renesas-ra/qstrdefsport.h`.

## LCD vsync helper

- Added a GLCDC VSYNC counter in `ports/renesas-ra/boards/VK_RA6M3/machine_lcd.c`.
- Added `lcd.vsync([timeout_ms=20])`.
- The method blocks until the next GLCDC line-detect/VSYNC event or returns `False` on timeout.
- Intended use: gate LVGL/direct-mode rendering to reduce visible flicker during frequent updates.

## ADC1 scan-end vector

- Added `ADC1_SCAN_END` to `ports/renesas-ra/boards/VK_RA6M3/ra_gen/vector_data.h`.
- Increased `VECTOR_DATA_IRQ_COUNT` from 57 to 58.
- Allocated IRQ slot 57:
  - `VECTOR_NUMBER_ADC1_SCAN_END`
  - `ADC1_SCAN_END_IRQn`
- Added slot 57 to `g_vector_table` in `vector_data.c`, using the existing `adc_scan_end_isr`.
- Added slot 57 to `g_interrupt_event_link_select` as `EVENT_ADC1_SCAN_END`.
- Purpose: make ADC1/Q scan-end observable for diagnostics, statistics and possible transfer activation source.
- This does not select the final I/Q transport design. Real I/Q capture should still avoid two independent ADC0/ADC1 transfers and use a synchronized transport path such as DTC chain or DMAC interleaving/offset.

## Build check

Built successfully from MSYS2/UCRT64:

```sh
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /home/teodor/renesas_micropython/ports/renesas-ra
make BOARD=VK_RA6M3 -j8
```

Resulting artifacts:

- `build-VK_RA6M3/firmware.bin`: 1545168 bytes
- `build-VK_RA6M3/firmware.elf`: 17033728 bytes
- `build-VK_RA6M3/firmware.hex`: 4346315 bytes

## Rebuild after PnDEN and PGAVSS ASEL fixes

Rebuilt successfully from MSYS2/UCRT64 after the current `ra_adc.c` PGA changes.

Included in the rebuilt artifact:

- `PnDEN` handling for the full PGA unit when differential input is selected.
- Rejection of mixed single-ended and differential PGA usage inside one ADC unit.
- PGAVSS analog mode (`PmnPFS.ASEL = 1`) for differential PGA.
- PGAVSS analog mode (`PmnPFS.ASEL = 1`) for single-ended PGA, where the board must wire PGAVSS to AVSS0.
- Previously committed `ADC1_SCAN_END` vector support.

Resulting artifacts from the rebuild:

- `build-VK_RA6M3/firmware.bin`: 1545320 bytes
- `build-VK_RA6M3/firmware.elf`: 17034924 bytes
- `build-VK_RA6M3/firmware.hex`: 4346736 bytes
- timestamp: 2026-08-21 20:31:50

Note: at the time of this rebuild, `ports/renesas-ra/ra/ra_adc.c` still had uncommitted changes.

## Coherent I/Q capture driver (SDR receive path)

Report tag: **SDR-RA6M3-BUILD-20260821-02**.

Files touched (absolute paths):

- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\ra\ra_iq_adc.h` — new.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\ra\ra_iq_adc.c` — new.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\Makefile` — build hook only.

- Added `ra/ra_iq_adc.h` and `ra/ra_iq_adc.c`: single-activation coherent I/Q capture.
- Added the RA6M3-only build hook to `ports/renesas-ra/Makefile`, right after the audioadc
  block:

  ```make
  ifeq ($(CMSIS_MCU),RA6M3)
  CFLAGS += -DMICROPY_HW_ENABLE_IQ_ADC=1
  HAL_SRC_C += ra/ra_iq_adc.c
  endif
  ```

Design as implemented (per ARCH-TRIG-002 / ARCH-ADC-002):

- AGT reserves a channel; its event drives `ELSR8` (ELC_AD00) and `ELSR10` (ELC_AD10)
  simultaneously — one trigger source for both units.
- Both units opened with `ADC_TRIGGER_SYNC_ELC` via copy-and-override of `g_adc0_cfg` /
  `g_adc1_cfg`; the generated files stay untouched.
- S&H enabled on both units, identical `ADSSTR` / `SSTSH`, `SHMD = 1`.
- Transport: one DTC chain from `ADC0_SCAN_END`. Descriptor 0 reads unit-0 `ADDR` into the I
  buffer and chains into descriptor 1, which reads unit-1 into the Q buffer and raises the
  interrupt. One activation, two samples — I/Q skew is structurally impossible.
- `ADC1_SCAN_END` pulls no data. The slot stays NVIC-disabled; `IELSR[57].IR` is read and
  cleared in the block callback as an O(1) liveness check for the Q unit (weak evidence: it
  proves at least one scan completed in the block, not per-sample coherence, but it is the only
  check that does not violate REQ-RT-004).
- The callback rewrites both descriptors' `p_dest` and `length` unconditionally, so the driver
  does not depend on unconfirmed DTC repeat-region reload semantics.

Code review / fix pass before commit:

- Closed the ADC open partial-failure leak: if `R_ADC_ScanCfg()` fails, the already-opened ADC
  unit is closed immediately.
- ADC1 scan-end is kept diagnostic-only: `R_ADC_Open()` can enable any valid scan-end IRQ, so the
  driver now disables and clears `VECTOR_NUMBER_ADC1_SCAN_END` immediately after opening unit 1.
- Fixed DTC repeat length handling after callback retargeting: live descriptors now use the same
  encoded repeat/block `length` format that FSP writes during `R_DTC_Open()` / `R_DTC_Reconfigure()`.
- `ra_iq_adc_start()` now reconfigures the DTC descriptor table before enabling capture and cleans
  up DTC/ADC state if either ADC scan start fails.
- Fixed the `P000 == 0` cleanup bug by tracking whether I/Q ADC pins were enabled, instead of using
  pin number zero as a sentinel.
- `ra_iq_adc_acquire()` now snapshots and clears the ready block under a short ADC0 scan-end IRQ
  critical section, avoiding a race with the DTC callback.

Not done (by the phasing rules): no Python wrapper (phase 5, after the C path is stable); buffers
come out as raw `uint16_t` with no centering (DC removal is phase 3).

Build check — built from MSYS2/UCRT64, `make BOARD=VK_RA6M3 -j8`, exit 0. `ra/ra_iq_adc.c`
compiled and linked cleanly.

- `build-VK_RA6M3/firmware.bin`: 1545320 bytes
- `build-VK_RA6M3/firmware.elf`: 17034924 bytes
- `build-VK_RA6M3/firmware.hex`: 4346736 bytes
- `build-VK_RA6M3/ra/ra_iq_adc.o`: 84896 bytes
- timestamp: 2026-08-21 21:13:49

Important caveat: `firmware.bin` is byte-for-byte identical to the pre-I/Q rebuild above. With
`-ffunction-sections` / `--gc-sections` the entire driver is garbage-collected because nothing
references it yet (no Python wrapper). This build therefore proves the driver **compiles and links
cleanly**, not that its code sits on a reachable path. Stronger evidence follows once the phase-5
wrapper calls into it.

Evidence class: **code review + clean compile only**. Nothing has been executed. First bench check
is exactly the DTC-chain behaviour in repeat/ping-pong mode: does the interrupt arrive once per
block, and does descriptor 1 actually run on every activation.

## Phase-5 Python wrapper: `machine.IQADC`

Report tag: **SDR-RA6M3-BUILD-20260821-03**.

Files touched (absolute paths):

- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\machine_iq_adc.c` — new wrapper.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\modmachine.c` — register `IQADC`.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\qstrdefsport.h` — new qstrs.
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\Makefile` — `SRC_C += machine_iq_adc.c`.

Wraps `ra_iq_adc_*` (the coherent I/Q driver above) as one MicroPython peripheral. The ISR/DTC
path stays entirely in C; Python is a polling control-plane consumer that receives an
already-complete block.

Python API:

```python
from machine import IQADC, ADC
from array import array

iq = IQADC(i_pin="P000", q_pin="P004", rate=48000, block=128,
           pga=ADC.PGA_BYPASS, gain=0)

ib = array('H', bytearray(2 * 128))   # pre-allocated ONCE, before start()
qb = array('H', bytearray(2 * 128))

iq.start()
seq = iq.read_block(ib, qb)           # -> int sequence, or None if no block ready
if seq is not None:
    ...                               # ib/qb now hold raw uint16 codes (no centering)
iq.stop()
```

Methods:

- `IQADC(i_pin, q_pin, *, rate=48000, block=128, pga=ADC.PGA_BYPASS, gain=0)` → `ra_iq_adc_init()`.
  `i_pin` must resolve to AN000..AN002, `q_pin` to AN100..AN102 (else `ValueError`);
  `block` must be even and in 10..256; `pga = RA_ADC_PGA_OFF` is rejected.
- `start()` → `ra_iq_adc_start()`; `stop()` → `ra_iq_adc_stop()`; `deinit()` → `ra_iq_adc_deinit()`.
- `read_block(ib, qb)` → `ra_iq_adc_acquire()`, `memcpy` into the two caller `array('H')` buffers,
  returns the block sequence number or `None` when no block is ready. For `block=N` the buffers
  hold `N` samples = `2*N` bytes.
- Zero-allocation getters for the running loop: `blocks()`, `overruns()`, `unit1_stalls()`,
  `last_error()`, `ready()`.
- `status()` → dict (`initialised, running, ready, rate, block, blocks, overruns, unit1_stalls,
  last_error`).

Realtime contract (REQ-RT-002 — no allocation after `start()`):

- Consumer pre-allocates `ib`/`qb` once, before `start()`.
- `read_block()` and the int/bool getters allocate nothing: they return only
  `MP_OBJ_NEW_SMALL_INT(...)` / `mp_const_*`.
- `status()` builds a dict and therefore **allocates** — it is control-plane only and must not be
  called from inside the running capture loop; use the int getters there instead.
- No per-sample and no per-block Python callback in this version; `read_block()` is pure polling.

Code review / fix pass before commit:

- Invalid `pga` and `gain` arguments are rejected as `ValueError` before touching the hardware path.
- Hot-path counter returns are guarded: if `seq`, `blocks`, `overruns` or `unit1_stalls` exceed
  `MP_SMALL_INT_MAX`, the getter raises `OverflowError` instead of returning a malformed small-int
  or allocating a long int.
- `read_block()` now sanity-checks the C driver's returned sample count before copying into the
  caller buffers.

Known limitation: `blocks`/`seq` are `uint32` and eventually exceed `MP_SMALL_INT_MAX`.
At 48 kHz with `block=128` this happens after about 16 days on this port; a sustained-capture
consumer should stop/deinit/recreate the object before then. Acceptable for v1; noted for the
sustained-capture path.

Not done (deferred by phasing): no gain auto-ranging, no DC removal (phase 3), no stream iterator,
no `read_block_into` fast path beyond the current caller-buffer form (it already is zero-alloc).

Build proof — built from MSYS2/UCRT64, `make BOARD=VK_RA6M3 -j8`, exit 0. Unlike the previous
driver-only builds, the wrapper now references `ra_iq_adc_*`, so `--gc-sections` no longer strips
it:

- `arm-none-eabi-nm firmware.elf | grep -Ei 'ra_iq_adc|iqadc'` → `machine_iqadc_type` plus the
  driver/wrapper method symbols (was 0 before the wrapper). The driver
  entry points `ra_iq_adc_init` / `ra_iq_adc_acquire` / `ra_iq_adc_deinit` / `ra_iq_adc_get_status`
  are now global `T` symbols, and the full `machine_iqadc_*` type/method table is present.
- `build-VK_RA6M3/firmware.bin`: 1548640 bytes (was 1545320 — the driver plus wrapper are now
  actually linked in).
- `build-VK_RA6M3/firmware.elf`: 17136636 bytes
- `build-VK_RA6M3/firmware.hex`: 4356080 bytes
- `build-VK_RA6M3/machine_iq_adc.o`: 183468 bytes
- timestamp: 2026-08-21 21:54:01

Evidence class: **code review + clean compile + link/symbol evidence**. This proves the I/Q code
now sits on a reachable path in the firmware; it has still **not** been executed on hardware. The
first runtime check remains the DTC chain in repeat/ping-pong mode, now driveable from the REPL via
`IQADC(...).start()` + `read_block()`.

## First hardware bring-up on VK_RA6M3 (COM18)

Report tag: **SDR-RA6M3-BRINGUP-20260821-01**. Evidence class: **hardware, on target**.

Flashed the wrapper build to the board with J-Link (`R7FA6M3AH`, SWD, `loadbin firmware.bin @ 0x0`,
program & verify O.K., 1572864 bytes) and drove `machine.IQADC` from the REPL over `mpremote`
(COM18). Registers read live with `machine.mem8/16/32`.

Result: `IQADC(...)` and `start()` run without fault (the REPL survives, so no hard fault), but
capture produces **zero blocks** — after 5 s `blocks=0, overruns=0, unit1_stalls=0, ready=0` with
`running=1`.

Register bisection of the AGT -> ELC -> ADC0/ADC1 -> DTC -> callback chain (all values below are
post-`start()`):

- AGT: reserved channel 0 runs. `AGTCR=0x23` -> `TSTART=1, TCSTF=1, TUNDF=1`, counter moving. The
  timer counts and underflows, i.e. it is generating `ELC_EVENT_AGT0_INT` (64).
- ELC: `ELCR=0x80` -> `ELCON=1` (ELC enabled). `ELSR[8]=ELSR[10]=0x040` = `ELC_EVENT_AGT0_INT`, so
  both ADC units are linked to the single AGT event (ARCH-TRIG-002 holds on silicon).
- ADC0/ADC1: `ADCSR=0x0240` -> `TRGE=1, EXTRG=0` (synchronous ELC trigger, armed). `ADSTRGR=0x090A`
  = the exact FSP value for a single ELC group-A trigger (`TRSA=0x09=ADC_ELC_TRIGGER` in bits
  13:8, `TRSB=0x0A` in bits 5:0). `ADANSA0=0x0001` (one channel selected). Configuration is correct.
- Transport, the decisive probe: `IELSR[48]` (ADC0_SCAN_END) = `0x0100004B` -> event 75, `DTCE=1`
  (bit 24), `IR=0`; `IELSR[57]` (ADC1_SCAN_END, NVIC-disabled, not a DTC source) = `0x00010051` ->
  event 81, **`IR=1` latched**. The latched ADC1 scan-end flag proves both ADC units are actually
  being triggered and completing scans, and the cleared ADC0 flag proves the DTC is being activated
  by ADC0_SCAN_END.

Root cause (localized, not yet fixed): the whole trigger path works end to end — AGT underflow ->
ELC -> both ADC units scan -> ADC0_SCAN_END activates the DTC. What never happens is the
**block-boundary completion interrupt reaching the CPU**: `ADC0_SCAN_END.IR` is cleared by the DTC
on every activation and the NVIC `adc_scan_end_isr` (the block callback that increments `blocks`
and flips `ready`) never runs. This is exactly the DTC repeat / ping-pong re-arm behaviour the
driver header flagged as unverified against the manual: in repeat mode the DTC transfers forever
without ever asserting the transfer-complete interrupt at the block boundary. The fix belongs in
the DTC descriptor setup in `ra/ra_iq_adc.c` (mode / transfer count / `DISEL` so the block boundary
raises the interrupt), and is the next step before any I/Q or coherence test.

Not a wrapper bug: `machine.IQADC`, the register-level configuration, the ELC link and the ADC
trigger arming are all correct on hardware. The gap is solely the DTC completion-interrupt
generation in the C driver.

## DTC mode fix — I/Q transport verified on hardware (GREEN)

Report tag: **SDR-RA6M3-BRINGUP-20260821-02**. Evidence class: **hardware, on target**.

Root cause confirmed and fixed in `ra/ra_iq_adc.c`: the DTC chain descriptors were built in
`TRANSFER_MODE_REPEAT`. Per FSP a repeat-mode transfer never "ends", so `TRANSFER_IRQ_END` is
never delivered to the CPU — exactly the `IELSR[48].DTCE=1, IR=0, blocks=0` signature observed
above. Change:

- Both chain descriptors switched from `TRANSFER_MODE_REPEAT` to `TRANSFER_MODE_NORMAL`, so the
  transfer completes after `block_samples` activations and the block-boundary interrupt reaches the
  CPU (runs `ra_iq_block_callback`).
- The block callback now re-arms the chain for the next ping-pong half every block: new `p_dest`,
  raw `length = block_samples`, then `R_DTC_Reconfigure(...)`. Normal-mode DTC self-clears its DTCE
  on completion, so the explicit re-arm is required to resume transport.
- `ra_iq_dtc_repeat_length()` removed: encoded repeat/block length is not used on the normal-mode
  path; the raw sample count is the correct `length`.

Rebuilt (`make BOARD=VK_RA6M3 -j16`, exit 0, `firmware.bin` 1548640 bytes), flashed over J-Link,
and driven from the REPL on COM18:

```
IQADC("P000","P004", rate=48000, block=128, pga=ADC.PGA_BYPASS)
```

Result — capture now runs:

- `blocks` increments; `seq` returned by `read_block()` is monotonic 1,2,3,... with no gaps.
- `overruns = 0`, `unit1_stalls = 0` over the sampled window: the ping-pong re-arm keeps up and the
  ADC1 (Q) scan-end liveness flag latches every block.
- Live data flowing on both channels (I ~3010 codes, Q ~2864 codes on floating/DC pins) — raw 12-bit
  `uint16`, no centering, as designed.

What this proves: the full transport — AGT -> ELC (single event to both units) -> ADC0/ADC1 sync
scan -> single-activation DTC chain -> block-boundary interrupt -> ping-pong swap -> `read_block()`
— works on silicon. What is still unproven: actual I/Q phase coherence, which needs a known coherent
signal source on P000/P004 (the transport is structurally single-activation, and `unit1_stalls=0`
is consistent with coherence, but it is not a phase measurement). That is the next bench step.

## Phase-3 block DSP: DC removal + x2 decimation (in C)

> Historical implementation note: the per-block mean subtraction described in
> this section was replaced on 2026-08-23 by the persistent streaming DC servo
> documented at the end of this file.  The original result is retained as the
> bring-up record, not as the current algorithm.

Report tag: **SDR-RA6M3-DSP-20260821-01**. Evidence class: **hardware, on target** (verified on
VK_RA6M3 / COM18 on 2026-08-22 after a J-Link reset cleared the AGT lock-out described below).

Bench result: `iq.dsp_status()` returns `{dsp_samples: 64, dsp_blocks: N, i_mean, q_mean}` with
`dsp_samples == block/2 == 64`, `dsp_blocks` incrementing in lock-step with `blocks`, and per-block
DC means computed live (e.g. i_mean ~2900 -> ~1900 as the floating input settled). `overruns == 0`,
`unit1_stalls == 0`. The C block-boundary DC-removal + x2-decimation stage runs on silicon.

Added an integer, allocation-free DSP stage that runs in the block callback, on the just-captured
half:

- `ra_iq_dsp_process()` in `ra/ra_iq_adc.c`: computes the per-block mean of I and Q, then produces
  `block_samples/2` centered, x2-decimated samples per channel as `centered = (x[2j]+x[2j+1])/2 -
  mean` (equivalent to averaging two mean-removed samples; mean removal is linear). Integer only, so
  no FPU state is touched in the ISR.
- Output buffers `s_i_dc` / `s_q_dc` (`int16`, `RA_IQ_ADC_MAX_BLOCK_SAMPLES/2`), overwritten each
  block. Counters in a new `ra_iq_dsp_status_t` (`dsp_blocks`, `dsp_samples`, `i_mean`, `q_mean`),
  reset in `start()`, read via `ra_iq_adc_get_dsp_status()`.
- Python: `iq.dsp_status()` returns `{dsp_blocks, dsp_samples, i_mean, q_mean}` (control-plane dict,
  not the realtime path). New qstrs in `qstrdefsport.h`.

## CMSIS-DSP enabled for VK_RA6M3

- `boards/VK_RA6M3/mpconfigboard.mk`: `MICROPY_HW_ENABLE_DSP = 1`. This compiles the CMSIS-DSP f32
  library (FIR, biquad, `arm_cmplx_mag_f32`, RFFT, RMS, ...) for `ARM_MATH_CM4`, available to C code
  (intended for the phase-3 AM demod / filters).
- `ports/renesas-ra/Makefile`: `moddsp.c` (the Python `dsp` module) is left OUT of the build. Its
  `MP_QSTR_*` tokens are not picked up by the qstr scan on this port and it collides with the
  frozen-collected `run` qstr; it is not needed for the C-side DSP path. The library itself still
  builds and links.

## Build environment note (IMPORTANT) — UCRT64 vs MINGW64

The firmware links from **UCRT64** (`arm-none-eabi-gcc`), but the LVGL Python binding
(`build/lvgl/lv_mpy.c`, ~39 k lines) is generated by `python_api_gen_mpy.py`, which needs a **host
`gcc` and `pycparser`** — present only in **MINGW64** (`/mingw64/bin/gcc`, pycparser 2.22), NOT in
UCRT64. So the generated `lv_mpy.c` must be produced under MINGW64 and is then compiled under
UCRT64. A `make clean` under UCRT64 deletes `lv_mpy.c`; regenerating it there produces an empty
stub (0-byte preprocessor output -> no bindings -> `undefined reference to mp_lv_roots` at link).

Recovery, if `lv_mpy.c` is ever lost: from a MINGW64 shell,
`export PATH=/mingw64/bin:/usr/bin:$PATH && make BOARD=VK_RA6M3 build-VK_RA6M3/lvgl/lv_mpy.c`, then
build the firmware normally from UCRT64. Do not `make clean` the VK_RA6M3 tree from UCRT64.

- `boards/VK_RA6M3/dave2d_port.c`: anchored `MP_REGISTER_ROOT_POINTER(void *mp_lv_roots)` and
  `mp_lv_user_data` here (the TU that owns their GC lifecycle), because the copies in the generated
  `lv_mpy.c` do not survive the qstr preprocessing pass on a clean build. Identical registrations
  deduplicate, so this is safe alongside a good `lv_mpy.c`.

Build after all of the above: MSYS2/UCRT64, `make BOARD=VK_RA6M3 -j8`, exit 0, `firmware.bin`
1550004 bytes, 50 `ra_iq_*`/`iqadc` symbols linked, CMSIS-DSP compiled, LVGL binding restored.

## Hardware lock-out found on the bench: AGT stuck running across warm reset

During bring-up the DSP build was initially blocked on hardware: `IQADC(...)` failed at construction
with `OSError(EIO)`. Instrumenting `ra_iq_adc_init()` localized it to the very first step:
`ra_iq_reserve_timer()` returning false. Register readout (`machine.mem8`) showed **both AGT0 and
AGT1 running** (`AGTCR = 0x23`: `TSTART=1, TCSTF=1`) at power-up, and `ra_agt_timer_reserve()`
correctly rejected a channel whose timer was already counting.

The AGT run state survives every warm reset tried (J-Link `r` = SYSRESETREQ, and RSetType 5 =
reset core+peripherals), and register writes to stop it (`AGTCR.TSTART=0`, `AGTCR.TSTOP=1`) do not
take. This matches the known RA behaviour that SYSRESETREQ does not clear all peripheral state. A
leftover from an earlier test session left both AGTs counting.

Conclusion: **this was a board-state lock-out, not a driver defect.** Recovery (operator,
2026-08-22): a **J-Link reset clears it — no power cycle needed**. `reset_go.jlink` (`r; g; q`) was
enough here; after it, `AGTCR = 0x00` on both units and `IQADC()` constructs and runs normally. The
Phase-3 DSP smoke test above was then verified on VK_RA6M3 / COM18. Operator rule: run a J-Link reset
**before every REPL / mpremote start** on this board. Robustness follow-up still worth considering:
have the I/Q driver force-stop a stuck AGT during reserve so a crashed session without `deinit()`
cannot lock out the next run without a debugger reset.

## Phase-4 AM demod -> timed DAC/DMAC audio sink

Report tag: **SDR-RA6M3-AMDAC-20260822-01**. Evidence class: **hardware, on target** (VK_RA6M3 /
COM18, 2026-08-22).

The full receive path now runs end to end in C: coherent I/Q capture -> DC removal + x2 decimation
(phase 3) -> AM envelope detect (alpha-max-beta-min, integer) -> IIR DC blocker to center at
mid-scale -> single-producer/single-consumer lock-free ring -> timer-paced, DTC/DMAC double-buffered
DAC output on DA0/P014. No Python and no CPU copy in the per-sample DAC path: the ra_dac
double-buffered stream clocks one sample to DADR per timer tick via DMAC, and the block callback only
runs the integer demod and pushes to the ring.

Implementation:
- `ra/ra_iq_adc.c` / `.h`: `ra_iq_am_produce()` (envelope + DC-block + ring push, called from the
  block callback after the DSP stage), `ra_iq_dac_fill()` (DAC fill callback: pops the ring, writes
  mid-scale on underrun, always returns true so the stream never self-stops), and
  `ra_iq_adc_am_dac_start(dac_pin, dac_ch)` / `_stop()` / `ra_iq_adc_get_am_status()`. The DAC is
  driven through the existing `ra_dac_write_timed_double_buffered(...)` API; the I/Q capture reserves
  one AGT for its ELC trigger and the DAC auto-timer (`-1`) takes the other.
- `machine_iq_adc.c` + `qstrdefsport.h`: `IQADC.am_dac(P014[, ch])`, `am_dac_stop()`, `am_status()`.

Generated-file change (REQ-GIT-007): `boards/VK_RA6M3/ra_gen/vector_data.c/.h` now allocate two DMAC
completion IRQ slots — `DMAC0_INT` (slot 58) and `DMAC1_INT` (slot 59), `dmac_int_isr`,
`EVENT_DMAC0_INT` / `EVENT_DMAC1_INT`, `VECTOR_DATA_IRQ_COUNT` raised 58 -> 60. This is required
because `ra_dac`'s double-buffered mode needs a DMAC channel with an allocated interrupt vector for
its ping-pong completion callback; the stock VK_RA6M3 vector table had none (only EDMAC0). Root cause
of the earlier failure: `ra_dac_write_timed_double_buffered` returned at stage `EVENT_MAP` with
`FSP_ERR_UNSUPPORTED` because `ra_dac_dmac_irq()` found no `VECTOR_NUMBER_DMAC0_INT`. (This is exactly
why the same DAC path already works on VK_RA4M2, whose generated vector table allocates DMAC0..7.)

Bench result: `iq.am_dac("P014")` starts with no error; `am_status()` reports `am_active=1,
audio_underruns=0, ring_overruns=0` over the sampled window (producer and consumer are rate-matched:
audio rate = ADC rate / 2). `DADR0` (0x4005E000) reads ~2046..2050, i.e. the DMAC is clocking the
AM-demodulated, mid-scale-centered envelope to the DAC; on floating/DC inputs the swing is just the
noise envelope. After `am_dac_stop()`, `DADR0 = 0`. Build: UCRT64, `-j16`, exit 0, `firmware.bin`
1550436 bytes.

Next bench step: apply a real AM-modulated carrier to the P000/P004 front end and observe the
recovered audio envelope on DA0/P014 with a scope (or a load/headphone). The transport, rate match,
and demod math are proven; only the analog end-to-end with a real signal remains.

## Decouple demod from DAC: audio stream + machine.DAC.stream

Report tag: SDR-RA6M3-DEMODDAC-20260822. Evidence class: code-complete, build and
HIL verification pending (recorded before the build on operator request).

Rationale: the AM path had put DAC control inside IQADC (am_dac / am_dac_stop /
am_status), which is AM-specific and does not generalize to the coming demod modes
(SSB, CW). The demodulator must produce a generic audio stream and the DAC must
play it through its own existing double-buffered streaming, with no CPU per sample.

Changes:

- ra/ra_iq_adc.c/.h: removed am_dac ownership (ra_iq_adc_am_dac_start/stop,
  ra_iq_dac_fill/stop_cb, the s_dac_buf_a/b DAC buffers, and the ra_dac.h include).
  Added a demod-mode enum (RA_IQ_DEMOD_OFF/AM) with ra_iq_adc_set_demod/get_demod;
  the block-callback producer (renamed ra_iq_demod_produce) now runs only when a
  mode is selected and dispatches on it (AM = alpha-max-beta-min envelope + IIR DC
  blocker, as before). Added the generic SPSC consumer ra_iq_adc_audio_pull(buf, n)
  (silence + underrun count on empty ring), ra_iq_adc_get_audio_params(freq,
  sample_count) = (rate/2, block/2), and ra_iq_adc_get_audio_status (underruns,
  overruns, demod_mode). ra_iq_adc_stop now just sets demod OFF.

- machine_iq_adc.c: replaced am_dac/am_dac_stop/am_status with iq.demod("am"|"off"),
  iq.audio_status() and iq.read_audio(buf) (debug pull, not the realtime path).

- machine_dac.c: added DAC.stream(iqadc[, freq]) on the existing machine.DAC. It
  owns the two ping-pong buffers and drives ra_dac_write_timed_double_buffered with
  a fill callback that pops the demod ring via ra_iq_adc_audio_pull. DAC.stop() /
  DAC.playing() are the existing methods. The DAC now moves samples via DMAC with
  no CPU per sample and no knowledge that the source is AM.

- Also present (orthogonal, earlier this session): manual I/Q imbalance correction
  iq.iq_correction(enable, amp, phase) / iq.iq_correction_status(), applied to the
  decimated I/Q before demod (I reference, Q' = amp*Q + phase*I in Q15), disabled by
  default.

New usage:

    iq = IQADC("P000","P004", rate=48000, block=128, pga=ADC.PGA_BYPASS)
    iq.start(); iq.demod("am")
    dac = DAC("P014"); dac.stream(iq)
    ...
    dac.stop()

Next: build from UCRT64, flash + J-Link reset, verify on COM18 that DAC.stream(iq)
runs with audio_status underruns/overruns 0 and DADR0 clocking, then commit.

### Verified on hardware (COM18, 2026-08-22)

SDR-RA6M3-DEMODDAC-20260822 is now hardware-verified. Two Python names had to change
because the bare qstrs collide with frozen modules on this board (asyncio uses
"stream", the LVGL frozen modules use "enabled"): the DAC method is
**DAC.stream_from(iqadc)** (not stream) and the iq_correction_status flag key is
**"correcting"** (not "enabled").

Bench: iq.demod("am") + dac.stream_from(iq) -> dac.playing()=True,
audio_status {demod:1, audio_underruns:0, ring_overruns:0}, DADR0 (0x4005E000)
clocking around mid-scale (~2044-2045) via DMAC with no CPU per sample.
iq.iq_correction(enable=True, amp=1.05, phase=0.02) -> iq_correction_status
{correcting:1, amp:1.04999, phase:0.01999} (Q15 round-trip), stream keeps running
with 0 underruns. dac.stop() halts cleanly. Build: UCRT64 -j16 exit 0,
firmware.bin 1551368 bytes.

Final API:

    iq = IQADC("P000","P004", rate=48000, block=128, pga=ADC.PGA_BYPASS)
    iq.start(); iq.demod("am")
    dac = DAC("P014"); dac.stream_from(iq)
    ...
    dac.stop(); iq.demod("off"); iq.stop()

iq.read_audio(buf) and iq.audio_status() are available for inspection;
iq.iq_correction(...) / iq.iq_correction_status() drive the manual I/Q imbalance.

## SSB USB/LSB Hilbert demod

Report tag: SDR-RA6M3-SSB-20260822. Evidence class: hardware, on target (COM18).

Added USB and LSB as new demod modes next to AM, using a phasing demodulator on the
decimated, imbalance-corrected I/Q:

- 31-tap Q15 Hilbert transformer (Type-III antisymmetric, Blackman-windowed;
  odd taps only, k=15 endpoint vanishes because w[0]=w[30]=0). Table generated from
  h[15+k] = -(2/(pi*k))*w[15+k], documented in ra/ra_iq_adc.c so it is reproducible.
- Persistent 31-deep circular histories for I and Q (span block boundaries), reset
  in ra_iq_adc_set_demod alongside the ring/DC-blocker reset.
- Per decimated sample: H(Q) via the FIR, I delayed by the 15-tap group delay;
  USB = I_delayed - H(Q), LSB = I_delayed + H(Q) (sign convention documented; swap
  if a real signal shows the opposite sideband). Centered to 2048, clamped, pushed
  to the same SPSC audio ring. No AM DC blocker (SSB carries no carrier DC).
  Integer only, no FPU/CMSIS/alloc/Python in the block callback.
- machine_iq_adc.c: iq.demod("am"|"usb"|"lsb"|"off"); enum am=1, usb=2, lsb=3.

Bench: iq.demod("usb"/"lsb") + dac.stream_from(iq) plays with ring_overruns=0 and
DADR0 clocking via DMAC; mode switching between am/usb/lsb works; an unknown mode
raises ValueError. Build UCRT64 -j16 exit 0, firmware.bin 1551744 bytes.

Known minor: switching demod mode while the DAC is streaming shows a one-time ~64
sample (2.7 ms) underrun burst, because set_demod resets the ring (needed to reset
the Hilbert history) while the DAC is already pulling. Benign control-plane event.

Pending: sideband correctness with a real signal (scope/known carrier) - on
floating inputs the SSB output is just Hilbert-filtered noise near mid-scale.

## CW demod + mode-switch gap fix

Report tag: SDR-RA6M3-CW-20260822. Evidence class: hardware, on target (COM18) for
function; tone/sideband correctness pending a real signal.

Mode-switch gap fix: ra_iq_adc_set_demod no longer flushes the audio ring; the
one-time flush moved to ra_iq_adc_start. Between two active demod modes the ring now
keeps flowing (old samples drain, new fill) instead of going empty. Bench: the
deterministic ~64-sample underrun burst on every switch is gone; a residual, timing-
dependent one-block (~2.7 ms) blip can still occur at a switch depending on where the
DAC ping-pong boundary lands, but steady streaming shows 0 underruns.

CW demod (RA_IQ_DEMOD_CW = 4): shifts the baseband up to a fixed 700 Hz beat tone and
outputs the real part, so a carrier becomes an on/off-keyed tone. NCO = 256-entry Q15
sine table (round(32767*sin(2*pi*n/256)), documented in ra_iq_adc.c) + a uint32 phase
accumulator; phase step s_cw_inc = (700<<32)/(sample_rate_hz/2) computed when CW is
selected. Per decimated sample: audio = (I*cos - Q*sin) >> 15 (real part of the
complex mix), centered at 2048. Integer only, no FPU/alloc/Python in the callback.
iq.demod("cw") maps to it. Build UCRT64 -j16 exit 0, firmware.bin 1552536 bytes.

Demod modes now: off / am / usb / lsb / cw. On floating inputs CW output stays near
mid-scale because the phase-3 DC removal takes out a 0 Hz carrier and there is no real
carrier to beat; the 700 Hz tone appears when a carrier is present at an offset. Next
proof for USB/LSB/CW is a known offset carrier on P000/P004 with a scope on DA0/P014.

## Committed (2026-08-22)

The SDR receive path is committed on master. Relevant commits (newest first):

- a815a6423  add SSB (USB/LSB) and CW demod modes
- bce6dcceb  decouple demod from DAC + manual I/Q imbalance correction
- a0f8f2657  add RA6 SDR capability layer and feature build gate
- 490c0fdf0  record AM-demod-to-DAC hardware verification
- d4e58f188  add IQADC DSP and AM DAC path
- 90183511d  fix IQADC DTC repeat->normal, blocks now delivered
- 2e3ed463f  add IQADC Python wrapper
- 623bfd5a2  add coherent IQ ADC capture driver

Full chain, hardware-verified for function on VK_RA6M3 / COM18:
I/Q capture (AGT -> ELC -> ADC0/ADC1 -> single-activation DTC -> ping-pong)
  -> phase-3 DSP (DC removal + x2 decimation + manual I/Q imbalance correction)
  -> demod: off / am / usb / lsb / cw (integer, block boundary)
  -> generic SPSC audio ring
  -> DAC.stream_from(iqadc): timer-paced DTC/DMAC double buffer on DA0/P014, no CPU
     per sample.

RA6-family: capability layer + MICROPY_HW_ENABLE_RA_SDR build gate in place; RA6M5
register compatibility confirmed at the FSP-header level (RA6M5-VERIFY-20260822),
with Table 47.14 / MSTPC14=ELC / S&H AC timing still open pending the RA6M5 manual.

Pending bench proof (needs a real offset carrier + scope on DA0): USB/LSB sideband
correctness and the CW 700 Hz beat tone. Next planned DSP: AGC, then spectrum/FFT.

## RMS AGC + output volume

Report tag: SDR-RA6M3-AGC-20260822. Evidence class: hardware, on target (COM18) for
control logic; audio-level effects need a real signal.

Two audio-output stages added to the shared demod tail (mode-agnostic, integer, in
the block callback), per the RMS design in ra6_sdr_next_steps.md:

- Shared tail: each demod case (am/usb/lsb/cw) now computes only signed audio, then
  one path applies AGC -> master volume -> peak limiter -> center 2048 -> ring push.
- RMS AGC: one-pole mean-square + integer isqrt for the RMS envelope; a slow
  multiplicative gain servo keeps the output RMS at target (driven by the smoothed
  envelope, not the sample, so it does not distort). Peak limiter to the DAC range
  with a clips counter. Modes off/fast/slow/manual (preset window/loop shifts);
  gain floor ~1/128, cap 8x. Reset per demod select.
- Master output volume: a manual Q15 gain applied after AGC (iq.volume(x), x 0..8).

API (SDR-unique qstrs to dodge frozen asyncio/LVGL collisions; mode via strcmp):
  iq.agc("off"|"fast"|"slow"|"manual", gain=1.0, rms_target=0.5)
  iq.agc_status()  -> {agc_mode, gain, rms_target, rms, agc_clips}
  iq.volume(x)     -> set; no-arg returns current

Bench: agc("off") gain 1.0; agc("fast") ramps gain to the 8x cap (floating inputs
are near-silence so the servo drives to max); agc("manual", gain=2.0) holds 2.0;
agc("slow", rms_target=0.5) moves the gain gradually; unknown mode raises
ValueError; volume() applies without fault; clips 0, no ongoing underruns. Full
convergence-to-target and volume scaling need a real carrier. Build UCRT64 -j16
exit 0, firmware.bin 1553608 bytes.

## AGC upgraded to a PI servo with asymmetric attack/decay

Report tag: SDR-RA6M3-AGC-PI-20260822. Evidence class: hardware, on target (COM18).

The gain regulator was a bang-bang (sign-only) integrator with symmetric rate and a
+/-1 dither. Replaced with a proper PI, log-domain, asymmetric loop:

- Error rel = (target - out_env)/target in Q15 (relative/dB-like error; 1/target is
  precomputed in set_agc so there is no per-sample divide).
- Integral: gain *= 1 + rel/2^sh, with sh = att_sh when the output is too loud
  (e<0) and dec_sh when quiet (e>0), att_sh < dec_sh so attack is faster than decay.
  The error-proportional step settles smoothly (no bang-bang dither).
- Proportional: an un-accumulated feed-forward of rel/2^kp_sh added to the applied
  gain for faster transient response (a true PI, not I-only).
- Anti-windup: gain clamped to [1/128, gain_max]. Presets fast {att5,dec9,kp6},
  slow {att6,dec13,kp8}.

Bench (quiet floating inputs -> servo in the decay direction, so this measures the
recovery rate): agc("fast") ramps gain 1.0 -> 8.0 in ~250 ms; agc("slow") ramps
1.0 -> 1.40 in the same 250 ms (about 16x slower decay, matching dec_sh 13 vs 9);
manual holds gain=3.0; off = 1.0; the fast gain settles at the 8x cap as a flat
[8,8,8,...] trajectory with no oscillation or dither. Full attack behaviour and the
proportional feed-forward need a real signal above target. Build UCRT64 -j16 exit 0,
firmware.bin 1553784 bytes.

## Spectrum (FFT) path

Report tag: SDR-RA6M3-SPECTRUM-20260822. Evidence class: hardware, on target (COM18).

A CMSIS-DSP magnitude spectrum of the decimated complex baseband, for a UI/waterfall.
The FFT is kept out of the ISR: the block callback only copies decimated I/Q into a
ping-pong int16 accumulator (256 deep, gated on spectrum-enable, integer only); the
float window+FFT+magnitude run inside iq.spectrum() (control plane, FPU allowed).

- 256-point complex FFT (arm_cfft_f32 + arm_cfft_sR_f32_len256), Hann window
  (arm_cos_f32; added arm_cos_f32.c to the Makefile DSP sources), arm_cmplx_mag_f32,
  fftshift so DC is centered (bin 128 = 0 Hz, span +/-12 kHz, 93.75 Hz/bin).
- API: iq.spectrum(buf) with buf=array('f',256) -> fills bins, returns 256 or None if
  no fresh snapshot; enables accumulation on first call. iq.spectrum_stop();
  iq.spectrum_info() -> {bins:256, bin_hz:93, center_hz:0}.
- Bug fixed during bring-up: spectrum_enable reset the accumulator on every call, so
  polling faster than one frame (~11 ms) never produced a snapshot; now it resets
  only on the 0->1 transition.

Bench: spectrum() returns 256; on floating inputs the magnitude is centered (low-freq
noise around DC, tapering to +/-fs/2), confirming the fftshift and window; updates
continuously. Build UCRT64 -j16 exit 0, firmware.bin 1562748 bytes. A real offset
carrier will show a sharp peak at its bin.

## Channel low-pass filter (pre-demod)

Report tag: SDR-RA6M3-CHFILT-20260822. Evidence class: hardware, on target (COM18).

A runtime-configurable channel low-pass on the decimated complex I/Q, applied in
ra_iq_dsp_process AFTER I/Q imbalance and BEFORE demod, so AGC/demod do not work on
out-of-channel noise. Integer, allocation-free, in the block callback.

- Structure: 4 cascaded one-pole low-pass sections per channel (~24 dB/oct),
  y_s += ((x - y_s)*alpha)>>15. Stable, integer; no Q15 biquad instability. Section
  state (int32) resets on cutoff change / demod select to avoid clicks.
- alpha_q15 = round(2*pi*fc/fs * 32768) computed in the control plane (single
  precision) from the cutoff and audio rate fs = sample_rate_hz/2.
- Per-mode default cutoff set in set_demod: AM 5000, USB/LSB 3000, CW 1000, OFF
  bypass. Override with iq.bandwidth(hz).
- API: iq.bandwidth(hz) (0 = bypass), iq.filter_status() -> {bandwidth, bypassed, fs}.

Bench (spectrum of the filtered baseband, floating-noise input): bw=2000 drops the
+/-fs/2 edge energy from ~15 to ~0.9 (center/edge ratio ~47); bw=800 fully suppresses
the edges. The narrow modes where channel filtering matters most (SSB, CW) filter
strongly. Build UCRT64 -j16 exit 0, firmware.bin 1563404 bytes.

Known limit (1-pole approximation): alpha = 2*pi*fc/fs saturates to unity at
fc >= fs/(2*pi) ~ 3820 Hz, so cutoffs above that (AM 5 kHz, 6 kHz) pass through
(bypassed=1 is reported honestly). Acceptable for now: AM is wide by nature and the
narrow modes are well served. Follow-ups if a real 5 kHz AM channel filter or steeper
skirts are wanted: use the exact one-pole alpha = 1 - exp(-2*pi*fc/fs), or a biquad.

## Half-band FIR decimator + DSP timing measurement

Report tag: SDR-RA6M3-TIMING-20260822. Evidence class: hardware, on target (COM18).

Two items closed together: the ×2 decimation was upgraded from a 2-sample average
to an 11-tap Q15 half-band FIR (per-channel history, allocation-free, block
callback), and the whole block-boundary DSP path was instrumented with the DWT
cycle counter so the integer chain's cost is measured before any CMSIS/f32 port.

- Timing spans ra_iq_dsp_process (DC removal + half-band decimation + I/Q imbalance
  + channel filter) plus ra_iq_demod_produce (demod + AGC + volume + limiter),
  wrapped by DWT->CYCCNT in ra_iq_block_callback. DWT is enabled and CYCCNT zeroed
  in ra_iq_adc_start (CoreDebug TRCENA + DWT CYCCNTENA). Getter
  ra_iq_adc_get_timing(last,max,avg,cpu_hz); avg = sum/count, cpu_hz =
  SystemCoreClock. Python: iq.timing() -> {last_cyc, max_cyc, avg_cyc, block_cyc,
  cpu_hz, max_pct}, block_cyc = cpu_hz*block/rate.

- Bench, rate=48000 block=128 (block period 320000 cyc = 2666.7 us @ 120 MHz):

  | stage                         | max cyc | % block | us   |
  |-------------------------------|---------|---------|------|
  | DSP only (DC+decim FIR+imbal) |  19 465 |  6.1%   | 162  |
  | AM + channel filter bypass    |  26 808 |  8.4%   | 223  |
  | AM + channel filter 2 kHz     |  33 006 | 10.3%   | 275  |
  | USB (31-tap Q15 Hilbert)      |  62 235 | 19.4%   | 519  |
  | CW (256-entry NCO)            |  32 633 |  ~10%   | 272  |
  | CW + AGC fast                 |  51 221 |  ~16%   | 427  |

  Worst case is USB at 62 235 cyc = 19.4% of the block budget -> ~80% headroom.
  The integer path fits comfortably; a full f32 realtime pipeline is therefore
  timing-feasible (see the hybrid CMSIS strategy in ra6_sdr_next_steps.md).

Build UCRT64 -j16 exit 0, firmware.bin 1564148 bytes.

## Hybrid CMSIS-DSP migration (stages 1-5) + all-f32 worst-case budget

Report tag: SDR-RA6M3-HYBRID-20260822. Evidence class: hardware, on target (COM18).

Each realtime DSP stage was given a runtime A/B switch (integer/hand kernel vs
CMSIS kernel), and the default was chosen by measuring the per-block DWT cost on
hardware -- never by assuming a type. Result per stage:

| stage           | default   | kernel                       | vs alternative              |
|-----------------|-----------|------------------------------|-----------------------------|
| 1 decimator     | integer   | hand 11-tap half-band FIR    | CMSIS +20 500 cyc (2x slower)|
| 2 SSB Hilbert   | CMSIS q15 | arm_fir_q15 (32-tap padded)  | -9 200 cyc (~15% faster)    |
| 3 channel filter| integer   | arm_biquad_cascade_df1_f32   | f32 +5 400, fixes 3820 Hz sat|
| 4 AM envelope   | integer   | arm_cmplx_mag_f32            | f32 +2 300, exact magnitude |
| 5 spectrum      | (f32)     | arm_cfft_f32 + cmplx_mag_f32 | already f32                 |

Switches: iq.dec_kernel / hil_kernel(default on) / chf_kernel / mag_kernel, each
[on]->bool, live-toggle, resetting iq.timing() so the next read isolates the kernel.

Rule learned: CMSIS wins DENSE kernels (SIMD amortised over all taps) and loses SPARSE
ones (a structure-aware hand loop skips the zero taps). f32 is used where accuracy beats
raw speed (channel filter response across a wide cutoff; exact AM envelope) because the
80% timing headroom allows it. Two CMSIS gotchas cost an iteration each and are recorded:
arm_fir_q15 requires numTaps even >= 4 (pad by PREPENDING a zero to keep group delay);
its q15 state buffer is numTaps + blockSize (not -1) under ARM_MATH_DSP on the M4. The
f32 biquad is the first FPU use inside the ADC block-callback ISR; Cortex-M4F lazy
stacking handles it (unit1_stalls = 0 under all-f32 load).

All-f32 worst-case budget (rate 48000, block 128 -> 320000-cyc / 2666.7 us block):

| kernel stack                                   | worst max | % block |
|------------------------------------------------|-----------|---------|
| recommended (dec int, hil CMSIS, chf+mag f32)  | 55 831    | 17.4%   |
| everything CMSIS/f32 (incl the slower decimator)| 77 079    | 24.1%   |

Even with every f32/CMSIS kernel enabled at once the chain fits with ~76% headroom and
I/Q coherence holds (unit1_stalls = 0), so f32 may be enabled freely. Commits:
bfd9b7b72 (stage 1 + switch), 0caea8688 (stage 2), 1fb5c87e3 (stage 3), 7aa6011bd
(stage 4). Build UCRT64 -j16 exit 0, firmware.bin 1566740 bytes.

## Post-demod audio filter (band-limit / CW peak)

Report tag: SDR-RA6M3-AUDIOFILT-20260822. Evidence class: hardware, on target (COM18).

The last missing filter: an audio-domain band-limiter after the demod, the complement
to the channel filter (which shapes the complex I/Q before the demod). Runs per audio
sample in the block callback, inserted at the head of ra_iq_audio_stage BEFORE the AGC,
so the servo and DAC only see in-band energy. Up to two f32 2nd-order biquad sections,
transposed direct-form II, designed by RBJ cookbook in the control plane.

Presets (per-demod default, overridable with iq.audio_filter(name)):
  off   - bypass
  am    - 4th-order Butterworth low-pass at 4.5 kHz (two LP sections)
  voice - band-pass ~300..2700 Hz for SSB (HP 300 + LP 2700)      <- default for USB/LSB
  cw    - two cascaded band-pass peaks at the 700 Hz BFO, Q=8 (narrow CW ring)
set_demod applies AM->am, SSB->voice, CW->cw, OFF->off; iq.audio_filter([name]) reads or
overrides ("off"/"am"/"voice"(alias "ssb")/"cw") and returns the current preset name.

Bench: per-mode defaults apply correctly (af reads am/voice/cw with the mode), override
works, unit1_stalls=0 under the extra per-sample FPU load. USB with the voice band-pass
is 18.7% of the 320000-cyc block budget (Hilbert CMSIS + integer channel filter + voice
audio filter together) -- still ~81% headroom. Build UCRT64 -j16 exit 0, firmware.bin
1567720 bytes.

Filter chain now complete end to end: anti-alias decimation -> DC removal -> I/Q
imbalance -> channel low-pass (pre-demod) -> demod (AM/SSB Hilbert/CW) -> audio
band-limit (post-demod) -> AGC -> volume -> limiter -> DAC; plus the FFT spectrum tap.

## Squelch

Report tag: SDR-RA6M3-SQUELCH-20260822. Evidence class: hardware, on target (COM18).

Closes the AGC known-limit (servo ramps to gain_max on silence and amplifies noise).
A gate on the pre-AGC audio envelope, per sample at the head of ra_iq_audio_stage:
while the one-pole |audio| envelope stays below the threshold the stage returns
mid-scale silence and early-returns, which also freezes the AGC servo + mean-square so
the gain does not ramp up during the quiet. Open/close hysteresis (open at thresh, close
below 3/4 thresh) stops chatter. Reset on demod select. iq.squelch([thresh]) -> {thresh,
open, env}; thresh == 0 disables (default).

Bench (no injected signal, so the DC-removed baseband envelope is ~0): thresh 0 -> gate
open; thresh above the floor -> gate closed (open False); back to 0 -> open. unit1_stalls
= 0. Mechanism proven; with a real carrier env rises and the gate opens at env >= thresh.
Build UCRT64 -j16 exit 0, firmware.bin 1568092 bytes.

## S-meter

Report tag: SDR-RA6M3-SMETER-20260822. Evidence class: hardware, on target (COM18).

Signal-strength readout for a UI / waterfall: the block RMS of the channel-filtered
complex baseband (sqrt(mean(i^2+q^2)) via ra_iq_isqrt32), one-pole smoothed over blocks
in ra_iq_dsp_process. Independent of the demod mode and the AGC, so it tracks the actual
in-channel level. iq.smeter() -> {rms, dbfs}; dbfs = 20*log10(rms/2047) computed in the
control plane, floored at -120 when rms == 0.

Bench (no injected signal): rms 0, dbfs -120 in every mode including demod off;
unit1_stalls = 0. Mechanism proven; a real carrier drives rms up toward 0 dBFS. Build
UCRT64 -j16 exit 0, firmware.bin 1568324 bytes.

## Tuning NCO (digital fine-tune) + per-mode bandwidths aligned to the SDR UI

Report tag: SDR-RA6M3-TUNE-20260822. Evidence class: hardware, on target (COM18).

Tuning NCO: a complex down-conversion of the decimated I/Q, in place, AFTER DC removal
+ imbalance and BEFORE the channel filter, shifting a chosen offset inside the captured
baseband (-fs/2..+fs/2, fs = sample_rate/2 = 24 kHz) down to 0 Hz for the fixed channel
filter + demod. Same 32-bit phase / 256-entry Q15 sine scheme as the CW BFO; phase is
continuous across blocks; s_tune_hz == 0 skips the mix (bit-identical to no NCO). Mix for
-f_tune: i' = (i*cos + q*sin)>>15, q' = (q*cos - i*sin)>>15. iq.tune([hz]) -> int, |hz|
clamped just inside fs/2.

Role in the receiver (from the SDR UI app, SDR_TRANCEIVER_UI/sdr_single): the analog
front end is a Tayloe/quadrature detector whose LO is a Si5351 running at 4x the tuned
frequency on CLK1, so the COARSE tuning is the Si5351 and this NCO is the DIGITAL
FINE-TUNE / passband offset within the +/-12 kHz captured window -- no I2C latency, sub-Hz
resolution.

Bench: tune(0/3000/-3000/11999) read back exactly; tune(+/-100000) clamps to +/-11999
(just inside the 12000 Nyquist); unit1_stalls=0 (the complex mix runs fault-free, I/Q
coherence held). Mechanism proven; with a signal, an offset carrier is moved to DC.

Per-mode default bandwidths were aligned to the UI FILTERS map: AM 6 kHz, USB/LSB 2.4 kHz,
CW 500 Hz (channel filter), and the SSB voice audio band-pass upper edge to 2.4 kHz. On
target filter_status reports bandwidth 6000/2400/2400/500 for am/usb/lsb/cw; AM 6k shows
bypassed=1 on the integer one-pole (its ~3820 Hz saturation) -- iq.chf_kernel(True) picks
the f32 biquad that realises the wide skirt. Build UCRT64 -j16 exit 0, firmware.bin
1568648 bytes.

## UI ↔ backend integration + app first start on hardware

Report tag: SDR-RA6M3-UIWIRE-20260822. Evidence class: hardware (app start), on target
(COM18). Full details in SDR_TRANCEIVER_UI/UI_BACKEND_BINDING.md.

The LVGL single-file app (sdr_single.py) was wired to the RA6M3 DSP backend. Firmware
API taken from the port source, not probed. Two applied-patch bugs fixed against the
real API: set_agc (there is NO agc_gain()/agc_target(); the real call is a single
agc(mode, gain=, rms_target=), mode strings off/fast/slow/manual), and poll_status field
names (blocks/overruns from status(), underruns from audio_status().audio_underruns,
clips from agc_status().agc_clips).

Three live paths wired (Ra6m3Backend + SdrApp, driven by the 2 Hz sdr_poll):
- Spectrum waterfall: read_spectrum() fills a once-allocated array('f',256) via
  iq.spectrum() (auto-enabled, None until a snapshot is ready), groups the fftshifted
  magnitude bins into the 27 UI bars by peak, log-scales ~50 dB; demo pattern restored
  on stop.
- S-meter: read_smeter() -> iq.smeter()['dbfs'] into a smeter-value floating label.
- Fine-tune NCO: fine +/- buttons -> iq.tune() offset within +/- fs/2 while RX is live;
  coarse (Si5351) re-centres the NCO; freq display shows f + fine_hz. Contract: coarse =
  Si5351 LO (CLK1 x4, Tayloe), fine = digital NCO -- never both for one delta.

Verified on hardware (headless, panel not observed here):
- sdr_single.py parses clean (host Python 3.10; C:\Python313 is a corrupt install and
  is not used).
- App STARTS: `from sdr_single import start; app = start()` returns exit 0, no traceback
  -- lv.init + display bring-up + UI build + backend init + event loop all clean;
  app.be.available = True (firmware IQADC/DAC present).
- FULL live chain proven: app.start_rx() -> app.be.running = True; app.refresh_live()
  pulls a live spectrum snapshot and the S-meter reads a real level (smeter = -38.6
  dBFS) with err = None. The wired spectrum / S-meter / fine-tune paths run end to end
  against the DSP backend on target.

The app stays a SINGLE self-contained file (no local module imports) and is
REPL-startable WITHOUT persisting to /flash: `mpremote run sdr_single.py` (executes the
file) or paste-run (the __main__ guard calls start()). /flash was left untouched.

## AGT x1/2 clock fix, DSP timing at true 48 kHz, GC/realtime proof, garbage slope

Report tag: SDR-RA6M3-CLOCK-GC-20260822. Evidence class: hardware, on target (COM18).
Commits: b5abf49de (clock fix), 36d943660 (alloc-free accessors).

### The x1/2 sample-rate bug (P0, fixed)

The IQADC's requested 48 kHz ran physically at 24 kHz. ra_agt_timer_set_freq computed the
AGT reload as (PCLK/2)/freq with the AGT counting from its PCLKB/2 source; PCLK is the
CPU/ICLK define (120 MHz on RA6M3) but PCLKB is PLL/4 = 60 MHz, so the period was 2x too
long. Measured block rate was 187.5/s (= 24000/128) instead of 375. Because the DAC's AGT
had the same error, producer and consumer stayed synchronised at half speed -> no
underrun -> silent until the block rate was checked. Every DSP frequency was wrong: CW BFO
700->350 Hz, NCO offset halved, channel-filter cutoffs halved, spectrum bin axis 2x off,
audio 12 kHz not 24 kHz.
Fix (set + readback paths in ra_timer.c): query the real PCLKB via
R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB), as the FSP AGT driver does, instead of the
PCLK macro. No-op where PCLK == PCLKB; corrects the 2x on RA6M3. Shared driver, so
machine.Timer accuracy improves too. After: 375 blocks/s (true 48 kHz).

### DSP time per demod at the true 48 kHz (block budget 2666 us = 320000 cyc @ 120 MHz)

| mode          | DSP max cyc | us  | % budget | FREE us |
|---------------|-------------|-----|----------|---------|
| off (DSP only)|   21 200    | 176 |  6.6%    | 2490    |
| AM            |   58 950    | 491 | 18.4%    | 2175    |
| USB / LSB     |   84 041    | 700 | 26.3%    | 1966    |
| CW            |   63 999    | 533 | 20.0%    | 2133    |

Worst case USB/LSB (32-tap Hilbert + AGC + voice band-pass) = 26.3%, leaving ~74% (1966
us) free every block. CW is lighter (NCO beat < Hilbert). Ample headroom at the corrected
rate for FM, steeper filters, more DSP. (Before the fix these percentages were 2x
optimistic because the real block period was 5.33 ms, not 2.67.)

### GC does NOT disturb the realtime ISR (counter-proof)

Source: on this port MICROPY_PY_THREAD=0 so GC_ENTER/EXIT are no-ops (py/gc.c) and
gc_collect() does not hold interrupts disabled around the collection (gccollect.c). The
ADC block callback is a hard C ISR (adc_scan_end_isr, ra_iq_adc.c:1206), the DAC refill is
in the DMAC ISR (machine_dac.c) -- both fire during GC.
Empirical (correct baseline rate, not an assumed period): across four ~18 ms gc.collect()
during RX at the true 48 kHz, blocks advanced EXACTLY as expected (7/7) and
audio_underruns += 0. So an 18 ms GC pauses only the Python foreground (UI hitch), never
the audio. An earlier "ISR BLOCKED" verdict was a measurement artefact (wrong 2.667 ms
block period assumed instead of the pre-fix 5.33 ms).
Consequence: GC stays ENABLED. It is not an audio-integrity problem, only a UI hitch.

### Garbage slope + text buffer

Measured with gc.disable() over 15 s of driven loop (true rate, no auto-GC masking):
~2232 B/s. Dominated NOT by our loop (spectrum bars/counters are alloc-free C accessors)
but by the LVGL Python callbacks in pRGB.py: the touch read cb is polled ~30 Hz and
allocates indev+data wrappers every poll (even untouched); the flush cb allocates
disp/area/color wrappers per render. At 2232 B/s and ~79 KB free, auto-GC fires ~every
35 s -> an 18 ms UI hitch (audio unaffected).
Applied: the BLK counter now uses a fixed bytearray + label.set_text_static() (mutate the
digit bytes in place, no str/format) -> zero Python string per update. It helped little
because the text was a minor contributor; the touch/flush callbacks dominate.
Remaining option (deferred, UX only): a C display/input bridge (C flush + C touch read +
C RENDER_START/VSYNC registered via the LVGL C API) would remove nearly all the
continuous garbage and let gc.disable() hold during RX. Not audio-critical.

## C LVGL bridge, touch hardening, slider fix, and faster movable spectrum

Report tag: `SDR-RA6M3-UI-SMOOTH-20260822`. Evidence class: mixed. The slider
crash fix and the C bridge were exercised on the panel; the latest touch-state
hardening and movable-spectrum path are source/build verified and still require
the final hardware-in-the-loop pass described below. This section supersedes the
earlier note that the C GUI bridge was only a deferred option.

### C display/input bridge and measured garbage reduction

- LVGL display flush, input read, and `RENDER_START` VSYNC callbacks were moved
  out of Python and into the C display/input bridge.
- The measured idle garbage slope fell from `2232 B/s` to `308 B/s`, an
  approximately `86%` reduction. In the later 40 s slider run the remaining
  background slope was approximately `375 B/s`.
- GC must remain enabled: LVGL can still require a comparatively large temporary
  draw allocation, and permanently disabling GC is not a supported operating
  mode. The previously measured approximately 18 ms collection does not stop the
  ADC ISR or starve the DAC ring, so the remaining effect is a UI hitch rather
  than an audio-integrity failure.

### Slider-at-maximum freeze — fixed and panel verified

- Root cause: the slider main radius was `8`, but its indicator radius was `6`.
  At maximum travel LVGL created an ARGB draw layer; the large contiguous
  allocation could fail on the fragmented MicroPython heap and leave the UI
  task guard stuck.
- Fix: the slider and indicator now use the same radius (`8/8`), eliminating
  that draw layer.
- Panel result: 40 s continuous operation, callback count `28 -> 1011`
  (approximately 25/s), zero `MemoryError`, zero `FROZEN`, and `alive=True`
  throughout. Repeated slider moves to maximum were then confirmed by the user.
- Firmware defence: `lv_utils.py` detects a leaked/stuck nesting guard after
  several ticks and bypasses it, while the scheduled-handler decrement is kept
  in a `finally` path. A future allocation failure should therefore cause at
  most a temporary hitch instead of permanently killing the UI loop.

### Touch bridge — persistent state and bounded I2C

The original bridge reported `PRESSED` only in a poll containing a fresh FT5x06
packet and immediately reported `RELEASED` in the next packet-less poll. It also
performed unchecked I2C work from the touch IRQ and could wait forever for a bus
completion flag.

The implemented hardening is:

- the external touch IRQ only sets a pending flag; I2C is serviced later from
  the C input/poll path rather than inside the IRQ;
- every `R_SCI_I2C_Write`/`Read` result is checked;
- completion waits are bounded to 5 ms and abort on error/timeout;
- `DOWN`, `MOVE`, and `HOLD` keep a persistent `touch_active` state and report
  `PRESSED`; `UP` or zero points report `RELEASED`;
- a packet-less LVGL poll preserves the last touch level and coordinates instead
  of inventing a release;
- `touch_debug()` exposes input polls, pressed polls, last X/Y, active state,
  IRQ count, I2C errors, and timeouts for the final panel test.

Expected UX effect: continuous slider drag, reliable long-press, and removal of
spurious `PRESS_LOST`. The final physical-panel verification of these latest
touch changes remains pending.

### Spectrum scheduler and visible tuning movement

- The shared 500 ms worker was replaced by one 100 ms GUI timer. Spectrum is
  consumed every tick (target `10 FPS`), while status is consumed every fifth
  tick (`2 Hz`). Hardware and volume writes remain pending/edge-driven work.
- The old periodic `hw_poll` path no longer reprograms Si5351 every two seconds.
  Only a real LO/routing/calibration change marks hardware work pending; digital
  fine tuning no longer causes an unnecessary Si5351 I2C transaction.
- The 256-point FFT tap was moved before the NCO/channel-filter path so the UI
  receives a panorama that can move as tuning changes.
- The C reducer shifts the 256 FFT bins by the current digital tuning offset
  before reducing them to 27 display bars. Python no longer applies a second
  coarse bar roll.
- Frequency labels are centred on `LO + fine_tune`. The demo backend keeps a
  residual accumulator so repeated sub-column tuning steps are not discarded.
- Fine alignment is calculated on the 256 FFT bins; the final visible position
  is still quantised by the physical 27 spectrum columns.

### Validation and current state

- `sdr_single.py` parses successfully with Python 3.10 `ast.parse`.
- The combined RA6M3 firmware worktree builds cleanly with exit code 0,
  including `ra/ra_iq_adc.c`, and produces `firmware.hex` and `firmware.bin`.
- Current `firmware.bin` size: `1,570,808` bytes. Link summary:
  `text=1,570,824`, `data=0`, `bss=639,104`, `dec=2,209,928`.
- `git diff --check` passes for the spectrum C/header changes.
- These changes are intentionally not recorded as a commit by this report.

Files involved:

- `C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_UI\sdr_single.py`
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\boards\VK_RA6M3\machine_lcd.c`
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\ra\ra_iq_adc.c`
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\ra\ra_iq_adc.h`
- `C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\machine_iq_adc.c`
- `C:\msys_64\home\teodor\renesas_micropython\modules\lv_utils.py`
- `C:\msys_64\home\teodor\renesas_micropython\modules\pRGB.py`

Final HIL checklist:

1. Flash the newly built firmware and upload the matching `sdr_single.py`.
2. Hold and drag the slider slowly and quickly, including repeated moves to both
   endpoints; confirm no `PRESS_LOST`, freeze, or broken long-press.
3. Check `touch_debug()` and require I2C error/timeout counters to remain zero.
4. Feed a stable carrier, confirm approximately 10 spectrum updates/s, then vary
   fine and coarse tuning in both directions and verify that the peak moves in
   the correct direction while the displayed frequency bounds stay coherent.

## Tear-resistant phased spectrum renderer

Report tag: `SDR-RA6M3-SPECTRUM-REPAINT-20260822`. Evidence class: clean
compile + hardware timing/counter test on VK_RA6M3 / COM18 + visual panel
acceptance by the user.

Firmware commit: `59a99e43e6f19fdd446c4340f115046fa8345d93`
(`[VK_RA6M3][LVGL][SDR] add tear-resistant phased spectrum renderer`).

The fast 10 Hz spectrum still looked like a simultaneous full redraw. The
underlying display is `LV_DISPLAY_RENDER_MODE_DIRECT` with one RGB565
framebuffer, so the GLCDC and LVGL necessarily read/write the same memory. FSP
configures `GLCDC_VPOS` at `back_porch + display_cyc + 1`, after the active 272
rows. Waiting for this pulse is correct, but it is not atomic double buffering:
the repaint must also finish before scanning reaches the spectrum at panel rows
118..169.

Implemented path:

- the 27 Python bar widgets are replaced at runtime by one native C-drawn LVGL
  surface (`LCD.spectrum_attach` / `LCD.spectrum_update`);
- the FFT reducer applies a stable reference plus asymmetric smoothing in C:
  fast attack and slower decay, without Python floats or allocations;
- one 10 Hz target frame is split into five interleaved groups at 20 ms:
  `0,5,10...`, then `1,6,11...`, etc. Only 5-6 distributed bars can change in
  one physical frame, while each individual bar still updates every 100 ms;
- repaint is delta-only. A growing bar writes only the new pixels above its old
  top; a shrinking bar clears only the released pixels. Unchanged bar pixels
  are never rewritten;
- each delta is kept as an independent narrow invalid area. It is no longer
  merged into a nearly 388-pixel-wide rectangle;
- `LCD.render_debug([reset])` reports `(last_us, max_us, frame_crossings,
  line_pulses)` so the single-buffer timing is measurable rather than inferred.

Measured with 100 forced 27-bar target changes over 10 s:

| renderer stage | last render | max render | frame crossings |
|---|---:|---:|---:|
| narrow strips, 3 phases, full-height repaint | 10.385 ms | 26.762 ms | 1 |
| delta repaint, 3 phases | 8.267 ms | 8.886 ms | 0 |
| delta repaint, 5 phases (final) | 4.656 ms | 6.107 ms | 0 |

The final firmware built with exit code 0 and generated `firmware.elf`,
`firmware.hex`, and `firmware.bin`; link summary is `text=1,572,712`, `data=0`,
`bss=639,808`. J-Link programmed and verified the image successfully. The
matching host `sdr_single.py` parsed cleanly and started with
`app._spec_native == True`.

Real RX soak after the repaint change:

- 30 s: app remained running, `mem_free=84,480`, total render max 14.304 ms,
  frame crossings 0;
- following steady 10 s delta: `blocks +3750`, `unit1_stalls +0`,
  `audio_underruns +0`, `ring_overruns +0`, `agc_clips +0`; total render max
  9.532 ms and frame crossings 0;
- the raw capture `overruns` counter also advances once per block because the UI
  deliberately does not consume the diagnostic `read_block()` slot. This is
  not a DSP/DAC loss; the dedicated audio and ring counters above stay flat.

The app is currently running from the host script (not persisted to `/flash`).
Final visual acceptance is **GREEN**: the user confirmed that the spectrum now
looks very good. The interleaved independent-column motion and delta repaint
remove the previous whole-graph flash/shimmer.

Repository note: the native renderer, timing diagnostics, and smoothed C
reducer are tracked in the `renesas_micropython` repository. The matching
`C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_UI\sdr_single.py` and this
external `ra6m3_done.md` directory are not Git repositories; they remain the
verified host-side UI and test record associated with the firmware commit.

## Touch bridge, GUI-freeze recovery, slider crash, fine-tune spectrum, RF/PGA gain, gains panel (2026-08-22)

Firmware commit `1c07a238b` on `master` (pushed to `origin`) plus the follow-up
`iq.gain` change carry this block of UI-hardening and control-plane work.

**Touch.** The FT5x06 path was moved off the interrupt: the external IRQ now only
sets a pending flag, and the I2C read runs inside the C LVGL indev poll with a
5 ms timeout and `R_SCI_I2C_Abort` on a stall; every `R_SCI_I2C_Write/Read`
return code is checked. DOWN/MOVE/HOLD hold a stable PRESSED, UP or zero active
points release, and a missing packet keeps the last state. A C LVGL bridge was
added (`LCD.lvgl_setup` installs C flush, indev and RENDER_START/VSYNC-gate
callbacks) so no MicroPython wrapper is allocated per frame or per input poll;
`pRGB.py` uses it when present and keeps the Python callbacks only as a fallback.
`LCD.touch_debug()` returns `(polls, pressed, x, y, active, irqs, i2c_errors,
timeouts)`. A 30 s hardware-in-the-loop run of hold / long-press / slow slider
drags reported `i2c_errors = 0`, `timeouts = 0`, a steadily climbing indev poll
count (no freeze) and a stable PRESSED during holds.

**GUI-freeze recovery (`lv_utils.py`).** A render or event-callback `MemoryError`
used to wedge the whole GUI permanently: the exception unwound past the binding's
`_nesting--`, so the re-entrancy guard `lv._nesting.value == 0` was never true
again and `task_handler` stopped running (proven: the indev poll count stalled
while the event loop still reported alive and ~78 KB of heap was free). The guard
is not writable from Python, so the fix detects the leak instead -- a genuine
re-entrancy clears within one scheduled tick, a leaked count persists, so after a
few blocked ticks `task_handler` runs anyway; the schedule slot is released in a
`finally`, the default exception sink no longer deinits the loop, and a
`gc.collect()` in the handler reclaims the transient render buffer. The freeze
now degrades to a momentary hitch.

**Slider drag-to-MAX crash.** Dragging the VOL slider to full scale froze the app
at exactly 100 %. Root cause was in `lv_bar.c`: when the indicator radius (6) is
smaller than the background radius (8) LVGL sets a `radius_issue` and renders the
indicator through a temporary ARGB8888 draw layer; at MAX that layer is about
16*130*4 ~= 8 KB and the contiguous allocation fails against the fragmented
MicroPython heap. Setting the indicator radius equal to the MAIN radius removes
the temporary layer. Verified: a 40 s watcher held `alive = True` and a climbing
poll count through repeated drag-to-MAX with no `MemoryError`.

**Spectrum tracks fine-tune.** The FFT tap was moved ahead of the NCO and channel
filter, and the 256-bin C reducer shifts the panorama by `fine_hz` before
reducing to the display columns, so digital fine-tuning physically moves the
spectrum (previously the tap sat after the NCO, which centred the channel before
the FFT so the panel could not move). The periodic Si5351 reprogram was removed
and `update_freq` no longer marks the analog LO for reprogramming on a pure
digital fine-tune; the spectrum consumer runs at 100 ms (10 Hz), status at
500 ms. The GLCDC panel refresh was measured directly through the VSYNC counter
at **50.0 Hz** (252 pulses over 5.037 s, 0 timeouts) -- the internal panel clock
is ~200 MHz / 24 = 8.30 MHz over a 525 x 316 total, not the assumed 60 Hz.

**RF/PGA live gain (`iq.gain`).** `ra_iq_adc_set_pga_gain` / `_get_pga_gain` were
added over the existing `ra_adc_pga_set_gain_ch` layer and exposed as
`IQADC.gain([code])` (code 0..14 = x2.0..x13.3, applied to both I and Q). It is a
safe no-op while the unit runs in `PGA_BYPASS`; making it audible is a separate
AFE-mode decision. The firmware FLASH region (`0x180000`, 1.5 MB) was full, so the
unused raw float `IQADC.spectrum()` accessor was removed to make room (the UI uses
the alloc-free `spectrum_bars` reducer); the method reuses the existing `gain`
qstr to avoid spending a new one. Build exits 0 at `text = 1,572,492`.

**Gains panel (host UI).** The top-right VOL header is now a toggle that opens a
five-slider panel: **RF** -> `IQADC.gain` (PGA code, param `rf`), **AF** -> master
volume (`IQADC.volume`, param `v`), **AGC** -> manual gain (`IQADC.agc(gain=)`,
param `again`) with a live current-gain readout from `agc_status()["gain"]` while
the panel is open, plus two reserved disabled columns. The last-touched slider
becomes active and is what the collapsed VOL spot shows after the panel closes;
all three values persist to data-flash (`rf` key added to the record). This is a
first testable version; the operator noted the layout is not yet the intended
final shape and will refine it. Backend, persistence and firmware are wired and
the file parses cleanly; visual acceptance is pending.

## Native combined spectrum + waterfall (2026-08-22)

The verified 256-point implementation is preserved as firmware checkpoint
commit `3c56616b0` (`[VK_RA6M3][LCD] add aligned 256-bin spectrum above
waterfall`) before the planned 512-point FFT experiment.

At this checkpoint, the alternate **WF** view uses the existing 256-bin C spectrum frame; it
does not run a second FFT and does not allocate a second spectrum buffer. The
same current frame drives both parts of the combined upper display:

- a 256-column live spectrum occupies the top 30 pixels. Every FFT bin is one
  physical X pixel and updates together with the new waterfall row;
- a two-pixel separator is followed by the waterfall, which advances one RGB565
  row at approximately 30 Hz;
- the existing GLCDC RGB565 framebuffer is the waterfall history. Rows are moved
  down in place and the new row is converted directly from the current 256 bins,
  so there is no LVGL image/canvas buffer and no Python rendering callback;
- `LCD.spectrum(0)` selects the original bars view, `LCD.spectrum(1)` selects the
  combined spectrum + waterfall view, and `LCD.spectrum()` reports
  `(view, rows, last_write_us, max_write_us)`;
- the upper spectrum and lower waterfall share the same centred 256-pixel X
  origin, FFT shift and source-bin mapping. A peak is therefore exactly above
  its own history, with no horizontal scaling or interpolation;
- the collapsed bottom **SPEC/WF** button changes only this upper visualization;
  the bottom navigation and its expanded mode/filter/step controls remain free.

The C surface timer runs every 10 ms. The original bars view keeps its proven
10 Hz cadence; the combined view draws all 256 current bins above the matching
waterfall row on a phase-preserving 33 ms deadline. The frozen `pRGB.py` event
loop was set to 50 Hz so the 33 ms deadline can actually deliver about 30
combined updates/s on the measured 50 Hz panel.

Hardware verification after the combined-view change:

| test | rows | measured row rate | last / max direct write | result |
|---|---:|---:|---:|---|
| 10 s | 303 | 30.3 Hz | 1.468 / 2.546 ms | green |
| 30 s soak | 909 | 30.3 Hz | 2.021 / 2.479 ms | green |

Across the 30 s soak, `unit1_stalls`, `audio_underruns`, `ring_overruns`, and
`agc_clips` all stayed at zero. Render frame crossings and touch I2C errors /
timeouts also stayed at zero; final `mem_free` was 73,696 bytes. The firmware
built with `EXIT=0` (`text=1,574,840`, `data=0`, `bss=640,352`) and J-Link
reported successful programming and verification. Functional/timing acceptance
is green; final operator visual acceptance of the new split is pending.

### 512-point FFT follow-up

After preserving the 256-point checkpoint, the spectrum acquisition and
foreground FFT were increased to **512 complex points**. The realtime ADC block
callback still copies the same 64 decimated I/Q samples per 128-sample block;
only the static accumulator depth and foreground CMSIS transform changed.

- `RA_IQ_SPECTRUM_N = 512` and both transform sites use
  `arm_cfft_sR_f32_len512`;
- the complex spectrum rate remains 24 kHz, giving 46.875 Hz/bin and a fresh
  512-sample capture every 21.33 ms, which is sufficient for a 30 Hz consumer;
- the display remains 256 physical X pixels. Each pixel takes the peak of its
  corresponding pair of FFT bins, and that one calculated level drives both the
  upper live spectrum column and the matching new waterfall pixel;
- there is still no second FFT/display history buffer. The static BSS cost of
  doubling the ping-pong I/Q, complex work, window, magnitude and smoothing
  arrays is exactly 6,656 bytes;
- `LCD.spectrum()` diagnostics now return `(view, rows, last_write_us,
  max_write_us, last_fft_us, max_fft_us)`.

Clean 30 s hardware soak after a J-Link reset:

| measurement | result |
|---|---:|
| FFT size / integer API bin spacing | 512 / 46 Hz |
| waterfall rows | 889 (29.6 Hz) |
| steady / maximum foreground FFT | 0.742 / 1.297 ms |
| last / maximum direct framebuffer write | 2.094 / 2.468 ms |
| ADC block period | 2.666 ms |
| realtime DSP average / maximum | 0.532 / 0.532 ms |
| maximum realtime DSP budget | 19.98% |
| final free MicroPython heap | 72,784 bytes |

`unit1_stalls`, `audio_underruns`, `ring_overruns`, `agc_clips`, touch I2C
errors/timeouts, and render frame crossings all stayed at zero in the clean soak.
The firmware built with `EXIT=0` (`text=1,577,016`, `data=0`, `bss=647,008`) and
J-Link reported successful programming and verification.

One earlier 30 s run recorded a single 23.342 ms LVGL render crossing while the
touch diagnostics also recorded 37 pressed polls. Audio/DSP fault counters
remained zero. Per the hardware-test rule, a J-Link reset was performed before
any further device action; the following untouched 30 s run above completed
with a maximum 11.606 ms LVGL render and zero crossings. The crossing belongs
to a separate touch/full-redraw stress case: `render_debug` starts at
`RENDER_START`, after the foreground FFT has already completed.

### Standalone SPEC aligned to the waterfall width

The original standalone 27-bar **SPEC** view no longer fills the old 388-pixel
LVGL object width. Its plot rectangle is now a centred **256 pixels**, using the
same X origin and width as the combined spectrum/waterfall surface. The object
itself keeps its original size, so switching views does not move the surrounding
layout; only the spectrum bars and their dirty rectangles are constrained to the
waterfall-shaped plot area. The accepted combined view remains unchanged: 30
pixels of live spectrum, a 2-pixel separator, then the waterfall.

The corrected firmware built cleanly with `EXIT=0` (`text=1,577,120`, `data=0`,
`bss=647,008`) and J-Link reported successful programming and verification. An
untouched COM18 run after programming kept the combined view active and produced
295 new rows in 10 seconds (29.5 Hz). `unit1_stalls`, `audio_underruns`,
`ring_overruns`, `agc_clips`, touch errors/timeouts, and combined-view render
crossings all stayed at zero; final free MicroPython heap was 71,968 bytes.

### Side-by-side spectrum/waterfall + DAC oscilloscope

The 388-pixel native upper surface is now completely used: the **256-pixel
SPEC/WF panel starts at the far-left edge**, followed by a 4-pixel gap and a
**128-pixel SCOPE panel**. The scope is always visible; the bottom view button
switches only the left panel `SPEC <-> WF`.

`SCOPE` displays the actual unsigned 12-bit samples filled for the DAC DMAC
stream, not an ADC or pre-demodulation tap. Capture occurs in
`machine_dac_iq_fill()` immediately after `ra_iq_adc_audio_pull()`, so the
waveform includes the complete demod, AGC, volume/audio-filter chain and would
also show the exact 2048 mid-scale samples inserted on a real DAC underrun.

- eight 64-sample DAC refill blocks form one independent 512-sample ping-pong
  capture at 24 kS/s (21.33 ms); the triggered right-hand view shows 128 samples
  / 5.33 ms;
- the ADC I/Q FFT capture and DAC scope capture run simultaneously. The new DAC
  ping-pong costs exactly 2,048 bytes of static BSS, not MicroPython heap;
- capture is integer-only and allocation-free. The renderer reduces the claimed
  half to 128 final Y coordinates before waiting for VSYNC, so the DAC producer
  can safely continue without a third buffer;
- the scope uses a rising hysteretic trigger and slow-release automatic vertical
  scale. The zero line and waveform are drawn directly into RGB565;
- in WF mode, the left live spectrum/waterfall and right scope are committed in
  one C framebuffer transaction after one line-detect pulse;
- in SPEC mode, the five interleaved 27-bar phases are retained, but their stable
  state and the right scope are also committed together directly. Periodic LVGL
  bar invalidations were removed, eliminating the measured render crossing.

The final firmware built with `EXIT=0` (`text=1,578,248`, `data=0`,
`bss=649,056`). The +2,048-byte BSS delta exactly matches the independent DAC
ping-pong. J-Link programming and verification completed with `O.K.`, and the
updated `sdr_single.mpy` was installed on the board.

Clean hardware integration test using the real selected AM receiver path (no
synthetic injection and no demod/AGC/filter/volume changes):

| simultaneous view | cadence | last / maximum direct write | render crossings |
|---|---:|---:|---:|
| `SPEC + SCOPE` | 148 / 5 s (29.6 FPS) | 1.655 / 2.060 ms | 0 |
| `WF + SCOPE` | 303 / 10 s (30.3 FPS) | 2.368 / 2.887 ms | 0 |

Realtime DSP maximum was 12.60% of the 2.666 ms block and final free
MicroPython heap was 68,016 bytes. `unit1_stalls`, `audio_underruns`,
`ring_overruns`, `agc_clips`, touch errors/timeouts and render frame crossings
all stayed at zero. The panel was left in `WF + SCOPE` for operator visual
inspection; source and firmware changes are not yet committed.

#### Scope alignment and HOME/menu framebuffer guard

The SCOPE zero line was moved four pixels upward from the geometric centre of
the asymmetric native object to the exact shared SPEC/WF divider coordinate:
`obj.y1 + 18 top pad + 30 spectrum height + 2 gap`. Waveform conversion and the
drawn grid line use the same coordinate. Vertical scaling now uses the smaller
distance to the upper/lower edge, so a full-scale trace does not clip after the
offset.

A real native-render ownership bug was also confirmed and fixed. `_modal`
previously stopped only the Python fallback consumer; the C timer continued
writing SPEC/WF/SCOPE pixels directly into the framebuffer. `lv_obj_is_visible()`
does not prove that an object's screen is active, so a full-screen gains,
settings, routing or frequency-entry view could be overwritten, as could an AGC
picker overlay on the receiver screen.

The C timer now permits direct writes only when
`lv_obj_get_screen(spectrum_obj) == lv_screen_active()` and the new explicit
`spectrum_pause` flag is clear. Python centralizes its non-HOME/overlay state in
`_set_modal()`, which drives `LCD.spectrum_pause()` for same-screen overlays.
Resuming resets the native deadlines/history and invalidates the surface so HOME
is rebuilt instead of exposing pixels owned by the menu.

The corrected firmware built with `EXIT=0` (`text=1,578,412`, `data=0`,
`bss=649,056`), J-Link programming/verification completed with `O.K.`, and the
new `sdr_single.mpy` was installed. On COM18, 400 ms holds produced zero native
rows under the AGC popup, the GAINS slider screen and the frequency-entry screen.
Forcing the explicit pause off while GAINS remained active still produced zero
rows, independently proving the C active-screen guard. Returning HOME resumed
at 61 rows / 2 seconds (30.5 FPS), with zero render frame crossings,
`unit1_stalls`, `audio_underruns`, `ring_overruns` and `agc_clips`. Changes remain
uncommitted pending operator visual confirmation.

## DSP path verification primitives + 256 KB flash reclaim (2026-08-22)

Local commit `62523210d` (not pushed).  Two earlier local commits pushed to
`origin/master` carry the RF/PGA gain and the flash-region change: `1c07a238b`
(RF/PGA gain, raw `IQADC.spectrum()` removed) and `02526517b` (linker reclaim).

**Flash reclaim.**  The firmware region was full (168 bytes free), which blocked
adding any verification control.  The internal `FLASH_FS` is not used on this board
-- the real filesystem is on the external QSPI flash -- so `vk_ra6m3.ld` moves the
FLASH / FLASH_FS boundary up 256 KB: FLASH 1.5 MB -> 1.75 MB, FLASH_FS 512 KB ->
256 KB @ 0x1C0000.  The storage start/end symbols derive from the region, so nothing
else changed.  Firmware now links at `text = 1,574,528` with ~256 KB headroom.

**Verification harness.**  Three primitives were added so every DSP block can be
driven from a known input and inspected in isolation, all control-plane, with no cost
to the realtime path when disarmed:

- `IQADC.inject(enable[, freq, ampl])` -- overwrite the raw ADC block with a synthetic
  complex tone at `freq` Hz, base amplitude `ampl` in ADC counts, scaled by the CURRENT
  PGA gain, so the whole front end (PGA -> decimate -> IQ corr -> NCO -> channel filter
  -> demod -> AGC -> volume) runs on a controllable stimulus without RF.
- `IQADC.tap(stage[, buf])` -- snapshot the decimated I/Q at a pre-demod boundary
  (1 = decim, 2 = nco, 3 = chfilt) into an `array('h')` as interleaved i/q pairs for
  numeric comparison over UART; one ISR-side copy only when the armed stage matches.
- `IQADC.demod("thru")` -- verification passthrough: the channel-filtered real part fed
  straight into the audio stage (no demod), so the pre-demod baseband is audible /
  scope-able while the AGC + volume tail still runs.

**On-target internal-source verification** (historical `test_verify.py`, PGA_SINGLE,
AM, auto AGC, 1 kHz inject). All three pre-demod taps returned 64 i/q pairs. Sweeping
the internally generated input made the AGC servo back its gain off and the S-meter
track the level, proving that the digital chain reacts to a known stimulus:

| input amplitude | AGC gain | S-meter rms | dBFS |
|---:|---:|---:|---:|
| 50   | 8.00 (max) |   99 | -26.3 |
| 600  | 8.00       | 1188 |  -4.7 |
| 1200 | 5.23       | 2229 |  +0.7 |
| 2000 | 3.42       | 2532 |  +1.8 |

Sweeping the PGA gain code at a fixed amplitude reproduced the same behaviour (rms
595 -> 2533, AGC gain 8.00 -> 3.42 from code 0 to 14). This verifies the internal
TESTER model: an IN source is multiplied by the configured PGA factor before entering
the digital blocks. It is **not** an analogue PGA transfer/bandwidth measurement,
because the synthetic source replaces the physical ADC samples.

**Still open:** a dual-DAC stage-output router (route any pre-demod stage's I to DAC0
and Q to DAC1 for the scope, audio stages to DAC0) and the Settings menu that drives
all of these per-block from the panel ("SDR RECEIVER" entry, VFO routing + CAL folded
in).  The router is the heavy realtime piece (a second DMAC DAC stream).

## Per-block verification model (operator spec, 2026-08-22)

The backend/verification screen is one ROW per DSP block, reached by drilling down two
full-screen levels: receiver -> "SDR RECEIVER" -> VFO ROUTING screen (routing + CAL +
a "BACKEND >" button) -> the per-block verification screen. Every level is a full screen
(no popups). Each block row carries three independent controls:

1. **ON/OFF** -- per block, independent. OFF short-circuits the block to the next one
   (bypass / passthrough), so a stage can be removed from the chain to isolate it.
2. **UART tap** (checkbox) -- dumps that block's samples over UART for numeric comparison.
   Only ONE block may be tapped at a time (radio behaviour across the whole screen).
3. **-> Scope (DAC)** -- routes that block's output to the DAC(s) for the oscilloscope.
   Only ONE block at a time (there are two DACs). Pre-demod blocks are complex, so I ->
   IDAC (DAC0) and Q -> QDAC (DAC1); from the demod onward the signal is mono, so it goes
   to a single DAC.

A separate INJECT control at the head feeds a synthetic tone (scaled by the PGA gain) so
the whole chain runs on a known stimulus.

| # | DSP block | signal | ON/OFF | UART tap | Scope (DAC) |
|---|---|---|---|---|---|
| - | INJECT (input) | stimulus | - | - | tone x PGA at the head |
| 1 | PGA | I/Q | yes | one-of | IDAC + QDAC |
| 2 | Decimation | I/Q | yes | one-of | IDAC + QDAC |
| 3 | IQ correction | I/Q | yes | one-of | IDAC + QDAC |
| 4 | NCO (tune) | I/Q | yes | one-of | IDAC + QDAC |
| 5 | Channel filter | I/Q | yes | one-of | IDAC + QDAC |
| 6 | Demod | I/Q -> mono | yes | one-of | DAC (mono) |
| 7 | AF filter | mono | yes | one-of | DAC (mono) |
| 8 | Squelch | mono | yes | one-of | DAC (mono) |
| 9 | AGC | mono | yes | one-of | DAC (mono) |
| 10 | Volume | mono | yes | one-of | DAC (mono) |
| 11 | Limiter | mono | yes | one-of | DAC (mono) |

Build status against this model: `iq.inject` (done, verified), `iq.tap` (done for the
decim/nco/chfilt boundaries, needs PGA + the post-demod audio boundaries + the one-of
radio), `iq.demod("thru")` passthrough (done). Still to build: a unified per-block ON/OFF
bypass (decim + a clean per-stage flag), the tap extended to every block with the
one-at-a-time rule, and the IDAC/QDAC + mono-DAC scope router (the heavy realtime part).
The three full-screen navigation levels (receiver -> routing -> backend) are done.

## Streaming DC removal rework — non-coherent tone fixed (2026-08-23)

The old DC remover recomputed one I/Q mean for every 64-sample decimated block.
A 1 kHz tone at 24 kHz contains 8/3 periods per block, so its finite-block mean is
non-zero and rotates by 240 degrees from block to block.  Subtracting that moving
mean shifted the complex origin once per transport block and made a constant-radius
tone look phase/amplitude modulated.  The transport boundary was incorrectly treated
as a signal boundary.

The production path in `ra/ra_iq_adc.c` now uses one persistent sample-by-sample
DC estimator per channel:

- signed Q16 state, alpha = 1/4096 (about 0.93 Hz high-pass corner at 24 kHz);
- output and state updates use sign-symmetric truncation toward zero;
- state survives ordinary transport blocks and resets only at stream/source lifecycle
  boundaries;
- the 11-tap hand and CMSIS decimators start from ADC midpoint history (2048), not
  zero, and a live hand/CMSIS request is applied by the producer on a block boundary;
- injection `{enable, frequency, amplitude}` is published as one block-boundary
  transaction; amplitude is clamped to the 12-bit centred range;
- valid raw block sizes are even 10..256; `dsp_status()` is a coherent sequence-guarded
  snapshot and reports 2048/2048 before the first block;
- `ra_iq_adc_acquire()` preserves a completion that becomes pending during its short
  IRQ snapshot by using `R_BSP_IrqEnableNoClear()`.

### Reference and source-contract proof

`tests/sdr_iq_unit_vectors.py`: **11/11 PASS**.  The new non-coherent/DC tests report:

- 1 kHz reference radius ripple 0.4007%; maximum phase error 0.1294 degrees;
- boundary phase error 0.1177 degrees; boundary radius jump 0.0884%;
- DC-step integer tail I/Q = 0/0;
- raw block partitions 64/128/256 produce bit-identical samples and final state.

### On-target proof (VK_RA6M3 / COM18)

Before the rework, the 1 kHz decimator tap measured 21.1383% radius ripple and
13.4034..16.7606 degree phase steps; exact AM magnitude produced 106 DAC counts
peak-to-peak from a constant-envelope tone.  The coherent 1.5 kHz control was stable,
which isolated the fault to block coherence rather than the NCO or DAC.

After the rework (`rate=48000`, `block=128`, injection amplitude 500):

| path | tone | radius ripple | phase-step min..max | average |
|---|---:|---:|---:|---:|
| hand decimator | 1 kHz | 0.4288% | 14.8124..15.1162 deg | 14.9974 deg |
| CMSIS decimator | 1 kHz | 0.4289% | 14.8124..15.1559 deg | 15.0012 deg |
| hand decimator | 1.5 kHz | 0.2322% | 22.3503..22.5670 deg | 22.4996 deg |

The independent `_scope_phase_ab.py` run measured 0.5026% at 1 kHz and reduced
the false AM output from 106 counts p-p to **1 count p-p** (1.5 kHz remained 0).
`tests/sdr_dc_streaming_hil.py` also proved that block 9/11/257 are rejected,
hand -> CMSIS -> hand switching keeps the producer alive, `unit1_stalls=0`, and
the maximum DSP time is 24,883 cycles = 7.776% of the 320,000-cycle block budget.

Full VK_RA6M3 build completed with exit 0 (`text=1,580,508`, `bss=649,632`,
`firmware.bin=1,580,492` bytes). J-Link program and verify completed with `O.K.`.
The receiver app then started from `/flash/sdr_single.mpy` with `APP_STARTED`; the
operator image confirmed the combined waterfall/spectrum and scope layout.  With
synthetic injection disabled by HIL cleanup, the scope correctly showed its flat
mid-scale/quiet trace.

Evidence boundary: synthetic injection is centred at 2048, so it proves the old
non-coherent-tone distortion is gone but does not electrically exercise a real ADC
DC step.  The exact step response is presently reference-tested; a physical DC-step
HIL still needs an analogue offset source or an injection API with independent I/Q
offsets.

## Shared-AGT dual-DAC scope router + I/Q ring desync fix (2026-08-23)

Local commit `36cbdfa90` (not pushed), 6 files, +668/-110: `ra/ra_dac.c`,
`ra/ra_iq_adc.c`, `ra/ra_iq_adc.h`, `machine_dac.c`, `machine_iq_adc.c`,
`qstrdefsport.h`. The IDAC/QDAC scope router -- the heavy realtime piece left open
above -- is now built.

**Blocker resolved (AGT pool exhaustion).** An architect review found the router
could not run because RA6M3 exposes only two AGT channels (`AGT_CH_SIZE = 2`,
`ra/ra_timer.c:48`). The IQ ADC holds one AGT for its ELC scan clock
(`ra/ra_iq_adc.c:1626`) and each DAC stream, as written, reserved its own AGT
(`ra/ra_dac.c`). IQ ADC + DAC0 therefore consumed both, so the second DAC (the
scope Q channel) failed with "no free AGT timer". DMAC is not contended (eight
channels; the IQ ADC uses DTC, not DMAC). The operator confirmed the fix
direction: one timer channel for both DACs.

**Fix -- one shared AGT drives both DAC streams.** `ra_dac.c` now refcounts a
single file-static sample clock (`s_dac_stream_timer_ch`, `s_dac_stream_timer_users`):
the first stream to start reserves the AGT and owns it; the second stream borrows
the same channel without reserving a new one and hangs its own DMAC on the same
AGT ELC event. Both DAC DMACs therefore fire on one tick, so DA0/DA1 clock
coherently from a single source (the ARCH-TRIG-002 single-source doctrine applied
to the output side). The AGT is released only when the last stream stops; a lone
audio DAC stream reserves on start and frees on stop exactly as before. No public
signature changed -- `ra_dac.h` and `machine_dac.c` needed no edit because the
existing binding already passes `timer_ch = -1`. The DTC path keeps a dedicated AGT
(DTC activation installs a per-IRQ completion callback on the AGT and is not
shareable); only the DMAC double-buffered path shares.

**I/Q ring desync fixed.** `ra_iq_scope_push_iq` (`ra/ra_iq_adc.c`) previously
advanced the Q head independently while a full I ring counted an overrun and left
its head, drifting the two rings by one sample and rotating the scope
constellation. The pair is now published atomically: on a full ring it counts one
overrun and advances neither head. The path stays lock-free SPSC.

**Evidence class: compilation only (REQ-WORK-004).** Firmware built `EXIT=0`,
`firmware.bin = 1,580,180` B (`text = 1,580,196`). The dual-DAC scope route
(P014/DA0 + P015/DA1) is not yet hardware-verified -- a J-Link reset plus a HIL run
of both DAC streams (both succeed, stopping one keeps the other, stopping both
frees the AGT) remains the acceptance step.

Note: an earlier full-build failure during this work was a stale `py/sequence.o`
from an interrupted parallel build, not a code defect; a rebuild linked cleanly.

## Mono/dual DAC lifecycle + deterministic I/Q restart phase (2026-08-23)

The compilation-only boundary above is now superseded by on-target HIL.  The output
topology is explicit: DAC0 can run alone for mono/post-demod audio, while a pre-demod
scope route runs DAC0=I and DAC1=Q from one shared AGT sample clock.  Adding or removing
DAC1 no longer blocks or resets the mono producer, and an inactive DAC1 returns to
mid-scale (2048).

The lifecycle defect behind `IQADC_INIT_FAIL:agt_reserve` was measured before the fix
as `IQADC_AGT_REJECT:9,9`: both AGT channels timed out waiting for `TCSTF` to clear.
The cause was register access before releasing AGT module-stop state.  The common
`ra_agt_timer_stop_wait()` now performs canonical FSP module-start (protected RMW plus
readback) before any AGT register access, waits against a real 20 ms bound, and fails
closed while retaining ownership if the count clock does not acknowledge the stop.
It does not treat an MSTP toggle as a peripheral reset.  The missing AudioADC soft-reset
hook was also added; its checked teardown releases both its AGT and the shared
`VECTOR_NUMBER_ADC0_SCAN_END` DTC activation source before IQADC is cleaned up.

IQADC teardown now gates the block callback, disables and clears ADC0/ADC1 scan-end IRQs,
stops both scans, and only then disables/closes the DTC.  This prevents a pending
scan-end callback from re-arming a descriptor after close.  PGA-off, ADC close, DTC
close, and AGT teardown results are checked; soft-reset cleanup remains fail-closed.

### Build and source/reference proof

- VK_RA6M3 full build: **exit 0**; `text=1,584,020`, `bss=649,664`;
  `firmware.bin=1,584,004` bytes; ELF/HEX/BIN all generated.
- `tests/sdr_iq_unit_vectors.py`: **11/11 PASS**.
- `git diff --check`: clean; final read-only P0/P1 review found no source blocker.
- J-Link flash: Program & Verify **O.K.**.  No separate recovery reset was issued after
  the successful flash or between the successful HIL runs.

### VK_RA6M3 / COM18 HIL

- Mono DAC0 probe: range `1552..2543`, 21 distinct sampled codes;
  `ring_overruns=0`, IQADC `running=1`, `unit1_stalls=0`.
- Dual scope probe: DAC0(I) `1552..2543`, DAC1(Q) `1552..2543`, 26 distinct sampled
  codes on each; `ring_overruns=0`, `unit1_stalls=0`.
- Topology/lifecycle hammer (25 complex <-> mono switches): `HIL_MODES_PASS`;
  initial dual `(1552,2543)/(1552,2543)`, late DAC1 join
  `(1552,2544)/(1552,2544)`, mono DAC0 `(1968,2123)`, stopped DAC1 exactly `2048`,
  `audio_underruns=0`, `ring_overruns=0`.
- Eight independent scope-route restarts produced correlation phases:
  `88.2331, 87.0650, 86.7696, 88.0047, 87.8214, 87.9410, 86.8284, 87.9697` degrees.
  Range: **86.77..88.23 degrees**, so the former arbitrary restart rotation is gone.

Evidence boundary: the phase test samples the live DAC peripheral codes through
`DAC.read()` and proves firmware/sample-index coherence across repeated starts.  A
simultaneous two-channel oscilloscope measurement at P014/DA0 and P015/DA1 is still the
required proof of the electrical pin-to-pin phase, analogue settling, and board loading.

## No-LAN memory build, modulation test source, DSP VERIFY controls and native I/Q constellation (2026-08-23)

The Ethernet/FSP driver is now excluded from the VK_RA6M3 SDR build (`USE_FSP_ETH`
undefined, `MICROPY_PY_LWIP=0`).  The final ELF contains no `g_ether0`, ETHER PHY,
EDMAC, LAN or lwIP symbols.  The reclaimed RAM is assigned to the MicroPython heap:

- `.heap = 0x47000` = **284 KiB**, `0x1fff3b00..0x2003ab00`;
- final `.bss = 645,596` bytes; the new stable I/Q snapshot costs exactly 2,048 bytes;
- linker RAM/framebuffer overlap assertion passes; `__RAM_segment_used_end__ =
  0x2003eb00`, leaving **5,376 bytes** before framebuffer memory at `0x20040000`;
- final `-j16` VK_RA6M3 no-LAN build: **exit 0**, `text=1,412,428`,
  `firmware.bin/.hex/.elf` generated.

### Persistent synthetic I/Q source and modulation proof

`IQADC.inject()` remains backward compatible and now accepts a block-atomic tuple:

`inject(enable, carrier_hz, amplitude, kind, mod_hz, depth_pct, gate_hz, phase_noise)`

Kinds are IQ, true AM, USB, LSB and CW.  The C producer preserves carrier/modulator/gate
phase across transport blocks and live parameter changes; the optional 50% pulse gate
does not restart carrier phase.  Deterministic bounded phase jitter is lookup-only and
cannot random-walk the phase accumulator.  The AM UI cap is 1000 counts, which leaves
12-bit headroom at the 50% depth preset.

Host reference/source-contract suite: `tests/sdr_iq_unit_vectors.py` **18/18 PASS**,
0 failures, exit 0.  It proves legacy bit identity, AM envelope/headroom, USB/LSB
polarity, CW, pulse phase continuity, deterministic LIVE jitter, and bit-identical
64/128/256 transport partitions.

Final COM18 modulation HIL on the flashed image:

- AM CLEAN: **999.849 Hz**, RMS 333.45, span 935;
- LSB wanted/reject RMS: **760.25 / 13.08** (about 58x);
- USB wanted/reject RMS: **769.71 / 12.86** (about 60x);
- CW CLEAN: **710.150 Hz**, RMS 490.70;
- pulse windows alternate strong ON (about 332 RMS) and near-zero OFF
  (0.38..2.23 RMS) while phase remains continuous;
- LIVE phase-step standard deviation **0.007174** versus CLEAN **0.001731**
  (4.14x visibly live, deterministic jitter);
- +938 processed blocks; `audio_underruns` unchanged at 64, `ring_overruns=0`,
  `unit1_stalls` unchanged; maximum DSP **20.46%**; `mem_free=213,456`.

### VERIFY as a real DSP control surface

The transient VERIFY screen now controls/reads the implemented backend instead of
displaying placeholders:

- test presets AM/USB/LSB/CW, SINE/PULSE, CLEAN/LIVE, amplitude and ON/OFF;
- per-block bypass, one-of TAP, and one-of SCOPE routing;
- channel bandwidth including bypass, NCO offset, AGC target, squelch threshold plus
  live envelope/open state, audio-filter preset, I/Q correction enable/amplitude/phase,
  and the four kernel selectors;
- limiter is truthfully labelled `SAFE` (the C clamp is unconditional);
- RF gain is truthfully disabled as `RF GAIN (BYP) / FIXED`, because this receiver is
  deliberately constructed with `PGA_BYPASS`;
- audio-filter presets now pass the required strings (`off/am/voice/cw`), and the
  Hilbert default is read from hardware rather than assumed false;
- callbacks commit their cache/paint only after the backend call succeeds; a void
  `tap()` success is distinguished from an unavailable/failed call.

On-target build/delete lifecycle proof (two consecutive VERIFY cycles):

- HOME before: 128,544 B free;
- VERIFY #1: 75,808 B; HOME #1: 127,600 B;
- VERIFY #2: 75,536 B; HOME #2: **127,600 B**;
- no retained screen/callback growth and no stale navigation flag.

### Native right-panel TIME / I-Q view

The 388-pixel native graph is now physically split into 256-pixel SPEC/WF, a 4-pixel
gap, and a 128-pixel right panel.  Tapping the left 256 pixels tunes; tapping the right
panel toggles `TIME` and `I-Q`.  The constellation is drawn at 30 Hz directly into the
RGB565 framebuffer from 64 consecutive complex samples.  It allocates no Python
objects/widgets and reuses the same completed 512-sample capture that feeds the FFT.
The left spectrum stays on its proven 10 Hz shared Q8 attack/release reducer, so changing
TIME/I-Q does not reintroduce the old whole-spectrum twitch.

A pre-flash ownership audit found that directly returning a producer ping-pong half was
racy near a half transition.  The foreground now atomically claims and copies one full
2 KiB I/Q frame under a short IRQ mask; FFT and constellation both use that immutable
snapshot.  Publication has a DMB, all three direct writers share physical framebuffer
bounds/stride checks, and LVGL SPEC redraw clears only the left plot rather than erasing
the right native panel.

Final constellation/DSP HIL:

- SPEC+I-Q: direct write last/max **2.263/2.461 ms**, FFT last/max **0.772/1.437 ms**;
- WF+I-Q: **104 rows in 3.5 s** (29.7 rows/s), direct max **2.983 ms**;
- WF+TIME continued to 138 rows; LVGL render max **7.968 ms**;
- `frame_crossings=0`; no increase in `unit1_stalls`, `audio_underruns`, or
  `ring_overruns`; final DSP maximum **26.627%** of the 320,000-cycle block budget;
- HOME heap after the complete HIL: **127,600 B free**.

J-Link programmed and verified the final no-LAN image with **O.K.**, followed by the
required separate J-Link reset+go.  The final `/flash/sdr_single.mpy` was uploaded and
both HIL programs completed with exit 0.

GitHub backup is complete: commit **`c42e1809e`** was pushed to `origin/master`
(`https://github.com/tvendov/vkth_micropython.git`).  The exact final UI source is at
`ports/renesas-ra/boards/VK_RA6M3/examples/sdr_single.py`; its SHA-256 is
`6140679D6F0C39F71E3B4152B8A283ADE45FBF8C2793D42EAB282EADFE010AA2`, identical to
the working UI file in `SDR_TRANCEIVER_UI`.

## VERIFY scrolling ownership fix (2026-08-23)

The transient VERIFY list no longer asks LVGL to reconstruct the whole screen for
each finger step.  Python stages one bounded `scroll_rect(x, y, w, h, dy)` request;
the C display bridge applies the overlap-safe RGB565 framebuffer move at
`RENDER_START`, immediately after VSYNC, and LVGL repaints only the newly exposed
strip.  The move therefore belongs to the same display transaction as the redraw,
instead of racing GLCDC from the touch callback.  VERIFY also defers live-label and
native-plot work during active scrolling and performs one coherent refresh after
release.

The measured 16.59 ms panel frame now uses one common 25 ms VSYNC timeout.  The C
bridge counts VSYNC timeouts, render crossings, render count, invalidate requests and
full-screen invalidate requests through the expanded `render_debug()` tuple.  This
source is included in the successful full build and flashed image.  The last operator
feedback confirmed the moving phase was substantially improved; a final subjective
check of the release frame remains a panel/UX observation, not part of the DSP HIL.

## Multi-point TESTER, analytic waveforms and strict target matrix (2026-08-23)

VERIFY's former TEST row is now a complete complex-signal TESTER.  Its compact row
selects the real insertion boundary (`IN`, `MID`, `OUT`), signal kind
(`AM`, `USB`, `LSB`, `CW`, `IQ`), waveform (`SIN`, `SQR`, `TRI`, `PULSE`),
deterministic phase-noise mode (`CLEAN`/`LIVE`), amplitude and ON/OFF.  TESTER is
bench-only and is not written into the persistent receiver configuration.

The insertion points are actual DSP boundaries, not UI labels:

- `IN`: unsigned raw ADC I/Q at 48 ksample/s, before x2 decimation;
- `MID`: centred signed I/Q at 24 ksample/s, after decimation/DC/IQ correction and
  before the tuning NCO;
- `OUT`: centred signed I/Q at 24 ksample/s, after the channel filter and before
  demodulation.

The complete point/wave/mode/frequency/amplitude tuple is published through one
seqlock and consumed only at a DSP block boundary.  Carrier, modulation, gate and
phase-noise state stay continuous across transport blocks.  SQUARE is a bounded
analytic odd-harmonic series with exact signed ratios `1:-1/3:+1/5:-1/7`;
TRIANGLE uses `1:+1/9:+1/25:+1/49`.  Harmonics above the selected point's Nyquist
limit are not generated.  AM always uses a sinusoidal analytic carrier and the
selected real envelope; CW SIN/SQR/TRI use a full-depth unipolar 10 Hz key and
PULSE uses the independent 50% gate.  USB changes only the final Q sign.

The fixed-point operation order remains legacy-bit-exact: AM/CW first calculate
`sample_amplitude = amplitude * gain >> 15`, then multiply the carrier, and USB
negates Q only after the amplitude multiply.  This preserves the old USB/LSB mirror
and AM headroom vectors exactly.

While TESTER is ON, spectrum/waterfall, constellation, stage-5 scope and DA0/DA1 all
observe the same post-channel-filter complex block.  DA0 is I and DA1 is Q.  Spectrum
frames carry capture-domain metadata, so a TEST frame already downstream of the NCO
is not shifted a second time by the foreground reducer.  Turning TESTER OFF restores
the previous demodulation, bandwidth and scope route.

### Build, host proof and strict COM18 HIL

- `tests/sdr_iq_unit_vectors.py`: **22/22 PASS**, including the unchanged original
  18 legacy oracles plus four exact point/wave/source-contract tests.
- Full VK_RA6M3 `-j16` build: **exit 0**; `text=1,403,936`, `bss=645,648`,
  `firmware.bin=1,403,920` bytes; ELF/HEX/BIN generated.
- J-Link flash: Program & Verify **O.K.**, followed by the required separate J-Link
  reset+go.  `/flash/sdr_single.mpy` uploaded successfully.
- `sdr_inject_point_wave_hil.py`: **PASS, 60/60 cases** = five modulation kinds x
  three insertion points x four waveforms.  A zero-allocation fault checkpoint runs
  after every source transition and every case, before any later `demod()` can reset
  audio counters.
- Measured analytic harmonic magnitudes:
  - SQR: `0.333574`, `0.200274`, `0.142403` for 3rd/5th/7th;
  - TRI: `0.111022`, `0.039676`, `0.020289`;
  - SIN unwanted harmonic leakage below `0.00013`.
- CW key and PULSE gate reached near-zero OFF and about 680--700 RMS ON at every
  applicable point.
- Physical-stream readback: DA0/DA1 spans `1382/1393` codes and absolute I/Q
  correlation `0.00133`; both DAC streams remained alive.
- Strict final realtime result: `unit1_stalls=0`, `audio_underruns=0`,
  `ring_overruns=0`; DSP `max_cyc=95,655` of `320,000`, **29.89%** maximum.

`IQADC.overruns` is intentionally not a realtime-fault counter in this mode: it
counts completed raw blocks not claimed through the optional Python `read_block()`
API.  The C DSP consumes every block independently, so the strict HIL checks that raw
snapshot drops track block progress while keeping the real stall/audio/ring fault
counters at zero.

The final source uploaded to the board and copied to
`ports/renesas-ra/boards/VK_RA6M3/examples/sdr_single.py` has SHA-256
`38674C899923689CE456B11FB7AEFB6536E7EC0537002E97372DE7AA09E7231E`.
The application was J-Link reset and started successfully after the final HIL.
GitHub backup commit **`57a614552`** was pushed to `origin/master`.

### Follow-ups completed in the next section

- IQ-file source: accept SDRangel 16-bit `.sdriq` (32-byte checked header) from
  `/flash`, with two rooted 8192-byte Python ping-pong buffers and event-driven refill;
  no static C sample ring and no resampler in v1 (`IN=48 ksample/s`,
  `MID/OUT=24 ksample/s`).
- Spectrum cursor channelisation: route the selected frequency through the existing
  NCO and channel filter to the scope, so moving the cursor away from a tone makes the
  scope signal disappear.  This must be a DSP routing change, not selection of one FFT
  bin.

## SDRangel I/Q file playback and cursor-channelised scope (2026-08-23)

Both formerly pending items above are now implemented and verified on COM18.

### Strict SDRangel FILE source

The TESTER row can switch between the native generator and `/flash/test.sdriq`.
Version 1 follows the SDRangel File Input 32-byte little-endian header contract:
sample rate, 64-bit centre frequency, 64-bit timestamp, sample size, zero reserved
word and CRC32 over the first 28 bytes.  It deliberately accepts only interleaved
signed S16LE I/Q.  `IN` requires 48 ksample/s; `MID` and `OUT` require 24 ksample/s.
CRC, reserved-word, word-size and rate mismatches are rejected before the native
consumer borrows a pointer.  SDRangel 24-bit files and files needing resampling are
reported explicitly and are not misinterpreted.

The native API is versioned as `FILE_API_VERSION=1`:

- `file_attach(buf0, buf1, point, refill_cb)`;
- `file_commit(index, valid_bytes)`, where zero is an ordered EOF marker;
- `file_start()`, `file_stop()`, `file_free()` and allocation-free
  `file_status_into(array('i', 15))`.

Python owns and roots two 8192-byte writable refill buffers for the life of the UI.
C owns only a 92-byte descriptor/control state plus three MicroPython GC roots; there
is no second static sample ring.  FREE/READY/ACTIVE publication uses barriers and the
scheduled refill callback is coalesced and epoch-protected.  If ACTIVE data ends in
the middle of one DSP fill, consumption continues immediately from the already READY
other buffer.  Zero fill and `underrun` happen only when no continuation exists.
Exceptions fail closed, ONCE EOF is ordered behind the partial tail, LOOP/ONCE and
progress are visible in TESTER, and the stage-5 scope route is restored on every
stop/error path.  The 32-bit native consumed count is extended across wraps in Python
for long-running progress display.

Two deterministic SDRangel files contain 4512 phase-continuous complex samples and
are deliberately not block-aligned.  The strict HIL additionally used 2049-complex
refill buffers (8196 bytes), forcing every ACTIVE-to-READY handoff one sample into a
DSP block rather than accidentally proving only an aligned swap.

On-target results:

- 48 ksample/s at `IN`: 25 observed handoffs, 76 consecutive DSP boundaries,
  minimum output radius 744, maximum phase error **0.001847 rad**;
- 24 ksample/s at `MID`: 12 handoffs, 79 consecutive boundaries, minimum radius
  11999, maximum phase error **0.0000297 rad**;
- 24 ksample/s at `OUT`: the same 12/79 handoff proof, radius 11999 and
  **0.0000297 rad** maximum phase error;
- ONCE stopped at exactly 4512 samples at all three insertion points;
- all runs: file underruns=0, scheduler failures=0, ADC1 stalls=0,
  audio underruns=0 and ring overruns=0;
- FILE DSP maximum: **10.92%** of the 320,000-cycle block budget at IN and
  **9.93%** at MID/OUT; paired DA0/DA1 streams remained alive.

### Cursor -> NCO -> channel filter -> scope

RX startup now selects the existing CMSIS f32 four-pole Butterworth channel filter
with `iq.chf_kernel(True)`.  The spectrum cursor still uses the real
`SdrApp.spec_jump()` mapping and absolute `Ra6m3Backend.set_fine()`/`iq.tune()` path;
no FFT-bin shortcut was added.  A +3 kHz analytic tone injected at `IN` measured
689.54 RMS when selected and 0.866 RMS when the cursor returned to centre: measured
off-channel rejection is **56.77 dB**.  The `OUT` control measured 699.41 RMS at both
cursor positions (ratio **1.000**), proving that the test distinguishes the real
channel-filter boundary.  The same HIL enabled scope stage 5 and both I/Q DAC streams;
DSP maximum was **22.80%**, with no stall/underrun/overrun counters.

### Reproducibility and backup

- existing exact host regression: **22/22 PASS**;
- full VK_RA6M3 `-j16` image: `text=1,407,188`, `bss=645,740`,
  `firmware.bin=1,407,172` bytes, SHA-256
  `06C5E0B4ECBDBC6F9706882517D814063854563C8968D25893E50C1400652966`;
- J-Link Program & Verify: **O.K.**, followed by the mandatory separate J-Link
  reset+go;
- UI MPY: 56,345 bytes, SHA-256
  `4C0A26F8366CCACB4E67A13B4EEB864F39D9AA3CACBB1A7892542811BF2B4002`;
- desktop and Git-backed `sdr_single.py` are byte-identical, SHA-256
  `02D4E2F2CE4E64B389D4B111BA825B688FB89FE18EDBA0F551D0860FB3E34FA8`;
- GitHub commit **`8194c9bb2`** (`[VK_RA6M3][SDR] add buffered SDRangel IQ playback`)
  was pushed to `origin/master`.
- The 48 ksample/s deterministic file was restored as `/flash/test.sdriq`; after
  the final J-Link reset+go the panel application reported `APP_STARTED True RX True`.

## Movable MID and truthful PROC ON/BYP (2026-08-23)

The TESTER insertion point called `MID` is no longer fixed.  The existing public
`IN=0`, `MID=1` and `OUT=2` values remain compatible, while the new
`iq.inject_mid()` API selects one of three 24 ksample/s boundaries:

- `M:IQC` / `INJECT_MID_IQCORR=3`: after DC removal and immediately before I/Q
  amplitude/phase correction;
- `M:NCO` / `INJECT_MID_NCO=4`: after I/Q correction and immediately before the
  frequency-shifting NCO; this is the legacy MID position;
- `M:CHF` / `INJECT_MID_CHFILT=5`: after the NCO and immediately before the
  complex channel filter.

The selector is part of the existing injection configuration seqlock, so GEN and
FILE observe the same point and a change becomes active only on a DSP block
boundary.  `INJECT_MID_API_VERSION=1` lets the UI distinguish the new firmware
from an older image.  The VERIFY screen reuses the three corresponding block-name
rows as one-of `[ ]/[x]` selectors; the point chip reports `M:IQC`, `M:NCO` or
`M:CHF`.  Source and MID position are locked while TESTER is ON.

The block column now states the real processing contract as `PROC ON/BYP`.
`BYP` always passes a signal to the next block; it does not mean zero, mute or
stopped processing.  Two rate/type boundaries need an explicit definition:

- Decimation BYP skips the FIR kernel but keeps the mandatory 48->24 ksample/s
  adapter by forwarding every second input I/Q pair;
- Demod BYP selects the PASS path and forwards I as the mono audio signal.

I/Q correction, NCO, channel filter, AF filter, squelch, AGC application and
volume multiplication use their corresponding bypass paths.  PGA is a read-only
reflection of the constructor hardware mode and is fixed `BYP` in this app.
Limiter remains fixed `SAFE` so DAC-range protection cannot be disabled.  Block
states and the MID position are read back from C instead of being shown only from
a Python-side cache.

Verification completed before hardware deployment:

- independent P0/P1 source review: no findings;
- `tests/sdr_iq_unit_vectors.py`: **22/22 PASS**;
- CPython syntax and `mpy-cross`: both UI copies pass and are byte-identical,
  SHA-256 `90DC5C65C0DF3719EC407374407E2EB45EAF21AA135EF0050F44CDBE436D02AA`;
- full VK_RA6M3 firmware build: exit 0; `firmware.bin=1,407,880` bytes,
  SHA-256 `B2435C31732044B010A102E9D7F761D26563511EE29EC999F6262342A9A38280`;
- `git diff --check`: clean.

**Hardware boundary:** this working image has not yet been flashed.  The new
`M:IQC`/`M:CHF` positions and the Decimation/Demod BYP paths therefore remain HIL
pending.  The older HIL result recorded above for `MID` applies specifically to
the current `M:NCO` position and must not be presented as proof of the two new
boundaries.

## TESTER receiver I/Q bank and complete movable-MID HIL (2026-08-23)

The hardware boundary above is now closed.  The current image was built, flashed
and exercised on COM18 through every implemented injection boundary.

### Receiver-test assets and UI contract

`make_sdriq_receiver_bank.py` builds 16 strict S16LE `.sdriq` files under one
shared format contract:

- deterministic AM, USB, LSB and keyed CW at both 48 and 24 ksample/s;
- converted real off-air AM, USB, derived LSB and keyed CW at both rates;
- `center_frequency=0` for every fixture.  The 9/14 MHz values in the source WAV
  metadata are historical RF tuning information, not frequencies presented to an
  RA6M3 ADC pin;
- the 48-ksample/s IN files are scaled for the native `/16` ADC-count conversion;
  the 24-ksample/s MID/OUT files are generated at the matching direct-injection
  level.  Internal peaks remain within the signed 12-bit scope range.

The four synthetic profiles are always shown by TESTER.  The four `R:*` profiles
are exposed only when the complete real bank exists in `/flash/iqbank`.  Selecting
a profile applies its real AM/USB/LSB/CW demodulator and bandwidth and restores the
operator's previous settings on OFF, EOF or error.  SCOPE remains an independent
operator selection: SCOPE off exercises normal mono demodulated audio; a selected
pre-demod block routes DA0=I and DA1=Q.  TESTER no longer forces stage 5 and can no
longer hide a broken demodulator behind an I/Q-only pass.

The third-party ZS-1 recordings and their derived binary fixtures remain local and
out of Git because the download page does not state a redistribution licence.  Git
contains only the reproducible converter and HIL source.

### Host and deployment evidence

- generator validation: **16/16 assets PASS**, including header CRC/rate/centre,
  internal level and USB/LSB rotation-sign checks;
- exact DSP/source regression: `tests/sdr_iq_unit_vectors.py` **22/22 PASS**;
- full VK_RA6M3 `-j16` build: exit 0; `firmware.bin=1,407,880` bytes, SHA-256
  `B2435C31732044B010A102E9D7F761D26563511EE29EC999F6262342A9A38280`;
- J-Link Program & Verify: **O.K.**, followed by a separate J-Link reset+go;
- all 16 files copied to `/flash/iqbank`; every target SHA-256 matched its host
  file.  LittleFS free space after provisioning: **10,635,264 bytes**;
- deployed UI MPY: 58,752 bytes, SHA-256
  `BC2032645CFD34EF5D4AE1AB407B2D636094C5ED3646AB23B896EE27804C32F1`;
- desktop and Git-backed `sdr_single.py` are byte-identical, SHA-256
  `010DC12713B91E34DB1C50FE1F1A6DFEA62CFAC1B3329326D74CFF98F3AD68C1`.

### Complete COM18 HIL result

Phase A ran all eight synthetic/real profiles through the actual demodulator with
SCOPE off and obtained non-blank mono audio for AM, USB, LSB and CW.  Phase B then
selected explicit I/Q scope routing and proved:

- synthetic AM/USB/LSB/CW through `IN`, `M:IQC`, `M:NCO`, `M:CHF` and `OUT`;
- every real 24-ksample/s asset through `M:NCO`;
- both DAC streams remained active and all measured peaks stayed below 2200;
- final counters: `[453, 452, 0, 0, 0, 0]`; all realtime stall, underrun and
  overrun/drop fault counters were zero;
- DSP `max_cyc=77,372` of `320,000`: **24.18%** maximum block budget.

The first run found one HIL false negative at `R:CW`: its single sample was taken
inside a genuine roughly 552-ms key-up silence.  The signal and 500-Hz channel
filter were healthy.  The final HIL keeps the original energy threshold and waits
up to one second for a fresh active keyed frame instead of weakening the test.
The complete rerun ended with `IQ RECEIVER BANK HIL PASS 8 profiles`.  A separate
J-Link reset+go was performed after the failed run and again after each successful
full run; `mpremote reset` was not used.

GitHub backup commit **`0723959b0`**
(`[VK_RA6M3][SDR] add receiver IQ test bank`) was pushed to `origin/master`.

## Live DSP AVG/PK load instead of a frozen maximum (2026-08-24)

The VERIFY header previously displayed `timing()["max_pct"]`.  That value was the
largest DSP block seen since RX start, so changing every configurable block from
`ON` to `BYP` could not lower the displayed number.  It was a historical high-water
mark, not the current DSP load.

The C callback now owns a bounded **500-ms wall-time window** with a 64-bit cycle
sum, per-window peak and block count.  A completed snapshot is published through an
odd/even sequence lock.  A block, demodulator, bandwidth, scope route or DSP-kernel
change advances a generation and invalidates the old snapshot; if a setting changes
inside a measured block, that mixed block is discarded.  Reads and LVGL scrolling do
not reset or drive the measurement.

The new live percentages use the **observed** block interval from `window_ms` and
`window_blocks`, not only the configured `rate/block` estimate.  This is important on
this target: one verified window contained 188 blocks in about 0.5 s.  The legacy
since-start `last_cyc/max_cyc/avg_cyc/max_pct` keys remain for compatibility, while
`timing()` also exposes `window_valid`, sequence/generation, window cycles, observed
block budget and current integer `avg_pct/peak_pct`.

The VERIFY header now shows `DSP AVG/PK`, for example `31/31%`.  During the first
clean post-change window it shows `--/--`; unchanged formatted values do not trigger
an LVGL repaint.

### Build, deployment and HIL evidence

- Python syntax and `git diff --check`: clean;
- full VK_RA6M3 `-j16` build: exit 0; `text=1,408,984`, `bss=645,820`,
  `firmware.bin=1,408,968` bytes;
- final `firmware.hex` SHA-256:
  `050C9EF363CFDFF54AB3358ABFED6BD62C3E1C6C6F0D881F1F1255F6BE267B97`;
- final `firmware.bin` SHA-256:
  `C927EE447FEE66E1A2E2BD7613CE8873CC15103A775D4056A94FF3264FD0B081`;
- J-Link Program & Verify: **O.K.**, followed by the mandatory separate J-Link
  reset+go;
- reproducible on-target test:
  `boards/VK_RA6M3/examples/sdr_timing_window_hil.py`;
- TESTER `GEN / IN / ON`, AM fixture, configurable blocks ON (median of three
  completed windows): **AVG/PK 31/31%**, `99,733 cycles/block`, 188 blocks/window;
- the same source with blocks 2..10 in BYP (median of three windows):
  **AVG/PK 20/20%**, `65,216 cycles/block`, 188 blocks/window;
- verified saving: **34,517 cycles/block**, or **34.6%** of the measured ON work;
- the HIL also proved generation invalidation, coherent percentage formula,
  450..650-ms window bounds, positive `AVG <= PK`, DSP deadline below 100% and a
  running DAC stream.  It ended with `DSP TIMING WINDOW HIL PASS`;
- a separate J-Link reset+go was performed after the HIL run; `mpremote reset` was
  not used;
- deployed UI MPY: 59,046 bytes, SHA-256
  `E65A987BB824C594E3368DDAB2B5D5AECD289C6946F9742C52F099DC10A89B3D`;
- desktop and Git-backed `sdr_single.py` are byte-identical, 194,916 bytes,
  SHA-256 `C980B38E92B6BA485005E84EC8ED98A7E4907CF47FAE76906559571B4AF2CD23`;
- after the UI upload, another J-Link reset+go was performed and the panel app
  reported `APP_STARTED True RX True TIMING_KEYS 16`.

`DSP AVG/PK` measures the timed C `dsp_process + demod_produce` section.  It is not
a whole-CPU percentage and intentionally excludes LVGL/Python UI work and DAC DMA
service outside that measured section.

## TESTER-aware NCO tuning and truthful DAC output labels (2026-08-24)

The four HOME tuning buttons no longer move the unrelated Si5351 RF oscillator
while an internal GEN/FILE TESTER source is armed:

- `<< / >>` move the digital baseband NCO by the complete selected STEP;
- `- / +` move the same NCO by STEP/10;
- `IN`, `M:IQC` and `M:NCO` are movable because the injected signal still passes
  through the NCO;
- `M:CHF` and `OUT` are already after the NCO and therefore perform a truthful
  no-op; the shared control says `NCO N/A`;
- an NCO block in `BYP` likewise performs no move and says `NCO BYP`;
- with TESTER off, the established receiver behavior is unchanged: coarse arrows
  tune RF/Si5351 and the small buttons tune the digital NCO.

The former `SCOPE` column is now described as physical `OUTPUT`.  Its existing
header label is reused, so this clarification allocates no additional LVGL widget.
Complex rows say `I/Q D0/1`, mono rows say `HEAR D0`, and the live header uses a
compact `source@stage` description, for example:

- `TEST@NCO` when the selected complex output contains the injected test signal;
- `ADC@DECIM` when the selected output is before the injection boundary and still
  contains the real ADC input;
- `AM1k@FINAL`, `SSB1k5@FINAL` or `CW700@FINAL` for normal generated mono audio;
- `RECORD@FINAL` for an off-air FILE recording;
- `RAW@SQL` / `RAW@FINAL` instead of claiming demodulated audio when Demod is `BYP`.

VERIFY now also has one graphical output-position marker in the left gutter of the
DSP block list.  It is a single reused 6 x 20 px LVGL object, not one object per row:

- it moves to the exact block currently routed to the physical DAC output;
- cyan means complex I/Q on DA0/DA1 (blocks 1-5);
- green means mono audio on DA0 (blocks 6-11);
- with SCOPE/output selection off, normal receiver audio is represented at the
  final Limiter row;
- `IGNORE_LAYOUT` keeps the marker out of the flex layout while allowing it to
  scroll with the block rows. `FLOATING` is deliberately not used because it would
  stay fixed during the native framebuffer scroll.

The earlier ASCII `>` marker was removed.  Moving one graphical object invalidates
only its old/new rectangles instead of rewriting all 11 block-name labels.

This label describes the physical converters.  TIME follows DAC0.  The on-screen
I-Q constellation intentionally remains the shared post-channel-filter FFT frame;
it is not falsely presented as the currently selected per-block DAC route.

### Verification and deployment evidence

- bundled CPython syntax, `mpy-cross` and `git diff --check`: clean;
- focused TESTER state-contract test: **PASS** for all five injection boundaries,
  NCO bypass, full/fine steps and output-source labels;
- exact DSP/source regression: `tests/sdr_iq_unit_vectors.py` **22/22 PASS**;
- desktop and Git-backed `sdr_single.py` are byte-identical, 201,610 bytes,
  SHA-256 `EC20B1732CC0628C25EC72F8065C3AAE4CF4B00D63D0DDB3FA2AE5117DF09D0A`;
- deployed `/flash/sdr_single.mpy`: 60,753 bytes, SHA-256
  `09A53DD3BAC6DE08510ADAB1F0D463C55D10017B2D01D2C21B9A917260A6A0D2`,
  exactly matching the host build;
- COM18 HIL invoked the deployed `SdrApp.tune()` method: a TESTER/IN 1-kHz command
  changed NCO to 1,000 Hz while RF remained 15,143,780 Hz; the same command at
  OUT changed neither RF nor NCO; `ADC@DECIM` and `TEST@NCO` assertions passed;
- the same HIL built VERIFY on the target, found one marker and verified geometrically
  that it moved inside block 4 (NCO), then inside block 11 (Limiter).  It ended with
  `TESTER_TUNING_MARKER_HIL PASS ... 11`;
- both the MPY upload and the successful HIL run were followed by their own separate
  J-Link reset+go.

## RX STOP/START full reconstruction and ADC ownership (2026-08-27)

The reported `STOP -> START` no-change behaviour had two independent lifecycle
causes.  `IQADC.stop()` halted conversions but intentionally retained ADC0/ADC1,
DTC, ELC, AGT and PGA configuration, while the application discarded the Python
handle.  In addition, ordinary `machine.ADC` and the legacy `AudioADC` could touch
the same ADC registers without checking who owned them.

The implemented contract is now explicit:

- application STOP performs `IQADC.stop()` followed by checked `IQADC.deinit()`;
- a failed deinit keeps the IQADC handle and returns failure, so HOME shows the
  backend error instead of claiming a clean release;
- ordinary `machine.ADC` constructor/read/PGA mutation raises `EBUSY` while either
  IQADC or AudioADC owns the peripheral;
- IQADC and AudioADC reject one another, including partially constructed instances;
- each successful application START reconstructs ADC/DTC/ELC/AGT/PGA and queues a
  fresh write of the active Si5351 route even when selected frequency equals cached LO.

### Build, deployment and HIL evidence

- bundled-Python syntax: clean;
- UI lifecycle/source contracts: **5/5 PASS**;
- exact DSP/source regression: **22/22 PASS**;
- full VK_RA6M3 `-j16` build: exit 0; `text=1,411,320`, `bss=647,004`,
  `firmware.bin=1,411,304` bytes;
- J-Link Program & Verify: **Verify successful**, followed by a separate mandatory
  J-Link reset+go;
- deployed `/flash/sdr_single.mpy`: 68,755 bytes, SHA-256
  `0179C7241E15CCC24ACCDC8910A1F69F67AEEE4B31E29D64923D73FB9F6019C8`;
- active RX advanced from block 285 to 510 in 600 ms: exactly **375 blocks/s**,
  `unit1_stalls=0`, backend error `None`;
- while RX was active, both `ADC("P002")` and `AudioADC("P002")` were rejected with
  `EBUSY=16`;
- application STOP returned `True`, set `running=False` and released the native
  handle (`iq is None`); ordinary `ADC("P002")` then read 2304;
- the following START succeeded and reached block 324 after 700 ms; both ownership
  guards were active again;
- final state: VFO B, route index 1 (`RX CLK1 x4`), selected/LO 3,500,000 Hz,
  NCO 0 Hz, no pending hardware work, synthesizer `ok=True`, RX running with no error.

### Separate unresolved physical-input observation

One non-destructive raw IQ snapshot after the successful reconstruction did **not**
contain the expected 2-kHz AM envelope.  P000/I was 397..3783 with mean 1909; its
strongest 128-sample DFT bin was 17.25 kHz (amplitude 1387), while the measured
2-kHz amplitude was only 6.  P004/Q remained near the lower rail at 2..12 with mean
8 and no measurable 2-kHz component.

This does not invalidate the STOP/START fix.  It separates the remaining problem:
the current external generator/LO/Tayloe-Q wiring is not presenting a valid complex
2-kHz AM baseband pair to P000/P004.  The next bench checks are CLK1 at 14.000 MHz,
P007/PGAVSS100 to AVSS0/GND, and the detector Q output physically reaching P004.

## TESTER IN PGA-aware amplitude safety and AM headroom correction (2026-08-27)

The flattened positive part of the internally generated AM waveform was traced to
the TESTER amplitude contract, not to DAC0. `IN` is deliberately modeled as a signal
before the PGA: C multiplies the requested amplitude by the active PGA factor, applies
the AM envelope and only then clamps the synthetic raw ADC pair to `0..4095`. The old
default `A=500` therefore left almost no headroom at x2.667 and clipped strongly at
larger saved gains.

The UI now enforces the same integer/Q15 operation order as the C generator:

- TESTER starts at `A=100` instead of `A=500` and adjusts in 10-count steps;
- live PGA enable/code readback determines the exact safe maximum for `IN`; MID/OUT
  remain unity because they enter after the PGA model;
- the source chip shows `GEN<=N`, and the amplitude chip shows `Sxxx` in SAFE mode;
- tapping the amplitude chip explicitly arms bench overload (`Oxxx` / `GEN OVR`);
  a predicted rail hit is red and reads `O!xxx` / `GEN CLIP`;
- SAFE clamps ordinary `+` edits and refuses a stale unsafe value caused by an older
  screen or a later PGA change; overload is never armed implicitly;
- TESTER OFF, an apply failure, RX stop or PGA reconstruction clears overload and
  repaints the row, so the guard is fail-closed and the visible state stays truthful;
- amplitude, waveform and LIVE edits republish only the block-atomic generator tuple.
  They no longer call `demod()` and `bandwidth()` again, so the AM envelope/Hilbert/
  filter state is not reset on every live edit. Activation and a real modulation-mode
  change still configure the receiver once.

### Exact limits and on-target evidence

The reference x2.667 / AM 50% boundary is reproduced exactly by the host contract:
`A=512 -> peak 2046` and `A=513 -> peak 2050`, so 512 is the last clean request.
An earlier direct on-target sweep agreed: 512 reached raw maximum 4094 without a rail
hit, while 513 reached 4095.

The deployed-board HIL used the board's actual saved PGA x5.714, not the earlier
x2.667 assumption:

- boot default: `A=100`, predicted post-PGA AM peak 855;
- computed SAFE maximum: `A=239`;
- SAFE raw DAC0/DADR observation: min 333, max 4094, `4095` hits = 0;
- `A=240` in SAFE was refused with `TESTER SAFE <=239; tap S for OVR`;
- explicit OVR at `A=250`: min 253, max 4095, `4095` hits = 214/6000;
- one receiver-mode setup occurred on activation; the following live amplitude edit
  left that count at 1, confirming that it did not restart the demodulator.

The first HIL attempt intentionally stopped on the stale x2.667 test assumption after
reporting the truthful live x5.714/239 limit; the required J-Link reset+go was performed
immediately. The gain-independent HIL then passed and was followed by its own separate
J-Link reset+go.

### Build and deployment evidence

- bundled CPython AST: **4/4 PASS**;
- focused UI/source contracts: **6/6 PASS**;
- `mpy-cross` and `git diff --check`: clean;
- desktop and Git-backed `sdr_single.py` are byte-identical, SHA-256
  `85398479F4A7991449EE1FDE4427807E5317FA218960CB04DF9E94AD3D7DF7C8`;
- deployed `/flash/sdr_single.mpy`: 70,175 bytes, SHA-256
  `23E7C7CF7918A1134B0FDAED34F62E80B8C9D4287739FD195DFE9558673AC050`;
- board readback has the same size and SHA-256 as the host build;
- upload, successful HIL and readback were each followed by the required explicit
  J-Link reset (`AIRCR.SYSRESETREQ -> g -> q`).

## External 2-kHz AM I/Q HIL: P004/ADC1 path localized (2026-08-28)

The earlier source-loading conclusion is withdrawn.  The generator ground had been
missing, so all measurements taken before the common ground was connected are invalid
as evidence about I/Q amplitude, impedance or phase.  The corrected bench condition is
an external carrier near 3.5 MHz with 50% AM at 2 kHz.  Every diagnostic reconstruction
used hardware `PGA_BYPASS`, disabled I/Q correction and no DAC stream.

The earlier ADC1-only ELC, ADC1-master/Q-first DTC and rate A/B runs remain useful
only as checks that those firmware paths execute; because the generator ground was
missing, their amplitude results are not reused below.  The conclusions in this
section come from the repeated post-ground measurements.  The mapping remained
P000/AN000/ADC0 for I and P004/AN100/ADC1 for Q, with hardware PGA bypass.

### Repeated measurements after connecting generator ground

- A physical-LO sweep from 3.495 to 3.505 MHz moved the I carrier peak through
  4.5, 2.5, 0.5, 1.5, 3.5 and 5.5 kHz as expected.  The corrected I carrier was
  125..186 ADC codes and its 2-kHz AM sidebands were present.  Q contained only
  1..2 codes at the corresponding carrier/sideband frequencies.
- A 997-S/s capture increased raw Q span/RMS, but that test measured only min/max/RMS
  over about 10 seconds.  At this rate the 2-kHz modulation aliases to about 5.85 Hz,
  so the increase is not proof of recovered useful Q and is not proof of high source
  impedance.  It may be aliased switching residue, hum, drift or outliers.
- The external I and Q source impedances are equal.  A physical interchange of the
  two signal wires did **not** move the strong spectrum: P000/ADC0 remained strong and
  P004/ADC1 remained at 1..2 spectral codes.  The fault therefore stays with the
  P004/ADC1-side path rather than following the detector output or the I/Q label.
- Runtime PFS readback was identical and correct on both pins:
  `PDR=0, PCR=0, ISEL=0, ASEL=1, PMR=0, PSEL=0`.  A stale pull-up or GPIO/peripheral
  pin mode is ruled out on the tested run.
- Independent software-triggered scans, with AGT/ELC/DTC stopped, still measured a
  much weaker P004 signal.  Repeating the acquisition-window A/B after the physical
  interchange (24, 64, 128 and 255 ADCLK states) did not recover Q.  DTC ordering,
  trigger synchronisation and ordinary sample-settling time are therefore excluded.
- The normal saved RX setting was PGA code 10 (`x5.714`).  With the present input it
  drove Q to 4076..4088 out of 4095.  This is pre-DSP rail clipping; AGC, volume and
  the output limiter cannot repair it.  `PGA_BYPASS` is the only setting currently
  proven safe; even the minimum PGA gain x2 has not yet been proven to have headroom.

AN000/ADC0 and AN100/ADC1 have the same documented input capacitance, source-impedance
limit and dedicated sample-and-hold timing.  The remaining unit-specific analog
difference is the reference domain: ADC0 uses VREFH0/VREFL0, while ADC1 uses
VREFH/VREFL (shared with DAC12).  This makes the physical P004 connection and the
ADC1 reference wiring/decoupling the remaining suspects; the measurements do not yet
distinguish between them.

This also explains the reported deep-AM distortion.  With useful Q nearly absent,
the AM magnitude kernel receives approximately `|I|` instead of
`sqrt(I*I + Q*Q)`, causing folding and beat ripple.  Manual I/Q correction cannot
reconstruct missing 90-degree information; its `Q' = amp*Q + phase*I` model only
mixes/scales the samples already present.  The AM DSP math is therefore not changed.

Next decisive bench action: while the signal wires remain interchanged and PGA stays
in BYPASS, probe the signal directly on the **MCU-side P004 pad** with AC coupling.
If the full low-frequency beat is absent there, repair the connector/trace/coupling
between the interchange point and P004.  If the full beat is present on the MCU pad
while ADC1 still reports 1..2 codes, verify VREFH/VREFL and their decoupling and then
test P004 with a known low-frequency voltage source.  Do not apply manual I/Q
correction until both raw channel amplitudes are comparable.

No production firmware or DSP source was changed by this diagnostic sequence.

## External 50% AM distortion: PGA clipping and automatic low-IF correction (2026-08-28)

The external generator was already enabled at 3.500 MHz with 2-kHz, 50% AM.  A
raw-I/Q plus final-audio A/B measurement separated two independent effects which
had previously been mixed together.

### Measured causes

- The saved RF setting was hardware PGA SINGLE, gain code 10 (x5.714).  The direct
  12-bit `IQADC.read_block()` samples were I=4077..4089 and Q=4075..4088 out of
  4095.  This is literal pre-DSP ADC saturation, not `read_u16` scaling and not a
  DAC/volume/AGC fault.
- PGA BYPASS restored useful input headroom.  In the first A/B capture it produced
  I=1206..1851 and Q=1243..1870.  The final 2-kHz audio returned immediately.
- Minimum PGA SINGLE gain x2 was also not accepted as proven safe: during the
  deliberate low-IF run Q reached code 0.  A later BYPASS capture also touched
  Q=0, so that contact is **not uniquely caused by x2**; it leaves a separate
  Q-channel bias/wiring bench question.  BYPASS is nevertheless the conservative
  setting because it removes all avoidable PGA gain, while x5.714 is conclusively
  clipped at the positive rail.
- A second, architectural AM issue was reproduced near exact zero IF.  The required
  I/Q DC servo runs before the NCO and envelope detector.  A centred AM carrier is
  indistinguishable from ADC DC, so after settling the servo removes the carrier
  vector and the magnitude detector folds the remaining 2-kHz sidebands into a
  strong 4-kHz component.  With PGA x2 near zero IF, final audio measured
  A2k=429.75 and A4k=435.56 (THD H2-H5=101.38%).
- The same signal with a deliberate 3-kHz low IF measured A2k=1154.56 and
  A4k=37.63.  The final deployed PGA-BYPASS run measured A2k=764.67,
  A4k=11.16 and THD=1.46% with FAST AGC; with AGC OFF it measured A2k=94.20
  and THD=1.90%.  The final capture had zero audio underruns.

The DC servo was deliberately **not** disabled.  The live BYPASS channel means are
far from the nominal ADC midpoint, so removing it would turn real analogue offset
into a large false signal.  The safe solution is low IF: the servo removes static
ADC offset while the RF carrier remains a rotating complex vector; the existing NCO
then centres that carrier before AM demodulation.

### Implemented application contract

`sdr_single.py` now defines `AM_LOW_IF_HZ = 3000` and derives the physical LO from
the selected RF station and modulation:

- AM: physical LO = selected station + 3000 Hz; native NCO = -3000 Hz;
- USB/LSB/CW: physical LO = selected station; native NCO = 0 Hz at recentre.

The same policy is applied by station jumps, VFO switches, NCO recentre, RX
STOP/START, PGA reconstruction and live entry/exit from AM.  The displayed and
persisted station remains the actual received RF frequency; only the private
physical LO centre moves.  Re-centre thresholding is measured relative to the
normal AM bias, so left/right panorama navigation remains symmetric.

### Deployment and HIL evidence

- desktop and Git-backed `sdr_single.py` are byte-identical, SHA-256
  `13C0BD1ECFE9BCEE0F8D5B7A6C209F084B78CE0BD4F5A5BF38E671D4030D964D`;
- bundled-CPython syntax passed for both copies;
- extracted low-IF source contract passed for AM, USB and both frequency limits;
- `mpy-cross` default-bytecode build succeeded;
- `/flash/sdr_single.mpy`: 70,545 bytes, SHA-256
  `455FA0D78A470618E2565FFF58EDA65D194C7D08A835BCDF7439C0664169AF1C`;
  on-board streaming readback matched both size and hash;
- an explicit J-Link reset+go completed, then the application was started separately
  from `/flash`; backend reported `running=True`, `err=None`;
- without any manual LO/NCO command after start, HIL reported station 3,500,000 Hz,
  physical LO 3,503,000 Hz and NCO -3000 Hz;
- RX STOP/START returned to the same 3,500,000 / 3,503,000 / -3000 state;
- switching to USB produced LO 3,500,000 / NCO 0; switching back to AM restored
  LO 3,503,000 / NCO -3000, with no pending work or backend error.

Final live state: AM, selected 3.500 MHz, physical CLK1 route target corresponding
to LO 3.503 MHz, NCO -3 kHz, PGA BYPASS, FAST AGC, RX running.  This deployment
updated only the `/flash` MicroPython application; it did not rebuild or flash a new
C firmware image.

## ADC ownership, ADC1 pin mapping and native receiver displays (2026-08-29)

Local firmware commit **`83700375e`**
(`[VK_RA6M3][SDR][UI] harden ADC ownership and native receiver displays`)
records the accumulated RA6M3 C-firmware work in 14 tracked files: 1090 inserted and
333 removed lines.  The file-by-file inventory below is derived from the committed
source diff.  It documents implemented source behaviour; it does not claim new HIL
or physical-panel proof.

This section supersedes the older statement that the I-Q constellation shares the
FFT foreground frame.  The committed implementation has an independent 64-point
final-complex, post-CHF/OUT constellation capture; the spectrum panorama and the
constellation therefore have separate truth boundaries and separate capture gates.

### Changes by firmware file

1. **`ports/renesas-ra/Makefile`**

   - Removed the false `-DRA6M2` compatibility define from `CFLAGS_MCU_RA6M3`.
   - A VK_RA6M3 build now selects native `RA6M3` conditional code only, while keeping
     the correct Cortex-M4 compiler tuning.

2. **`ports/renesas-ra/boards/VK_RA6M3/machine_lcd.c`**

   - Reworked the native spectrum from 27 widget-like bars to 256 retained column
     heights, one value for every physical X column of the panorama.  The live path
     reduces the 512-bin FFT directly to those columns; the legacy
     `spectrum_update(array('h', 27))` API is still accepted and expanded to the new
     column state.
   - Added finite-edge clipping.  A tuned FFT coordinate outside the captured
     bandwidth is painted as background instead of wrapping data from the opposite
     Nyquist edge.
   - Added a permanent five-pixel tuning cursor with a black outline, white sides and
     cyan centre.  It remains visible when no carrier is present and is independent
     of spectrum amplitude.
   - Added `spectrum_center(selected_hz)`.  It keeps retained waterfall rows in the
     absolute selected-frequency coordinate and pans their history horizontally when
     tuning changes.  Fractional pixel remainder is retained between steps; a move
     beyond the whole width clears history rather than displaying false frequencies.
   - Fixed TIME scope scaling to the DAC-domain full scale `-2048..2047`.  Removed
     per-frame peak normalization, so amplitude now changes visibly and is not hidden
     by an implicit display AGC.
   - Separated the native consumers: SPEC updates at 100 ms, TIME/I-Q at 33 ms, while
     WF can combine the available left and right data in one framebuffer operation.
   - I-Q view now consumes `ra_iq_adc_constellation_frame()` rather than reusing the
     spectrum snapshot.
   - Added explicit OFF states: `spectrum(2)` disables and clears the left native
     panel; `scope_view(2)` disables and clears the right native panel.
   - Added `lcd_native_capture_apply()` to gate spectrum, constellation and TIME scope
     independently according to attach, pause and current-view state.  All gates are
     closed at LCD/LVGL soft reset, object delete, invalid-object and detach
     boundaries.  Resume reapplies the capture matrix even after IQADC reconstruction.

3. **`ports/renesas-ra/boards/make-pins.py`**

   - Corrected the generator field from the unused `_adc_bit` to `_adc_bits = 12`.
   - Corrected ADC1 channel generation: `AN100..AN131` now map to the driver's flat
     channel range `32..63`; ADC0 `AN000..AN031` remains `0..31`.
   - Generated `PIN_ADC(...)` records therefore carry the correct 12-bit resolution
     and ADC-unit/channel identity for pins such as P004/AN100.

4. **`ports/renesas-ra/machine_adc.c`**

   - Added `machine_adc_require_unowned()`.  Construction, `read()`, `read_u16()`,
     `pga()` and `set_gain()` now raise `OSError(EBUSY)` while IQADC or AudioADC owns
     shared ADC/scan/trigger resources.
   - A standalone `machine.ADC` now calls `ra_adc_pga_prepare_direct_ch()` instead of
     inheriting SINGLE/DIFFERENTIAL routing from an earlier owner.  Failure to
     normalize the direct path is reported as `OSError("pga bypass failed")`.

5. **`ports/renesas-ra/machine_audioadc.c`**

   - Added the reciprocal ownership guard against `ra_iq_adc_owns_adc()`.
   - Constructing `AudioADC` while IQADC owns any complete or partial ADC resource
     now fails explicitly with `OSError(EBUSY)` before the Storm ADC path starts.

6. **`ports/renesas-ra/machine_iq_adc.c`**

   - Added the reciprocal guard against `ra_storm_adc_owns_adc()`; an active or
     partially constructed AudioADC prevents IQADC construction with
     `OSError(EBUSY)`.
   - Expanded `IQADC.timing()` from 6 to 16 fields.  In addition to the legacy
     counters it returns `window_valid`, `window_seq`, `window_ms`, `window_blocks`,
     `window_generation`, `window_avg_cyc`, `window_peak_cyc`,
     `observed_block_cyc`, `avg_pct` and `peak_pct`.
   - `observed_block_cyc` is derived from real elapsed time and completed blocks in a
     coherent 500-ms window; live average/peak percentages no longer assume that the
     configured rate exactly equals the measured block cadence.
   - `IQADC.gain(code)` now checks the low-level result and raises `OSError(EBUSY)`
     when a live ADC scan prevents changing `ADPGAGS`, instead of returning an old
     gain code as though the write had succeeded.
   - Updated the allocation-free spectrum contract from 256-to-N circular reduction
     to 512-to-N finite-edge reduction.

7. **`ports/renesas-ra/qstrdefsport.h`**

   - Registered the ten new MicroPython dictionary keys used by `IQADC.timing()`:
     `window_valid`, `window_seq`, `window_ms`, `window_blocks`,
     `window_generation`, `window_avg_cyc`, `window_peak_cyc`,
     `observed_block_cyc`, `avg_pct` and `peak_pct`.

8. **`ports/renesas-ra/ra/ra_adc.c`**

   - Hardened `ra_adc_set_pin()`: analog selection now first clears stale GPIO and
     peripheral state (`PMR`, `PDR`, `PCR`, `ISEL`, `NCODR`, `PSEL`) before changing
     `ASEL`.  A previous pull-up, IRQ route or output configuration can no longer
     silently contaminate a small analog signal.
   - Added `ra_adc_pga_clear_unit_diff()` to clear all three unit-wide
     `ADPGADCR0/PnDEN` fields when no differential amplifier remains.
   - Added `ra_adc_pga_prepare_direct_ch()`.  With no active scan it normalizes the
     whole ADC unit, disables stale amplifier/gain/differential routing and selects
     BYPASS for a PGA-capable standalone channel.  It also handles the AN007/AN107
     direct-input constraint.
   - OFF, BYPASS and SINGLE transitions in `ra_adc_pga_config_ch()` now clear the
     complete unit differential state when safe, rather than only the current nibble.

9. **`ports/renesas-ra/ra/ra_adc.h`**

   - Exported `bool ra_adc_pga_prepare_direct_ch(uint8_t ch)` and documented its
     standalone direct-path contract, including the AN007/AN107 PnDEN dependency and
     failure during an active scan.

10. **`ports/renesas-ra/ra/ra_config.h`**

    - Added `RA6M3` to the RA6M1/RA6M2 configuration branch.
    - RA6M3 now receives the intended SCI limits/buffer sizes and default
      `PCLK=120000000`; a board-level `MICROPY_HW_MCU_PCLK` definition still has
      priority.  This completes removal of the old RA6M2 alias.

11. **`ports/renesas-ra/ra/ra_iq_adc.c`**

    - Added a low-level AudioADC ownership check in `ra_iq_adc_init()`.  A conflict is
      latched as init error `audioadc_owner` before AGT or ADC resources are reserved.
    - Added `ra_iq_adc_owns_adc()`, which remains true for complete and partial IQADC
      ownership (`opened0/1`, DTC, ELC or reserved timer) until checked teardown has
      released everything.
    - IQADC init and checked deinit now close spectrum, constellation and TIME-scope
      gates and discard partial display frames, separating ADC ownership from display
      producer lifetime.
    - Extended the legacy timing sum to 64 bits and made counter reads coherent under
      masked IRQs.  Added a minimum 500-ms rolling window with average, peak, elapsed
      milliseconds, block count, sequence and configuration generation, published as
      a coherent snapshot.
    - Every real DSP configuration change invalidates the old timing generation;
      `stop()`, kernel selection, block bypass, demodulation, scope point and bandwidth
      changes cannot leave a stale percentage displayed as current.
    - FILE injection at IN now simulates the selected PGA gain before ADC midpoint and
      12-bit saturation.  BYPASS remains unity, while enabled PGA modes reproduce the
      corresponding pre-DSP clipping behaviour for file-based tests.
    - Split spectrum and constellation capture.  Spectrum is pre-NCO for real RX and
      early TESTER points (IN, MID:IQC, MID:NCO), and post-CHF for late MID:CHF/OUT
      sources.  The 64-point constellation is always final complex post-CHF/OUT data.
    - Added independent `ra_iq_adc_constellation_enable()` and
      `ra_iq_adc_constellation_frame()` APIs and a stable ping-pong foreground
      snapshot.  Spectrum, constellation and DAC scope can now be gated separately;
      disabling spectrum no longer disables TIME scope.
    - Applied NCO spectrum shift only to pre-NCO snapshots and only while the NCO block
      is active.  `ra_iq_adc_spectrum_reduce()` now clips out-of-range bins instead of
      wrapping them to the opposite edge.
    - `ra_iq_adc_spectrum_frame()` exposes the panorama magnitudes and, only when
      requested, claims I/Q pointers from the independent final-complex frame.  NULL
      I/Q outputs leave that constellation frame unconsumed.

12. **`ports/renesas-ra/ra/ra_iq_adc.h`**

    - Declared `ra_iq_adc_owns_adc()` and documented ownership through partial
      construction until checked deinit.
    - Added `ra_iq_timing_window_t` and `ra_iq_adc_get_timing_window()`.
    - Added `ra_iq_adc_constellation_enable()` and
      `ra_iq_adc_constellation_frame()`.
    - Updated the `ra_iq_adc_spectrum_frame()` contract to distinguish panorama
      magnitudes from the optional independent final-complex constellation snapshot.

13. **`ports/renesas-ra/ra/ra_storm_adc.c`**

    - Added the low-level reciprocal IQADC ownership check before AudioADC reserves an
      AGT timer or the shared ADC0 scan-end transfer path.
    - Added `ra_storm_adc_owns_adc()`, covering both the initialized state and partial
      resources (`opened`, DTC, timer, pin and ELC).  This makes failed construction
      and retry/teardown paths visible to all wrappers.

14. **`ports/renesas-ra/ra/ra_storm_adc.h`**

    - Exported `bool ra_storm_adc_owns_adc(void)` and documented that it covers every
      held ADC0/trigger resource, including partial initialization.

### Commit and verification status

- Commit: **`83700375e3b1d2ec608c8e796747c849c2558fe3`**.
- Scope: exactly the 14 tracked files listed above; no HIL scripts, task notes or
  generated `sdr_single.mpy` artifact are included.
- `git diff --cached --check` was clean before commit.
- A firmware build with the native MSYS/MinGW Python path completed with exit code 0:
  `firmware.elf`, `firmware.hex` and `firmware.bin` were generated; linked image
  summary was text 1,411,608 bytes and BSS 647,004 bytes.
- No new C firmware image was flashed and no new panel/HIL run was claimed by this
  commit step.
- The commit is local and not pushed.  `master` is two commits ahead of
  `origin/master` (`f0a61f08e` and `83700375e`).
- Current panorama frequency-to-pixel mapping uses
  `LCD_PANORAMA_SPAN_HZ = 24000`, which matches the present 48-kS/s IQ capture.  If
  that capture rate becomes configurable, this constant must be derived from the
  actual rate.

## 2026-09-05 — FM demodulator and TESTER FM verification

### Implemented behavior

- Added an internal constant-envelope FM source to the compiled IQ/DSP path.  The
  generated complex carrier has a selectable modulation frequency and frequency
  deviation; the initial TESTER preset is a 1-kHz sine with `±4 kHz` deviation.
- Extended the append-only injection API with `INJECT_FM=5` and
  `INJECT_API_VERSION=2`.  The old injection call remains available and source
  changes are still copied atomically at a DSP block boundary.
- Added FM to the TESTER screen at all three useful insertion points:
  - `IN`: source at `+6 kHz`, before decimation/NCO; TESTER NCO is `+6 kHz`;
  - `MID:CHF`: centered source at the channel-filter input;
  - `OUT`: centered source immediately before demodulation.
- FM always uses a continuous sine modulator.  The square/pulse gate and AM depth
  controls are deliberately disabled for this source.
- Turning FM TESTER off restores the normal receiver configuration, including the
  saved `-6 kHz` low-IF NCO.  Changing amplitude or LIVE state no longer resets an
  NCO position that the operator moved manually during the test.
- Fixed the Python control-flow indentation that made the FM bandwidth update
  unreachable.

### External 30-MHz FM reference

Generator conditions: `30.000 MHz` carrier, `1 kHz` modulation, `±3999.9 Hz`
deviation.  This individual board used the calibration already stored in dataflash,
`161.7 ppm`; the source-code default was not changed.

| Observation point | Measured result |
| --- | ---: |
| Pre-NCO carrier | `-6015.91 Hz` |
| Post-NCO carrier | `-5.17 Hz` |
| Post-filter carrier | `-3.38 Hz` |
| Post-filter deviation | `3907 Hz` |
| Post-filter THD, harmonics 2…6 | `8.23%` |
| Compiled C FM audio, 1-kHz component | `508.58` counts |
| Compiled C FM audio THD, harmonics 2…6 | `8.86%` |
| Audio underruns | `0` |

The verified receiver state was station `30.000 MHz`, Si5351 LO `30.006 MHz`,
NCO `-6000 Hz`, mode FM and channel bandwidth `6000 Hz`.

### Internal FM source HIL

Conditions: TESTER sine `1 kHz`, deviation `±4 kHz`, amplitude setting `500`;
post-demod AF/SQL/AGC/VOL blocks bypassed so the FM discriminator itself is measured.

| Injection point | 1-kHz amplitude, counts | THD 2…6 | Sample min/max |
| --- | ---: | ---: | ---: |
| `OUT` | `1060.33` | `3.37%` | `-1076 / +1077` |
| `MID:CHF` | `1059.72` | `6.05%` | `-1039 / +1041` |
| `IN` | `1042.87` | `9.30%` | `-1027 / +1150` |

All three points produced the requested 1-kHz demodulated signal.  `IN` exercises
the complete chain, while `OUT` isolates the demodulator/audio side.  After the
diagnostic capture, normal DAC playback was restored with `audio_underruns=0` and
`ring_overruns=0`.

### UI and deployment verification

- UI HIL: arming FM selected mode FM, TESTER NCO `+6000 Hz` and bandwidth
  `6000 Hz`; moving the NCO to `+5500 Hz` and then changing amplitude preserved
  `+5500 Hz`; disarming restored normal NCO `-6000 Hz` and active DAC playback.
- Firmware build succeeded.  `firmware.bin` is `1,574,760` bytes with SHA-256
  `F78155FDDBF55ED95B7190EFD4FD44185B017F3730F00688200FC0D8EA6A1860`.
- Flash target was selected explicitly by J-Link serial number `1120000058`.
  Programming used `loadbin` and read-back `verifybin`; verification succeeded.
  A separate J-Link reset/run followed the flash.
- `/flash/sdr_single.mpy` was read back and matched the host artifact exactly:
  `71,681` bytes, SHA-256
  `4AAF101F7308D7E3663F905C57BDA31FF164E5B163D4A93EF370305EFF90D984`.
- Final board state: application on HOME, TESTER off, FM receiver running at
  `30.000 MHz`, LO `30.006 MHz`, NCO `-6000 Hz`, DAC playing, no audio underruns or
  ring overruns.

### Proof boundary and Git status

- The waveform and distortion figures above are digital captures from the compiled
  DSP/audio path.  They prove signal flow and numerical behavior, not perceived
  loudspeaker quality.
- The complete six-file working set was committed locally as
  **`dc521805624fcaf763d130bf1e094a61ed4a18c5`**
  (`feat(VK_RA6M3): add FM DSP and refine SDR tuning UI`).  It includes the FM
  DSP/TESTER work together with the earlier NCO/VFO and native tuning-marker
  changes that were present in the same requested “commit everything” scope.
  The incremental `BOARD=VK_RA6M3 -j16` build and both Git whitespace checks
  completed successfully.  The commit has not been pushed.

## 2026-09-05 — TESTER ownership across VERIFY, HOME and PGA rebuild

### Corrected runtime contract

- Leaving VERIFY no longer stops an active TESTER.  HOME shows `TST` in the RX
  button for as long as GEN or FILE owns the DSP input; mode and filter can be
  changed from HOME while the same test source keeps running.
- TESTER is a real DSP source, not a visual overlay.  ADC/DTC/AGT provide only
  the sample cadence and block boundary.  Real ADC samples are not processed
  before the selected insertion point (`IN`, `MID:IQC`, `MID:NCO`, `MID:CHF` or
  `OUT`).
- GEN and FILE ownership changes are published atomically at a DSP block
  boundary.  Source changes reset signal history and advance a source epoch so
  stale spectrum, constellation and TIME frames cannot be accepted as frames
  from the new source.
- A FILE refill/EOF transition keeps a zero-I/Q FILE owner until the Python
  control path explicitly stops or replaces it.  This prevents a silent return
  of real ADC samples under a still-visible `TST` state.

### PGA reconstruction and checked teardown

- Before the first ADC block of a PGA reconstruction, the new IQADC instance is
  armed with zero I/Q at `IN`.  The old runtime chain, exact TESTER NCO and exact
  GEN/FILE source are restored before a new spectrum generation is published.
- RX acquisition is stopped and checked before the Python FILE object is
  detached.  A failed checked teardown therefore cannot expose one real-ADC
  block between FILE detach and ADC stop.
- If checked teardown does not release IQADC, the FILE buffers remain attached
  and rooted, PGA restart is aborted, and all pending LO/station/axis updates are
  cancelled.  A subsequent IQADC start is refused until a hardware reset,
  instead of constructing a second owner over retained hardware resources.
- Any runtime-chain or source-rearm failure clears TESTER visibly; HOME cannot
  continue to claim `TST` after the source has failed to arm.

### Verification and artifacts

- Host verification passed: Python compile, TESTER source contract, navigation
  source contract, FM bank source tests `3/3`, DSP vectors `22/22`, and UI
  contracts `6/6`.
- The stale Si5351 host test was corrected: `100,000,001 Hz` is deliberately
  valid through PLLB; the first frequency above the driver's inclusive
  `200,000,000 Hz` ceiling is `200,000,001 Hz`.  Both the PLLB transition and the
  inclusive upper limit now have positive assertions.
- Final `BOARD=VK_RA6M3 -j16` firmware build completed with exit code 0.
  `firmware.bin`: `1,577,320` bytes, SHA-256
  `245DA5D88134611F20C1D14973E1764F6F62E1A28D45CD22614ADC7FF8EEF298`.
- Canonical and external `sdr_single.py` are byte-identical: `285,893` bytes,
  SHA-256 `F4AD0E86811950BD0A7921BBA1C5C72B83CC3DA88221444A785F187A3837BF18`.
  Their `sdr_single.mpy` artifacts are also byte-identical: `78,177` bytes,
  SHA-256 `106E6A1CEE8697E1D96B8489F0181AE29613607D3BBC21FCFA5E2D1A515323BF`;
  rebuilding the MPY from the repository-relative source reproduced that hash
  exactly, so the artifact is current rather than merely copied.
- No firmware was flashed, no J-Link reset was issued, and neither COM18 nor
  COM24 was opened in this verification pass.  The results above prove source
  contracts and compilation only; panel/HIL behavior remains to be verified on
  the board.
- These changes remain uncommitted in the firmware worktree.

## 2026-09-06 — R:AM FILE completion and responsive BACK

### Cause and correction

- `BACK` itself was neither disabled nor blocked by navigation state.  The native
  FILE refill trampoline could remain inside an unbounded drain loop while new
  refill bits arrived, monopolising the same MicroPython scheduler used by LVGL
  touch and timers.
- `machine_iq_adc.c` now services one FREE buffer per scheduler turn, keeps the
  second request pending, requeues exactly one successor at the queue tail and
  returns.  Round-robin selection prevents either of the two buffers from being
  preferred indefinitely.  Epoch checks still make stale work harmless after
  stop, free or re-attach.
- A scheduler collision raised during the initial attach notification is cleared
  immediately before native start only when FILE is attached, inactive and both
  buffers are already READY.  Failures occurring after source activation remain
  visible in `file_status_into()`.
- `UND` and `SCHED` are cumulative transport counters, not terminal states.  The
  native transport already zero-fills a missed block, preserves the pending FREE
  mask and retries.  Python therefore no longer truncates the recording at the
  first recoverable UI/render delay.  File I/O/refill exceptions and ordered EOF
  retain the fail-closed teardown path.

### Verification and deployment

- Direct flash read timing for `/flash/iqbank/zam48.sdriq`, using the same 8192-byte
  buffer, was `404 us` minimum, `553 us` average and `1058 us` maximum over 30
  reads.  The early stop was therefore not storage-throughput exhaustion.
- Host checks passed: Python compile, canonical and external TESTER source
  contracts, navigation source contract, receiver-bank source tests `3/3`, UI
  contracts `6/6`, and DSP vectors `22/22`.  `git diff --check` was clean.
- The `BOARD=VK_RA6M3 -j16` build completed with exit code 0.  `firmware.bin` is
  `1,577,440` bytes, SHA-256
  `4EA3AABF1B75FD080CB631EEEE8D4D7281073BFA68BE0BA4FFAF5AE361A9089F`.
- J-Link serial `1120000058` was selected explicitly.  Its current CDC mapping was
  `COM25`; the other probe, serial `1120000060`, was `COM26` and was not opened.
  Programming reported `Program & Verify O.K.` and the independent `verifybin`
  read of all `1,577,440` bytes reported `Verify successful`.  A separate J-Link
  reset+go followed the flash.
- Deployed `/flash/sdr_single.mpy`: `78,164` bytes, SHA-256
  `E53B42DDE712813D369B461F9E75B35B0B6E3388C686A1C709AC41B7ECFCD907`,
  exactly matching the host artifact.  A separate J-Link reset+go followed the
  upload.
- On-target `R:AM / IN / ONCE`: the BACK action ran while FILE was still active at
  sample `15,744`, VERIFY closed, and playback then reached exactly
  `96,000 / 96,000` real file samples.  Final FILE and backend errors were both
  `None`.
- On-target `R:AM / IN / LOOP`: BACK again ran while FILE was active, HOME remained
  visible, and FILE was still requested and active after more than one complete
  pass.  The diagnostic snapshot reached `945,792` real samples with `SCHED=0`;
  recoverable zero-fill events remained counted as `UND=118` instead of stopping
  the source.  The test then stopped FILE explicitly and a final separate J-Link
  reset+go restored the normal application state.

### Proof boundary

The automated HIL proves scheduler progress, navigation completion and full FILE
consumption.  It does not prove that a physical finger press has ideal visual or
audio feel; that remains a short panel acceptance check.  These changes are not
committed.

## 2026-09-06 — PGA removed from the active SDR product

### Product decision

The configurable analogue PGA is no longer part of the SDR receiver.  Bench work
showed that its electrical constraints and available headroom do not match the
current Tayloe front end reliably.  The active design therefore uses a fixed unity
I/Q input path on P000 (I) and P004 (Q).  Digital AGC and volume remain independent
post-demodulation controls.

### Python UI and backend

- Removed the RF/PGA slider, enable state, persisted `rfe`/`rf` parameters and all
  stop/rebuild/rearm logic that existed only to change analogue gain.
- The GAINS panel now contains AF, AGC, SQL and ATT only.
- `Ra6m3Backend` constructs `IQADC(P000, P004, rate=..., block=...)` without gain
  arguments and has no gain getter/setter.
- TESTER and FILE input headroom now use unity scaling.  They no longer simulate an
  analogue gain stage.
- The visible VERIFY chain is fixed at block IDs 2 through 11.  Raw ABI ID 1 remains
  a permanently enabled, read-only `INPUT` observation point so existing scope and
  source contracts are not renumbered.
- Canonical and external `sdr_single.py` are byte-identical: SHA-256
  `FF746F9505B5855A352ECE289659AB37FA63C5E944034F0D17B142430A321A4A`.
- Their rebuilt `sdr_single.mpy` artifacts are also byte-identical: 73,981 bytes,
  SHA-256 `4E48B00E845F666D07FEE07831BBF548622153F511BDD08FD6BA170E9DFEB2CD`.

### Firmware API and input route

- Removed the public `machine.ADC` PGA constants and methods.
- Removed `pga=` and `gain=` from `machine.IQADC`, together with `IQADC.gain()` and
  the configurable gain API below it.
- Removed gain simulation from native TESTER and FILE injection.
- Block ID 1 is now a fixed `INPUT=ON` stage; it cannot be bypassed.  IDs 2 through
  11 keep their established meanings.
- P000/P004 still require a particular ADC12 direct-input register selection on the
  RA6M3.  That silicon routing is private driver setup, is cleared during teardown
  and is not exposed as a user-adjustable gain feature.

### Verification and build

- Python syntax and byte identity checks passed for both application copies.
- UI contracts passed `8/8`; both TESTER source contracts passed; the navigation
  source contract passed; receiver-bank source tests passed `3/3`; DSP unit vectors
  passed `22/22`; `git diff --check` passed for the scoped changes.
- Firmware was built with the established native MSYS2/UCRT64 helper
  `ports/renesas-ra/_b.sh`, which runs `make BOARD=VK_RA6M3 -j16` with the correct
  toolchain PATH.  Build result: `EXIT=0`.
- `build-VK_RA6M3/firmware.bin`: 1,581,440 bytes, timestamp
  `2026-09-06 04:19:26`, SHA-256
  `3E116DFC3BBAF9351323835DE7B2892603A824841916A41219FFDCB3EDA2361C`.
- J-Link probe `1120000058` was selected explicitly; Windows mapped it to `COM25`.
  The second probe `1120000060` (`COM26`) was not opened.
- `loadbin` reported `Program & Verify O.K.` and a separate `verifybin` read and
  matched all 1,581,440 bytes at address zero.  An independent J-Link reset+go
  followed the firmware operation.
- `/flash/sdr_single.mpy` was copied and hashed on the board: 73,981 bytes,
  SHA-256 `4E48B00E845F666D07FEE07831BBF548622153F511BDD08FD6BA170E9DFEB2CD`,
  identical to both host copies.  A second independent J-Link reset+go followed
  the upload.
- On-target API/capture HIL passed.  The removed legacy `pga=` constructor keyword
  raised `TypeError`; the fixed direct P000/P004 capture advanced by 281 blocks in
  750 ms with zero ADC1 stalls; block IDs 1 and 11 remained enabled when a bypass
  write was attempted.
- Full application HIL reached 872 capture blocks with `running=True`, zero ADC1
  stalls, no backend exception and 72,672 bytes of free Python heap.  The observed
  `last_error=-1` was accompanied by the deliberately unread raw debug buffer's
  overrun count; capture continued at the expected rate and this is not a hardware
  stop condition.
- A final independent J-Link reset+go left the normal `/flash/main.py` application
  running on the panel.
- The PGA removal remains uncommitted.  Concurrent OpenCV/QSPI edits in
  `bsp_cfg.h`, `main.c`, `mpconfigport.h`, `ra_flash.c` and `cv2_qspi.mk` were
  preserved and were not treated as part of this change.

## 2026-09-06 — compact IQTX memory and HOME RX/TX handoff

### Git checkpoint before the new work

- The preceding integration state was committed as
  `9943cecc55086f0704e4fd7d3cdefc31aa228120` and tagged
  `ra6m3-sdr-tx-ui-pga-removal-hil-and-qspi-profile-v1`.
- The compact-LUT and RX/TX handoff changes described below are later working-tree
  changes and remain uncommitted.  Nothing was pushed.

### Compact transmitter tables

- IQTX now allocates according to the selected modulation instead of reserving a
  131071-byte block for every AM/FM instance.
- CW uses no LUT allocation.  AM uses an 8192-byte table aligned to 8192 bytes
  (maximum allocation 16383 bytes) and 7 DTC descriptors.
- FM uses a 1024-byte planar table aligned to 1024 bytes (maximum allocation
  2047 bytes): the I plane starts at `+0x000`, the Q plane at `+0x200`.
  FM gain 1/2 uses 14/15 DTC descriptors respectively.
- The original allocation object remains a MicroPython GC root.  A constructor
  failure can still be recovered through the static `IQTX.release()` path.

### Existing HOME button now requests RX <-> TX

- The existing top-right `rx-button` is reused; no second PTT button was added.
- The UI has explicit `RX_OFF`, `RX`, `TO_TX`, `TX`, `TO_RX` and `FAULT` ownership
  states.  The button shows `RX`, `WAIT`, `TX` or `ERR` from that state.
- RX -> TX first refuses unsupported USB/LSB, active TESTER/FILE and unfinished
  LO/VFO work.  These refusals happen before receiver teardown.
- A permitted AM/FM/CW transition stops RX, preserves the physical DAC handles,
  deinitializes DAC1/Q before DAC0/I, creates IQTX, verifies prepared state, starts
  it and verifies running/output state.  CW starts key-up.
- Constructor/start failure performs static IQTX release and reconstructs the full
  RX backend.  TX -> RX uses checked stop/deinit/release before RX reconstruction.
- Unexpected worker exceptions queue the same checked RX recovery instead of
  leaving a possibly active owner behind a passive ERR state.  Uncertain teardown
  is retried at most three times; roots remain retained if all retries fail.
- Frequency keypad, VFO/tuning, route/CAL, mode/filter and step mutations are
  blocked during TX and either transition.
- This remains a baseband I/Q handoff only.  TX LO routing, PA enable, RF relay,
  hardware PTT/mute and an external RF interlock are not implemented.

### Verification and artifacts

- Actual `ra_tx_core.c` host test: PASS, 7 groups and 5,609,421 checks.  This covers
  compact size/alignment/address models, every AM ADC code, every 16-bit FM phase
  value and all CW ramp lengths; it is not target-register or RF proof.
- New host-only SDR switch test passed: button binding/state requests, labels,
  pre-teardown guards, Q-before-I release, constructor MemoryError rollback,
  runtime-fault recovery request and bounded cleanup retry.
- Python syntax passed for `sdr_single.py` and all three updated IQTX HIL scripts.
  `git diff --check` passed.
- `BOARD=VK_RA6M3 -j16` compile/link completed with exit code 0.  Final size:
  text 1,581,373; data 0; bss 649,552 bytes.  The build is the complete current
  working tree and therefore also contains the unrelated parallel
  `machine_lcd.c` `touch_into()` change.
- `firmware.elf` SHA-256:
  `6C19D6958D54709A96DFB15B9A2D65F087E8DB4DB5F97E9E75A8239F3F60576C`.
  `firmware.bin` SHA-256:
  `01843DA0B6490A1ECC430ECB30CBE07B6AC1E9979EB5A03103EFA0FB66E7252B`.
- Repository and external working `sdr_single.py` are byte-identical, SHA-256
  `96482AB5494C12CE1D87F66044727D7B374C252E13FCA136CE535F5DB0DEBF5A`.
  The external `sdr_single.mpy` was rebuilt with mpy-cross v6.3: 78,524 bytes,
  SHA-256 `82D52BBD7C1AD921B9FD7060EDBB9E72EC089BA9AC05B9AC7762A38DC59F0EBC`.
- No firmware or MPY was uploaded to the board, no J-Link reset was issued, and no
  COM port was opened in this stage.  Compact retained-UI RAM, real RX -> TX -> RX,
  DTC timing and the analog I/Q output remain HIL work.

## 2026-09-06 — QSPI backup, 12/4 migration, paired upload and SDR heap blocker

- User explicitly requested deployment, then a complete external-flash backup
  and migration to 12 MiB filesystem + 4 MiB QSPI code. Only J-Link `1120000058`
  was used; serial enumeration mapped it to COM25. Board UID was checked as
  `5434032d35373948384edaf926275454`. Probe `1120000060` was not opened.
- The SDR-only internal image `01843DA0...` was initially written and verified.
  After the expanded request, the paired OpenCV/QSPI profile replaced it as
  described below. Neither image's successful flash is an RX/TX functional test.
- Full original external flash: 16,777,216 bytes from `0x60000000`, saved and
  independently compared with J-Link `verifybin`. SHA-256:
  `1062A0BECD3329BA740E60889CF8EB561B8133DDAE6994CB779AC690E194C21A`.
- Backup directory: `backups/qspi-1120000058-20260906-204109/`. All 48 files
  (3,366,339 file-content bytes) were also extracted locally and hashed.
- FAT16 had 3,053 allocated clusters, highest cluster 3,073. The reduced volume
  permits 6,127 data clusters, so no file data relocation was needed. Only BPB
  byte offset 20 changed: total sectors 32,768 -> 24,576. All extracted files were
  read and hash-checked against the candidate 12 MiB filesystem before programming.
- The SRAM programmer first positively identified JEDEC `1F 89 01`, then wrote
  and verified the 4 KiB boot sector and QSPI code sectors. A full 12 MiB readback
  matched the prepared filesystem image. The matched firmware/QSPI pair came
  from the successful `build-CV2-QSPI-noalloc` 204 KiB-heap build at 20:46.
- Internal image: 1,812,976 B, SHA-256
  `61A58FF9442C00CC6E82DEA236C18AB91079875F603F0338542EAEBA8BA15333`.
  QSPI code at `0x60C00000`: 677,064 B, SHA-256
  `790604B5F07CCEF8FA15C7E7A260E898F9FA80D2447501A2D7E731CBA9C33886`.
  Both matched target readback; internal `0x001C0000..0x001FFFFF` remained unchanged.
- Runtime mount reports `statvfs=(2048,2048,6127,3074,3074,0,0,0,0,255)` before
  the application update. All 48 original files passed target SHA-256 checks.
- `/flash/sdr_single.mpy` was then updated to 78,524 B, SHA-256
  `82D52BBD7C1AD921B9FD7060EDBB9E72EC089BA9AC05B9AC7762A38DC59F0EBC`.
  All 47 other file hashes still matched, including boot/main and IQ recordings.
- **RX startup is NOT passing on this profile.** The first old-MPY startup left
  insufficient RAM even for a 136-byte diagnostic allocation. A subsequent normal
  J-Link reset with the updated MPY recorded `MemoryError: allocating 72 bytes`
  in `_build_freq_input()` at `sdr_single.py:927`, while constructing keypad buttons.
  No TX transition or analog/RF test was performed.
- ELF accounting identified a Python heap reduction from 290,816 to 208,896 B
  (80 KiB). `.bss` grew 77,776 B and `.data` grew 2,112 B. Static `cv2_c_arena`
  alone reserves 65,536 B; other static growth is 14,352 B. The remaining 2,032 B
  of heap reduction becomes increased unassigned RAM margin.
- A halted on-target snapshot of `cv2_c_arena` at `0x1FFE1968` found 2,040 B of
  occupied payload, 336 B metadata, and 63,160 B free payload. This is not a
  peak-demand measurement and does not justify blindly reducing the arena.
- Final board state: REPL, reached by J-Link reset and interrupting autostart;
  no MicroPython soft reset and no boot/main changes. Normal reset will attempt
  the same application again. Heap-budget remediation remains open.
- Detailed analysis and exact accounting: `heap_analysis_20260906.md`. The backup
  migration directory contains image copies, per-file hashes, programmer logs,
  readback results, ELF/symbol accounting and the failing startup traceback.
- This stage did not commit or push repository changes.

## 2026-09-06 — OpenCV: exact internal/QSPI deployment procedure

Recorded at the user's request. This is the local SparkFun/OpenCV dual-camera
project, not an SDR/RF hardware test. Target: COM26, J-Link serial 1120000060,
device R7FA6M3AH, SWD 4000 kHz. Do not use the other probe/COM25.

### Host commands

```powershell
Set-Location 'C:\Users\teodor\Documents\Codex\2026-09-06\new-chat-2'
$py = 'C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe'

# Build and software gates only; this command does NOT access hardware.
& .\vendor\micropython-opencv\platforms\ra6m3\build_profile.ps1 `
  -ProfileBuild build-CV2-QSPI-noalloc -Jobs 8

# Paired firmware deployment; run only after successful build/audits.
& $py .\work\noalloc_flash.py `
  --candidate 'C:\msys_64\home\teodor\renesas_micropython\ports\renesas-ra\build-CV2-QSPI-noalloc' `
  --previous '.\outputs\noalloc-flash-20260906-203713\candidate'

# MPY modules: compile, upload with SHA-256 verification, then soft-reset/start.
& $py .\work\dual_cv2_package.py
& $py .\work\dual_cv2_deploy.py upload
& $py .\work\dual_cv2_deploy.py start
```

The --previous path above identifies the actual paired image installed at the
time this procedure was first recorded. For subsequent updates use the exact
verified snapshot corresponding to the currently installed pair, not an older
build directory that may have been overwritten. The script compares both live
images against that snapshot before erasing anything; mismatch refuses flashing.
Do not run a second deployment/COM session concurrently.

### What writes each flash

Internal firmware.bin is written through the J-Link device flash loader:

```python
j.open(serial_no=1120000060)
j.set_tif(pylink.enums.JLinkInterfaces.SWD)
j.connect('R7FA6M3AH', speed=4000)
j.flash_file(str(run/'candidate/firmware.bin'), 0x00000000)
```

External firmware-qspi.bin is NOT passed to that internal-flash command. A
bounded helper from work/qspi_ram_programmer.bin executes temporarily from SRAM:

```python
helper = h.RamProgrammer(j)
helper.call(0)  # Require exact JEDEC 1F 89 01 before any erase.
# For each changed 4096-byte sector of the validated image envelope:
helper.call(1, 0x00C00000 + offset, sector)
helper.restore()
```

These snippets explain the implementation, not a safe standalone replacement
for noalloc_flash.py. The wrapper saves/restores CPU registers, interrupted
stack bytes and SRAM framebuffer scratch, drains/stops its own app, disables
interrupts while the SRAM helper operates, and verifies readback.
The QSPI chip offset 0x00C00000 is mapped to CPU address 0x60C00000.
The helper issues sector erase 0x20 and page program 0x02 in 256-byte pages;
host readback checks every 4096-byte sector. No chip erase is used.
The paired deployment only targets the final 4 MiB code partition; it does not
invoke the separate boot-sector migration capability of the historical helper.

Sequence: back up all files and actual old internal/QSPI contents -> program
and verify external code -> program and verify internal image -> check reserved
internal tail and filesystem boot sector unchanged -> reset -> check APIs and
all original file hashes. A programming failure leaves the CPU halted with
backup evidence; an incompatible half-pair is not deliberately booted.

QSPI layout: filesystem 0x60000000..0x60BFFFFF (12 MiB), executable image
0x60C00000..0x60FFFFFF (4 MiB). MPY files go only into
/flash/thermal_cv2_touch; boot.py/main.py are not edited.

### Eight-filter status when this procedure was recorded

- The 192 KiB heap eight-mode build was compiled, paired-flashed and verified.
- 3200 standalone operations (8 modes x 200 calls x 2 geometries) passed
  numerical reference checks and heap-lock/C allocator-counter checks.
- It was NOT accepted as a complete dual-camera demo: MLX initialization failed
  with MemoryError allocating 12192 bytes, leaving its image black.
- A 204 KiB heap profile (+12 KiB reserved at startup, no runtime allocation)
  was being rebuilt to recover setup headroom. Stack size remains 16 KiB;
  final linker and real dual-camera checks are required before acceptance.
- Evidence root: C:\Users\teodor\Documents\Codex\2026-09-06\new-chat-2\outputs.
  Initial eight-mode flash: noalloc-flash-20260906-203713.
  Standalone vectors: filters8-vectors-20260906-203850.

### Final acceptance — 204 KiB heap, eight OpenCV modes plus original bilinear

- The MLX black image was resolved: the 204 KiB profile was built, audited,
  written and checked. Both sensors initialized successfully on COM26.
- Final internal firmware: 1,812,976 B; current controlled baseline 1,581,360 B;
  profile delta 231,616 / 307,200 B. Against the original 1,580,864 B baseline
  the full delta is 232,112 B, also within the requested limit.
- External image: 677,064 / 4,194,304 B. Heap 208,896 B; total mapped RAM use
  389,856 / 393,216 B includes the unchanged 16 KiB stack. The 261,120-byte
  GLCDC framebuffer remains separate. No further filesystem resizing occurred.
- Final internal SHA-256:
  `61a58ff9442c00cc6e82dea236c18ab91079875f603f0338542eaeba8ba15333`.
  External SHA-256:
  `790604b5f07ccef8fa15c7e7a260e898f9fa80d2447501a2d7e731cba9c33886`.
- Verified current firmware snapshot for a future --previous argument:
  `C:\Users\teodor\Documents\Codex\2026-09-06\new-chat-2\outputs\noalloc-flash-20260906-204723\candidate`.
  This replaces the older snapshot in the historical command above for future
  deployments from the current board state. The guard must still match live bytes.

The same dual-camera demo now has nine independently selectable display modes:

| ID | Bottom view |
|---:|---|
| 0 | OpenCV blur 3x3 |
| 1 | OpenCV threshold >127 |
| 2 | OpenCV Sobel X 3x3 |
| 3 | OpenCV Gaussian 3x3 |
| 4 | OpenCV Sobel Y 3x3 |
| 5 | OpenCV linear FilterEngine with four-neighbour Laplacian kernel |
| 6 | OpenCV linear FilterEngine with sharpen kernel |
| 7 | OpenCV inverse threshold |
| 8 | Original float-temperature bilinear interpolation |

RAW stays above; touch press/release in one column only cycles that column.
The legacy mode is deliberately not labelled OpenCV. It retains the original
float32 bilinear/palette operation sequence and reproduces the REPR_C rounding
of the original Python weight table (py/obj.h). Weights are calculated in FPU
registers, so neither the old large image buffers nor per-axis tables are needed.
It writes at LCD stride directly. Only a 16-byte geometry array and cached
destination memoryview per camera are prepared at setup.

Final validation:

- Eight host tests passed, including original RAW/acquisition preservation,
  nine-step independent touch wrap/debounce, eight OpenCV output routes and
  the separate legacy path (no temperature-to-GRAY8 quantization in legacy).
- Same final QSPI payload: 3200 standalone OpenCV operations passed numerical
  and locked-heap/C-counter checks; all eight operations also matched independent
  reference pixels on real AMG and MLX grayscale frames.
- Legacy FPU renderer matched the actual original ASM renderer pixel-for-pixel
  on 8x8->70x70 and 32x24->96x72, three temperature patterns each. Full 480x272
  comparison also checked that every pixel outside the destination was unchanged.
  All six comparisons ran under heap_lock with unchanged allocated bytes.
- The complete nine-mode application passed all 81 mode pairs, two real
  acquisition/update cycles per pair, with locked heap and unchanged C counters.
- A roughly 30-second asynchronous run with 24 sleeps of 1250 ms and 12+12
  software-triggered touch handlers produced 304 AMG frames, 161 MLX updates
  and 65 complete MLX subpage pairs. Heap 195136 -> 195136 B; C counters
  [471,325,0,0,0] unchanged; zero I2C errors and no update failure.
- The final firmware was traced at PC 0x60C83FA8 in QSPI prepared code.
  CMSIS arm_min_q15 at 0xCCCBC, arm_max_q15 at 0xCCCE0 and DSP USUB8/SEL
  at 0x21976/0x2197A were reached/executed; QSPI bytes matched the exact build.
- The actual framebuffer was captured and visually checked with both lower
  views labelled BILINEAR original and both upper RAW images present.
- Final MPY SHA-256 checks passed. Sizes: amg8833 6268, mlx90640 25609,
  i2c_buffer_pool 894, thermal_cv2_filters 2435,
  thermal_dual_opencv_touch 12975, pRGB 2044 bytes.
  Original demo, source drivers, boot.py/main.py and protected root files remain
  unchanged. The app was left running with modes 8/8; both frame counts advanced.

Evidence directories under the outputs root above:

- noalloc-flash-20260906-204723 — current firmware backup/deploy/readback.
- filters8-dual-20260906-204834 and filters8-real-20260906-204953 — eight-mode HIL.
- prepared-execution-20260906-205014 — current QSPI/DSP instruction trace.
- original-bilinear-20260906-210816 — final no-table legacy pixel comparison.
- nine-mode-dual-20260906-211108 — 81 combinations and live locked test.
- dual-display-20260906-211230 — actual framebuffer PNG and board counters.
- dual-cv2-touch-hil/noalloc-final.json — final hashes and running-state proof.
- dual-cv2-touch-package/manifest.json — final source/MPY identities.

Deployment tool clarification: `dual_cv2_deploy.py upload` now stops its own demo
and soft-resets to an empty VM after checking the known boot/main hashes. Binary
transfer command parsing needs temporary heap; this is outside the no-allocation
application window. An earlier attempt while diagnostics occupied RAM truncated
only our AMG file to zero bytes; its identity/scope was verified, the exact full
file was restored, and final hashes passed. No user file was removed. Raw-REPL
entry now retries bounded synchronization after a USB reset, without reflashing.

Temporary table-based legacy variants were not accepted due to initialization
MemoryError. After an isolated LCD test, one startup stopped responding without
a CPU fault; a hardware reset restored control. No firmware driver cause was
claimed from that observation. Acceptance above is for the final table-free
MPY variant. Physical finger-touch is not inferred from software handler calls;
boot/import/init/diagnostic transfer allocations and arbitrary fault paths are
outside the verified steady-state no-allocation claim. No commit/push was made.

## 2026-09-06 — SDR: OpenCV OFF по подразбиране, постоянен QSPI layout 12/4

Изискване: Python приложението да остане лесно редактируемо; frozen SDR е
последен вариант. Текущият SDR firmware задължително се компилира без OpenCV.

Промени по файлове:

- `ports/renesas-ra/Makefile`: OpenCV е OFF по подразбиране; ключът приема само 0/1.
- `boards/VK_RA6M3/vk_ra6m3.ld`: 12 MiB файлове + 4 MiB запазен код в стандартния
  board linker, независимо от OpenCV; добавени граници и linker assertions.
- `boards/VK_RA6M3/mpconfigboard.h`: включен независим `MICROPY_HW_QSPI_CODE_RESERVE`.
- `mpconfigport.h`: подразбиране 0 за останалите платки; запазени съществуващите промени.
- `main.c`: filesystem размерите и забраната за auto-format след mount failure
  зависят от запазения QSPI дял, не от OpenCV. OpenCV boot остава само при ON.
- `ra/ra_flash.c`: erase/write не могат да навлизат в кодовите 4 MiB и при OFF.
- Външен `vendor/micropython-opencv/micropython.mk`: OFF не включва код, ulab,
  allocator или библиотеки, дори USER_C_MODULES пътят да остане зададен.
- `platforms/ra6m3/make_linker.py`: ON използва готовия кодов резерв чрез alias;
  не променя файловия дял; отказва стария 16 MiB layout.
- `platforms/ra6m3/build_profile.ps1`: `-OpenCV Off|On`, подразбиране OFF,
  native MinGW Python с изчистена Python среда, 16 compile задачи по подразбиране.
- `platforms/ra6m3/tests/test_profile_switch.py`: проверки на защитите, OFF default,
  отказ при стар layout, ELF граници, heap и липса/наличие на OpenCV.
- `build_sdr.ps1` в SDR папката: лесен вход, OFF по подразбиране, използва
  съществуващите build папки; няма операции с платка.
- `OPEN_CV_SWITCH_BG.md` във firmware board папката и `OPENCV_BUILD_BG.md`
  в SDR папката: команди, ограничения и доказателства.

Проверено: `-OpenCV Off -ProfileBuild build-VK_RA6M3 -Jobs 16` — build PASS,
ELF audit PASS, 0 CV2/OpenCV символи, няма OpenCV архиви в link map,
heap **290 816 B**, файлове **12 582 912 B**, кодов резерв **4 194 304 B**.
Върнати **81 920 B (80 KiB)** heap спрямо OpenCV варианта с 204 KiB.
Negative тестът с ключ 2 отказва с ясна грешка. PowerShell parse/default тест PASS.

`firmware.bin`: 1 581 440 B; SHA-256
`1f9bfa264f6981e5f1b62fc3a455f922555066a6b7eba698f17a11c2412c0fbc`.
Лог: `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2\opencv-off-12-4-build.log`.
Отчет: `build-VK_RA6M3/profile-switch-audit.json`.

Това е комбиниран build на споделеното работно дърво, не изолиран commit.
ON е проверен само за генериране на linker layout, не е rebuild-ван тук.

### Качен OFF firmware и подготвен външен QSPI flash

Изрично разрешено от потребителя: подготовка на външния flash, със запазване
на файловете. Целта е запомненият **J-Link 1120000058 / COM25**;
1120000060 / COM26 не е използван. Не се повтарят проверки за идентичност
при всяка команда; остават проверките на записа и архива.

Архив:
`C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_RA6M3\backups\opencv-off-1120000058-20260906-224133`

1. Запазени са пълни изображения преди операцията: вътрешен code flash
   2 MiB, външен QSPI 16 MiB и снимка на вътрешната data flash 64 KiB.
   `candidate` пази точния OFF firmware и ELF/map/audit.
2. Файловият дял вече е бил FAT16 с размер 12 MiB. Неговият първи 4 KiB
   сектор е записан и проверен със същите данни. **Първите 12 MiB остават
   идентични**, включително всичките **48 файла**, boot/main и SDR `.mpy`.
   Приложението не е презаписвано и не е превръщано във frozen модул.
3. В последните 4 MiB са изчистени 166 сектора по 4096 B със стар OpenCV код.
   Целият резерв `0x60C00000..0x60FFFFFF` е проверен като `0xFF`.
   Старото съдържание може да се възстанови от пълния QSPI архив.
4. Вътрешният OFF `firmware.bin` е записан и проверен байт по байт.
   Запазеният вътрешен край `0x1C0000..0x1FFFFF` е непроменен.
5. Прочетени са повторно всичките 16 MiB външен flash; съвпадат с подготвения
   образ. QSPI SHA-256 след операцията:
   `ea74a5b5f630772082f4747513f26237abfe2af823d19fd7afa7f0b798d14499`.
6. След успешния readback е направен J-Link reset чрез `AIRCR.SYSRESETREQ`.
   Съществуващият auto-start стартира SDR; тестът не вика `start()` повторно
   и не активира TX.

Уточнение за спрялата междинна проверка: вътрешната **data flash** е отделна
от външния **QSPI flash**. Първоначалното сравнение на всички 64 KiB data flash
е било неподходящо: изтритите клетки нямат определена стойност при четене
([Renesas RA6 Quick Design Guide, Figure 22](https://www.renesas.com/en/document/apn/ra6-quick-design-guide?r=1493931)).
Истинският SDR1 запис е 6 B заглавие + 252 B JSON + 2 B подравняване = **260 B**;
всичките му байтове съвпадат. Не е правено сляпо възстановяване на празната
област. `error.json` пази първоначалния отказ; `deployment.json` е окончателният
PASS. Довършващият скрипт само проверява и ресетва, без повторен flash запис.

### SDR проверка след качване

- Auto-start app е налично; `STATE RX True None None`, бутон RX,
  работещ LVGL event loop, без създаден IQTX обект.
- `import cv2` дава `ImportError` — OpenCV липсва и при изпълнение.
- GC: **284 096 B общо**, 211 344 B използвани / 72 752 B свободни при
  първата снимка. В края след cleanup: **72 192 B свободни**.
  Linker heap 290 816 B минус GC pool 284 096 B = **6 720 B** служебна област.
  GC pool е с **80 000 B** повече от измерените преди 204 096 B при OpenCV.
- Отваряне на клавиатура: 73 024 B свободни; връщане към HOME: 73 104 B.
  Няма MemoryError в приложението при тези действия.
- IQ `blocks`: **11 510 → 54 364**; render count: **64 → 293**.
  RX остава активен, DAC `playing=True`; audio underruns и ring overruns са 0.
- Не се твърди, че всички диагностични полета са нула: последната IQ снимка
  съдържа `overruns=54363`, `last_error=-1`; причината им не е предмет на този
  тест. Пълните снимки са запазени, без да се обявяват за RF/аналогов тест.
- Една raw-paste UART команда е прекъсната от фоновото `SDR saved` съобщение.
  Проверяването е довършено през стандартен raw REPL, без reset и без повторно
  стартиране на приложението. `runtime-final.json`: **SDR_OFF_RUNTIME_PASS**.

Доказателства в архива: `backup.json`, `files-preserved.json`, `deployment.json`,
`autostart-boot.log`, `runtime-check.json`, `runtime-final.json`.
Хост логове в `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2`:
`opencv-off-qspi-deploy.log`, `opencv-off-finish.log`, `opencv-off-runtime.log`,
`opencv-off-runtime-final.log`.

SDR е оставен работещ в RX; COM25 е освободен. Физически touch и аналогов/RF
изход не са проверявани. Няма commit/push.

## 2026-09-06 — USB/LSB предаване: код и build, без качване

Заявка: реализиране на USB/LSB предаване. Добавен е цифров I/Q път и избор
през съществуващия HOME RX/TX бутон. Няма нови операции с COM/J-Link, flash
или включване на TX в тази задача. Това е baseband реализация, не завършен
и измерен RF предавател.

### Как работи

- P001 / ADC0 AN001 приема микрофонния сигнал при 12 000 измервания/s.
  Нужни са външен входен усилвател, постоянно отместване в допустимия ADC
  диапазон и аналогов филтър. `adc_mid` задава измереното ниво при тишина.
- C филтърът формира I и Q за гласовата лента 300..3000 Hz. USB оставя
  положителните честоти в `I+jQ`, LSB обръща Q. Реалната RF странична лента
  зависи и от свързването/знаците на външния квадратурен смесител.
- Изходите са P014 / DAC0 за I и P015 / DAC1 за Q. Записите са последователни;
  аналогова едновременност и разлика във времето не са измервани.
- Историята на филтъра остава между всички семпли и входни порции. Само нов
  старт я инициализира. Закъснението на двата канала е еднакво: 127 семпъла,
  или 10.583 ms. Постоянният вход се отхвърля, включително при първи старт.
- SSB обработката е в C ADC callback, без Python, float или heap алокация
  на семпъл. Това използва CPU; не е автономният DTC/DOC път на AM/FM/CW.
- USB/LSB избират автоматично 12 kS/s; друг изрично зададен rate се отказва.
  CW/AM/FM запазват подразбиращите се 44 kS/s и предишния хардуерен път.

### Промени по файлове

Пътищата по-долу са относителни към `ports/renesas-ra` във firmware repo.

1. `ra/ra_tx_hw.h`: USB/LSB константи, SSB rate, DSP deadline грешка и шест
   брояча за семпли, цикли, бюджет, превишения и ограничаване на амплитудата.
2. `ra/ra_tx_core.h`: постоянен 256-семплов контекст и API за обработка на
   отделен семпъл. Контекстът не се създава наново на границите на порциите.
3. `ra/ra_tx_core.c`: 255-коефициентен комплексен FIR с целочислени операции,
   съгласувано закъснение I/Q, избор USB/LSB, начално запълване с входния bias,
   общ I/Q ограничител и проверки на DAC диапазоните.
4. `ra/ra_tx_ssb_coeffs.h` (нов): 510 B постоянни коефициенти в code flash.
   Симетрията намалява броя умножения; квантованата DC сума е точно нула.
5. `ra/ra_tx_hw.c`: ADC callback за SSB, DAC записите и диагностика на C
   обработката; SSB не отваря DTC/DOC chain. Checked stop забранява неговия
   ADC IRQ след спиране на източника. 520-B SSB контекстът използва чрез union
   съществуващото CW работно място, без втори голям RAM буфер.
6. `machine_tx.c`: `IQTX.USB`/`IQTX.LSB`, режимно подразбиране на rate,
   проверки преди init и DSP полета в `status()`. Няма SSB LUT алокация.
7. `boards/VK_RA6M3/examples/sdr_single.py`: двата режима са разрешени за TX;
   RX/TX lifecycle и TESTER/FILE забраните се запазват. UI следи напредването
   на `dsp_samples` и заявява checked връщане към RX при спрял поток.
   Същата промяна е приложена и в проектния `sdr_single.py`; двете `.py`
   копия са идентични. Съществуващият `.mpy` е стар и не е обновяван.
8. `tests/tx/gen_ssb_coeffs.py` (нов): възпроизводим генератор с `--check`,
   проверки на симетрията, DC сумата и безопасните int32 акумулатори.
9. `tests/tx/test_tx_core.c`: четири нови SSB групи върху действителния C код:
   DC/валидация, странични ленти, непрекъснат поток и филтърно закъснение.
   Невалидният mode е преместен от вече валидния 3 към 5.
10. `tests/tx/test_sdr_tx_switch.py`: host RX→TX→RX за петте режима,
    откази/rollback, SSB спрял брояч и коректно превъртане на uint32 брояча.
11. `boards/VK_RA6M3/examples/iqtx_probe.py`: USB/LSB са позволени при изрична
    подготовка. Import не стартира TX.
12. `tests/tx/iqtx_board_hil.py`: актуализирани невалидни mode/rate случаи;
    скриптът не е изпълняван на платката в тази задача.
13. `boards/VK_RA6M3/examples/README_IQTX_BG.md`: API, алгоритъм, RAM/flash,
    проверки, ограничения и отделяне на историческите измервания.
14. `tests/tx/IQTX_HIL_RESULTS_BG.md`: отбелязано, че новият SSB има само
    host/build проверки; запазен е старият HIL протокол без преинтерпретация.

### Проверки и аритметика

- Действителният `ra_tx_core.c`: **11 групи, 6 465 784 проверки — PASS**.
  56 USB/LSB тона 300..3000 Hz: най-лошо цифрово потискане на нежеланата
  странична лента **67.85 dB**. Това не е RF измерване.
- Различни размери на входните порции дават битово идентичен резултат.
  Проверени са DC, 127-семплово закъснение, извънлентови тонове 3.5..5.5 kHz,
  претоварване, DAC граници, USB/LSB съпряжение и нов старт.
- Първият тест за средна стойност под 0.1 DAC кода отчете 0.2 при 2400 Hz.
  Границата е коригирана до 0.70 кода според двете закръгляния при A=800:
  `0.5*800/2048 + 0.5 = 0.6953125`. Отделният тест за постоянен ADC вход
  продължава да изисква точно нулев комплексен изход. Първият лог е запазен.
- Проверка на коефициентите, UI host тест и Python syntax: PASS.
- Build: `build_sdr.ps1 -OpenCV Off -Jobs 16`, общата `build-VK_RA6M3`: PASS.
  OpenCV/CV2 символи: **0**. Linker heap: **290 816 B**, без промяна.
  QSPI: **12 582 912 B файлове + 4 194 304 B резерв**, без промяна.
- Коефициенти: 256 + 254 = **510 B code flash**. SSB state: **520 B**, вътре
  в старото CW работно място. Целият native TX state: 1484 → 1508 B,
  или **+24 B** за шестте диагностични полета. Няма нов SSB LUT heap разход.
- `firmware.bin`: 1 581 440 → **1 582 944 B**, увеличение **1504 B** спрямо
  предходния качен OFF образ. SHA-256 на новия:
  `b36ac6b23ec9598b1bd033ce8269eddc27ba266ae724ba874274d8539c1f78d2`.
  ELF size: text 1 582 957, data 0, bss 649 568 B. Това е комбиниран build
  на споделеното работно дърво; bss не означава свободна RAM.
- Остава linker warning за RWX LOAD segment; няма compile/link грешки.

Логове в `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2`:
`ssb-tx-host-tests-final.log`, `ssb-tx-ui-tests.log`,
`ssb-tx-opencv-off-build-final.log`. ELF audit:
`build-VK_RA6M3/profile-switch-audit.json`.

### Непроверено и следваща стъпка

Новият firmware и Python source трябва да се качат заедно; старият `.mpy`
не съдържа USB/LSB UI промените. Няма frozen SDR и не е променян външният
файлов дял. Няма commit/push в тази задача.

SSB ISR време, истинска честота/пропуснати ADC измервания, устойчивост с GUI,
RX→TX→RX, аналогови I/Q и RF странична лента изискват нов тест на платката.
`dsp_*_cycles` измерват C обработката, не целия IRQ вход/изход; нулев deadline
брояч сам по себе си не доказва липса на загубени измервания. Преди TX HIL
трябва да е потвърдено разкачено/блокирано RF стъпало.

TX LO routing, PA enable, RF relay и хардуерен PTT/interlock не са реализирани
с тази промяна. DAC neutral/disable не замества външна RF защита.

## 2026-09-07 — USB/LSB качен; цифров DAC тест; оставен USB TX

Потребителят поиска продължаване, тест на DAC и изрично оставяне в TX.
Използвани са запомнените J-Link 1120000058 / COM25, без повторно търсене на
платки/портове. PA/PTT не са управлявани. Няма commit/push.

### Качване и запазени данни

Пълен архив преди промените:
`backups/ssb-tx-1120000058-20260907-001143` — вътрешен code flash 2 MiB,
външен QSPI 16 MiB, data-flash snapshot 64 KiB, точни кандидат BIN/ELF/map,
Python source, manifest и SHA-256 на оригиналните 48 файла.

- Първата preflight команда отказа, защото приложението беше в TX. Последва
  checked връщане в RX: `RX True None None`; преди него няма flash запис.
- SRAM QSPI помощникът отказа offset4096 с `0xe003`: неговият обхват е само
  boot секторът и последните 4 MiB. Отказът е преди erase; секторът и старият
  firmware бяха проверени като непроменени. Ограничението не е разширявано.
- Първото VFS прехвърляне без serial timeout заседна преди първия payload.
  Хост I/O броячите спряха; собственикът на сесията е прекратен. След J-Link
  reset файлът е наблюдаван като 0 B. Повторението с timeout3s, 256-B порции
  и progress завърши: `.py` 284948 B, SHA-256 съвпада.
- Всичките 48 оригинални файла са проверени по SHA-256 като непроменени.
  OFF firmware 1 582 944 B е записан и прочетен обратно; hash остава
  `b36ac6b23ec9598b1bd033ce8269eddc27ba266ae724ba874274d8539c1f78d2`.
  Вътрешният запазен край и действителният SDR1 data-flash запис са непроменени
  от firmware flash. Външният 4-MiB резерв остава изцяло `0xFF`, layout12/4.
- J-Link reset е `AIRCR.SYSRESETREQ`, не MicroPython Ctrl-D.

### Измерено ограничение на редактируемия `.py`

Пълният `sdr_single.py` не се компилира в наличния heap на платката:
`main.py`, import на ред1 → `MemoryError ... allocating136 bytes`.
Това не е липса на flash място или firmware flash грешка. След cleanup са
свободни265920 B; логът от неуспешния boot е запазен.

За работещия TX е компилиран MPY v6.3 на хоста и е качен чрез временен файл,
проверен по hash, след което преименуван. Runtime `/flash/sdr_single.mpy`:
**78708 B**, SHA-256 `00fae19c0a70871e76273cfa8ebeb4bc6b2938761ab17a142d25ef4c7678aa59`.
Обновен е и проектният `sdr_single.mpy`, със старо копие в архива.
Няма frozen приложение. Source остава `/flash/sdr_single.py.source` и нормален
`.py` в repo/проектната папка. Старият runtime е `/flash/sdr_single.pre-ssb.mpy`;
всичките 48 оригинални файлови съдържания са отново проверени като запазени.
`boot.py`/`main.py` не са променяни. Новият boot мина без MemoryError.
За бъдеща директна редакция/компилация на платката остава разделяне на
монолитния source; този въпрос не е решен чрез MPY и не се представя за решен.

### USB/LSB DAC runtime проверки

Реалното приложение е заредено от `sdr_single.mpy`; TX_MODES включва петте
режима. `cv2` липсва при import. Начален RX heap71984 B. Изпълнен е маршрутът
**RX → USB → RX → LSB → RX → USB**, без reset между превключванията.
SSB изборът в теста променя mode полетата/надписа, без RX set_mode recenter
към Si5351; не е проверено TX LO routing, което още липсва.

| Режим | Нови DSP семпли | Интервал us | Измерен среден темп |
|---|---:|---:|---:|
| USB, първо включване | 62037 | 5167286 | 12005.724/s |
| LSB | 62135 | 5177145 | 12001.790/s |
| USB, финално включване | 62229 | 5184696 | 12002.440/s |

И трите: C max4122 / бюджет10000 цикъла (**41.22% за C частта**), последни
наблюдавани C времена около3097–3271 цикъла. `error=0`, deadline misses0,
unexpected callbacks0, dsp_clips0, LUT allocation0 B. Timer30MHz/2500=12kHz.
Броячът и времето са отделни снимки; това доказва работещ среден темп, не
липса на всеки изгубен IRQ. C времето не включва целия IRQ вход/изход.

И двата DAC output-enable са активни според status; I/Q кодовете се изменят.
Пинове: **P001 микрофонен ADC; P014 DAC0/I; P015 DAC1/Q**. Няма подаван от
теста калибриран тон, аналогови напрежения, фазова грешка или RF потискане.
67.85dB остава host numerical резултат от предходния етап, не RF измерване.

Междинните връщания в RX възстановяват backend и DAC playback без TRX грешка.
В началната RX снимка има `audio_underruns=64`, `ring_overruns=0`; причината
не е изследвана в този SSB тест и броячът не се обявява за нула.

В края: **USB TX оставен включен**, бутон `TX`, free heap**71808 B**,
`dsp_samples=87469` в края на основния тест, грешка0. Последваща проверка без
reset: **TX USB**, `dsp_samples=2454577`, outputs_enabled=True, error0,
deadline misses0, unexpected callbacks0, clips0. COM25 е освободен.
Не е задаван автоматичен TX след reset; това е текущото работещо състояние.

Протоколи в архива: `deployment.json`, `source-upload.json`,
`mpy-fallback.json`, `autostart-boot.log` (OOM), `mpy-autostart-boot.log`,
`ssb-dac-live.json`, `final-tx-status.json`. Хост скриптове/логове са в работната Codex папка:
`deploy_ssb_tx_20260907.py`, `finish_ssb_deploy_20260907.py`,
`deploy_ssb_mpy_fallback_20260907.py`, `ssb_tx_dac_live_20260907.py`.
README_IQTX_BG.md и IQTX_HIL_RESULTS_BG.md са обновени с новите доказателства.

## 2026-09-07 — Git checkpoint преди FM корекции; завършен анализ

### Запазено в Git

- Repository: `C:\msys_64\home\teodor\renesas_micropython`.
- Commit: `200b8d43c0a36c7848241be4e0d020d4a2fc5951`.
- Анотиран таг: `vk-ra6m3-ssb-tx-opencv-off-pre-fm-v1`.
- Включени са всички тогавашни **26 modified + 4 untracked = 30 файла**:
  **2110 добавени / 273 премахнати реда**. Работното дърво след commit е чисто.
  Клонът е 31 commit-а напред и 0 назад спрямо upstream към тази проверка.
  **Push не е изпълняван.**
- Обхват: USB/LSB TX и тестове; компактни AM/FM таблици; HOME RX/TX и
  checked cleanup; IQADC stop/raw-consumer интерфейс; `LCD.touch_into`;
  опционални Measurement hooks; OpenCV OFF и независимият QSPI layout12/4;
  съпътстващите примери и документация. Това е checkpoint на натрупаната
  работа, не твърдение, че всички функции са физически проверени.
- Проектният и Git `sdr_single.py` са побитово еднакви, SHA-256:
  `7bc1f21c9c0863e5fadbeada068ee596d5cb4d52c2e240b553c2a978457cf861`.
  `SDR_TRANCEIVER_RA6M3` не е отделно Git хранилище; настоящите проектни
  `ra6m3_done.md` и `ra6_sdr_next_steps.md` не са включени в този commit.

Повторени непосредствено преди commit: действителният C TX core —
**PASS, 11 групи / 6 465 784 проверки**; UI mock/AST тестовете за RX/TX —
**PASS**; staged `git diff --check` — **PASS**. Използван е изричният
`C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe -E`
и MinGW GCC. Нов пълен firmware build, flash, reset, COM/J-Link или аналогов
тест за този checkpoint **не са изпълнявани**.

### Установено за сегашния FM TX — анализ на код, не измерване

Източници в `ports/renesas-ra` на горния commit:
`ra/ra_tx_hw.c:187`, `ra/ra_tx_core.c:94,145`, `machine_tx.c:38` и
`boards/VK_RA6M3/examples/sdr_single.py:2394,2880,2942`.

- Фазата се натрупва непрекъснато: `phase += fm_gain * (ADC - adc_mid)`
  по модул65536. Това е FM, не замяна на фазата с моментната стойност на звука.
  Вход: **P001**; I/Q изходи: **P014/P015**.
- UI създава `IQTX(mode=mode)` с подразбирани настройки: FM rate44000,
  `fm_gain=2`, `adc_mid=2048`, amplitude800. Няма TX настройка за девиация в Hz.
- При конфигуриран timer clock30MHz и период682 изчислената честота е
  `Fs = 30000000/682 = 43988.2698 Hz`. Девиацията е
  `Fs * fm_gain * (ADC - adc_mid) / 65536`.

| Отклонение на ADC от 2048 | Девиация при gain2 |
|---|---:|
| +/-100 кода | +/-134.24 Hz |
| +/-800 кода | +/-1073.93 Hz |
| +/-1500 кода | +/-2013.62 Hz |
| +/-2047 кода | +/-2747.92 Hz |

- Практическият симетричен максимум е **около +/-2.75 kHz**. Дори при
  Fs48000 и същото преобразуване максимумът е около +/-3 kHz; +/-4 и +/-5 kHz
  изискват промяна на преобразуването, не само на семплиращата честота.
- Няма премахване/проследяване на постоянната съставка в проверения FM път.
  Разлика100 ADC кода между реалната среда и2048 дава постоянно отместване
  около134 Hz. Това е пример по формулата, не измерена грешка на P001.
- RX VOL/AF, AGC и FILTER не управляват FM TX. `amplitude` задава I/Q размаха,
  не девиацията. В FM кода липсват звуково филтриране и ограничител на девиацията;
  наличието им във външната аналогова схема не е проверено.
- Изходната таблица има256 фазови позиции, т.е. стъпка1.40625 градуса.
  Натрупването остава16-битово. Спектралните примеси от тази апроксимация
  предстои да се измерят; не се обявяват предварително за чуваем дефект.

### Уточнение към предходния DAC status отчет

`ra_tx_hw_get_status()` (`ra_tx_hw.c:619`) използва
`(DACR & 0xc0) != 0`: `outputs_enabled=True` доказва **поне един**, не непременно
два включени DAC изхода. Инициализацията задава и двата бита, но status флагът
сам по себе си не доказва състоянието на всеки изход. Предходната формулировка
„и двата DAC output-enable са активни според status“ е твърде силна и се
коригира с това уточнение. Аналоговото измерване остава отделна проверка.

### Какво НЕ е завършено

FM корекциите не са реализирани, компилирани или качени. Няма ново доказателство
за FM девиация, аналогови I/Q или RF качество. Последното документирано състояние
е USB TX; то не е проверявано наново и не е променяно при анализа/commit-а.
Задачите FM-1..FM-8 и критериите за приемане са записани най-отгоре в
[ra6_sdr_next_steps.md](ra6_sdr_next_steps.md), отделно от историческия план.

### Документална итерация — 2026-09-07

Обновени са и двата проектни файла. Актуалният FM план е поставен най-отгоре
в next_steps; старият план от 22 август е обозначен като исторически.
По изрична заявка правилото за обновяване на двата файла след всяка итерация
е записано и като постоянна бележка за следващите сесии. Няма промяна по
firmware, приложението или платката в тази документална итерация.

## 2026-09-07 — FM-A: C обработка и хост тестове

Добавен е гласов FM C път паралелно на стария DOC път. API по подразбиране
избира `deviation_hz=2500`, `mic_gain=100` (проценти), amplitude800.
Изричен стар `fm_gain=1/2` без девиация запазва raw/DOC режима за сравнение.

- `ra_tx_core.h/.c`: DC servo30Hz с начално установяване по входа, Butterworth
  low-pass3kHz, микрофонно усилване0..1600%, симетричен пиков ограничител,
  непрекъснат32-битов фазов акумулатор. Девиация100..5000Hz се изчислява от
  действителния timer clock/period. Звуковата лента и девиацията участват във
  валидацията на честотата; това не е доказателство за RF спектрална маска.
- Същият1KiB LUT пази нормализирани I/Q стойности; линейна интерполация на
  фазата и отделна скала за DAC амплитудата. Няма по-голяма таблица или
  Python/heap работа на семпъл. FM състоянието използва съществуващия CW/SSB union.
- `ra_tx_hw.h/.c`: C ISR за гласов FM; старият DTC път остава само за raw FM.
  Измерване на C цикли, клипове, ADC крайни кодове, DC оценка и звуков пик.
  Новото конфигуриране публикува трите контроли между семпли, без restart на
  фазата/филтрите. `i_enabled`/`q_enabled` са отделни; общият флаг изисква двата.
- `machine_tx.c`: `deviation_hz`, `mic_gain`, `fm_configure(...)` и диагностиката.
  Невалидна настройка се отхвърля, без умишлено спиране на здрав предавател.
- `tests/tx/test_tx_fm.inc`, `test_tx_core.c`: действителен C тест, не Python
  модел на модулацията. **PASS15 групи / 7 055 959 проверки**. Включени са
  DC/стъпка,1kHz при три timer честоти и девиации100/2500/4000/5000Hz,
  независимо изходно ниво, произволни порции, различен DC, претоварване,
  ADC крайни кодове, restart,3kHz филтър и10kHz затихване. Старите режими минават.

Тест: native Python310 `-E tests/tx/run_host_tests.py --cc
C:\msys_64\mingw64\bin\gcc.exe`, exit0. UI не е свързан на този етап.
ARM build, ISR време на платката, качване и аналогов/RF тест още не са правени.
Не са използвани COM/J-Link; оставеният TX не е променян.

## 2026-09-07 — FM-B: контекстни HOME/BACKEND настройки и build

### Изискване и реализация

Уточнено от потребителя: **HOME плъзгачите и BACKEND са контекстни**. Пътят
остава SDR -> PLL/VFO routing + CAL -> долният BACKEND. При RX се отваря
досегашният VERIFY; при TX — TX BACKEND за текущата модулация. Не е добавен
отделен навигационен вход. BACK затваря само екрана и не спира TX.

Промени по файлове:

- `sdr_single.py` — FM конструкторът получава `txdev`, `txmic`, `txlevel`.
  HOME показва MIC (x0..x16), DEV (+/-0.1..5.0 kHz), TX LEVEL (0..100%).
  Единственият pin checkbox избира кой плъзгач остава вдясно. RX избраният
  плъзгач и стойностите му се възстановяват след целия RX/TX преход.
- Същият файл — премахната е забраната за отваряне на SDR/ROUTE при стабилен
  TX. BACKEND се означава RX/TX и избира съответния екран при натискане.
  TX FM BACKEND има същите три настройки с +/- и повторение при задържане,
  плюс цифрова диагностика от съществуващия бавен status poll. Няма нов timer
  за семпли или Python DSP. ROUTE се освобождава преди изграждане на BACKEND.
- Същият файл — AM/CW/USB/LSB също показват TX, не RX VERIFY. Конфигурираното
  I/Q ниво е само за четене, защото live setters още има само за FM. В HOME
  това е неактивен TX плъзгач; при липсваща стойност се показва `--`, не измислен
  процент. VFO routing/CAL са неактивни при TX; RF LO управление не е добавено.
- Същият файл — настройка се показва като приложена само след успешния native
  setter. При отказ се запазват старите стойности. `txdev/txmic/txlevel` са
  отделни от RX в съществуващия вътрешен dataflash запис; външният QSPI не се
  пипа. Записът се отлага до RX и обичайното 60-s изчакване. При изключване
  на захранването преди това незаписаната последна промяна се губи.
- Същият файл — HOME показва звуков максимум след start и ADC/TIME/CLIP
  показатели. TX BACKEND показва last/max C натоварване, DC оценка и двата DAC
  enable бита. Отвореният TX екран се освобождава при преминаване към RX/fault
  recovery. MemoryError при построяване изтрива частичния екран и връща HOME.
- `tests/tx/test_sdr_fm_controls.py` — нови хост тестове на действителни Python
  методи и callback-и: HOME настройките, отхвърлена промяна, отложен запис,
  стари flash записи, 512-byte граница, SDR -> ROUTE -> BACKEND RX/TX, BACK без
  спиране, възстановяване на SQL избора, забрана при преход, MemoryError cleanup.
- `tests/tx/test_sdr_tx_switch.py` — актуализиран FM constructor contract и FM
  stall guard; RX/TX и освобождаването се изпитват за всичките пет модулации.
- `tests/tx/iqtx_ui_hil.py` — подготвен за новия C FM: семпли, бюджет, двата
  enable бита и промяна/readback на трите настройки. **Не е изпълняван на платка.**
- `boards/VK_RA6M3/examples/iqtx_probe.py` — разграничава новия гласов FM от
  стария raw/DOC режим и показва правилните параметри за всеки.
- `boards/VK_RA6M3/examples/README_IQTX_BG.md` — API, контекстната навигация,
  скали, запазване, RAM/flash резултати и ясни граници на доказателствата.

### Проверки и аритметика

- Повторен C тест: **PASS 15 групи / 7 055 959 проверки**. Запазени са старите
  CW/AM/raw FM/USB/LSB резултати. FM DC, девиация, непрекъсната фаза, филтър,
  ограничаване и независимо I/Q ниво минават на хоста.
- `test_sdr_tx_switch.py` — PASS. `test_sdr_fm_controls.py` — PASS, включително
  построяване и callback-и с widget заместители. Това не доказва touch/layout
  или липса на трептене на физическия панел.
- `mpy-cross` компилира основния Python файл — PASS; тестов кандидат **83 622 B**:
  `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2\sdr_single_fm_candidate.mpy`.
  Изходният `.py` остава свободно редактируем; не е frozen в firmware.
- Firmware: `build_sdr.ps1 -OpenCV Off -Jobs 16` — PASS, общата
  `build-VK_RA6M3`, без нова build директория. Използван е изричният работещ
  MinGW Python; няма опит със счупения MSYS runtime.
- ELF: text **1 585 029 B**, data **0 B**, bss **649 584 B**. BIN **1 585 016 B**:
  `1 585 016 - 1 582 944 = 2 072 B` допълнително спрямо SSB checkpoint.
  Native TX state: `1524 - 1508 = 16 B` допълнително. LUT остава 1 KiB.
  Heap **290 816 B**, CV2 символи **0**. QSPI:
  `12 582 912 + 4 194 304 = 16 777 216 B` (12 MiB файлове / 4 MiB резерв).
- SHA-256 firmware.bin:
  `9347c6c9d0116da41b1939f7ccdcd199b56f00f6f5c297bebf980054eeff9be2`.
  Linker предупреждението за RWX LOAD segment остава; build не е без warnings.
- Синхронизиран е **основният проектен файл**
  `C:\Users\teodor\Desktop\stem\sdr\SDR_TRANCEIVER_RA6M3\sdr_single.py`
  с Git mirror `ports/renesas-ra/boards/VK_RA6M3/examples/sdr_single.py`.
  Двата SHA-256 са еднакви:
  `8b10de6cc2336cf7d512f6005356c70bb03cfdc9ab04bfe911a427ca659c8e5c`.
  Преди синхронизацията основният файл е проверен срещу HEAD; няма презаписани
  чужди промени. Scoped `git diff --check` — PASS.

Логове в `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2`:
`fm-tx-build-20260907.log`, `fm-tx-core-final-20260907.log`,
`fm-tx-context-ui-20260907.log`, `fm-tx-switch-final-20260907.log`.

### Незавършено и непроверено

**Няма flash, COM/J-Link, reset, цифров FM HIL, аналогов или RF тест в тази
итерация.** Последното записано състояние USB TX не е проверявано наново.
Не е сменен файл във външната flash и не е създаден нов commit/push.
Новите FM функции изискват съгласувано качване на firmware и приложението.
Следва: панелен HOME/BACKEND тест, свободен heap с меню, измерен ISR бюджет,
после 1 kHz вход и действителна девиация. `ra6_sdr_next_steps.md` е обновен.

## 2026-09-07 — FM качване; изпълнението е прекъснато преди runtime тестовете

По искане „качвай и тествай“ е стартиран
`C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2\deploy_fm_tx_20260907.py`.
Използвани са записаните J-Link 1120000058 / COM25. Първата host команда е
отказала със SyntaxError от PowerShell quoting, преди да се свърже с платката;
следващите действия са в самостоятелен Python скрипт, с изричен Python310 `-E`.

- Преди качването serial командата е отчела съществуващ USB TX, error0.
  След това е изпълнено checked връщане към RX, спиране на SDR worker/RX и
  освобождаване на IQTX/DAC ресурсите; лог: `SDR_AND_TX_QUIESCED`.
- Пълен архив: `backups/fm-tx-1120000058-20260907-112542` — 2 MiB вътрешен
  code flash, 16 MiB външен QSPI, 64 KiB dataflash snapshot и manifest на
  50 съществуващи файла. Източниците и точните кандидати са в `candidate`.
- Качени са съгласуваните `/flash/sdr_single.py.source` и `/flash/sdr_single.mpy`.
  Предишните са запазени като `/flash/sdr_single.pre-fm.py.source` и
  `/flash/sdr_single.pre-fm.mpy`. Не е форматирана файловата система.
- Firmware SHA-256:
  `9347c6c9d0116da41b1939f7ccdcd199b56f00f6f5c297bebf980054eeff9be2`.
  Source SHA-256:
  `8b10de6cc2336cf7d512f6005356c70bb03cfdc9ab04bfe911a427ca659c8e5c`.
  MPY SHA-256:
  `a1caaa08aeee717e39fb17572d45a6c81a789c4fd326cbc8de263e742b81873e`.
- `FM_DEPLOY_READBACK_PASS`: проверени са firmware, приложенията, запазването
  на всички стари файлови съдържания, SDR1 dataflash записа, запазения вътрешен
  край и външния 4-MiB резерв. `boot.py`/`main.py` не са променяни.
- Скриптът е изпълнил J-Link reset с `AIRCR.SYSRESETREQ` и е записал банера
  `MicroPython 200b8d43c0-dirty on 2026-09-07; VK-RA6M3 with RA6M3` и REPL.
  Това не доказва, че приложението е стартирало и работи правилно.

След прекъсване на разговора потребителят възрази „нарушаваш правилата“.
Проверени са само локалният `fm-tx-deploy-20260907.log` и host процесите:
deployment вече е завършил и няма останал `deploy_fm_tx_20260907.py` процес.
Няма последваща команда към платката. Не са пускани FM runtime/HIL, UI/touch,
аналогови или RF тестове. Причината за възражението още не е уточнена;
не се приписва предполагаемо нарушение на конкретно потребителско правило.
Работата по платката е спряна до изясняване. `ra6_sdr_next_steps.md` е обновен.

### Уточнение: тестове само в RAM — 2026-09-07

Потребителят уточни възражението: в текущата задача **не се флашват тестови
скриптове**. Тестовете трябва да се изпълняват в RAM/REPL, без запис във
вътрешна/външна flash или boot/main. Не е необходим нов flash/reset за тях.
Посоченият приблизително 300-kB heap е конфигуриран като 290 816 B; свободната
част при работещ UI е отделна величина и предстои да се измери.

Проверени са само локалните `files-before.json`, `files-after.json` и
`deployment.json` от FM архива. Единствените файлови промени са:
`/sdr_single.mpy` 83 622 B, `/sdr_single.py.source` 298 443 B и запазените
стари `/sdr_single.pre-fm.mpy` 78 708 B и `/sdr_single.pre-fm.py.source`
284 948 B. Няма добавен тестов файл. Това е проверка на запазения readback
manifest, не нова команда към платката. Правилото е отразено и в next_steps.

### Първа RAM runtime проверка и уточнение за reset — 2026-09-07

Изпълнен е само RAM/REPL код, без качен тестов файл. Резултат:
`FM_RUNTIME_APP True`, source `sdr_single.mpy`, RX работи, TRX грешка `None`,
избрана USB, FM API `fm_configure` присъства. Defaults:2500Hz/100%/40%.
След `gc.collect()`: free **66 400 B**, allocated **217 696 B**,
`free + allocated = 284 096 B`. Това е runtime GC отчет, отделен от ELF
конфигурацията heap290816 B; разликата6720 B не е изследвана в този тест.

Твърдението „предишният .py е изчерпал RAM“ е уточнено: то се основава на
стария записан `main.py` import `MemoryError ... allocating136 bytes`, не на
нов тест с днешния source. Старият неуспех не доказва, че текущият `.py`
задължително ще откаже. В тази итерация няма нов .py import тест.

Потребителят изрично поиска **J-Link reset след всеки тест**. Това заменя
предишната уговорка без reset за следващите RAM проверки; flash тестови
файлове продължават да са забранени. Reset се изпълнява отделно през
запомнения probe1120000058; не се използва MicroPython Ctrl-D.

### FM/HOME/BACKEND RAM тест — PASS; J-Link reset след него

Контролерът `C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2\fm_ram_hil_20260907.py
--context` изпълнява порции Python само през raw REPL, без soft reset преди
теста и без VFS/flash запис. След края затваря COM25 и задължително прави
J-Link reset през1120000058. Това е изричното ново указание на потребителя.
Предходната RAM проверка на heap също е последвана от отделен J-Link reset.

- Истинското приложение премина RX -> FM TX без TRX грешка. Native voice FM:
  `cpu_dsp=True`, requested44000, actual43988.27Hz, timer30MHz/682, LUT1024B,
  allocation2047B, без DTC descriptor chain за FM (`transfer_count=0`).
- Реални LVGL `send_event` проверки: HOME отваря плъзгачите MIC/DEV/TX;
  освобождаване на DEV при40 прилага4000Hz. Pin checkbox избира DEV и след
  затваряне десният контрол показва DEV. SDR отваря ROUTE; `BACKEND TX >`
  отваря TX екрана и освобождава ROUTE. Извикан е и действителният DEV плюс
  callback:4100Hz. BACK callback затваря екрана, без да спира TX.
- След реалния LVGL layout координатите на стойностите са в480x272:
  MIC `(260,70)..(387,85)`, DEV `(260,112)..(387,127)`, TX `(260,154)..(387,169)`.
  Това доказва разположение на проверените контроли, не физически touch,
  визуален контраст или липса на трептене.
- С отворен TX BACKEND след GC: free**45 104B**. Live текстът показва
  DSP21%/max31%, PK876, DC1304, ADC rail0, CLIP0, missed0, DAC I ON/Q ON.
  PK е натрупан максимум след старта, не калибрирана моментна амплитуда.
- TX LEVEL0/100/40% прилага съответно0/2047/819 DAC амплитуда. При0 са
  проверени действителните DAC кодове `i_code=q_code=2048`. MIC250% и
  DEV5000Hz са прочетени обратно от native status като приложени.
- Нови семпли**518 853** за**11 797 158us**, средно**43 981.2/s** срещу
  timer43988.27Hz. Timestamp и counter са отделни снимки; това е среден темп,
  не доказателство за липса на всеки отделен пропуснат IRQ.
- C last573цикъла, max**853**, бюджет**2728**: `853/2728*100=31.2683%`.
  Измерена е C частта, не целият ISR вход/изход. `error=0`,
  `dsp_deadline_misses=0`, `unexpected_callbacks=0`, `adc_rails=0`, clips0.
  `i_enabled=True`, `q_enabled=True`; I/Q кодовете се изменят.
- TX -> RX е проверено: `RX None`, backend работи, `_tx is None`, предишният
  HOME контрол е възстановен и `RX_DAC_PLAYING True`. Free след GC**57 904B**;
  тестови refs и построеният inline панел още са в RAM, затова тази снимка
  не се сравнява като доказателство за memory leak с чистия boot.
- Тестът завършва с `FM_RAM_CONTEXT_TEST_PASS`, exit0, после
  `JLINK_RESET_AFTER_RAM_TEST` (`AIRCR.SYSRESETREQ`) и boot/REPL банер.
  След reset не е правена нова runtime проверка. Не е оставен умишлен TX.

Логове: `fm-ram-runtime-20260907.log`, `fm-ram-test1-reset-20260907.log`,
`fm-ram-context-test-reset-20260907.log` в горната host директория.
Тестовите параметри са само RAM промени; save timer е паузиран и reset е
преди отложения запис. Не са добавяни тестове към flash, boot или main.
Няма калибриран звуков генератор, аналогово измерване на девиация или RF тест.
Платката не е префлашвана между проверките. `ra6_sdr_next_steps.md` е обновен.

### 2026-09-07 — точен баланс на heap след въпроса „къде са 230 kB“

Само локален анализ на запазени логове, ELF map и GC source; няма нов тест,
COM/J-Link достъп, reset или flash в тази итерация.

- Конфигуриран heap: **290 816 B**, не точно 300 000 B. Потвърден от FM
  `candidate/profile-switch-audit.json` и текущия `firmware.map`:
  `_heap_start=0x1fff4af0`, `_heap_end=0x2003baf0`.
- Записаната HOME снимка след GC в `fm-ram-runtime-20260907.log:5`:
  **217 696 B заети + 66 400 B свободни = 284 096 B GC pool**.
- Разликата **6 720 B** вече е обяснена по `py/gc.c:139-166`, а не само
  наречена „служебна“: allocation table **4 439 B**, finaliser table **2 220 B**,
  разделител **1 B** и остатък от целочисленото оразмеряване **60 B**.
  При GC block 16 B: `floor((290816-1)*8/524)=4439`, pool `4439*4*16=284096`.
- Пълен баланс: **217 696 + 6 720 + 66 400 = 290 816 B**. Несвободни са
  **224 416 B**. Разликата до условните „300 kB“ е още **9 184 B**, които не
  са част от конфигурирания heap, не са runtime загуба.

Текущите **217 696 B** нямат нова поетапна разбивка по собственици. Общият
отчет включва заделените обекти на работещата среда и диагностиката; не
доказва нито теч, нито задържан предишен тест. Старите startup JSON от
2026-09-06 измерват друг MPY/профил и не трябва да се представят като
актуален отчет. В тях се виждат baseline 57 136 B, import +68 128 B, файлови
буфери +16 592 B, display init +5 840 B, HOME +34 144 B. В early-native
варианта attach освобождава 7 488 B, след което клавиатурата добавя 26 032 B.
Тези разходи не са адитивна точна разбивка на днешните 217 696 B.

Следваща диагностична задача: актуален поетапен отчет от чист boot до HOME
и RX/TX, със същия source/runtime и минимална отчетена диагностика. Без
тестови файлове във flash; J-Link reset след всеки тест.

### 2026-09-07 — директен framebuffer срещу разход за UI обекти

Проверено само по текущ source и linker map; без достъп до платката,
без нов build, flash или runtime доказателство за използвания render path.

- `boards/VK_RA6M3/machine_lcd.c:2776-2786`: LVGL получава
  `g_display0_cfg.input[0].p_base` като единствен буфер и NULL като втори,
  в `LV_DISPLAY_RENDER_MODE_DIRECT`. Flush callback (:851) само завършва
  flush; не копира втори екран към GLCDC.
- `modules/pRGB.py:62-78`: `memoryview(self.display)`, `buf2=None`;
  Python fallback също използва DIRECT. Заявено doublebuffer се отказва.
- `ra_gen/hal_data.c:422,531`: GLCDC ползва `fb_background[1]`.
  `build-VK_RA6M3/firmware.map:43612` поставя този един RGB565 framebuffer
  в `.fb0` на **0x20040000**, размер **0x3fc00 = 261 120 B**.
  Heap е отделно: **0x1fff4af0..0x2003baf0**, **290 816 B** (:44580).
  Framebuffer не е част от отчетените 217 696 B заети GC обекти.
- Спектърът/waterfall/осцилоскопът имат C пътища за директен запис в същия
  `p_base` (например machine_lcd.c:1428,1820,1999). Това не означава,
  че всички останали елементи на приложението заобикалят LVGL.
- `sdr_single.py:510,524,533` още създава `lv.obj`, `lv.label`, `lv.button`;
  менюта, styles, текст и callbacks остават обекти в RAM.
  `lib/lv_bindings/lib/lv_conf.h:96` избира `LV_STDLIB_MPY`;
  `ext_mod/lvgl/mem_core.c:63-69` насочва LVGL алокациите към MicroPython GC.
- DIRECT не забранява временни pixel layers: `lv_conf.h:161-168` описва
  случаите, а `lvgl/src/draw/lv_draw.c:478-501` има път за създаването им.
  **Не е измерено** дали/колко такива слоеве се заделят от сегашния HOME.
  Настройката 24 KiB е целеви размер на chunk, не доказан постоянен разход.

Извод: единственият основен display framebuffer е директен по кода. Не е
реализиран изцяло UI без LVGL обектно дърво, нито е гарантирано „нула временни
графични буфери“. Тези три различни изисквания не бива да се обявяват за едно
изпълнено изискване. Не приписваме всички 217 696 B на UI без измерване.

### 2026-09-07 — план за UI алтернативи с външен flash

Създаден `ui_memory_alternatives_plan.md`; актуализиран `ra6_sdr_next_steps.md`.
Сравнени са олекотен LVGL, общи custom панели, малък C renderer с Python
контролер, frozen/XIP и отделна възможност за по-малък физически framebuffer.
Препоръката е C renderer с ресурси във flash и редактируем Python, проверен
първо върху един панел. Не се замразява SDR приложението и не се връща OpenCV.

Фиксирани са граници: един framebuffer; ограничен row/tile scratch; явна
собственост на графичните области; без запис на waterfall във flash; отделен
отчет за Python, статична C RAM и временни layers. Цели 8 KiB state / 4 KiB
scratch за новия модул са проектни бюджети, не измерени резултати. Разделянето
12 MiB filesystem / 4 MiB reserve се запазва; суров QSPI asset pack е втори,
незадължителен етап с отделна интеграция, не готова функция на текущия build.

Проверени са местният linker script/build wrapper, UI lifecycle/file buffers
и официалните MicroPython/LVGL документи. Аритметика: RGB565 екран 261 120 B,
десет фона 2 611 200 B flash, ред 960 B, плочка 32×16 — 1 024 B. Няма обещана
икономия на heap преди ново контролирано измерване. Няма кодови промени,
build, COM/J-Link достъп, reset, flash, тест на платката или commit в тази итерация.

### 2026-09-07 — корекция на UI плана: само LVGL, без нова библиотека

Потребителят отхвърли собствен renderer. Предишната препоръка за C GUI модул
е оттеглена. `ui_memory_alternatives_plan.md` е заменен с актуален LVGL-only
план; `ra6_sdr_next_steps.md` също е коригиран. Запазваме widgets, touch,
drag/repeat, events и рисуването в LVGL. Целта е ресурси във flash, по-малко
декоративни обекти, споделени styles и ограничен брой живи страници.

Намерено конкретно в текущия LVGL source: `LV_BIN_DECODER_RAM_LOAD=0`
(`lib/lv_bindings/lib/lv_conf.h:809`), RGB565 file path през get_area_cb
(`lv_bin_decoder.c:238-250`) и едноредов буфер (:401-460). Това е стандартен
LVGL механизъм; не е нужно да пишем нов image decoder. 960 B е payload на
480-пикселен RGB565 ред, не общият RAM разход на decoder/file/cache.

Остава да се интегрира/потвърди LVGL достъпът до /flash: в SDR не е намерено
регистриране на filesystem driver. Наличният binding `fs_driver.py` прави
read и struct.pack алокации; не е zero-alloc. Cache и Dave2D/software draw
съвместимостта се измерват в прототипа. Няма нов build, промяна на firmware,
COM/J-Link достъп, reset, flash или тест на платката. Променени са само плановите бележки.

### 2026-09-07 — AM/SSB синтетичен одит и разлика между RX/TX DAC

Само source/запазени логове и хост C тестове; няма COM/J-Link, reset,
firmware build/flash или старт на реален DAC в тази итерация. Production кодът
не е променян. Потребителят изиска синтетичен вход вместо микрофон.

- Повторен `tests/tx/run_host_tests.py`: PASS15 групи/7055959 проверки,
  native Python310 `-E`, MinGW gcc. USB/LSB:56 тона300..3000Hz,
  worst digital image rejection67.85dB; това не е RF измерване.
- Допълнителен host-only `audit_tx_synthetic_20260907.c` в
  `C:/Users/teodor/Documents/Codex/2026-08-22/new-chat-2`, резултат в
  едноименния `.log`. Компилира реалния `ra_tx_core.c`, без промяна на алгоритъма.
  AM1/2kHz при0/10/50/100% PASS. При50% I2448..3248, носеща800кода над
  i_zero2048, дълбочина50.0118%/50.0269%. При100% I2048..3648, без DAC rail.
  Q2048 е проверено по DTC source, не измерено на платката.
- USB/LSB1/2kHz: Q-I фаза-90/+90градуса, fitted Q/I1.000000/1.000302,
  clips0. Различни моментни пикове при2kHz произлизат от фазите на семплиране;
  fitted амплитудите са почти равни. DFT над140dB при1kHz е числена симетрия,
  не обещание за аналогово/RF потискане.
- Готовност: C математиката минава. Хардуерният IQTX още чете P001
  (`ra_tx_hw.c:131,186,425-446`); няма native SYNTH input API, а RX TESTER/FILE
  се блокира от `sdr_single.py:2302-2307`. Не се приема за свързан TX генератор.
- Локалният предходен clock patch има `sdr_single.py:2030 mult=1`. Оттегля се
  универсалното твърдение „TX винаги x1“: множителят зависи от външния модулатор.
  Класически QSE /4 иска4*RF, QSE2 /2 иска2*RF; източник
  https://kf5n.com/t41-2/qse2dcez/ . Patch-ът не е качван в тази итерация.
- AM/SSB UI още създава `IQTX(mode=mode)` с native amplitude800;
  MIC/LEVEL live API и UI са само за FM (`sdr_single.py:2437-2442,3019-3079`).
- **RX DAC**: `sdr_single.py:1246,1327-1334` прави DAC(P014).stream_from(iq).
  `ra_dac.c:909-924` -> start(:871) -> output_amp_init(:146): DAAMP0=1,
  DAOE0=1; DAASW0 е временно1 при старта, след4us се връща0. Right-aligned
  12-bit кодове, DAADST=0. Нулевото аудио е2048; потокът е DMAC ping-pong
  (`machine_dac.c:406-487`), честота/порция от IQADC/2
  (`ra_iq_adc.c:4138-4144`), т.е. при48000/128 ->24000/64.
- **TX DAC**: `ra_tx_hw.c:403-415` изрично задава DAAMPCR=0 и P014/P015 analog.
  I/Q се записват последователно (:145-146); общ аналогов latch не е доказан.
  Следователно твърдението „без вътрешни буфери“ се отнася за IQTX,
  **не за RX аудио DAC**. Външните товар/DC/филтри и RF още се съгласуват.

Двете app копия съвпадат: SHA256
`964c4cadcea61cffbcc5b0db38e19c73515329ec2efc9f592e0b7d43101b69af`.
Провереният `ra_tx_core.c`: SHA256
`57feceafe8ca11a97cc78025a7176d7af92c6e6c9f56b64e136ba21dc3de9dd0`.
Обновен е `ra6_sdr_next_steps.md`. Няма Git commit или запис на тест във flash.

### 2026-09-07 — честота на CPU обслужването на RX DAC

Проверени `ra_dac.c:663-705`, `machine_dac.c:105-117`,
`ra_iq_adc.c:4138-4165`. DMAC пренася отделните семпли; C callback при край
на порцията превъоръжава следващия буфер и пълни освободения от audio ring.
При SCOPE се извиква и gated scope_push; няма Python callback на семпъл.
За номинални24000семпъла/s и64семпъла:375 DMA-completion callbacks/s,
интервал2.6667ms. 128/48000=64/24000, т.е. деленето на rate И block по2
не увеличава честотата на порциите. Това не е измерване на CPU процента:
той зависи от продължителността на callback и останалия DSP/IRQ товар.
По-големи буфери намаляват фиксирания IRQ overhead, но не броя копирани
семпли или демодулационната работа и добавят буфериране/закъснение.
Само source анализ; без нов тест на платката, reset или flash.

### 2026-09-07 — RX DAC: порции от 64 на 512 семпъла

Изрично изискване: DAC аудиото да остане на номинални 24 kS/s, но да се
подава на порции по 512. Променен е транспортът към DAC, не размерът на
ADC/DSP блока и не математиката на демодулаторите. Python приложението
не е редактирано в тази итерация.

Промени по файлове в `ports/renesas-ra`:

- `ra/ra_iq_adc.h`: отделна константа `RA_IQ_AUDIO_DMA_SAMPLES=512` и C API
  `ra_iq_adc_audio_chunk_ready`. `get_audio_params` запазва досегашния размер
  на обработения блок за диагностичното `read_audio`; ADC максимумът остава 256.
- `machine_dac.c`: двата ping-pong буфера за всеки DAC вече са по 512 семпъла.
  Честотата идва от IQADC/2, но DMA размерът е независим. При недостатъчно
  данни се подава цяла неутрална порция 2048 и частичната опашка се пази.
  SCOPE получава същите кодове. При повторно `stream_from` старият DMA се
  спира ПРЕДИ началното запълване на статичните буфери.
- `ra/ra_iq_adc.c`: двата ring буфера са увеличени от 512 на 2048 позиции
  (2047 използваеми). Има запас за задържана частична порция, следващия DMA
  период и един производителски блок, включително когато размерът му не дели
  512. Двата I/Q callback-а ползват едно решение за готовност независимо от
  реда им; нов ADC блок между тях не позволява само единият да консумира.
  Решението се нулира при старт, смяна на източник и scope routing. Mono
  не чака липсващ Q. Неутралните DAC0 порции се броят в audio_underruns.
- `boards/VK_RA6M3/ra_cfg/fsp_cfg/bsp/bsp_cfg.h`: OpenCV OFF/LVGL heap
  `0x47000 → 0x44c00`, т.е. `290 816 → 281 600 B`. Това е изрично отделен
  резерв от 9216 B за новите аудио масиви, не изчезнала/изтекла памет.
  Стекът остава 16 384 B; единственият RGB565 framebuffer остава 261 120 B.
- `tests/rx/test_dac_chunks.py`: компилира действителните C функции за
  ring/pull/readiness/refill/scope с хост IRQ stubs. Проверява mono, I/Q,
  реда на каналите, ADC публикация между callback-и, wrap, handoff, Q stop,
  границите на DMA масивите и запазената 512-точкова SCOPE порция.

Аритметика на масивите:

| Резерв | Преди, B | Сега, B | Разлика, B |
| --- | ---: | ---: | ---: |
| DAC ping-pong, 2 канала × 2 половини | 1024 | 4096 | +3072 |
| Audio/I + Q ring | 2048 | 8192 | +6144 |
| Общо масиви | 3072 | 12288 | +9216 |
| Резервиран heap | 290816 | 281600 | −9216 |

Първият build с ring=1024 и непроменен heap спря на linker проверката:
`region RAM overflowed by 3824 bytes`, `RAM sections overlap framebuffer`.
Нищо не е записано на платката. Последващият вариант е с достатъчен ring
запас и явното преразпределение по-горе; финалният build мина с exit 0.

Проверки до момента:

- Native Python 3.10 с `-E`, MinGW GCC, без MSYS Python.
- RX C тест: PASS, 127 532 407 assertions, 99 200 моделирани DMA периода,
  обработени блокове от 5 до 128 семпъла. Това е детерминиран модел,
  не измерване на реалните IRQ приоритети, честота или аналогов изход.
- TX регресии: PASS, 15 групи / 7 055 959 проверки на реалния AM/SSB/FM C core.
- Номинално `24000/512 = 46,875` DAC completions/s на канал; период
  `512/24000 = 21,333 ms`. Фиксираната честота на обслужване намалява 8 пъти;
  общият CPU товар НЕ е измерен и не се твърди, че намалява 8 пъти.
- Двата начални неутрални DMA буфера траят общо 42,667 ms. Ако първото
  попълване още няма 512 готови семпъла, полезният изход се отлага с още
  една порция. Това не е обещание за обща end-to-end латентност.

Логове: `C:/Users/teodor/Documents/Codex/2026-08-22/new-chat-2/`:
`rx-dac512-host-20260907.log`, `rx-dac512-tx-regression-20260907.log`,
`rx-dac512-build-20260907.log`, `rx-dac512-build-final-20260907.log`.
Обновен е `ra6_sdr_next_steps.md`. Без COM/J-Link, reset, flash или commit.

Финална проверка на RX DAC 512 build:

- Същата обща папка `build-VK_RA6M3`; `BOARD=VK_RA6M3`,
  `MICROPY_PY_CV2_QSPI=0`, `-j16`, изричен native Python 3.10 с `-E`.
  Билдът е върху текущия общ worktree, включително предходните TX промени,
  не върху изолиран commit. Запазени са известните LVGL unused-symbol и
  linker RWX предупреждения; няма C/linker грешки във финалния build.
- `firmware.bin`: 1 585 312 B, SHA-256
  `33a32b54b942adea2e18a274e715496f51434c3db9153f9f11380c15cdd0bd48`.
- `firmware.map`: двата DMA масива са по `0x800` B; audio/I и Q ring —
  по `0x1000` B. Новият C readiness helper е задържан в ELF на `0x00099acc`.
  Търсене в defined ELF symbols за `mp_module_cv2`, `cv2_`, `opencv`: 0 попадения.
- Основна RAM аритметика: **93 936 B преди heap + 281 600 B heap +
  16 384 B stack + 1296 B guard = 393 216 B**. Heap е
  `0x1fff6ef0..0x2003baef`, stack завършва на `0x2003faf0` exclusive;
  framebuffer започва на `0x20040000` и е `0x3fc00` B. Linker проверката
  за неприпокриване е изпълнена успешно. Двата нови еднобайтови флага
  се побират в досегашното подравняване; крайният guard не е намален.
- Scoped `git diff --check`: PASS. Двете Python копия не са променени и
  съвпадат със SHA-256 `964c4cadcea61cffbcc5b0db38e19c73515329ec2efc9f592e0b7d43101b69af`.
- **Не е качено.** Няма доказателство за реален DMA период/натоварване или
  аналогов сигнал с новите 512 семпъла. Преди deployment остава и описаното
  по-горе съгласуване на предходния Python TX clock patch с външния модулатор;
  този patch не бива да се качва неявно заедно с аудио промяната.

### 2026-09-07 — RX DAC 512: качване и проверка на платката

По изричното „КАЧИ ГО И ГО ПРОВЕРИ“ е записан само готовият вътрешен
firmware през запомнения J-Link **1120000058 / COM25**. Другата платка не
е използвана. Няма нов build, промяна на Python приложението, QSPI запис,
запис на тестови файлове във flash или Git commit в тази итерация.

**Архив и запис.** Пълен архив преди операцията:
`backups/rx-dac512-1120000058-20260907-153648/` в тази проектна директория:
2 MiB вътрешен flash, 64 KiB data flash, всички 16 MiB външен QSPI,
manifest на 52 файла и копия на candidate BIN/ELF/MAP.
Старият firmware е проверен по SHA преди запис; новият е прочетен обратно
и съвпада с 1 585 312 B / SHA-256
`33a32b54b942adea2e18a274e715496f51434c3db9153f9f11380c15cdd0bd48`.
Запазени са вътрешната файлова опашка и всичките 16 MiB QSPI, байт по байт.
Следователно остават предишните `sdr_single.mpy`, `.py.source`, boot/main
и IQ записи. Локалният незавършен Python TX clock patch НЕ е качен.

Първият verifier спря след успешния запис/readback заради 11 338 различни
байта при сравнение на всички 64 KiB data flash. Те са извън запазения
304-байтов SDR запис (6 B заглавка + 298 B payload), който съвпада точно.
Повторната проверка беше само четене, без втори flash. На платката
`dataflash.is_blank(304, 65536-304)` върна **True**. Това е празна област,
чиито обикновени read стойности не са надежден критерий за запазване;
не е загуба на настройки. Външният QSPI flash е отделна памет и съвпада целият.

**Метод.** Първите дълги REPL команди при работещ RX пристигаха с липсващи
букви: SyntaxError, `_r5q` вместо `_r5iq`, `_rpart` вместо `_r5part`.
Промяна на pacing не даде надежден резултат. Успешните тестове първо спряха
RX и паузираха Python worker/save timer, после заредиха кода само в RAM,
провериха всеки откъс чрез echo и целия код чрез SHA-256 и го изпълниха
като една поредица. При самото изпълнение RX и HOME бяха възстановени.
Причината за загубата на REPL байтове не е доказана и няма A/B със стария build.

**Измерени резултати — цифрови, не измерване на физическия пин:**

| Проверка | Резултат |
| --- | --- |
| Mono DAC0 DMA | 46,875 презареждания/s, период 21,333 ms; брояч до 508 при дискретно четене на програмирана порция 512 |
| Двоен I/Q DAC0 / DAC1 | 46,875 Hz и на двата канала; източниците попадат в новите 512-семплови ping-pong масиви |
| Реален темп, от броя ADC блокове | Около 24 008 обработени семпъла/s; кратко измерване с гранична неопределеност от един блок |
| Mono HOME, 3 × 5 s | DAC playing=True, be.err=None, ring_overruns=0; render_count 183 → 213 |
| I/Q и обратно mono | Двата DAC работят; Q носи отделния квадратурен сигнал; не е проверена фазата на физическите пинове |
| GEN → ADC → GEN | Потокът продължава; след преходното установяване няма увеличение на аудио underrun/overrun |
| R:AM IN + LOOP на HOME | `/flash/iqbank/zam48.sdriq`, 48 kS/s, 96 000 I/Q семпъла = 2 s; над 10 повторения |
| FILE напредък | samples_consumed 48 768 → 1 013 760 за четири изчаквания по 5 s; error=None, active=1, scheduler_failures=0 |
| FILE → ADC | DAC продължава; след установяване няма нови аудио underrun/ring overrun |

За точното броене на DMA презарежданията LVGL timer-ите бяха спрени само
през всеки 1,5-секунден register poll; ADC/DSP/DAC IRQ продължават.
Първият poll с активен UI отчете само 33,66 Hz, защото Python не вижда
всички reload-и между графични callback-и. Той НЕ е валидно измерване на
хардуерната честота. Отделните HOME интервали са с включено рисуване.
R:AM poll даде 46,891 Hz в границите на дискретното измерване.

Не се твърди „нула underrun от старта“. В GEN теста установеният начален
audio_underruns беше 1024 семпъла; при всяка последваща смяна на източник
или mono/IQ се добавяше по една неутрална порция от 512, до общо 3072.
Между преходите броячът оставаше постоянен. Във FILE теста audio_underruns
остана 512, а отделният FILE underruns беше 3 още при началната снимка и
не се увеличи през 20-секундния HOME интервал. Началните паузи остават
за отделно изследване. Raw `status.last_error=-1` е неполученият Python
raw-block mailbox; не е аудио ring overrun. `unit1_stalls=0` през проверките.

В DADR са наблюдавани диапазони 1985…2113 за тестовата AM настройка,
1755…2341 / 1749…2347 за I/Q и 1664…2470 при R:AM. Това доказва цифрово
подаване към DAC, не напрежение, форма, товар или качество на физическия изход.
Свободният heap след GC в основния тест е 49 728 B, със зареден тестов код;
не е чист HOME baseline и не е измерване на теч. CPU товарът/IRQ продължителността
не са измерени; 8 пъти по-малко callbacks не означава 8 пъти по-малък DSP товар.

Логове в `C:/Users/teodor/Documents/Codex/2026-08-22/new-chat-2/`:
`rx-dac512-deploy-20260907.log`, `rx-dac512-readback-20260907.log`,
`rx512-transport-probe.log`, `rx512-one-session-hil-v2.log`, `rx512-file-hil.log`.
Успешни маркери: `FIRMWARE_QSPI_SDR_RECORD_READBACK_PASS`,
`RX_DAC512_RAM_HIL_PASS`, `RX_DAC512_FILE_LOOP_HIL_PASS`.
След всеки приключил или неуспешен тест е изпълнен J-Link reset
`AIRCR.SYSRESETREQ`; тестовете не променят boot/main. Обновен е и
`ra6_sdr_next_steps.md` с оставащите аналогова проверка, дълъг тест,
начални преходи, CPU/IRQ измерване и REPL проблема.

Финално нормално стартиране след reset: `HOME_RX RX True None`,
`TESTER_OFF True`. Приложението е оставено на RX HOME с работещ DAC,
без активен тестер; COM25 е освободен. Лог: `rx512-final-home.log`.

### 2026-09-07 — TX TIME: уточнен източник и проверено текущо свързване

Изискване: в TX осцилоскопът да показва реалния модулиращ аудио сигнал.
При MIC той идва от микрофона; при R:AM/R:USB/R:LSB или друг R:xx файл
файлът заменя микрофона изцяло. Няма смесване. Общата точка за TIME е
след избора MIC/FILE и преди TX модулацията, а не старият RX DAC буфер.

Прочетените текущи локални файлове показват, че това още не е реализирано:

- `sdr_single.py`, `_tx_guard_error()` около ред 2296: активен `_inj_on`,
  чакащ TESTER или свързан файлов reader връща `turn TESTER/FILE off before TX`.
  Следователно R:xx не заменя микрофона в наличния TX път.
- `ra/ra_tx_hw.c`, `tx_adc_callback()` около ред 134: C USB/LSB/voice-FM
  чете `R_ADC0->ADDR[1]`. `tx_build_chain()` около ред 189 ползва същия ADC
  регистър в автономния AM/raw-FM DTC път. `ra_tx_hw_init()` около ред 427
  конфигурира P001. В прочетения TX код няма файлов source selector.
- `boards/VK_RA6M3/machine_lcd.c`, `lcd_native_capture_apply()` около ред 380
  и `lcd_lv_spectrum_timer_cb()` около редове 2113/2181 свързват TIME само с
  `ra_iq_adc_scope_enable/frame`. Не е намерен TX monitor producer към LCD.
- `_IqFileSource` и `_IQ_FILE_REAL_PRESETS` в `sdr_single.py` описват R:xx
  като SDRIQ S16LE I/Q с 48/24-kS/s варианти. За замяна на микрофона първо
  трябва да се получи аудио чрез демодулация според записа и съгласуване на
  семплирането. Директно I→MIC или Q→MIC не изпълнява това изискване.

Записан е планът в началото на `ra6_sdr_next_steps.md`: общ MIC/FILE audio
вход, TIME tap от същите консумирани семпли, ясен надпис на източника,
контролирано освобождаване на RX/FILE преди TX, без тайно връщане към MIC
при EOF/грешка, запазен директен framebuffer и предварителен RAM отчет.
Режимът на записа за декодиране не се смесва с избраната TX модулация.

Тази итерация е анализ и фиксиране на изискването. Редактирани са само
done/next; няма C/Python промяна, build, flash, COM/J-Link операция или нов
хардуерен тест. Не се твърди, че TX FILE или TX осцилоскопът вече работят.

### 2026-09-07 — анализ на замръзналия TX осцилоскоп, без реализация

След уточнението „анализирай и коментирай“ работата остава read-only по
приложението/фърмуера; обновени са само done/next. Дефектът на MIC TIME
е отделен от бъдещото TX FILE и е поставен преди него в next.

Проследена е конкретната верига: `_service_to_tx()` спира RX чрез
`be.stop_rx()` (`sdr_single.py:2419`). RX DAC refill е производителят на
monitor семпли (`machine_dac.c:115,127` -> `ra_iq_adc_scope_push`).
`ra_iq_adc_scope_frame()` (`ra_iq_adc.c:4579`) връща false без нов/разрешен
RX кадър. `machine_lcd.c:2113,2181` няма TX алтернатива и не подава нови
координати за осцилограмата. TX DSP/хардуерният DTC път не публикуват monitor
кадър. Това обяснява запазената последна RX следа; само по нея не може да се
заключи, че TX обработката, DAC или целият UI са спрели. Не е доказателство
за проблем от промяната на RX DAC порциите на 512.

Разлика между режимите: USB/LSB/voice-FM вече получават действителния вход
в `tx_adc_callback()` (`ra_tx_hw.c:121–159`). AM/raw-FM ползват автономна
DTC/DOC верига (`tx_build_chain():178`), а не същия per-sample C callback.
За тях обикновен `scope_push()` в CPU callback не е достатъчен. Предложен е
пасивен хардуерен capture за отделна проектна/хардуерна проверка; не е
твърдение, че вече е конфигуриран. Не се предлага втори ADC или бавен
Python register poll, който би изкривил времевата картина.

Общият monitor трябва да сменя RX_DAC/TX_INPUT собственик и поколение,
да изчиства стария кадър и да публикува стабилни завършени поредици.
Потенциал за RAM reuse: `s_scope_audio[2][512]` е 2048 B (`ra_iq_adc.c:990`);
безопасното споделяне още не е реализирано/измерено. Времевият мащаб също
трябва да се подава от източника: `lcd_scope_prepare()` използва 128
последователни samples (`machine_lcd.c:231,1527`), а TX defaults са
12 kS/s SSB и 44 kS/s за останалите режими (`machine_tx.c:55`).
Съществуващият фиксиран вертикален делител 2048 (`machine_lcd.c:234,1533`)
не бива да се заменя с автоматично нормализиране на всеки кадър.

Няма C/Python редакция, build, flash, reset, достъп до платката или нов HIL.

Последно уточнение на потребителя: при започване на предаване осцилоскопът
трябва да показва **AF**, от микрофонния вход или от файла според избора.
Наблюдава се модулиращото аудио преди TX модулацията, не изходните I/Q.
При FILE микрофонът е заменен изцяло. Уточнението е внесено и в next;
не е отчетено като вече реализирана функция.

### 2026-09-07 — реализация на TX AF MIC/FILE; хост проверки, без качване

Изпълнено е уточненото изискване и за FILE, не само за MIC. При избран
R:AM/R:USB/R:LSB запис той се декодира до аудио и заменя микрофона изцяло.
Осцилоскопът получава същите AF стойности, които консумира TX модулаторът,
с надпис `AF R:AM`, `AF R:USB` или `AF R:LSB`; при MIC — `AF MIC`.
Суровите комплексни I/Q от записа не се представят като микрофонно аудио.
Това е реализирано в локалния код и проверено на хоста, НЕ на платката.

Промени по файлове в `C:/msys_64/home/teodor/renesas_micropython/ports/renesas-ra/`:

- `ra/ra_tx_core.h`: FILE source flag и CPU-path избор за FILE AM/FM/SSB.
- `ra/ra_tx_core.c`: проверка на FILE midpoint 2048 и AF rate: 24 kS/s за
  AM/voice-FM, 12 kS/s за USB/LSB. FILE към keyed CW или raw-FM е отказан.
  Съществуващите MIC режими не се подменят с FILE обработка.
- `ra/ra_tx_hw.h`: AF capture API и status полета за AF кадри/грешка/състояние,
  FILE source и FILE underrun; върнатият кадър носи и sample rate.
- `ra/ra_tx_hw.c`: пасивен MIC DMAC snapshot от действителния TX ADC0 ADDR[1],
  с borrowed RX scope buffers, stable completed half и bounded checked stop.
  Без per-sample Python и без промяна на MIC AM/raw-FM DOC/DTC модулатора.
  FILE използва AGT IRQ, не стартира MIC ADC и не отваря неговите DTC/DOC
  ресурси. Един AF sample се подава към scope и след това към модулатора.
  При празна опашка се подава 2048 (тишина), не MIC. Start изисква prefill.
  Собствеността и generation се сменят при нов TX; cleanup остава проверен.
- `ra/ra_iq_adc.h`: API за временно заемане на scope workspace и за
  foreground FILE demodulation/audio consumer без RX ADC собственост.
- `ra/ra_iq_adc.c`: преизползва съществуващия декодер, DSP histories и audio
  ring. FILE service обработва най-много 24 блока на извикване и цели 1536
  queued AF samples; спира при липса на READY данни/EOF. За USB/LSB изход
  VOICE филтърът предхожда 24→12-kS/s адаптера. Capture workspace е същият
  `s_scope_audio[2][512]`, не второ копие.
- `machine_tx.c`: `file_mode` (демодулация на записа), `file_tune`, съвместим
  borrowed-buffer FILE API и GC roots за двата съществуващи файлови буфера.
  FILE service не прави файлов I/O или Python callback в sample IRQ.
  Промяна/освобождаване на файловите буфери при работещ TX е отказана.
- `boards/VK_RA6M3/machine_lcd.c`: TX AF consumer вместо чакане на RX DAC
  кадър; изчиства старата следа/спектър при RX↔TX или нов TX източник.
  Директното рисуване и фиксирaният вертикален мащаб се запазват. Времевата
  ос се съгласува с подадения rate. OFF/modal спира capture/draw, не TX.
  FILE scope не се нулира повторно при всеки LCD timer tick.
- `boards/VK_RA6M3/examples/sdr_single.py` и каноничният
  `C:/Users/teodor/Desktop/stem/sdr/SDR_TRANCEIVER_RA6M3/sdr_single.py`:
  FILE→TX handoff, checked RX teardown, отваряне на същия запис и prefill
  преди TX start; независим 20-ms feeder работи и извън HOME/при OFF.
  ONCE изчаква AF опашката и заявява RX cleanup; грешка не включва MIC.
  TX надпис `AF <източник>`, TX AF/OFF цикъл; RX TIME/I-Q/OFF е запазен.
  Двете Python копия са идентични: SHA256
  `499366ab0f2f8596da3d95854090fe0597c51f1da4ecba5e95c0e9d7c825ab17`.
- `tests/tx/test_tx_af.py`: нов host harness върху извлечени действителни C
  функции за scope, FILE queue/callback, DMAC lifecycle и rate-aware renderer.
- `tests/tx/test_sdr_tx_switch.py`: FILE R:AM/USB/LSB, source label,
  prefill/teardown ред, hidden/OFF feeder и EOF drain чрез реални Python
  методи с mock hardware/LVGL.
- `tests/tx/test_tx_core.c`: допълнителна група FILE source validation.

Начин на използване след бъдещото качване: в RX TESTER се избира FILE
R:AM/R:USB/R:LSB и се включва; след това се избира TX. Записът започва
отначало, със същите файл, точка на подаване, sample rate и LOOP настройка.
Режимът на записа определя декодирането, а режимът на TX — изходната
модулация. Файлът не се смесва с MIC. В TX се гледа аудиото преди модулация;
OFF или отваряне на меню не спира файловото захранване. CW output няма
модулиращ AF и показва `AF --`; FILE към този output засега е отказан.

Реално изпълнени проверки на компютъра:

- TX core: PASS 16 групи / 7 055 977 проверки, включително съществуващите
  AM LUT, SSB и voice-FM математически тестове и новите FILE ограничения.
  Лог: `tx-af-core-tests-20260907.log` в работната Codex директория.
- TX AF actual-C harness: PASS 1465 проверки — MIC DMA ready/in-flight,
  bounded timeout/retry, FILE exact AF values към scope/modulator при 24/12k,
  ring wrap, underrun тишина, OFF и фиксирани времеви/вертикални мащаби.
- SDR TX switch + FILE: PASS; FM UI/persistence/BACKEND regression: PASS.
- RX DAC512 actual-C regression: PASS 127 532 407 assertions,
  99 200 моделирани DMA периода. Това не са hardware DMA измервания.
- AST syntax на двете Python копия и `git diff --check` за редактирания
  обхват: PASS. Не е изпълнен Python import на платката.

Build и памет — точна граница на доказателството:

- Използвана е само общата `build-VK_RA6M3`, OpenCV OFF, `-j16`, изричен
  Python310 `-E`; няма нова build директория или намаление на heap настройката.
- Успешен по-ранен link: `tx-af-file-build-final-20260907.log`, text 1 590 125,
  bss 649 768 B. Спрямо предишния RX512 bss 649 584 това е +184 B общо;
  не са добавени нови 2048 B sample масив или framebuffer. Това е сравнение
  на тези два build snapshot-а, не измерване на живия Python heap.
- След последните защитни проверки incremental link
  `tx-af-file-build-verified-20260907.log` НЕ мина: references към Measurement/
  IQGenerator/ra_iq_adc_raw_owned. Наблюдаван е друг make в същата папка с
  `USER_C_MODULES=.../sdr_lab` и различни флагове, включващи тези модули.
  Това изисква съгласуван rebuild при свободна обща папка. Чуждата работа
  не е спирана/изтривана; последният firmware image не е утвърден за качване.
  Първоначални смесени-object link откази също не се представят като реален
  нов RAM разход от AF функцията.

Няма flash, QSPI запис, reset, COM/J-Link достъп, commit или промяна на
самостартирането в тази итерация. Не са измерени действителни R:xx decode,
UI кадри, FILE refill margin под GC, целият IRQ бюджет, MIC/DAC напрежение
или RF качество. Преди deployment трябва да се отдели и предходната,
непроверена Python TX-clock `mult=1` промяна; не се качва мълчаливо с AF.
Оставащите build/deployment/HIL/аналогови критерии са в `ra6_sdr_next_steps.md`.

### 2026-09-07 — TX AF качен; R:AM/USB/LSB проверени цифрово на платката

Изпълнено е „качвай“. Използвани са запомнените J-Link `1120000058` и
`COM25`, без избор/употреба на втория probe. RadioOnly е компилиран отново
в общата `build-VK_RA6M3`, без паралелен make: OpenCV OFF, без LAB
Measurement/IQGenerator, `-j16`, Python310 `-E`. Логът е
`tx-af-radio-deploy-build-20260907.log` в
`C:/Users/teodor/Documents/Codex/2026-08-22/new-chat-2`.
Последващата поправка на binding-а е компилирана успешно; лог
`tx-af-bytearray-build.log`. Последният size е text 1590285, data 0,
bss 649768 B; heap reservation 281600 B, stack 16384 B, guard 1112 B.
Не е ново измерване на свободния heap при зареден UI.

Допълнителна промяна в `ports/renesas-ra/machine_tx.c`: FILE attach преди
приемаше само typecode `'B'`, а действителният MicroPython bytearray връща
`BYTEARRAY_TYPECODE` (1). Добавени са `py/binary.h` и проверка за bytearray,
unsigned/signed byte buffers; запазени са native размер/подравняване/
припокриване и ownership проверки. Поправеният firmware е качен и
прочетен обратно преди успешния тест с реалните файлови bytearray буфери.

Качен е AF-only Python артефакт, който запазва Si5351 поведението на
предходното инсталирано приложение. Непроверената локална TX-clock
промяна `mult=1` НЕ е качена с AF. Каноничният `sdr_single.py` и Git mirror
не са презаписвани с deployment варианта; тяхното различие е умишлено
и трябва да се отчете преди следващото качване. Host harness върху
deployment варианта потвърди FILE handoff/надписи/OFF/EOF и запазените
clock методи. Скрипт: `prepare_tx_af_deploy.py` в същата работна папка.

Точният архив на качването е:
`C:/Users/teodor/Desktop/stem/sdr/SDR_TRANCEIVER_RA6M3/backups/tx-af-1120000058-20260907-184101`.

- `candidate/firmware.bin`: 1590272 B, SHA256
  `a440c435a65fd6d76c1806ce6bebcead9119fba0823ba012055a10807d218ed6`.
- `candidate/sdr_single.py`: 302596 B, SHA256
  `59aa1c35757e3b7d96e8d97f453da2b864672aab312e164da04281a3f00bdd9f`.
- `candidate/sdr_single.mpy`: 84710 B, SHA256
  `e8a10f24ed05a5ca400206f27648c36a61f19457d90789f2db81ad84f76b4b09`.
- На платката: `/flash/sdr_single.mpy` и `/flash/sdr_single.py.source`;
  старите версии са запазени като `sdr_single.pre-txaf.*`.

Преди записа са архивирани вътрешният flash, dataflash и целият външен
QSPI 16 MiB. Readback потвърди вътрешния образ, съдържанието на всички
52 предходни файла (старото приложение е под backup имената), непроменени
записи R:xx, boot/main и резервираните последни 4 MiB QSPI. Няма формат,
промяна на 12/4 MiB организацията или тестови файлове на платката.
`TX_AF_DEPLOY_READBACK_PASS` е в `tx-af-deploy-finish.log`.
След binding поправката е обновен само вътрешният firmware, с точен
readback: `TX_AF_BYTEARRAY_FIRMWARE_READBACK_PASS` в
`tx-af-bytearray-deploy.log`; този втори запис не пише QSPI.

Dataflash е отделен от QSPI: валидният 304-byte SDR settings record е
непроменен. Първоначалната проверка на целите 64 KiB беше неправилна,
защото raw четене на изтритата част не е доказателство за нейното съдържание.
Тя е заменена с сравнение на валидния record и реално изпълнена
`dataflash.is_blank(304, dataflash.size()-304)` — PASS в RAM-only HIL.
Не е наблюдавана загуба на настройките; отказът беше в проверката.

Транспортните неуспехи са отчетени: първите пет опита прекъснаха преди
flash запис, първият при липсващ app handle, следващите при загубени
символи/неверен checksum на RAM кода. Raw-paste и паузиране на LVGL не
решиха надеждно прехвърлянето под работещото приложение. Използван е
еднократен SAFE_MODE чрез J-Link breakpoint на `boardctrl_run_boot_py`,
адрес от точно инсталирания ELF, проверен firmware hash и промяна само
на RAM полето `reset_mode` от 1 на 2. Това пропуска boot/main за този boot;
не ги редактира и не изключва постоянно самостартирането. Диагностиката е
подадена с echo/checksum само в RAM. След теста е изпълнен нормален
J-Link reset (`AIRCR.SYSRESETREQ`); портът е затворен.

Реален тест: `tx_af_ram_hil.py`, лог `tx-af-file-ram-hil.log` в работната
папка. Използвани са съществуващите `zam48`, `zusb48`, `zlsb48` от
`/flash/iqbank`, IN, LOOP и tune 0. Настройките на теста не са записвани.

| Запис | TX AF rate | AF кадри в status snapshot | Scope samples min/max | FILE underruns в snapshot |
| --- | ---: | ---: | ---: | ---: |
| R:AM | 24000 | 94 | -464 / 454 | 471 |
| R:USB | 12000 | 87 | -1273 / 1188 | 0 |
| R:LSB | 12000 | 81 | -1113 / 1111 | 406 |

За всеки запис: RX→TX е успешен, `file_source/running=True`, `error=0`,
`af_error=0`, надписът е точно `AF R:*`; AF кадрите и DSP samples нарастват.
След първоначално изчакване 900 ms няма нови FILE underruns през следващите
измерени 3 s. При OFF DSP samples продължават, AF frames спират; при ON
пак нарастват. Проверен е обратният TX→RX с освободен FILE/TX собственик.
Краен маркер: `TX_AF_FILE_RAM_HIL_PASS`.

Ограничения: 471 AM и 406 LSB underruns са натрупани преди steady-state
сравнението и НЕ се обявяват за решени. USB/LSB имат по 16 DSP clips в
snapshot; причината/моментът не са определени. Нулевият DSP deadline
брояч не обхваща целия FILE IRQ. Няма MIC тон, дълъг LOOP/GC stress,
ONCE/EOF на платката, физически touch/визуален приемателен тест, измерени
DAC напрежения или RF качество. Това е цифрово доказателство за AF FILE
пътя и управлението на наблюдението, не пълна аналогова TX валидация.
Няма commit/push в тази итерация. Следващите проверки са в next.

### 2026-09-07 — приоритет Si5351 преди FILE дефектите

Потребителят зададе ред Si5351 TX clock, после т. 1/2/3/5 от последния
статус; MIC тестът (т. 4) остава отложен. Редът е внесен в next.
Сравнени са каноничният Python, AF-only deployment артефактът и последният
host harness. Локалният код има RX teardown → TX clock → IQTX start и
TX release → RX clock restore → RX start. Тези промени не присъстват в
качения AF-only Python; безусловният TX множител 1 изисква потвърждение
спрямо текущия модулатор. Предходното указание е същият CLK и делене на
предходния RX ×4 до TX ×1, не автоматично предположение за всички QSE.

Повторно изпълнен `ports/renesas-ra/tests/tx/test_sdr_tx_switch.py` с
Python310 `-E`: PASS за clocks, rollback, ownership и FILE AF. Това са
host mocks/AST, не регистрово или физическо измерване на Si5351. Към този
запис няма ново качване/COM/J-Link действие или промяна в firmware кода.

### 2026-09-07 — потвърден TX ×1: Python качване и Si5351 readback

Потребителят потвърди същия CLK и TX ×1. Качена е каноничната clock промяна
в `/flash/sdr_single.mpy` и `.py.source`, без C firmware rebuild/запис.
Кодът спира RX/DAC, задава избраната RF честота на същия CLK с ×1, после
стартира IQTX. При връщане възстановява RX LO с route множителя преди ADC.
TX не ползва RX low-IF или NCO. Коментарът изрично ограничава ×1 до
потвърдената връзка, не го представя като универсален QSE множител.

Архив: `backups/tx-clock-1120000058-20260907-202710` в проекта.
Source SHA256 `9b2cbb3393138fd894386b51f5764dd0803424dcf95a787e64674944bafc4a2b`;
MPY SHA256 `446f50b193e9b37856e6c6de62d72a0602e17c1b08a10eb6ec0ebfe41eb48e05`.
Firmware остава `a440c435a65fd6d76c1806ce6bebcead9119fba0823ba012055a10807d218ed6`.
Readback PASS: всичките 54 предходни файла са съхранени (предходното app
под `sdr_single.pre-txclock.*`), boot/main, dataflash record и QSPI резервът
са непроменени. Лог: `tx-clock-app-deploy.log` в работната Codex папка.

Реален регистров RAM тест след качването: CLK1 control=15, output enabled,
PLLA и MultiSynth байтове съвпадат с очаквания план и записаната CAL:

| Режим | RX програмирана честота | TX програмирана честота | Връщане в RX |
| --- | ---: | ---: | --- |
| AM | 14012000 Hz | 3500000 Hz | PASS |
| USB | 14000000 Hz | 3500000 Hz | PASS |
| LSB | 14000000 Hz | 3500000 Hz | PASS |
| FM | 14024000 Hz | 3500000 Hz | PASS |

Тестът е `tx_clock_file_ram_hil.py`, резултат
`TX_CLOCK_FILE_RAM_HIL_PASS` в `tx-clock-file-ram-hil-no-tx-print.log`.
Всички режими използват наличните R:xx записи, не микрофон. След теста —
J-Link reset, COM25 затворен. Няма тест във flash, промяна на настройки,
физическо честотомерно/аналогово/RF доказателство или commit.

Първият диагностичен run спря преди LSB на TESTER guard; не е заобиколен.
Повторният run изчаква до 3 s за готовност и мина. Печатът е преместен след
връщане в RX: дългите status редове по UART в първия run се смесваха с
измерването на FILE захранването. Без печат при TX няма нови underruns в
измерените 1.5-s устойчиви прозорци. USB/LSB clips все пак нарастват с по 8,
FM с 1: не са само начален преход и остават отделна задача.

Началната диагностика (включва overhead от status snapshots): първото
допълване идва при 2228 AM samples (~92.8 ms), 1410 USB (~117.5 ms) и
1429 LSB (~119.1 ms). Prefill е 1536/24000=64 ms аудио. Недостигът е
съответно 692/642/661 samples преди първия service, а не спиране на ADC.
Update UI частта е ~13–20 ms; останалото забавяне след нея се изследва.
Подготвен е локален вариант: TX контролите и стандартен LVGL refresh се
правят преди `tx.start()`, докато бутонът още показва WAIT; без нов buffer.
Host FILE handoff/order тестът мина. Вариантът още НЕ е качен в този запис;
първо се проверява чрез RAM patch, отделно от вече каченото TX ×1.

### 2026-09-07 — FILE TX старт: RAM поправка без допълнителен буфер

`_prepare_tx_file_frame()` подготвя TX контролите с надпис WAIT и извиква
стандартния `lv.refr_now(None)`, докато IQTX още е quiescent. След `tx.start()`
се сменя само WAIT → TX и се допълва AF; второто пълно `update_rx()` отпада
само за FILE. MIC пътят и провереният rollback са запазени. Каноничният
Python и Git огледалото са еднакви; host `test_sdr_tx_switch.py` е PASS.

Първите големи RAM harness варианти спряха преди helper-а с allocation
16383 B. Това е AM LUT: 8192 B таблица + 8191 B alignment allowance в
`machine_tx.c`, не доказан draw-layer проблем. Тестовото дублиране на
Python методите увеличава RAM разхода. Минималният RAM harness стартира
при 54–56 KB свободни и позволи сравнение. Само предварителният refresh
остави AM/USB/LSB underruns 112/282/329; този вариант НЕ е приет за поправен.

Крайният RAM вариант, без повторното обновяване на контролите, даде:

| FILE | Underruns при първо/второ четене | DSP clips при първо/второ четене |
| --- | ---: | ---: |
| R:AM | 0 / 0 | 0 / 0 |
| R:USB | 0 / 0 | 8 / 16 |
| R:LSB | 0 / 0 | 8 / 16 |
| R:FM | 0 / 0 | 1 / 2 |

Лог `tx-file-minimal-finalize-hil.log`, `MINIMAL_FILE_RAM_HIL_PASS`.
Четенията са след 1.4 s и още 1.5 s; печатът е след TX release.
След всеки тест е изпълнен J-Link reset. Тестови файлове във flash няма.
Това доказва краткия RAM вариант, не още продължителния качен вариант.
Ограничителите при SSB/FM се изследват отделно; не са скрити с reset на брояч.

Качен Python вариант: `backups/tx-file-start-1120000058-20260907-205209`.
Source SHA256 `e1f38cdb7edf7b12a02b9e5633ee38718b970fa1a2afd3e4908fe871ee624dd6`,
MPY SHA256 `7acaf3ee111bd5d8fc0165891305934bd8933c67c1e37231c730eff6dffac66e`.
`TX_FILE_START_APP_READBACK_PASS` в `tx-file-start-app-deploy.log`:
56 предходни файла са съхранени, предходният app е под `pre-txfilestart`;
firmware/dataflash record/boot/main/резервните QSPI 4 MiB са непроменени.

### 2026-09-07 — R:USB/R:LSB: установена причина и момент на ограничаването

От качения C FILE декодер са извлечени по 72000 AF семпъла на 24 kS/s
(3 s) от двата реални записа. IQTX остава quiescent, running=False,
dsp_samples=0. DAC каналите са включени на средните кодове 2048/2048;
quiescent НЕ означава outputs_enabled=False. Първият capture harness
имаше тази грешна проверка и спря; поправената проверка мина. Диагностиката
е единственият потребител на AF ring и придвижва tail в RAM при спрян
таймер; не е непрекъснато измерване на работещ DAC. След теста — J-Link reset.

`tx_ssb_decode_capture.py` / `tx-ssb-decode-capture-midpoint.log`:
QUIESCENT_AF_CAPTURE_PASS. Последователностите се възпроизведоха на host
през НЕПРОМЕНЕН `ra_tx_core_ssb_sample`, със същото 24→12 kS/s усредняване.
`tx_ssb_replay_capture.py` / `tx-ssb-capture-replay.log`: PASS.

- При двата файла AF след усредняване достига кодове 0..4087; четири
  семпъла са на долната цифрова граница още преди TX SSB филтъра.
- Пикът преди общия I/Q ограничител е 2367 при праг 2048. Той сработва
  16 пъти за 3 s: по 8 около 268–631 ms и 2268–2631 ms. Това е особеност
  на повторения 2-s запис, НЕ рестарт на FIR и не само начален преход.
- Изходните кодове са 1248..2848 при amplitude=800. `dsp_clips` тук
  брои цифровия общ ограничител ПРЕДИ DAC мащабирането; не доказва
  аналогово насищане на пин.
- Host проба с AF gain 75% преди TX FIR: 0 ограничения, пик 1774,
  DAC 1389..2741. Това още не поправя вече ограничените AF семпли от
  декодера. Правилната следваща проверка е headroom преди неговия limiter.
  Нито limiter-ът е изключен, нито ново усилване е качено като поправка.

Продължителният първи menu run потвърди 0 стартови/HOME underruns при
R:AM, но 5672 след три ROUTE→BACKEND→HOME обиколки, при 7 FILE цикъла.
Не е приет за PASS. Следващият режим спря на проверка за AF надпис.
Директното REPL викане на UI метод може да бъде прекъснато от LVGL worker;
следващият harness подава UI действията чрез LVGL callback като реалния touch.
Разглежда се обслужване между стъпките на построяване на менюто. Отделен
Python soft timer сам не решава това: `mp_sched_run_pending()` държи
scheduler locked по време на текущия scheduled LVGL callback.

Допълнителна headroom проверка: в quiescent RAM сесия съществуващото
`s_vol_q15` е зададено на 24576 (75%) ПРЕДИ първото декодиране. Няма
добавен gain след вече ограничен AF. `tx-ssb-headroom-capture.log` и
`tx-ssb-headroom-replay.log`: и USB, и LSB дават AF 405..3584, 0 крайни
семпъла, пред-TX-limiter пик 1783, 0 ограничения за 3 s и изчислени
DAC кодове 1389..2744. Това е потвърдена корекция на нивото за тези
записи, но НЕ е записана като firmware промяна. Reset възстановява
каченото поведение с unity FILE ниво. MIC/AM/FM нивата не са променени.

Кооперативният RAM menu probe не е приет: дори при допълване по време
на построяването остава недостиг (AM 3085, USB 5466, LSB 4702 за трите
обиколки). Не е качен втори Python workaround и не е добавен soft timer.
FM случаят в този harness отказа с `FM FILE NCO 0 != -6000`: самият
тест презаписваше управляваното от FM offset с нула. Следващият тест
запазва правилния FILE FM NCO; предишният run не доказва FM аудио качество.

### 2026-09-07 — финален checkpoint: качен FILE старт, проверени граници

Двете Python копия и каченият source са еднакви, SHA256
`e1f38cdb7edf7b12a02b9e5633ee38718b970fa1a2afd3e4908fe871ee624dd6`.
След този Python deployment няма друг запис на приложение/firmware.
RAM font/checkpoint и volume пробите са премахнати чрез reset, не чрез
качване на нова версия. C кодът не е редактиран в тази clock/start итерация.

`tx-file-am-ssb-home-final-hil.log`, действия през LVGL callback:

| FILE | LOOP обиколки | Обработени TX семпли | Старт/HOME/финални underruns |
| --- | ---: | ---: | ---: |
| R:AM | 7 | 340430 | 0 / 0 / 0 |
| R:USB | 7 | 172562 | 0 / 0 / 0 |
| R:LSB | 7 | 171590 | 0 / 0 / 0 |

Във всеки случай осцилоскопът е изключен и включен три пъти. OFF запазва
растящ DSP sample counter и спира claimed AF frames; ON ги възстановява.
След всеки режим RX е възстановен и FILE owner е освободен. Това са
цифрови HOME проверки, без физически touch/пинове/RF. SSB limiter counts
остават ненулеви, защото 75% headroom още не е внедрен.

Пълният run НЕ е PASS: последвалото повторно AM включване за ONCE отказа
с allocation 16383 B. Това не е загубен файл; отказът е в AM LUT allocation.
Самостоятелен свеж `--once-only` run в `tx-file-once-fresh-hil.log` мина:
ONCE_EOF_RX_PASS None. TX възпроизвежда AM файла, EOF връща RX и
освобождава owner-а без грешка. PASS се отнася само за този свеж ONCE случай,
не за повторните стартове след целия натоварващ набор.

`tx-file-menu-render-profile.log` потвърди LVGL render до 73432 us при
връщане HOME (други измерени пълни HOME redraw: 58190/58834 us). Prefill
1536/24000 е 64000 us; един такъв render сам надвишава този резерв.
Първият AM menu run и RAM checkpoints не покриват условието без underruns.
Не е въведена непроверена промяна в scheduler, framebuffer или audio ring.

FM в разширения run остана непокрит: след запазване на -6000 Hz пак не
се освободи TESTER guard в зададените 5 s. Старият кратък регистров clock
PASS остава валиден само за clock програмирането; не затваря FILE FM lifecycle.

Последни host проверки: `test_sdr_tx_switch.py` PASS; actual C core —
16 групи / 7055977 проверки PASS (`tx-core-regression-after-clock.log`).
`git diff --check` за двата редактирани tracked файла е PASS. Общият Git
diff включва и предходните итерации; не се представя като нов diff само
от този turn. Няма commit/push. След последния RAM тест — J-Link normal
reset, COM25 затворен. Чуждите промени не са включвани в нов firmware build.

### 2026-09-08 — FILE AM/USB/LSB/FM и Si5351: свежи цифрови тестове

Обхватът е променен по искане на потребителя: без UI оптимизация и без
промяна на аудиобуферите; кратко отклонение при смяна на екран е допустимо.
Текущият фокус е установеният TX I/Q тракт, включително синтезатора и
отместванията. Няма build/flash, запис на тестове на платката, промяна на
boot/main, файловите записи или постоянните параметри.

Нов host harness `tx_file_clock_stage_20260908.py` в
`C:\Users\teodor\Documents\Codex\2026-08-22\new-chat-2` използва съществуващите
RAM transport, еднократен SAFE_MODE и LVGL event dispatch. Параметрите
3.500 MHz/режим/FILE LOOP се задават само в RAM; `save_params` е изключен
само за теста. Всеки режим започва след отделен reset. След всеки тест е
изпълнен J-Link normal reset, портът е затворен. Цел: 1120000058 / COM25.

| Файл | LOOP | TX семпли за интервала | Нови AF кадри | File underruns старт/край | DSP clips старт/край |
| --- | ---: | ---: | ---: | ---: | ---: |
| R:AM | 10 | 480237 | 500 | 0 / 0 | 0 / 0 |
| R:USB | 10 | 240051 | 463 | 0 / 0 | 8 / 88 |
| R:LSB | 10 | 240094 | 465 | 0 / 0 | 8 / 88 |
| R:FM | 10 | 480085 | 500 | 0 / 0 | 0 / 0 |

Интервалът между snapshots включва 20-s изчакване; броят семпли не е
независимо измерване на тактовата честота. AM/FM са конфигурирани на
24 kS/s, USB/LSB на 12 kS/s. Във всички крайни snapshots:
`running/file_source/outputs_enabled/i_enabled/q_enabled=True`,
`error/fsp_error/dsp_deadline_misses=0`. Надписът е `AF R:...`, AF кадрите
напредват. USB/LSB имат по 80 допълнителни ограничения в установен режим;
тяхното качество НЕ е PASS. Не е прилагана предходната RAM 75% корекция.

CLK1 е прочетен през I2C, с проверка на MultiSynth/PLLA регистрите,
control=15 и разрешен изход; номинални планове след текущата ppm корекция:

| Режим | RX CLK1 | TX CLK1 | Възстановен RX CLK1 |
| --- | ---: | ---: | ---: |
| AM | 14.012 MHz | 3.500 MHz | 14.012 MHz |
| USB/LSB | 14.000 MHz | 3.500 MHz | 14.000 MHz |
| FM | 14.024 MHz | 3.500 MHz | 14.024 MHz |

Приемният LO е запазен през TX. RX low-IF не е наследен от TX носещата.
Файловият decoder NCO е 0 за AM/USB/LSB и -6000 Hz за R:FM IN.
FM работи от свеж старт без заобикаляне на guard-а; това не затваря
предходния отказ при многорежимния тест в една сесия. FM статусът показва
зададена девиация 2500 Hz, не измерена аналогова девиация.

Протоколи в същата host папка:
`tx-file-clock-stage-am-20260908.log`, `tx-file-clock-stage-usb-20260908.log`,
`tx-file-clock-stage-lsb-20260908.log`, `tx-file-clock-stage-fm-20260908.log`.
Маркерът `FILE_CLOCK_STAGE_DIGITAL_PASS` се отнася до старта, FILE/AF
напредването, липсата на underruns, clock регистрите и връщането RX;
НЕ се отнася до SSB ограничаване, аналогова форма или RF качество.

Отделно е изпълнен съществуващият host набор върху текущия `ra_tx_core.c`:
16 групи / 7055977 проверки PASS. Включва AM LUT за всички ADC кодове,
56 USB/LSB тона 300..3000 Hz (най-лош образ 67.85 dB под желания в този
цифров тест), непрекъснатост при произволни граници на порции и FM 1-kHz
тонове с девиации 100/2500/4000/5000 Hz. Протокол:
`tx-core-stage-host-20260908.log`. Това е C алгоритъм на компютъра, не
аналогово измерване на P014/P015 или външен модулатор.

Синтетичният RX TESTER GEN все още се блокира от `_tx_guard_error` при
TX; не е премахвана защитата. Затова текущият HIL е с наличните R: файлове,
а синтетичните проверки в тази итерация са host-only. Генераторен вход в
приложението и физически CLK/IQ/RF измервания остават за следващ етап.

Двете локални копия на `sdr_single.py` са непроменени, SHA256
`e1f38cdb7edf7b12a02b9e5633ee38718b970fa1a2afd3e4908fe871ee624dd6`.
Не е правен commit/push. Обновени са този отчет и `ra6_sdr_next_steps.md`.

### 2026-09-08 — TX HOME: реално започната промяна, локална проверка

След уточнението „не виждам напредък“ е редактиран `sdr_single.py`, а не
само планът. Обхват: първият HOME етап. Няма достъп до платката, build,
flash, reset, запис на тестове в QSPI или RF/аналогово измерване.

Промени в приложението:

- Общ `_start_tx_owner()` за първи RX→TX старт и TX→TX reconfigure.
  HOME callback само заявява работа. Worker проверява освобождаването на
  стария owner, програмира същия Si5351 CLK на TX ×1, създава новия native
  режим и проверява старта. При отказ се връща към checked RX cleanup,
  без едновременни ADC/DAC собственици и без FILE→MIC fallback.
- AM/USB/LSB/FM могат да се изберат през съществуващия долен HOME ред.
  Честотната клавиатура и << / >>, - / + вече подават TX честота; RX NCO
  не се използва за това. Това е stop/start преконфигуриране, не плавен RF sweep.
- RX mode/frequency/step се запазват преди TX и се възстановяват при изход.
  TX промените не презаписват трите RX VFO записа. Няма нов запис на flash в TX.
- Заглавие `SDR TRANSMITTER`, централен бутон mode / FIX / step.
  VFO индикаторът се скрива в същия header, за да има място за заглавието;
  routing/BACKEND остава на него. RX RF скалата се скрива при TX.
- AGC pill в TX е селектор MIC / наличните R:AM/R:USB/R:LSB/R:FM.
  FILE decoder mode е независим от RF TX modulation. Пренесен файл запазва
  rate/point/NCO; нов избор на реален файл е IN 48 kS/s, FM NCO -6000 Hz,
  останалите 0. При reconfigure файлът започва отначало, избраният LOOP остава.
- FILE се обслужва и докато новата заявка чака worker; prefill и тежкото
  FILE HOME прерисуване са преди native start. Няма нов per-sample Python код.
- Scope остава AF MIC или AF R:...; TX BACKEND вече не описва FILE като MIC.
  `FIX` отваря само информация за фиксиран филтър. AM/SSB sliders не са
  фиктивно отключени; live gain API остава FM-only до следващ етап.
- RX TESTER надписът `TST` е възстановен. Старият source-contract първо
  спря на липсващия надпис; след поправката мина без промяна на самия contract.
- HOME показва началото на `_trx_error` в ограниченото S-meter поле;
  дългият текст е наличен в самото поле на състоянието, но може да е отрязан
  на екрана. Не се твърди, че целият exception е визуално четим.

Нови/обновени host тестове в `ports/renesas-ra/tests/tx`:

| Файл | Промяна и резултат |
| --- | --- |
| `test_sdr_tx_home.py` | Нов: actual Python methods и реалният nested mode callback; HOME навигация, независимо FILE/RF, LOOP/prefill/AF, RX restore, пет вида отказ — PASS |
| `test_sdr_tx_switch.py` | Добавени новите worker методи към AST harness, RX snapshot и TST регресия; предходният ownership/clock/rollback набор — PASS |
| `test_sdr_fm_controls.py` | Проверява активния MIC source pill вместо забранения AGC; FM gain/persistence/BACKEND — PASS |

Допълнително изпълнени на host: actual `ra_tx_core.c` — 16 групи,
7 055 977 проверки PASS; TX AF — 1465 проверки PASS; RX chunk/paired refill —
127 532 407 assertions, 99 200 моделирани DMA периода PASS.
`sdr_ui_contracts.py`: 8 теста PASS; navigation и tester source contracts PASS.
Регистри, ADC/DAC timing и analog/RF не се симулират като физическо доказателство.

Ограничения: няма native TX GEN, няма настройваем AM/SSB gain/filter,
няма нов TX FFT; повторен AM allocation и SSB FILE clips не са затворени.
Няма нов framebuffer или render оптимизация. Физически layout/touch,
повторни mode/source смени и RF качество остават в next_steps.

Git обхват: включват се и натрупаните преди тази итерация native промени
в DAC/IQ capture, TX voice-FM/FILE/AF, RX 512-sample portions и feature-gated
LAB hooks. Те НЕ са написани наново в този HOME етап. Host проверките по-горе
не представляват пълен firmware build на целия combined diff.
Каноничните дневници са извън Git repository; техни точни snapshots се
добавят в `boards/VK_RA6M3/examples/project-status/`, без нов независим дневник.
Commit-ът се идентифицира с анотирания tag `vk-ra6m3-tx-home-context-file-source-v1`;
не се прави push или качване на платката в тази итерация.

Финалният повторен host run е PASS за всичките девет извикани набора;
пълният изход е в `tests/tx/results/tx-home-20260908.log` във firmware Git.
`git diff --check` е PASS. Каноничният и tracked `sdr_single.py` са идентични:
SHA256 `77a174d221e5872179138c705bcea50c562987ab694eea26e3732d6e919658a6`.
