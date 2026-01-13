# RA4M1 CLICKER - Туториал за начинаещи

## 1. Компаратор (Comparator) - Сравнява напрежения

### Какво прави?
Компараторът сравнява **две напрежения** и казва кое е по-голямо:
- Ако **входът > референция** → изход = **1** (HIGH)
- Ако **входът < референция** → изход = **0** (LOW)

### Къде се използва?
- Детектор за ниска батерия
- Сензор за светлина (фоторезистор)
- Детектор за прекалено високо напрежение

### Пример 1: Прост детектор (polling)

```python
from machine import Comparator
from time import sleep

# Създай компаратор 0
cmp = Comparator(
    0,                          # Канал 0
    input=Comparator.INPUT0,    # Вход CMPIN0
    ref=Comparator.REF_IVREF,   # Сравнявай с 1.2V
    speed=Comparator.SPEED_HIGH
)

cmp.enable()  # Включи компаратора

# Четене на стойността
while True:
    if cmp.value() == 1:
        print("Напрежението е ВИСОКО")
    else:
        print("Напрежението е НИСКО")
    sleep(0.5)
```

### Пример 2: С прекъсване (IRQ) - автоматично

```python
from machine import Comparator, Pin

led = Pin("LED", Pin.OUT)

# Функция, която се вика автоматично при промяна
def on_change(comp):
    led.value(comp.value())
    print("Промяна! Ново ниво:", comp.value())

cmp = Comparator(
    0,
    input=Comparator.INPUT0,
    ref=Comparator.REF_IVREF,
    edge=Comparator.BOTH  # Реагирай на RISING и FALLING
)

cmp.irq(on_change)
cmp.enable()

# Сега програмата чака, а компараторът работи сам!
while True:
    sleep(1)
```

---

## 2. OPAMP (Операционен усилвател) - Усилва сигнали

### Какво прави?
OPAMP **усилва** слаби сигнали или ги буферира.

### Къде се използва?
- Усилване на сигнал от микрофон
- Буфер за ADC
- Филтри за аудио

### Пример: Включи OPAMP канал 0

```python
from machine import OPAMP

# Създай OPAMP на канал 0
op = OPAMP(0, mode=OPAMP.LOW_POWER)

op.start()  # Включи усилвателя

print("OPAMP активен:", op.status())  # True

# OPAMP работи в хардуера автоматично!
# Входовете/изходите са фиксирани според схемата

# Когато приключиш:
# op.stop()
```

**Важно:** Кои пинове са вход/изход зависи от хардуера (виж схемата).

---

## 3. I2C Target (Slave режим) - Стани I2C устройство

### Какво прави?
Вместо да **четеш** от I2C сензори, **ставаш сам сензор**!
Друго устройство (Master) може да чете/пише данни от теб.

### Къде се използва?
- Симулиране на I2C сензор
- Комуникация между два микроконтролера
- Custom I2C периферия

### Пример 1: Прост регистров файл

```python
from machine import I2CTarget

# Създай 64-байтов буфер (като регистри в сензор)
mem = bytearray(64)
mem[0] = 0x42  # Регистър 0 = версия
mem[1] = 0x55  # Регистър 1 = статус

# Стани I2C Target с адрес 0x55
target = I2CTarget(
    1,              # I2C1
    addr=0x55,      # Твоят адрес
    mem=mem,        # Автоматичен буфер
    mem_addrsize=8  # 8-битов адрес
)

# Callback при четене/писане
def on_event(t):
    flags = t.irq().flags()
    if flags & I2CTarget.IRQ_END_WRITE:
        print("Master записа в регистър", t.memaddr)
        print("Нова стойност:", mem[t.memaddr])
    if flags & I2CTarget.IRQ_END_READ:
        print("Master прочете регистър", t.memaddr)

target.irq(on_event)

print("I2C Target готов на адрес 0x55")
```

### Пример 2: Ръчен режим

```python
from machine import I2CTarget

target = I2CTarget(1, addr=0x42)

def on_write(t):
    buf = bytearray(10)
    n = t.readinto(buf)
    print("Получих", n, "байта:", buf[:n])

def on_read(t):
    t.write(b'Hello!')

target.irq(on_write, trigger=I2CTarget.IRQ_END_WRITE)
target.irq(on_read, trigger=I2CTarget.IRQ_READ_REQ)
```

---

## Обобщение

| Модул | Какво прави | Кога да го ползваш |
|-------|-------------|-------------------|
| **Comparator** | Сравнява напрежения | Детектори, прагове |
| **OPAMP** | Усилва сигнали | Слаби сензори, аудио |
| **I2CTarget** | Ставаш I2C устройство | Комуникация, симулация |

---

## Следващи стъпки

1. Тествай примерите на реална платка
2. Свържи сензори към компаратора
3. Използвай друг микроконтролер като I2C Master

🎉 **Готово! Успех!**

