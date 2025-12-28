# RA4M1_CLICKER / Arduino Nano R4 – Лог на корекциите

## 1. Промяна в `pins.csv`

**Файл:** `ports/renesas-ra/boards/RA4M1_CLICKER/pins.csv`

Добавени са липсващите пинове, за да могат да се ползват в MicroPython като `Pin("Pxxx")`:

- `P104`
- `P105`
- `P106`
- `P303`

Съкратен диф (след промяната):

```text
P100,P100
P101,P101
P102,P102
P103,P103
P104,P104
P105,P105
P106,P106
P107,P107
P108,P108
...
P301,P301
P302,P302
P303,P303
P304,P304
```

### Тест скрипт за новите пинове (`test_new_pins.py`)

```python
from machine import Pin

# Новодобавени пинове в pins.csv
NEW_PINS = ["P104", "P105", "P106", "P303"]

for name in NEW_PINS:
    p = Pin(name, Pin.OUT)
    p.value(1)
    print("Pin", name, "OK: set to 1")
    p.value(0)
    print("Pin", name, "OK: set to 0")
```

Очакване: скриптът да се изпълни без `ValueError` за липсващ пин; на съответните изводи да се вижда промяна на нивото (с LED/осцилоскоп).

---

## 2. Промяна в `mpconfigboard.h` – PWM (GPT)

**Файл:** `ports/renesas-ra/boards/RA4M1_CLICKER/mpconfigboard.h`

Секцията за PWM (GPT) вече активира **всички GPT0..GPT7 канали (A/B)**, с изключение на пиновете за REPL (UART0_RX/TX):

- `MICROPY_HW_PWM_0A`  → `P107` (GPT0_A, MBPWM)
- `MICROPY_HW_PWM_0B`  → `P106` (GPT0_B)
- `MICROPY_HW_PWM_1A`  → `P105` (GPT1_A)
- `MICROPY_HW_PWM_1B`  → `P104` (GPT1_B)
- `MICROPY_HW_PWM_2A`  → `P103` (GPT2_A, MBSSL)
- `MICROPY_HW_PWM_2B`  → `P102` (GPT2_B, MBSCK)
- `MICROPY_HW_PWM_3A`  → `P111` (GPT3_A, SPI1_SCK)
- `MICROPY_HW_PWM_3B`  → `P112` (GPT3_B, SPI1_SSL)
- `MICROPY_HW_PWM_4A`  → `P205` (GPT4_A, MBSCLI / I2C alt)
- `MICROPY_HW_PWM_4B`  → `P301` (GPT4_B, UART2_RX / SW2)
- `MICROPY_HW_PWM_5A`  → `P409` (GPT5_A, LED1)
- `MICROPY_HW_PWM_5B`  → `P408` (GPT5_B, LED2)
- `MICROPY_HW_PWM_6A`  → `P400` (GPT6_A, I2C0_SCL)
- `MICROPY_HW_PWM_6B`  → `P401` (GPT6_B, I2C0_SDA)
- `MICROPY_HW_PWM_7A`  → `P304` (GPT7_A, USR бутон / IRQ9)
- `MICROPY_HW_PWM_7B`  → `P303` (GPT7_B)

Секцията в `mpconfigboard.h` (съкратено):

```c
// PWM (GPT)
// GPT0
#define MICROPY_HW_PWM_0A           (pin_P107) // GPT0_A (MBPWM)
#define MICROPY_HW_PWM_0B           (pin_P106) // GPT0_B

// GPT1
#define MICROPY_HW_PWM_1A           (pin_P105) // GPT1_A
#define MICROPY_HW_PWM_1B           (pin_P104) // GPT1_B

// GPT2
#define MICROPY_HW_PWM_2A           (pin_P103) // GPT2_A (MBSSL)
#define MICROPY_HW_PWM_2B           (pin_P102) // GPT2_B (MBSCK)

// GPT3
#define MICROPY_HW_PWM_3A           (pin_P111) // GPT3_A (SPI1_SCK)
#define MICROPY_HW_PWM_3B           (pin_P112) // GPT3_B (SPI1_SSL)

// GPT4
#define MICROPY_HW_PWM_4A           (pin_P205) // GPT4_A (MBSCLI / I2C alt)
#define MICROPY_HW_PWM_4B           (pin_P301) // GPT4_B (UART2_RX / SW2)

// GPT5
#define MICROPY_HW_PWM_5A           (pin_P409) // GPT5_A (LED1)
#define MICROPY_HW_PWM_5B           (pin_P408) // GPT5_B (LED2)

// GPT6
#define MICROPY_HW_PWM_6A           (pin_P400) // GPT6_A (I2C0_SCL)
#define MICROPY_HW_PWM_6B           (pin_P401) // GPT6_B (I2C0_SDA)

// GPT7
#define MICROPY_HW_PWM_7A           (pin_P304) // GPT7_A (USR button / IRQ9)
#define MICROPY_HW_PWM_7B           (pin_P303) // GPT7_B
```

### Тест скрипт за PWM пиновете (`test_pwm_gpt.py`)

```python
from machine import Pin, PWM

PWM_PINS = [
    ("P107", "GPT0_A"),
    ("P106", "GPT0_B"),
    ("P105", "GPT1_A"),
    ("P104", "GPT1_B"),
    ("P103", "GPT2_A"),
    ("P102", "GPT2_B"),
    ("P111", "GPT3_A"),
    ("P112", "GPT3_B"),
    ("P205", "GPT4_A"),
    ("P301", "GPT4_B"),
    ("P409", "GPT5_A"),
    ("P408", "GPT5_B"),
    ("P400", "GPT6_A"),
    ("P401", "GPT6_B"),
    ("P304", "GPT7_A"),
    ("P303", "GPT7_B"),
]

for name, label in PWM_PINS:
    p = Pin(name)
    pwm = PWM(p, freq=1000, duty_u16=32768)
    print("PWM OK:", name, label)
```

Очакване: за всеки пин да се инициализира `PWM` без грешка; на изхода да се наблюдава PWM сигнал ~1 kHz, 50% duty.

### 2.3. Практически стъпки за тестване на всички PWM (GPT) пинове

1. **Build на firmware** (ако не е вече направен) от `ports/renesas-ra`:

   ```bash
   make BOARD=RA4M1_CLICKER
   ```

2. **Флашване** на `build-RA4M1_CLICKER/firmware.hex` или `firmware.bin` към платката (Arduino Nano R4 / RA4M1 CLICKER).
3. **Свързване към REPL** през сериен порт (UART0 върху USB на Arduino Nano R4).
4. **Копиране на `test_pwm_gpt.py`** на платката (напр. чрез `mpremote cp` или друга предпочитана метода).
5. В REPL стартирай:

   ```python
   import test_pwm_gpt
   ```

6. Наблюдавай изхода в REPL – трябва да се изпишат редове `PWM OK: <Pxxx> <GPTn_X>` за всички 16 пина, без traceback.
7. При нужда от хардуерна верификация – измери с осцилоскоп/логически анализатор на съответните пинове (очакване: ~1 kHz, ~50% duty).

---

## 3. Build на firmware и базова верификация

**Команда за build:** изпълнена от `ports/renesas-ra` (MINGW64 среда):

```bash
make BOARD=RA4M1_CLICKER
```

Резултат: успешно генерирани

- `build-RA4M1_CLICKER/firmware.elf`
- `build-RA4M1_CLICKER/firmware.hex`
- `build-RA4M1_CLICKER/firmware.bin`

### Тест стъпки след флашване на платката

1. Флашни `firmware.hex` или `firmware.bin` на Arduino Nano R4 / RA4M1 CLICKER.
2. Свържи се към сериен порт и отвори MicroPython REPL.
3. Изпълни:

```python
import os
print(os.uname())
```

Очакване: да се изпише информация за платката/портa (board, release), което потвърждава, че новият firmware стартира коректно.

След това могат да се стартират `test_new_pins.py` и `test_pwm_gpt.py` за пълна проверка на направените корекции.

---

## 4. Допълнителни възможни PWM (GPT) пинове и конфликти

**Цел:** да се документират всички хардуерно възможни PWM (GPT) изходи на RA4M1 CLICKER, които са налични на платката, и какви периферии ще се засегнат, ако ги ползваме като PWM.

### 4.1. Вече активирани PWM

Текущата конфигурация в `mpconfigboard.h` (раздел 2) активира **всички GPT0..GPT7 канали (A/B)**, с изключение на REPL пиновете P410/P411:

- GPT0_A → P107  (`MICROPY_HW_PWM_0A`)
- GPT0_B → P106  (`MICROPY_HW_PWM_0B`)
- GPT1_A → P105  (`MICROPY_HW_PWM_1A`)
- GPT1_B → P104  (`MICROPY_HW_PWM_1B`)
- GPT2_A → P103  (`MICROPY_HW_PWM_2A`)
- GPT2_B → P102  (`MICROPY_HW_PWM_2B`)
- GPT3_A → P111  (`MICROPY_HW_PWM_3A`)
- GPT3_B → P112  (`MICROPY_HW_PWM_3B`)
- GPT4_A → P205  (`MICROPY_HW_PWM_4A`)
- GPT4_B → P301  (`MICROPY_HW_PWM_4B`)
- GPT5_A → P409  (`MICROPY_HW_PWM_5A`)
- GPT5_B → P408  (`MICROPY_HW_PWM_5B`)
- GPT6_A → P400  (`MICROPY_HW_PWM_6A`)
- GPT6_B → P401  (`MICROPY_HW_PWM_6B`)
- GPT7_A → P304  (`MICROPY_HW_PWM_7A`)
- GPT7_B → P303  (`MICROPY_HW_PWM_7B`)

### 4.2. Допълнителни „чисти" кандидати за PWM

Остават само хардуерни алтернативи за вече активираните канали, които НЕ са свързани към REPL/debug и към момента не са избрани като основни:

- **P501** – GPT2_B (GTIOC2B), `GPIOHD7`  → реално свободен пин, алтернатива на P102 за GPT2_B.
- **P502** – GPT3_B (GTIOC3B), `GPIOHD6`  → алтернатива на P112 за GPT3_B (по‑удобен GPIO вместо SPI1_SSL).

### 4.3. Кандидати с условни конфликти (зависи от use‑case)

Те вече са активирани чрез `MICROPY_HW_PWM_xA/B` (раздел 4.1) и трябва да се ползват с ясното съзнание, че жертват съответните периферии:

- **P205** – GPT4_A (GTIOC4A)  → споделя се с I2C линия към mikroBUS (MBSCLI).
- **P301** – GPT4_B (GTIOC4B)  → `UART2_RX` и бутон `SW2`.
- **P409** – GPT5_A (GTIOC5A)  → `LED1`.
- **P408** – GPT5_B (GTIOC5B)  → `LED2`.
- **P400** – GPT6_A (GTIOC6A)  → `I2C0_SCL` (mikroBUS I2C0).
- **P401** – GPT6_B (GTIOC6B)  → `I2C0_SDA` (mikroBUS I2C0).
- **P111** – GPT3_A (GTIOC3A)  → `SPI1_SCK` (часовник на SPI1).

### 4.4. Практически нежелателни като PWM (все още неактивирани)

Следните пинове могат хардуерно да са GPT изходи, но са критични за отстраняване на грешки или за REPL и **съзнателно** не са активирани като `MICROPY_HW_PWM_xA/B`:

- **P108** – GPT0_B (GTIOC0B)  → SWDIO (TMS/SWDIO, debug).
- **P300** – GPT0_A (GTIOC0A)  → SWCLK (TCK/SWCLK, debug).
- **P410** – GPT6_B (GTIOC6B)  → `UART0_RX` (основен REPL RX).
- **P411** – GPT6_A (GTIOC6A)  → `UART0_TX` (основен REPL TX).

Заключение: в момента всички GPT0..GPT7 канали са достъпни през някакъв пин, с изключение на най‑рисковите (debug и REPL). При нужда могат да се пренасочат към алтернативи като P501/P502.

### 4.5. Таблица с всички активирани PWM (GPT) пинове

Обобщение на активните PWM канали (виж и раздел 2):

| GPT канал | MicroPython макро      | Пин   | Основна функция / конфликт |
|----------|-------------------------|-------|-----------------------------|
| GPT0_A   | `MICROPY_HW_PWM_0A`    | P107  | MBPWM (mikroBUS PWM)        |
| GPT0_B   | `MICROPY_HW_PWM_0B`    | P106  | GPIO на платката           |
| GPT1_A   | `MICROPY_HW_PWM_1A`    | P105  | GPIO / Arduino pin          |
| GPT1_B   | `MICROPY_HW_PWM_1B`    | P104  | GPIO / Arduino pin          |
| GPT2_A   | `MICROPY_HW_PWM_2A`    | P103  | MBSSL (mikroBUS SPI SS)     |
| GPT2_B   | `MICROPY_HW_PWM_2B`    | P102  | MBSCK (mikroBUS SPI SCK)    |
| GPT3_A   | `MICROPY_HW_PWM_3A`    | P111  | SPI1_SCK                    |
| GPT3_B   | `MICROPY_HW_PWM_3B`    | P112  | SPI1_SSL                    |
| GPT4_A   | `MICROPY_HW_PWM_4A`    | P205  | I2C към mikroBUS (MBSCLI)   |
| GPT4_B   | `MICROPY_HW_PWM_4B`    | P301  | UART2_RX / бутон SW2        |
| GPT5_A   | `MICROPY_HW_PWM_5A`    | P409  | LED1                        |
| GPT5_B   | `MICROPY_HW_PWM_5B`    | P408  | LED2                        |
| GPT6_A   | `MICROPY_HW_PWM_6A`    | P400  | I2C0_SCL (mikroBUS I2C0)    |
| GPT6_B   | `MICROPY_HW_PWM_6B`    | P401  | I2C0_SDA (mikroBUS I2C0)    |
| GPT7_A   | `MICROPY_HW_PWM_7A`    | P304  | USR бутон / IRQ9            |
| GPT7_B   | `MICROPY_HW_PWM_7B`    | P303  | GPIO / Arduino pin          |

---

## 5. Външни прекъсвания (ICU / ExtInt) за RA4M1 CLICKER / Arduino Nano R4

**Цел:** всички налични хардуерни външни прекъсвания (ICU IRQ0..IRQ12, IRQ14, IRQ15) на RA4M1 да са достъпни от MicroPython (`ExtInt` и `Pin.irq`) върху реалните пинове на платката.

### 5.1. Промени във `vector_data.h` / `vector_data.c`

**Файлове:**

- `boards/RA4M1_CLICKER/ra_gen/vector_data.h`
- `boards/RA4M1_CLICKER/ra_gen/vector_data.c`

**Какво е направено:**

1. Актуализиран брой вектори:

    - `VECTOR_DATA_IRQ_COUNT`: `27` → `32` (използваме индексите `0..31`).

2. Добавени са дефиниции **само** за част от ICU IRQ каналите, за да се съобразим
   с хардуерния лимит от 32 вектора при RA4M1:

    - Нови вектори (индекси в `g_vector_table`):
      - `27` → `ICU_IRQ0`  (EXTINT0)
      - `28` → `ICU_IRQ1`  (EXTINT1)
      - `29` → `ICU_IRQ2`  (EXTINT2)
      - `30` → `ICU_IRQ3`  (EXTINT3)
      - `31` → `ICU_IRQ4`  (EXTINT4)

3. Съществуващите ICU вектори са запазени:

    - `ICU_IRQ5` → индекс `12`
    - `ICU_IRQ6` → индекс `25`
    - `ICU_IRQ9` → индекс `26`

   Така общо имаме 8 активни външни прекъсвания: IRQ0..IRQ6 и IRQ9.

4. За всеки ICU индекс (нов и стар) са добавени/запазени:

    - `g_vector_table[...] = r_icu_isr`  – общ ISR за всички външни прекъсвания.
    - `g_interrupt_event_link_select[...] = BSP_PRV_IELS_ENUM(EVENT_ICU_IRQx)` – правилно IELS събитие за съответния канал.

### 5.2. Пинове на платката с ExtInt поддръжка

Според `ra_icu.c` (секцията за `RA4M1`) и реалните пинове от `pins.csv`, а
също и предвид ограничения брой активни канали (IRQ0..IRQ6 и IRQ9), следните
пинове на RA4M1 CLICKER могат **реално** да се ползват за външни прекъсвания
в текущата конфигурация:

- **IRQ0**  → `P105`, `P206`, `P400`
- **IRQ1**  → `P101`, `P104`, `P205`
- **IRQ2**  → `P002`, `P100`, `P213`
- **IRQ3**  → `P004`, `P110`, `P212`
- **IRQ4**  → `P111`, `P411`, `P402`
- **IRQ5**  → `P302`, `P410`, `P401`
- **IRQ6**  → `P000`, `P301`, `P409`
- **IRQ9**  → `P304`, `P414`  (USR бутонът е на P304)

Останалите хардуерни канали (IRQ7, IRQ8, IRQ10, IRQ11, IRQ12, IRQ14, IRQ15)
**нямат заделени вектори** в тази версия и няма да са достъпни през
`Pin.irq()` / `ExtInt`.

Важно: RA драйверът `ra_icu.c` конфигурира PFS (ISEL/PCR) динамично през `ra_icu_set_pin()`, така че първоначалното `IOPORT_CFG_IRQ_ENABLE` в `pin_data.c` **не ограничава** кои пинове могат да се ползват за ExtInt.

### 5.3. Как се ползва от MicroPython

Два начина:

1. През `Pin.irq()`:

    ```python
    from machine import Pin

    def cb(pin):
        print("IRQ from", pin)

    p = Pin("P304", Pin.IN)  # USR бутон (IRQ9)
    p.irq(trigger=Pin.IRQ_FALLING, handler=cb)
    ```

2. През `ExtInt` директно:

    ```python
    from machine import Pin
    from pyb import ExtInt

    def cb(line):
        print("ExtInt line", line)

    p = Pin("P105", Pin.IN)  # IRQ0
    e = ExtInt(p, ExtInt.IRQ_FALLING, Pin.PULL_NONE, cb)
    ```

Очакване: при промяна на нивото върху съответния пин да се генерира прекъсване и да се извика callback‑а.

### 5.4. Тест план

Минимален smoke тест (след `make BOARD=RA4M1_CLICKER` и флашване):

1. Потвърди, че USR бутонът продължава да работи:

    ```python
    from pyb import Switch

    sw = Switch()
    sw.callback(lambda: print("SW pressed"))
    ```

2. Тествай няколко различни IRQ линии (например P105/IRQ0, P400/IRQ0,
   P409/IRQ6, P304/IRQ9) със `Pin.irq()` или `ExtInt`, както е горе.

3. Следи за:

    - липса на `ValueError("The Pin object(...) doesn't have EXTINT feature")` за посочените пинове;
    - коректно извикване на callback‑а при фронт/ниво според конфигурацията.

**Забележка:** Поради конфликтите с UART/I2C/SPI (описани по‑горе и в секции 2–4), използването на някои от тези пинове като ExtInt може да изключи съответната периферия – това е очаквано поведение и трябва да се преценява според конкретния use‑case.
