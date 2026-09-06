# IQTX: цифрови bench тестове на RA6M3

Дата: 2026-09-06. Резултат: самостоятелният CW/AM/FM цифров път премина
описаните проверки; **AM/FM с текущия SDR UI не могат да се инициализират**.
Причината е установен недостиг на heap, не предположение за DMA/DOC отказ.

## Среда и безопасност

- Target: COM25, J-Link CDC `001120000058`; UID
  `5434032d35373948384edaf926275454`.
- `os.uname()`: RA6M3 / MicroPython 1.28.0-preview /
  `5abae577cb-dirty on 2026-09-06`. Не е доказана побитова идентичност с локален ELF.
- P001 ADC; P014/P015 DAC I/Q. Операторът потвърди изключена/разкачена RF PA.
- Само RAM изпълнение през serial. Първите два suite-а използват raw-REPL soft
  reset без стартиране на `main.py`; третият запазва съществуващия SDR UI.
  Няма firmware flash, качени файлове или промяна на `boot.py`/`main.py`.
- Работата е в общия port и съществуващата `tests/tx` папка, без нов build/worktree.

## Резултати

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

## Граница на доказване

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

Следваща инженерна стъпка: решение за LUT RAM, съвместимо с UI, и намаляване на
CW control latency. Production firmware не е променян като част от тези тестове.
