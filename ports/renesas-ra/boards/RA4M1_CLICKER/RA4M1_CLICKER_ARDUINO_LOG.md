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

Добавена е секция за PWM (GPT) с мапинг към пиновете на Arduino Nano R4 / RA4M1 CLICKER:

- `MICROPY_HW_PWM_0A`  → `P107` (GPT0_A)
- `MICROPY_HW_PWM_0B`  → `P106` (GPT0_B)
- `MICROPY_HW_PWM_1A`  → `P105` (GPT1_A)
- `MICROPY_HW_PWM_1B`  → `P104` (GPT1_B)
- `MICROPY_HW_PWM_2A`  → `P103` (GPT2_A)
- `MICROPY_HW_PWM_2B`  → `P102` (GPT2_B)
- `MICROPY_HW_PWM_3B`  → `P112` (GPT3_B)
- `MICROPY_HW_PWM_7A`  → `P304` (GPT7_A)
- `MICROPY_HW_PWM_7B`  → `P303` (GPT7_B)

Секцията в `mpconfigboard.h` (съкратено):

```c
// PWM (GPT)
#define MICROPY_HW_PWM_0A           (pin_P107) // GPT0_A
#define MICROPY_HW_PWM_0B           (pin_P106) // GPT0_B
#define MICROPY_HW_PWM_1A           (pin_P105) // GPT1_A
#define MICROPY_HW_PWM_1B           (pin_P104) // GPT1_B
#define MICROPY_HW_PWM_2A           (pin_P103) // GPT2_A
#define MICROPY_HW_PWM_2B           (pin_P102) // GPT2_B
#define MICROPY_HW_PWM_3B           (pin_P112) // GPT3_B
#define MICROPY_HW_PWM_7A           (pin_P304) // GPT7_A
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
    ("P112", "GPT3_B"),
    ("P304", "GPT7_A"),
    ("P303", "GPT7_B"),
]

for name, label in PWM_PINS:
    p = Pin(name)
    pwm = PWM(p, freq=1000, duty_u16=32768)
    print("PWM OK:", name, label)
```

Очакване: за всеки пин да се инициализира `PWM` без грешка; на изхода да се наблюдава PWM сигнал ~1 kHz, 50% duty.

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

Текущата конфигурация в `mpconfigboard.h` (раздел 2) активира следните GPT канали:

- GPT0_A → P107  (`MICROPY_HW_PWM_0A`)
- GPT0_B → P106  (`MICROPY_HW_PWM_0B`)
- GPT1_A → P105  (`MICROPY_HW_PWM_1A`)
- GPT1_B → P104  (`MICROPY_HW_PWM_1B`)
- GPT2_A → P103  (`MICROPY_HW_PWM_2A`)
- GPT2_B → P102  (`MICROPY_HW_PWM_2B`)
- GPT3_B → P112  (`MICROPY_HW_PWM_3B`)
- GPT7_A → P304  (`MICROPY_HW_PWM_7A`)
- GPT7_B → P303  (`MICROPY_HW_PWM_7B`)

### 4.2. Допълнителни „чисти" кандидати за PWM

Това са пинове, които могат да бъдат GPT изходи и не са вързани към критични периферии (REPL, debug, основен I2C/SPI). Подходящи са за бъдещо разширяване на PWM поддръжката:

- **P501** – GPT2_B (GTIOC2B), `GPIOHD7`  → реално свободен пин, добър кандидат за втори изход на GPT2_B.
- **P502** – GPT3_B (GTIOC3B), `GPIOHD6`  → свободен пин за GPT3_B, позволява да се премести PWM от P112 (SPI1_SS) към по‑удобен GPIO.
- **P409** – GPT5_A (GTIOC5A), `LED1`     → конфликт само с LED1; удобен за PWM управление на LED1.
- **P408** – GPT5_B (GTIOC5B), `LED2`     → конфликт само с LED2; удобен за PWM управление на LED2.

Забележка: към момента GPT5 и GPT2/3 през тези пинове **не са** активирани в `mpconfigboard.h`. При нужда могат да се добавят нови `MICROPY_HW_PWM_xA/B` дефиниции.

### 4.3. Кандидати с условни конфликти (зависи от use‑case)

Тук са пинове, които могат да бъдат PWM, но са свързани с I2C/UART/SPI върху mikroBUS или други функции. Използването им като PWM означава да се жертва съответната периферия:

- **P205** – GPT4_A (GTIOC4A)  → споделя се с I2C линия към mikroBUS (MBSCL). Ако I2C върху mikroBUS не се ползва, може да се ползва като PWM.
- **P301** – GPT4_B (GTIOC4B)  → `UART2_RX` и бутон `SW2`. PWM е възможен само ако се откажем от UART2 RX и/или SW2.
- **P302** – GPT4_A (GTIOC4A)  → `UART2_TX` и `MBINT` към mikroBUS. PWM е възможен без UART2 и без INT от mikroBUS.
- **P101** – GPT5_A (GTIOC5A)  → `I2C1_SDA` / `MBMOSI`. PWM означава да няма I2C1 (и евентуално SPI0) на този пин.
- **P100** – GPT5_B (GTIOC5B)  → `I2C1_SCL` / `MBMISO`. Аналогично – PWM само без I2C1/SPIO.
- **P400** – GPT6_A (GTIOC6A)  → `I2C0_SCL` (mikroBUS I2C0). PWM само без I2C0.
- **P401** – GPT6_B (GTIOC6B)  → `I2C0_SDA` (mikroBUS I2C0). PWM само без I2C0.
- **P111** – GPT3_A (GTIOC3A)  → `SPI1_SCK` (часовник на SPI1). PWM само без SPI1.
- **P109** – GPT1_A (GTIOC1A)  → `SPI1_MOSI` и debug TRACESWO. PWM без SPI1 и без trace.
- **P110** – GPT1_B (GTIOC1B)  → `SPI1_MISO` и JTAG TDI. PWM без SPI1 и без JTAG.

### 4.4. Практически нежелателни като PWM

Следните пинове могат хардуерно да са GPT изходи, но са критични за отстраняване на грешки или за REPL и не се препоръчва да се ползват за PWM:

- **P108** – GPT0_B (GTIOC0B)  → SWDIO (TMS/SWDIO, debug).
- **P300** – GPT0_A (GTIOC0A)  → SWCLK (TCK/SWCLK, debug).
- **P410** – GPT6_B (GTIOC6B)  → `UART0_RX` (основен REPL RX).
- **P411** – GPT6_A (GTIOC6A)  → `UART0_TX` (основен REPL TX).

Заключение: за бъдещо разширяване на PWM поддръжката най‑подходящи са P501, P502, P409 и P408. Останалите кандидати изискват отказ от конкретни периферии (I2C, UART, SPI, debug) и трябва да се активират само при ясно съзнателно решение за съответния use‑case.
