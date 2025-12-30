# Етап 1: RX-DTC (Software Buffering) за I2C Slave
## Дата: 2025-12-30

## Цел
Намаляване на per-byte ISR wakeups при I2C slave RX когато има `mem != None`.
Вместо N прекъсвания за N байта → само 2:
1. Едно за първия байт (register pointer)
2. Едно на STOP (или TX преход при combined transaction)

## Направени промени

### 1. ra_i2c_slave.h
- `RA_I2C_DTC_RX_BUF_SIZE` define (128 байта)
- Нови полета в `ra_i2c_slave_obj_t`:
  - `bool use_dtc_rx` - флаг за software RX buffering
  - `uint8_t rx_buf[128]` - статичен RX буфер
  - `uint16_t rx_buf_count` - брой байтове
  - `bool rx_first_byte_received` - флаг за първия payload
- Нови функции: `ra_i2c_slave_enable_dtc_rx()`, `ra_i2c_slave_flush_rx()`

### 2. ra_i2c_slave.c
- `ra_i2c_slave_irq_enable_ex()` - с `enable_rxi_cpu` параметър
- `ra_i2c_slave_init()` - инициализация на новите полета
- `ra_i2c_slave_enable_dtc_rx()` - имплементация
- `ra_i2c_slave_flush_rx()` - връща буфера и нулира
- `ra_i2c_slave_rxi_handler()` - при use_dtc_rx: първи байт→callback, останали→buffer
- `ra_i2c_slave_txi_handler()` - flush при RX→TX преход (combined transaction)
- `ra_i2c_slave_eri_handler()` - flush при STOP, добавя RX_READY към events

### 3. machine_i2c_target.c
- `#include <string.h>` за memcpy
- `mp_machine_i2c_target_make_new()` - активира DTC-RX при mem != None
- `mp_machine_i2c_target_read_bytes()` - чете от буфера при DTC режим

## Поведение при mem != None:
```
Master WRITE: [ADDR+W] [REG] [D0] [D1] ... [Dn] [STOP]
ISR calls:       1       1    0    0  ...   0     1
```
Вместо N+2 прекъсвания → само 3!

## Combined Transaction:
При WRITE(reg) + repeated START + READ: автоматичен flush преди TX.

## Следващи стъпки:
- Хардуерен DTC backend (пълно елиминиране на CPU за payload)
- TX-DTC за read операции

