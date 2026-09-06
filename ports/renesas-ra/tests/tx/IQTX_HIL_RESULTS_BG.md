# IQTX: исторически цифрови bench тестове на RA6M3 (pre-compact LUT)

Дата: 2026-09-06. Този файл пази измерванията от стария 64-KiB LUT firmware.
Самостоятелният CW/AM/FM цифров път премина описаните проверки, а AM/FM с тогавашния
SDR UI не можаха да се инициализират поради установен недостиг на heap. Стойностите
по-долу са исторически и не са HIL доказателство за текущия compact LUT код.

## USB/LSB — нов цифров HIL, 2026-09-07

SSB OFF firmware е записан с readback проверка на J-Link 1120000058 / COM25.
Изпълнено е App RX→USB→RX→LSB→RX→USB, без reset между режимите. В края USB TX
е оставен активен по изрична заявка; бутонът показва TX, двата DAC са включени.

| Режим | Нови семпли | Интервал us | Семпли/s | Max C цикли / бюджет |
|---|---:|---:|---:|---:|
| USB 1 | 62037 | 5167286 | 12005.724 | 4122 / 10000 |
| LSB | 62135 | 5177145 | 12001.790 | 4122 / 10000 |
| USB 2 | 62229 | 5184696 | 12002.440 | 4122 / 10000 |

И трите: `error=0`, `dsp_deadline_misses=0`, `unexpected_callbacks=0`,
`dsp_clips=0`, `lut_allocation_bytes=0`. DAC кодовете се изменят; микрофонният
вход е P001, I=P014, Q=P015. И двете връщания в RX възстановяват работещ
backend и DAC playback без TRX грешка. В началната RX снимка има 64 audio
underruns; причината им не е установена тук. В края са свободни 71808 B.

Това са цифрови status/counter наблюдения, не измерена аналогова форма или RF
странична лента. Времевите снимки не са атомарни; C цикловият брояч не включва
целия IRQ вход/изход. PA/PTT не са управлявани и няма калибриран входен тон.

Точният firmware hash е `b36ac6b23ec9598b1bd033ce8269eddc27ba266ae724ba874274d8539c1f78d2`.
След OOM при компилация на целия `.py` работи обновен external MPY v6.3,
78708 B, hash `00fae19c0a70871e76273cfa8ebeb4bc6b2938761ab17a142d25ef4c7678aa59`;
source и старият MPY са запазени. Подробности: `README_IQTX_BG.md`, протокол:
проектната `backups/ssb-tx-1120000058-20260907-001143/ssb-dac-live.json`.

## USB/LSB — предходен host/build етап, 2026-09-06

Добавен е C-DSP USB/LSB път: 12 kS/s, непрекъснат 255-коефициентен комплексен
филтър и избор през HOME RX/TX. Действителният C core премина **11 групи /
6 465 784 проверки**; цифровото потискане на нежеланата странична лента е поне
**67.85 dB** за тестовите тонове 300..3000 Hz. Общият build с OpenCV OFF мина.
Това не е измерване на платката или RF резултат. В този етап firmware още не е
качен; старите HIL резултати по-долу не доказват новия път. Актуалните ресурси, API,
ограничения и hash са в `boards/VK_RA6M3/examples/README_IQTX_BG.md`.

## Предходен compact LUT етап — исторически host/build статус

| Режим | LUT | Подравняване | Максимална алокация | DTC дескриптори |
|---|---:|---:|---:|---:|
| CW | 0 B | — | 0 B | без LUT банка |
| AM | 8192 B | 8192 B | 16383 B | 7 |
| FM gain 1 | 1024 B | 1024 B | 2047 B | 14 |
| FM gain 2 | 1024 B | 1024 B | 2047 B | 15 |

Host C тестът на този етап е **PASS: 7 групи, 5 609 421 проверки**. Общият
VK_RA6M3 firmware compile/link също е успешен. Това не е flash, board runtime или
аналогово доказателство; старите резултати по-долу не трябва да се пренасят към
compact варианта без повторен HIL.

## Историческа среда и безопасност

- Target: COM25, J-Link CDC `001120000058`; UID
  `5434032d35373948384edaf926275454`.
- `os.uname()`: RA6M3 / MicroPython 1.28.0-preview /
  `5abae577cb-dirty on 2026-09-06`. Не е доказана побитова идентичност с локален ELF.
- P001 ADC; P014/P015 DAC I/Q. Операторът потвърди изключена/разкачена RF PA.
- Само RAM изпълнение през serial. Първите два suite-а използват raw-REPL soft
  reset без стартиране на `main.py`; третият запазва съществуващия SDR UI.
  Няма firmware flash, качени файлове или промяна на `boot.py`/`main.py`.
- Работата е в общия port и съществуващата `tests/tx` папка, без нов build/worktree.

## Исторически резултати

| Проверка | Резултат | Какво е наблюдавано |
|---|---|---|
| `iqtx_board_hil.py` | 19 477 assertions, failures=[] | API/граници, CW фронтове и hold, DOC add/wrap, AM/FM, GC и избрани resource guards |
| `iqtx_descriptor_hil.py` | 248 assertions, PASS | CW хардуерна промяна на D0; 8 AM + 8 FM1 + 8 FM2 спрени DTC snapshot-а |
| `iqtx_ui_hil.py`: CW | RUN_PASS | 3008 ms, 148 polls, 30 тестови LVGL heartbeat callbacks |
| `iqtx_ui_hil.py`: AM | UI_MEMORY_BLOCKED | 71 808 B свободни, искани 131 071 B |
| `iqtx_ui_hil.py`: FM | UI_MEMORY_BLOCKED | 71 536 B свободни, искани 131 071 B |
| Cleanup / RX restore | PASS | Нов CW owner след отказите; RX AM 7 048 000 Hz, volume46, същите параметри, backend error=None |

19 725 е сумата на assertion-ите в първите два suite-а, не брой отделни семпли.
AM, FM gain1 и FM gain2 работиха по 10 s, с 1078/1067/1063 периодични status
прочита. Наблюдавани са променящи се ADC/phase/DAC кодове; status не е атомарен.
Тестовете не сравняват произволен live ADC/phase/DAC snapshot като един семпъл.

CW `key()` отне **1384–1412 us** в отчетените извиквания при 8 kHz/256 и
44 kHz/220 ramp samples. Командата не чака цялата рампа, но задържа управлението
около 1.4 ms. Това още не изпълнява буквално изискване за нулево задържане.

Примери за действителни дескриптори: AM/FM след checked stop, CW в установен hold:

- CW hold D0: settings `0x50000000`, CRA `0x0101`, fixed source към крайния код.
- AM: bank `0x20000000`, ADC1360 → SAR I `0x20000aa0` = bank + 2×ADC.
- FM1: phase51007 = `0xc73f` → SAR I `0x2000c700`, SAR Q `0x2000c702`.
- FM2: phase54968 → SAR I `0x2000d600`, SAR Q `0x2000d602`.

AM/FM standalone LUT алокацията премина с около 113 KB останал heap. При запазен
UI дори след освобождаване на RX остава около 72 KB. Следователно сегашната
131071-B резервирана област за подравняване не е съвместима с този UI snapshot.
Heap цифрите включват тестовия код; не са универсален профил за всяка UI конфигурация.

## Граница на историческото доказване

- Четени са DAC регистри, не измерени напрежения. Няма осцилоскопен I/Q skew,
  AM depth/THD, FM DC/deviation linearity или image-rejection резултат.
- `unexpected_callbacks=0` е наблюдаван; няма пълен ISR-entry trace. Тестовият
  UI heartbeat е инструмент за event loop, не част от обслужването на TX.
- DOC CPU-write векторите доказват реално add/wrap/overflow. DTC snapshot-ите
  доказват крайно адресиране; не доказват всеки фазов прираст, липса на изпуснати
  семпли, такт с външна точност или пълна sample-to-sample непрекъснатост.
- P001 не е захранен с контролиран тестов сигнал; default `adc_mid=2048` не е
  калибрация на наличния вход. AGT отчетът 43988.27 Hz е регистрово изчисление.
- GC тестът доказва retained ownership/release/reallocation и допустими прочетени
  кодове, не всеки DAC семпъл по време на GC.
- Не са правени 10-minute soak, stop-timeout fault injection или active-TX reset.
  SSB и физически CW-key GPIO остават извън реализирания път.

## Повторение и логове

С изключена RF PA и предварително проверена платка, от port директорията:

```text
python -E tests/tx/run_board_hil.py --port COM25 --snapshot
python -E tests/tx/run_board_hil.py --port COM25 --run tests/tx/iqtx_board_hil.py
python -E tests/tx/run_board_hil.py --port COM25 --run tests/tx/iqtx_descriptor_hil.py
python -E tests/tx/run_board_hil.py --port COM25 --run-live tests/tx/iqtx_ui_hil.py
```

Използван host Python: `C:\Users\teodor\AppData\Local\Programs\Python\Python310\python.exe`
с инсталиран `mpremote`/`pyserial`. Примерното `python` трябва да сочи към него.
`--run-live` очаква вече работещ `sdr_single`; payload-ът възстановява RX в `finally`.
Не възстановявайте стара snapshot върху различна платка или различна текуща сесия.
`--restore` е recovery за чист VM, не обща команда за live app reconfiguration.

Суровият append-only transcript е [iqtx_hil_transcript.log](iqtx_hil_transcript.log),
началните параметри — [iqtx_hil_snapshot.json](iqtx_hil_snapshot.json).
Логът съдържа изпълнения source payload и runtime резултатите. След успешния първи
run harness-ът е подсилен да хвърля exception при непразен failures списък;
историческият PASS тук е проверен по самия `failures=[]`, не само host exit code.

Следваща инженерна стъпка: повторение на същите HIL сценарии с compact firmware,
включително AM/FM с retained UI и RX→TX→RX handoff. Намаляването на CW control
latency остава отделна задача. Production firmware не е променян от историческите
тестове в този файл.
