# RA4M1 CLICKER – Comparator (ACMPLP) и OPAMP от MicroPython

Този файл показва практически примери как да се ползват вградения **компаратор** (ACMPLP)
и **операционен усилвател** (OPAMP) от MicroPython порта за Renesas RA, и прави паралел
с Arduino (UNO R4 / Nano R4).

## 1. Machine Comparator (ACMPLP)

Класът е `machine.Comparator` и е RA‑специфичен.

Конструктор:

```python
from machine import Comparator

cmp = Comparator(
    0,                       # канал: 0 или 1 на RA4M1 CLICKER
    input=Comparator.INPUT0, # кой CMPIN вход да се ползва
    ref=Comparator.REF_IVREF,# референтно ниво (вътрешно / DAC / външен пин)
    filter=Comparator.FILTER_OFF,
    edge=Comparator.RISING,  # за IRQ: RISING / FALLING / BOTH
    speed=Comparator.SPEED_LOW,
    invert=False,
    output=False,            # ако True – изкарва изход към специален изходен пин
)
```

Основни методи:

- `cmp.enable()` / `cmp.disable()` – включва/изключва хардуерния компаратор.
- `cmp.value()` – моментна стойност на изхода (0 или 1).
- `cmp.irq(callback)` – регистрира IRQ callback (или `None` за изключване).
- `cmp.deinit()` – спира модула и освобождава ресурси.

Константи (частичен списък):

- Филтър: `FILTER_OFF`, `FILTER_PCLK8`, `FILTER_PCLK16`, `FILTER_PCLK32`
- Ръбове: `RISING`, `FALLING`, `BOTH`
- Скорост: `SPEED_LOW`, `SPEED_HIGH`
- Входове: `INPUT0..INPUT3` (CMPIN0..3 – виж pins.csv / схемата)
- Референции: `REF_EXT0`, `REF_EXT1`, `REF_DAC0`, `REF_DAC1`, `REF_IVREF`

### 1.1. Прост пример – polling на компаратор

```python
from machine import Comparator
from time import sleep

# Comparator 0, вход CMPIN0, референтно вътрешно ниво (IVREF)
cmp = Comparator(
    0,
    input=Comparator.INPUT0,
    ref=Comparator.REF_IVREF,
    filter=Comparator.FILTER_PCLK8,
    edge=Comparator.RISING,
    speed=Comparator.SPEED_HIGH,
)

cmp.enable()

try:
    while True:
        print("comp =", cmp.value())
        sleep(0.1)
finally:
    cmp.deinit()
```

**Паралел с Arduino:**

- В класически Arduino (UNO, Nano на AVR) обикновено правиш сравнение в софтуер:
  `analogRead()` и `if (value > threshold) ...`.
- На RA4M1 компараторът е отделен хардуерен блок (ACMPLP) – тук го ползваш
  директно през `Comparator(...)` и четеш готов **двоичен** резултат с `value()`.

### 1.2. Пример с прекъсване (irq)

```python
from machine import Comparator, Pin
from time import sleep

led = Pin("LED", Pin.OUT)  # или конкретен пин според платката

# Глобален обект, за да е достъпен в callback-а
cmp = Comparator(
    0,
    input=Comparator.INPUT0,
    ref=Comparator.REF_IVREF,
    filter=Comparator.FILTER_PCLK32,
    edge=Comparator.BOTH,
)

# Callback, който се вика от scheduler-а (НЕ блокирай дълго тук)
def on_comp(c):
    # c е самият Comparator обект
    led.value(c.value())
    print("IRQ: comp=", c.value())

cmp.irq(on_comp)   # включва IRQ в хардуера (ACMPLP + ICU)
cmp.enable()

try:
    while True:
        sleep(1)
finally:
    cmp.irq(None)
    cmp.deinit()
```

**Паралел с Arduino:**

- Концептуално това е подобно на `attachInterrupt()` върху изход на аналогов
  компаратор или външен пин: регистрираш callback, който се вика при RISING/FALLING.
- Разликата е, че тук високо‑ниво MicroPython API директно управлява RA ACMPLP,
  вместо да настройваш регистри (ACSR, ADCSRB и т.н., както на AVR).

## 2. Machine OPAMP (операционен усилвател)

Класът е `machine.OPAMP` и ползва RA OPAMP блока на RA4M1 (до 4 канала).

```python
from machine import OPAMP

# Канал 0, ниска консумация по подразбиране
op0 = OPAMP(0, mode=OPAMP.LOW_POWER)
```

Методи:

- `op.start()` – стартира съответния OPAMP канал.
- `op.stop()` – спира канала.
- `op.status()` – връща `True/False` дали каналът е активен според хардуера.
- `op.deinit()` – спира канала (alias на `stop()` + почистване на състояние).

Константи:

- `OPAMP.LOW_POWER` – по‑ниска консумация, по‑ниска скорост.
- `OPAMP.HIGH_SPEED` – по‑висока скорост, по‑висока консумация.

### 2.1. Пример – буфер/усилвател на входен сигнал

```python
from machine import OPAMP
from time import sleep

# Инициализация в режим ниска консумация
op0 = OPAMP(0, mode=OPAMP.LOW_POWER)

op0.start()
print("OPAMP0 active:", op0.status())

try:
    while True:
        # Тук просто държим OPAMP-а включен.
        # Реалната схема (кои пинове са +/−/OUT) е по хардуер/cheat sheet.
        sleep(1)
finally:
    op0.stop()
```

**Паралел с Arduino:**

- На Arduino UNO R4 / Nano R4 има официална OPAMP библиотека, където в C++
  конфигурираш режим, входове и gain на RA4M1 OPAMP блока.
- В MicroPython за RA идеята е същата, но API-то е опростено:
  избираш **канал** и **режим** (`LOW_POWER`/`HIGH_SPEED`), а свързването към
  конкретни пинове и топологията идват от хардуера и FSP настройките.

## 3. Arduino → MicroPython: стъпкови примери

### 3.1. Аналогово сравнение в Arduino vs хардуерен Comparator в MicroPython

**Arduino (класически UNO / Nano, AVR):**

```cpp
const int sensorPin = A0;
const int ledPin = 13;
const int threshold = 512;  // ~половината от 10-битовия обхват (0..1023)

void setup() {
  pinMode(ledPin, OUTPUT);
}

void loop() {
  int v = analogRead(sensorPin);
  if (v > threshold) {
    digitalWrite(ledPin, HIGH);
  } else {
    digitalWrite(ledPin, LOW);
  }
}
```

**MicroPython на RA4M1 CLICKER със `Comparator`:**

```python
from machine import Comparator, Pin
from time import sleep

led = Pin("LED", Pin.OUT)  # или конкретен GPIO пин

cmp = Comparator(
    0,
    input=Comparator.INPUT0,   # CMPIN0 → свържи външния сигнал тук
    ref=Comparator.REF_IVREF,  # вътрешно референтно ниво ~постоянен праг
    filter=Comparator.FILTER_PCLK8,
    edge=Comparator.BOTH,
)

cmp.enable()

try:
    while True:
        led.value(cmp.value())
        sleep(0.01)
finally:
    cmp.deinit()
```

**Какво е различно/общото:**

- В Arduino четеш **аналогова стойност** (`analogRead`, 0..1023) и сравняваш
  в софтуер с `threshold`.
- В MicroPython на RA **хардуерният ACMPLP** вече прави сравнението, а ти
  четеш директно **0/1** с `cmp.value()`.
- Реалният праг при `REF_IVREF` се задава от вътрешния reference блок/
  конфигурация в FSP (аналогично на вътрешен компаратор reference в AVR).

### 3.2. OPAMP: Arduino UNO R4 (концептуално) vs MicroPython `OPAMP`

В Arduino UNO R4 / Nano R4 има OPAMP библиотека в core-а, която позволява в
`setup()` да конфигурираш канал (OPAMP0/1/2/3), режим (buffer, gain и т.н.)
и режим на консумация.

**Псевдо-пример в стил Arduino:**

```cpp
// Псевдо-код – имената на функциите зависят от конкретната библиотека,
// идеята е:
//   - избери OPAMP канал 0
//   - сложи го в follower / buffer режим (unity gain)
//   - използвай low-power режим

void setup() {
  // OPAMP0.beginFollowerLowPower();
  // или подобен API според официалната OPAMP библиотека
}

void loop() {
  // OPAMP работи в хардуера; в loop() може да няма нищо
}
```

**MicroPython на RA4M1 CLICKER със `OPAMP`:**

```python
from machine import OPAMP

# Канал 0, ниска консумация (еквивалентно на "low power" режим)
op0 = OPAMP(0, mode=OPAMP.LOW_POWER)

op0.start()
print("OPAMP0 active:", op0.status())

# OPAMP блокът вече буферира/усилва според хардуерната схема и FSP настройките.
# В основния цикъл често не е нужно да правиш нищо специално в софтуер.
```

**Съответствия:**

- Arduino: избираш **OPAMP канал** и **режим** през функция на библиотеката.
- MicroPython: `OPAMP(канал, mode=...)` прави същото на по-опростено ниво.
- И в двата случая аналоговото свързване (кои пинове са IN+/IN-/OUT, какъв
  е gain-ът и т.н.) е определено от хардуера (RA4M1 + платката) и FSP /
  core конфигурацията, не от самия код.

---

**Бележка:**

- Кой точно пин е свързан към `INPUT0..INPUT3` на компаратора и към OPAMP
  входовете/изходите зависи от схемата на конкретната платка (RA4M1 CLICKER).
- За точни връзки ползвай:
  - `pins.csv` и `pins_RA4M1_CLICKER.c` в порта
  - официалния schematic / cheat sheet за RA4M1 CLICKER / UNO R4.

