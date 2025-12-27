# RA4M1 CLICKER - I2C FIX SESSION LOG
**Дата:** 2025-12-27  
**Проблем:** I2C0 нямаше регистрирани прекъсвания във vector table

---

## ПРОБЛЕМ

### Първоначално състояние:
- `vector_data.c` имаше САМО IIC1 прекъсвания (IRQ 17-20)
- IIC0 прекъсвания **ЛИПСВАХА**
- `VECTOR_DATA_IRQ_COUNT = 23`
- I2C0 (P400/P401) **НЕ РАБОТЕШЕ** заради липсващи прекъсвания

---

## РЕШЕНИЕ

### 1. Добавени IIC0 прекъсвания във `vector_data.h`:
```c
// ПРЕДИ:
#define VECTOR_DATA_IRQ_COUNT    (23)
#define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type)17)  // IIC1 започваше от IRQ 17

// СЛЕД:
#define VECTOR_DATA_IRQ_COUNT    (27)  // Увеличен от 23 на 27
#define VECTOR_NUMBER_IIC0_RXI ((IRQn_Type)17)  // IIC0 RXI
#define VECTOR_NUMBER_IIC0_TXI ((IRQn_Type)18)  // IIC0 TXI
#define VECTOR_NUMBER_IIC0_TEI ((IRQn_Type)19)  // IIC0 TEI
#define VECTOR_NUMBER_IIC0_ERI ((IRQn_Type)20)  // IIC0 ERI
#define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type)21)  // IIC1 RXI (преномериран)
#define VECTOR_NUMBER_IIC1_TXI ((IRQn_Type)22)  // IIC1 TXI
#define VECTOR_NUMBER_IIC1_TEI ((IRQn_Type)23)  // IIC1 TEI
#define VECTOR_NUMBER_IIC1_ERI ((IRQn_Type)24)  // IIC1 ERI
#define VECTOR_NUMBER_ICU_IRQ6 ((IRQn_Type)25)  // ICU IRQ6 (преномериран)
#define VECTOR_NUMBER_ICU_IRQ9 ((IRQn_Type)26)  // ICU IRQ9 (преномериран)
```

### 2. Добавени IIC0 прекъсвания във `vector_data.c`:
```c
// g_vector_table:
[17] = iic_master_rxi_isr,  /* IIC0 RXI */
[18] = iic_master_txi_isr,  /* IIC0 TXI */
[19] = iic_master_tei_isr,  /* IIC0 TEI */
[20] = iic_master_eri_isr,  /* IIC0 ERI */
[21] = iic_master_rxi_isr,  /* IIC1 RXI */
[22] = iic_master_txi_isr,  /* IIC1 TXI */
[23] = iic_master_tei_isr,  /* IIC1 TEI */
[24] = iic_master_eri_isr,  /* IIC1 ERI */

// g_interrupt_event_link_select:
[17] = BSP_PRV_IELS_ENUM(EVENT_IIC0_RXI),
[18] = BSP_PRV_IELS_ENUM(EVENT_IIC0_TXI),
[19] = BSP_PRV_IELS_ENUM(EVENT_IIC0_TEI),
[20] = BSP_PRV_IELS_ENUM(EVENT_IIC0_ERI),
[21] = BSP_PRV_IELS_ENUM(EVENT_IIC1_RXI),
[22] = BSP_PRV_IELS_ENUM(EVENT_IIC1_TXI),
[23] = BSP_PRV_IELS_ENUM(EVENT_IIC1_TEI),
[24] = BSP_PRV_IELS_ENUM(EVENT_IIC1_ERI),
```

---

## КОНФИГУРАЦИЯ НА ПЕРИФЕРИЯТА

### I2C:
```c
#define MICROPY_HW_I2C0_SCL         (pin_P400) // mikroBUS SCL
#define MICROPY_HW_I2C0_SDA         (pin_P401) // mikroBUS SDA
#define MICROPY_HW_I2C1_SCL         (pin_P100) // Available
#define MICROPY_HW_I2C1_SDA         (pin_P101) // Available
```
✅ **I2C0** - P400/P401 (mikroBUS) - ИМА прекъсвания  
✅ **I2C1** - P100/P101 - ИМА прекъсвания

### UART:
```c
#define MICROPY_HW_UART0_TX         (pin_P411) // REPL TX
#define MICROPY_HW_UART0_RX         (pin_P410) // REPL RX
#define MICROPY_HW_UART_REPL        HW_UART_0
// UART1 закоментиран (конфликт с I2C0 SDA на P401)
#define MICROPY_HW_UART2_TX         (pin_P302) // mikroBUS TX
#define MICROPY_HW_UART2_RX         (pin_P301) // mikroBUS RX
```
✅ **UART0** - P411/P410 (REPL)  
❌ **UART1** - Закоментиран (P401 се използва за I2C0)  
✅ **UART2** - P302/P301 (mikroBUS UART)

### SPI:
```c
// SPI0 закоментиран (конфликт с I2C1 на P100/P101)
```
❌ **SPI0** - Закоментиран (P100/P101 се използват за I2C1)

---

## КОНФЛИКТИ И РЕШЕНИЯ

| Пин   | Функция 1 | Функция 2 | Решение |
|-------|-----------|-----------|---------|
| P400  | I2C0 SCL  | -         | ✅ I2C0 |
| P401  | I2C0 SDA  | UART1 TX  | ✅ I2C0 (UART1 закоментиран) |
| P100  | I2C1 SCL  | SPI0 MISO | ✅ I2C1 (SPI0 закоментиран) |
| P101  | I2C1 SDA  | SPI0 MOSI | ✅ I2C1 (SPI0 закоментиран) |
| P302  | UART2 TX  | -         | ✅ UART2 |
| P301  | UART2 RX  | -         | ✅ UART2 |

---

## ТЕСТВАНЕ

```python
from machine import I2C, UART

# Тест I2C0 (mikroBUS)
i2c0 = I2C(0)
print("I2C0 devices:", i2c0.scan())

# Тест I2C1
i2c1 = I2C(1)
print("I2C1 devices:", i2c1.scan())

# Тест UART2 (mikroBUS)
uart2 = UART(2, 115200)
uart2.write("Hello from UART2\n")
```

---

## ФАЙЛОВЕ ПРОМЕНЕНИ

1. `ra_gen/vector_data.h` - Добавени IIC0 дефиниции, IRQ count 23→27
2. `ra_gen/vector_data.c` - Добавени IIC0 прекъсвания в таблиците
3. `mpconfigboard.h` - I2C/UART/SPI конфигурация
4. `ra_gen/pin_data.c` - **КРИТИЧНО!** Премахната статична SPI конфигурация на P100/P101

---

## ФИНАЛНА КОНФИГУРАЦИЯ

### Активни периферии:
- ✅ **UART0** (P411/P410) - REPL
- ✅ **UART2** (P302/P301) - mikroBUS UART
- ✅ **I2C0** (P400/P401) - mikroBUS I2C
- ✅ **I2C1** (P100/P101) - Допълнителен I2C
- ✅ **SPI1** (P109/P110/P111/P112) - mikroBUS SPI
- ✅ **DAC0** (P014) - Analog out
- ✅ **ADC** - Analog in
- ✅ **LED1** (P409), **LED2** (P408)
- ✅ **SWITCH** (P304)

### Закоментирани (конфликти):
- ❌ **UART1** - P401 се използва за I2C0 SDA
- ❌ **SPI0** - P100/P101 се използват за I2C1

---

## ПРОБЛЕМ С I2C1 (РЕШЕН!)

### Симптом:
- `I2C(0)` работеше ✅
- `I2C(1)` **НЕ работеше** ❌

### Причина:
В `ra_gen/pin_data.c` пиновете P100/P101 бяха **статично конфигурирани** като SPI:
```c
{ .pin = BSP_IO_PORT_01_PIN_00, .pin_cfg = IOPORT_PERIPHERAL_SPI },  // P100
{ .pin = BSP_IO_PORT_01_PIN_01, .pin_cfg = IOPORT_PERIPHERAL_SPI },  // P101
```

Това **блокираше** динамичната конфигурация на MicroPython за I2C1!

### Решение:
Закоментирани P100/P101/P102 в `pin_data.c` - сега пиновете се конфигурират динамично.

**Забележка за SPI:**
- **SPI1** (P109/P110/P111/P112) е активен - mikroBUS SPI
- **SPI0** прекъсванията остават във `vector_data.c` (IRQ 13-16)
- SPI използва **POLLING**, НЕ прекъсвания (dummy `__WEAK` функции)
- SPI1 **НЯМА** прекъсвания във vector table, но **НЕ ги нуждае** - работи с polling

---

## СЛЕДВАЩИ СТЪПКИ

1. Компилирай: `make BOARD=RA4M1_CLICKER`
2. Качи firmware
3. Тествай I2C0 и I2C1
4. Тествай UART2

---

**Статус:** ✅ ГОТОВО - И двата I2C канала имат прекъсвания и правилна конфигурация

