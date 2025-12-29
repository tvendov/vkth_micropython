# RA4M1 I2CTarget – Анализ и план за имплементация

## Обзор

Този документ описва как да се добави поддръжка за `machine.I2CTarget` в MicroPython порта за Renesas RA4M1, следвайки официалното API от [документацията](https://docs.micropython.org/en/latest/library/machine.I2CTarget.html).

---

## 1. Какво предлага RA4M1 хардуерът

### IIC модули (IIC0/IIC1)

В **slave режим** хардуерът автоматично:
- Разпознава адреса на слейва по шината
- Генерира ACK/NACK
- Генерира прекъсвания за:
  - START / STOP condition
  - Address match
  - RX (байт получен)
  - TX (буферът е празен – иска следващ байт)
  - Error / NACK от master

**Резултат:** CPU се буди само при събития, няма нужда от polling.

### Ключови регистри за Slave режим

| Регистър | Описание | Типична стойност |
|----------|----------|------------------|
| `ICSER` | Slave address enable | `0x01` (SAR0 enable) |
| `SARL0` | Slave address low | `addr << 1` |
| `SARU0` | Slave address upper | `0x00` (7-bit mode) |
| `ICMR3.ACKWP/ACKBT` | ACK control | Auto ACK |
| `ICIER` | Interrupt enable | RIE, TIE, SPIE, NAKIE |

---

## 2. MicroPython I2CTarget архитектура

```
┌─────────────────────────────────────────────────────────────────────┐
│                    extmod/machine_i2c_target.c                       │
│                    (Общ код за всички портове)                       │
├─────────────────────────────────────────────────────────────────────┤
│  • State machine (IDLE, READING, WRITING, MEM_ADDR_SELECT)          │
│  • Memory buffer handling (автоматично за mem=bytearray)            │
│  • IRQ dispatching (I2CTarget.irq())                                │
│  • Python bindings (.readinto(), .write(), .memaddr)                │
├─────────────────────────────────────────────────────────────────────┤
│                              ▼                                       │
│            #include MICROPY_PY_MACHINE_I2C_TARGET_INCLUDEFILE        │
│                              ▼                                       │
├─────────────────────────────────────────────────────────────────────┤
│                ports/renesas-ra/machine_i2c_target.c                 │
│                (Порт-специфична имплементация)                       │
├─────────────────────────────────────────────────────────────────────┤
│  Портът трябва да предостави:                                       │
│  • machine_i2c_target_obj_t struct                                  │
│  • mp_machine_i2c_target_make_new()                                 │
│  • mp_machine_i2c_target_print()                                    │
│  • mp_machine_i2c_target_deinit()                                   │
│  • mp_machine_i2c_target_get_index()                                │
│  • mp_machine_i2c_target_read_bytes()                               │
│  • mp_machine_i2c_target_write_bytes()                              │
│  • mp_machine_i2c_target_irq_config()                               │
│  • mp_machine_i2c_target_event_callback()                           │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. API и CPU ангажимент

### Основни методи

| Метод/Атрибут | Описание | CPU ангажимент |
|---------------|----------|----------------|
| `I2CTarget(id, addr, mem=buf)` | Създава target с автоматичен memory buffer | **Минимален** |
| `.readinto(buf)` | Чете pending байтове | Само при извикване |
| `.write(buf)` | Подготвя данни за master | Само при извикване |
| `.irq(handler, trigger)` | Регистрира callback | **1× на транзакция** |
| `.memaddr` | Последен адрес в буфера | Read-only |

### IRQ типове

| Trigger | Hard IRQ? | Кога се вика | CPU ангажимент |
|---------|-----------|--------------|----------------|
| `IRQ_END_READ` | Soft OK | Master завърши четене | **1× на транзакция** |
| `IRQ_END_WRITE` | Soft OK | Master завърши писане | **1× на транзакция** |
| `IRQ_READ_REQ` | **Hard** | Master иска байт | Всеки байт (ако няма mem) |
| `IRQ_WRITE_REQ` | **Hard** | Master изпрати байт | Всеки байт (ако няма mem) |

---

## 4. Low-CPU режим с `mem=bytearray`

Когато се използва `mem=bytearray(N)`:

```python
mem = bytearray(64)
target = I2CTarget(1, addr=0x42, mem=mem, mem_addrsize=8)
target.irq(on_stop, trigger=I2CTarget.IRQ_END_READ | I2CTarget.IRQ_END_WRITE)
```

**Какво се случва:**
1. Master пише: `[addr_byte] [data0] [data1] ...`
2. Хардуерът слага `data0` в `mem[addr_byte]`, `data1` в `mem[addr_byte+1]`, ...
3. **CPU се буди САМО при STOP** – callback `on_stop()` се изпълнява

**Резултат:** CPU ангажимент = **1 прекъсване на транзакция**, не на байт!

---

## 5. Конфигурация за RA4M1

### Добавки в mpconfigport.h

```c
#define MICROPY_PY_MACHINE_I2C_TARGET           (1)
#define MICROPY_PY_MACHINE_I2C_TARGET_INCLUDEFILE "ports/renesas-ra/machine_i2c_target.c"
#define MICROPY_PY_MACHINE_I2C_TARGET_MAX       (2)  // IIC0 + IIC1
#define MICROPY_PY_MACHINE_I2C_TARGET_HARD_IRQ  (1)
```

### Пинове на RA4M1 CLICKER

| IIC канал | SCL | SDA | Бележка |
|-----------|-----|-----|---------|
| IIC0 | P400 | P401 | mikroBUS |
| IIC1 | P100 | P101 | Свободни |

---

## 6. ISR mapping

| RA4M1 ISR | extmod функция |
|-----------|----------------|
| `iic_slave_rxi_isr` | `machine_i2c_target_data_write_request()` |
| `iic_slave_txi_isr` | `machine_i2c_target_data_read_request()` |
| `iic_slave_eri_isr` | Error handling |
| STOP detection | `machine_i2c_target_data_stop()` |

---

## 7. Файлове за имплементация

| Файл | Действие | Описание |
|------|----------|----------|
| `ports/renesas-ra/machine_i2c_target.c` | **Нов** | Порт-специфичен MicroPython binding |
| `ports/renesas-ra/ra/ra_i2c_slave.c` | **Нов** | Low-level IIC slave driver |
| `ports/renesas-ra/ra/ra_i2c_slave.h` | **Нов** | Header файл |
| `ports/renesas-ra/mpconfigport.h` | **Промяна** | Добави MICROPY_PY_MACHINE_I2C_TARGET |
| `ports/renesas-ra/Makefile` | **Промяна** | Добави новите .c файлове |

---

## 8. Пример за използване

```python
from machine import I2CTarget
import time

# 64-байтов регистров файл
mem = bytearray(64)
mem[0] = 0x42  # Предварително зареди стойности

# I2C Target на IIC1, адрес 0x55
target = I2CTarget(1, addr=0x55, mem=mem, mem_addrsize=8)

def on_transfer(t):
    flags = t.irq().flags()
    if flags & I2CTarget.IRQ_END_WRITE:
        print("Master wrote at addr", t.memaddr)
    if flags & I2CTarget.IRQ_END_READ:
        print("Master read from addr", t.memaddr)

target.irq(on_transfer)

# CPU може да спи – хардуерът обработва I2C трансферите!
while True:
    time.sleep(1)
    print("mem:", list(mem[:8]))
```

---

## 9. План за имплементация

### Фаза 1: Low-level IIC Slave Driver

**Файлове:** `ra/ra_i2c_slave.c`, `ra/ra_i2c_slave.h`

```c
// Основни функции
void ra_i2c_slave_init(R_IIC0_Type *inst, uint16_t addr, bool addr_10bit);
void ra_i2c_slave_deinit(R_IIC0_Type *inst);
size_t ra_i2c_slave_read(R_IIC0_Type *inst, uint8_t *buf, size_t len);
size_t ra_i2c_slave_write(R_IIC0_Type *inst, const uint8_t *buf, size_t len);

// ISR handlers
void iic_slave_rxi_isr(void);
void iic_slave_txi_isr(void);
void iic_slave_tei_isr(void);
void iic_slave_eri_isr(void);
```

### Фаза 2: MicroPython Binding

**Файл:** `machine_i2c_target.c`

Имплементира функциите, изисквани от `extmod/machine_i2c_target.c`:
- `mp_machine_i2c_target_make_new()`
- `mp_machine_i2c_target_read_bytes()`
- `mp_machine_i2c_target_write_bytes()`
- `mp_machine_i2c_target_deinit()`
- и други

### Фаза 3: Тестване

1. Използвай друг микроконтролер (ESP32, RP2040) като I2C Master
2. Тествай read/write на регистри
3. Провери IRQ callbacks
4. Измери CPU ангажимент

---

## 10. Референтни имплементации

Други портове с I2CTarget поддръжка:

| Порт | Файл | Бележки |
|------|------|---------|
| **RP2** | `ports/rp2/machine_i2c_target.c` | Добра референция, чист код |
| **ESP32** | `ports/esp32/machine_i2c_target.c` | Използва ESP-IDF I2C slave |
| **MIMXRT** | `ports/mimxrt/machine_i2c_target.c` | NXP LPI2C slave |
| **SAMD** | `ports/samd/machine_i2c_target.c` | SERCOM I2C slave |
| **STM32** | `ports/stm32/machine_i2c_target.c` | HAL I2C slave |
| **Alif** | `ports/alif/machine_i2c_target.c` | ARM-based |

---

## 11. Сравнение: С и без DTC

### Вариант A: Само IRQ (препоръчителен за начало)

```
Master TX → IIC RXI IRQ → ISR чете ICDRR → записва в mem[]
                                          ↓
                              STOP IRQ → Python callback
```

**CPU:** ~10-20 цикъла на байт в ISR, 1 Python callback на транзакция

### Вариант B: С DTC (по-късно подобрение)

```
Master TX → IIC RXI → DTC трансфер → mem[] автоматично
                                     ↓
                         STOP IRQ → Python callback
```

**CPU:** 0 цикъла на байт, 1 Python callback на транзакция

**Бележка:** Вариант B изисква допълнителна DTC конфигурация и е по-сложен.
Препоръчвам да започнем с Вариант A, който вече е достатъчно ефективен.

---

## 12. Статус

- [ ] Фаза 1: ra_i2c_slave.c/h
- [ ] Фаза 2: machine_i2c_target.c
- [ ] Фаза 3: mpconfigport.h промени
- [ ] Фаза 4: Makefile промени
- [ ] Фаза 5: Тестване
- [ ] Фаза 6: Документация

---

*Създаден: 2025-12-29*
*Автор: Augment Agent*

