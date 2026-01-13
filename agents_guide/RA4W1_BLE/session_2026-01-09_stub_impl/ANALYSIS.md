# Анализ на BLE имплементацията за RA4W1

**Дата:** 2026-01-09
**Файлове:** `ports/renesas-ra/ble/ra_ble.c`, `ra_ble.h`, `ra_ble_events.h`
**Общо редове код:** 564 (ra_ble.c), 82 (ra_ble.h), 102 (ra_ble_events.h)

---

## Executive Summary

| Метрика | Стойност |
|---------|----------|
| **Готовност за production** | 60% |
| **GAP функционалност** | ✅ 100% |
| **GATTS функционалност** | ⚠️ 70% (липсва DB reg, write data) |
| **GATTC функционалност** | ❌ 0% (не е имплементирано) |
| **Memory footprint** | ~82 bytes globals + event queue |
| **Критични липси** | GATT DB registration, write data copy |

---

## 1. Архитектура

### 1.1 Слоеве
```
┌─────────────────────────────────────────┐
│          MicroPython (modble_renesas)   │  ← Python API
├─────────────────────────────────────────┤
│              ra_ble.c / ra_ble.h        │  ← C Wrapper Layer
├─────────────────────────────────────────┤
│            ra_ble_events.c/.h           │  ← Event Queue (ISR-safe)
├─────────────────────────────────────────┤
│         FSP BLE Stack (libr_ble.a)      │  ← Renesas Compact Library
├─────────────────────────────────────────┤
│              BLE Hardware               │  ← RA4W1 Radio
└─────────────────────────────────────────┘
```

### 1.2 State Machine
```
OFF ──init()──> IDLE ──startAdv()──> ADVERTISING ──CONN_IND──> CONNECTED
 ^                ^                       │                        │
 │                │                       │                        │
 └──deinit()──────┴───────ADV_OFF─────────┴────DISCONN_IND─────────┘
```

---

## 2. Имплементирани функции

| Функция | FSP API | Статус | Бележки |
|---------|---------|--------|---------|
| `ra_ble_init()` | `R_BLE_Open`, `R_BLE_GAP_Init`, `R_BLE_GATTS_Init`, `R_BLE_GATTS_RegisterCb` | ✅ | Пълна |
| `ra_ble_deinit()` | `R_BLE_GAP_Terminate`, `R_BLE_Close` | ✅ | Пълна |
| `ra_ble_gap_start_advertising()` | `R_BLE_GAP_SetAdvParam`, `R_BLE_GAP_SetAdvSresData`, `R_BLE_GAP_StartAdv` | ✅ | + auto payload |
| `ra_ble_gap_stop_advertising()` | `R_BLE_GAP_StopAdv` | ✅ | Пълна |
| `ra_ble_gap_disconnect()` | `R_BLE_GAP_Disconnect` | ✅ | reason=0x13 |
| `ra_ble_gatts_notify()` | `R_BLE_GATTS_Notification` | ✅ | Пълна |
| `ra_ble_gatts_indicate()` | `R_BLE_GATTS_Indication` | ✅ | Пълна |
| `ra_ble_gap_set_device_name()` | Локално | ✅ | FSP няма API |
| `ra_ble_gap_get_device_name()` | Локално | ✅ | За adv builder |
| `ra_ble_gap_get_address()` | Локално | ⚠️ | TODO: FSP query |
| `ra_ble_process_events()` | `R_BLE_Execute` | ✅ | Main loop pump |

---

## 3. Callback обработка

### 3.1 GAP Callback (`ra_ble_gap_cb`)
| FSP Event | Действие | MicroPython Event |
|-----------|----------|-------------------|
| `BLE_GAP_EVENT_STACK_ON` | Set `g_ble_stack_on`, try rand addr | `BLE_EVT_STACK_READY` |
| `BLE_GAP_EVENT_ADV_ON` | State → ADVERTISING | `BLE_EVT_GAP_ADV_STARTED` |
| `BLE_GAP_EVENT_ADV_OFF` | State → IDLE | `BLE_EVT_GAP_ADV_STOPPED` |
| `BLE_GAP_EVENT_CONN_IND` | State → CONNECTED, extract conn_hdl | `BLE_EVT_GAP_CONNECTED` |
| `BLE_GAP_EVENT_DISCONN_IND` | State → IDLE, extract reason | `BLE_EVT_GAP_DISCONNECTED` |

### 3.2 GATTS Callback (`ra_ble_gatts_cb`)
| FSP Event | Действие | MicroPython Event |
|-----------|----------|-------------------|
| `BLE_GATTS_EVENT_WRITE_REQ` | Extract attr_hdl | `BLE_EVT_GATTS_WRITE` |
| `BLE_GATTS_EVENT_READ_REQ` | Extract attr_hdl | `BLE_EVT_GATTS_READ` |
| `BLE_GATTS_EVENT_HDL_VAL_CNF` | Indication ACK | `BLE_EVT_GATTS_INDICATE_COMPLETE` |

---

## 4. Advertising Payload

### 4.1 Default формат (auto-build)
```
Offset  Bytes   Description
0       1       Length (2)
1       1       AD Type: Flags (0x01)
2       1       Flags value: 0x06 (LE General Discoverable | BR/EDR Not Supported)
3       1       Length (1 + name_len)
4       1       AD Type: Complete Local Name (0x09) или Shortened (0x08)
5+      N       Device name bytes
```

### 4.2 Ограничения
- Max adv payload: 31 bytes
- Max device name: 29 bytes (31 - 2 за flags structure)
- Ако името не се побира, се използва Shortened Local Name (0x08)

---

## 5. Потенциални проблеми и рискове

### 5.1 ⚠️ GATTS Write Event - липсват данни
```c
// Текущ код (линия 153-156):
ra_ble_event_push(BLE_EVT_GATTS_WRITE,
                  p_event_data->conn_hdl,
                  write_evt->attr_hdl,
                  NULL, 0);  // ← Данните НЕ се подават!
```
**Проблем:** При WRITE_REQ не се копират написаните данни в event queue.  
**Решение:** Трябва да се извика `R_BLE_GATTS_GetAttr()` или да се парснат директно от event data.

### 5.2 ⚠️ Липсва GATT DB registration
Текущият код не регистрира GATT services/characteristics. Необходимо е:
- `R_BLE_GATTS_SetDbInst()` или еквивалент
- Или генериране на GATT DB от e2studio

### 5.3 ⚠️ Само една връзка
State machine поддържа само едно състояние (CONNECTED). При множество връзки ще има проблеми.

### 5.4 ⚠️ Timeout при wait_stack_on
```c
if (!ra_ble_wait_stack_on(50000)) {
    return RA_BLE_ERR_TIMEOUT;
}
```
Блокираща операция с фиксиран timeout. При бавен FSP stack може да блокира.

### 5.5 ⚠️ GATTS callback регистрация - non-fatal error
```c
st = R_BLE_GATTS_RegisterCb(ra_ble_gatts_cb, 1);
if (st != BLE_SUCCESS) {
    // Non-fatal: continue without GATTS callback
}
```
Ако FSP версията не поддържа, GATTS events няма да се получават.

---

## 6. Препоръки за подобрения

### 6.1 Критични (блокиращи реална работа)

| # | Проблем | Решение | Приоритет |
|---|---------|---------|-----------|
| 1 | Липсва GATT DB | Добави `R_BLE_GATTS_SetDbInst()` с минимална DB | 🔴 HIGH |
| 2 | WRITE данни не се копират | Използвай `R_BLE_GATTS_GetAttr()` в callback | 🔴 HIGH |
| 3 | Липсва connection handle tracking | Добави `g_ble_conn_hdl` за текущата връзка | 🟡 MEDIUM |

### 6.2 Желателни подобрения

| # | Подобрение | Описание |
|---|------------|----------|
| 1 | Non-blocking init | Направи `ra_ble_wait_stack_on` async |
| 2 | Multiple connections | Масив от connection handles |
| 3 | Scan support | Добави `R_BLE_GAP_StartScan` wrapper |
| 4 | Security/Pairing | `R_BLE_GAP_SetBondInfo`, `R_BLE_GAP_StartPairing` |
| 5 | MTU negotiation | `R_BLE_GATTC_ReqExMtu` wrapper |

---

## 7. Глобални променливи (memory usage)

| Променлива | Тип | Размер | Описание |
|------------|-----|--------|----------|
| `g_ble_state` | enum | 1 byte | Текущо състояние |
| `g_ble_open` | bool | 1 byte | FSP stack open flag |
| `g_ble_stack_on` | bool | 1 byte | Host stack ready |
| `g_ble_set_rand_addr_pending` | bool | 1 byte | Pending addr set |
| `g_ble_adv_hdl` | uint8_t | 1 byte | Advertising handle |
| `g_ble_own_addr_le` | uint8_t[6] | 6 bytes | Own BLE address |
| `g_ble_own_addr_valid` | bool | 1 byte | Address valid flag |
| `g_ble_device_name` | char[30] | 30 bytes | Device name |
| `g_ble_device_name_len` | uint8_t | 1 byte | Name length |
| `g_ble_default_adv_data` | uint8_t[31] | 31 bytes | Default adv payload |
| `g_ble_default_adv_data_len` | uint8_t | 1 byte | Payload length |
| `g_ble_cfg_static_random_addr_le` | const uint8_t[6] | 6 bytes | Compile-time addr |
| **Total** | | **~82 bytes** | + event queue |

---

## 8. FSP API използване

### 8.1 Използвани FSP функции
```c
// Core
R_BLE_Open()
R_BLE_Close()
R_BLE_Execute()

// GAP
R_BLE_GAP_Init(callback)
R_BLE_GAP_Terminate()
R_BLE_GAP_SetRandAddr(addr)
R_BLE_GAP_SetAdvParam(params)
R_BLE_GAP_SetAdvSresData(data)
R_BLE_GAP_StartAdv(hdl, duration, max_events)
R_BLE_GAP_StopAdv(hdl)
R_BLE_GAP_Disconnect(conn_hdl, reason)

// GATTS
R_BLE_GATTS_Init(cb_num)
R_BLE_GATTS_RegisterCb(callback, priority)
R_BLE_GATTS_Notification(conn_hdl, data)
R_BLE_GATTS_Indication(conn_hdl, data)
```

### 8.2 Неизползвани (потенциално нужни)
```c
R_BLE_GATTS_SetDbInst(db)           // ← КРИТИЧНО за GATT services
R_BLE_GATTS_GetAttr(conn, attr, val) // ← За четене на написани данни
R_BLE_GATTS_SetAttr(conn, attr, val) // ← За промяна на characteristic value
R_BLE_GAP_GetBdAddr(addr)           // ← За четене на реален адрес
```

---

## 9. Съвместимост с MicroPython BLE API

### 9.1 Mapping към стандартен bluetooth.BLE

| MicroPython API | ra_ble.c функция | Статус |
|-----------------|------------------|--------|
| `BLE.active(True)` | `ra_ble_init()` | ✅ |
| `BLE.active(False)` | `ra_ble_deinit()` | ✅ |
| `BLE.gap_advertise(interval, data)` | `ra_ble_gap_start_advertising()` | ✅ |
| `BLE.gap_advertise(None)` | `ra_ble_gap_stop_advertising()` | ✅ |
| `BLE.gap_disconnect(conn)` | `ra_ble_gap_disconnect()` | ✅ |
| `BLE.gatts_notify(conn, handle, data)` | `ra_ble_gatts_notify()` | ✅ |
| `BLE.gatts_indicate(conn, handle, data)` | `ra_ble_gatts_indicate()` | ✅ |
| `BLE.config(gap_name="X")` | `ra_ble_gap_set_device_name()` | ✅ |
| `BLE.config("mac")` | `ra_ble_gap_get_address()` | ⚠️ partial |
| `BLE.gatts_register_services()` | ❌ липсва | 🔴 нужно |
| `BLE.gatts_read(handle)` | ❌ липсва | 🟡 нужно |
| `BLE.gatts_write(handle, data)` | ❌ липсва | 🟡 нужно |
| `BLE.gap_scan()` | ❌ липсва | ⚪ optional |

---

## 10. Заключение

### Силни страни ✅
- Чиста архитектура с ясно разделение на слоеве
- ISR-safe event queue за callback → Python комуникация
- Пълна GAP advertising/connection поддръжка
- Automatic advertising payload с device name

### Слабости ⚠️
- Липсва GATT service registration (критично)
- WRITE event не подава данни (нужна доработка)
- Само single connection support
- Няма scan функционалност

### Препоръчани следващи стъпки
1. **Добави GATT DB** - минимална таблица с 1 service + 1 characteristic
2. **Fix WRITE data** - копирай данните в event queue
3. **Build test** - компилирай и провери за link errors
4. **Hardware test** - flash и тест с nRF Connect app

