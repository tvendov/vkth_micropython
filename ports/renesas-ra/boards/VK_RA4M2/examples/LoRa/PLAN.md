# План за LoRa поддръжка на VK_RA4M2 — EU868 + SenseCAP

**Хардуер:** Seeed **Wio-SX1262 for XIAO** (Header Board вариант, с 14-pin XIAO
header — НЕ Kit-вариантът с B2B конектор, който се поставя върху XIAO ESP32-S3).
**Регион:** Европа → **EU868** (863–870 MHz, ETSI EN 300 220, duty cycle ≤ 1%).
**Сцена:** мониторинг през **SenseCAP** gateway → SenseCAP Cloud.

**PHY драйвер:** поддържат се **две паралелни Python библиотеки** — потребителят
избира коя да импортира; могат да съществуват едновременно:

| Опция | Източник | Импорт | Кога е удобна |
|-------|----------|--------|---------------|
| **A (препоръчителна)** | `lora-sx126x` от `micropython-lib` (`mpremote mip install lora-sx126x`) | `from lora import SX1262` | Чист API, директна поддръжка на `dio2_rf_sw` и `dio3_tcxo_millivolts`, поддържан от MicroPython проекта |
| **B (алтернатива)** | https://github.com/ehong-tl/micropySX126X | `from sx1262 import SX1262` | Разширен `begin()` (FSK, IQ inversion, current limit); полезен при портиране от съществуващ код |

Двата драйвера използват **един и същ pin map** и могат да живеят
едновременно в `/flash/lib/` (различни namespace-и).

## 0. Критично уточнение: SenseCAP = LoRaWAN

SenseCAP gateway-ите (M2 indoor / M4 outdoor) са **LoRaWAN** концентратори.
Те не препредават случайни LoRa пакети — само LoRaWAN frame-ове (PHY +
LoRaWAN MAC: `MHDR`, `DevAddr`, `FCnt`, MIC по AES-CMAC, payload криптиран
по AES-CTR с `AppSKey`/`NwkSKey`).

**Никой от двата PHY драйвера не включва LoRaWAN MAC.** Затова планът е
разделен на **два трака**:

| Трак | Цел                                  | Стек                           | SenseCAP visibility |
|------|--------------------------------------|--------------------------------|---------------------|
| **A** — raw LoRa | bring-up, board-to-board, валидация на SPI/IRQ | `lora-sx126x` или `micropySX126X` | ❌ (gateway-ът ги отхвърля) |
| **B** — LoRaWAN  | Class-A end-node, виден в SenseCAP Cloud | избран PHY + минимален LoRaWAN MAC на Python | ✅ |

Трак A е предпоставка за Трак B (без работещ PHY няма как да проверим MAC-а).

## 1. Хардуер: Wio-SX1262 for XIAO (Header Board)

- Чип: **Semtech SX1262**, 868 MHz EU вариант.
- **TCXO:** да, **1.8 V** (захранван през `DIO3` на SX1262). В
  `micropySX126X.begin(...)` подаваме `tcxoVoltage=1.8`.
- **DIO2 като RF превключвател:** да — антенният switch се управлява от
  `DIO2` (нужно е `setDio2AsRfSwitchCtrl(True)` при init).
- Антена: U.FL съединител (или SMA при някои варианти) → **антена за
  868 MHz, 50 Ω**.
- Захранване: 3V3, peak ~120 mA при +22 dBm. VK_RA4M2 LDO-то го издържа.

### XIAO header → SX1262 (Header Board)

| XIAO pin | Сигнал към SX1262 | Посока |
|----------|-------------------|--------|
| D1       | DIO1 (IRQ)        | вх. за MCU |
| D2       | RESET             | изх. от MCU |
| D3       | BUSY              | вх. за MCU |
| D4       | NSS (CS)          | изх. от MCU |
| D5       | DIO2 / RXEN (RF switch) | управлява се от SX1262, MCU не пипа |
| D8       | SCK               | изх. от MCU |
| D9       | MISO              | вх. за MCU |
| D10      | MOSI              | изх. от MCU |
| 3V3, GND | захранване        | —      |
| D0, D6, D7 | свободни        | —      |

## 2. Pin map: XIAO RA4M1 равностойност → VK_RA4M2

Използваме **същите чип-пинове на RA4M1**, които Seeed XIAO RA4M1 извежда
на XIAO header-а. RA4M1 и RA4M2 споделят port mux-а в долните портове,
така че всички тези пинове съществуват и на VK_RA4M2 (виж `pins.csv`).
SPI каналът съвпада с **SPI(1)** на VK_RA4M2 (P111/P110/P109).

CS пинът се дърпа на Pin.OUT, а не на хардуерния SSL — драйверът чете
`BUSY` между транзакциите.

| Wio-SX1262 | XIAO label | RA4M1/RA4M2 пин | Конфигурация на VK_RA4M2  |
|------------|------------|------------------|---------------------------|
| SCK        | D8 / SCK   | **P111**         | SPI(1) RSPCK              |
| MOSI       | D10 / MOSI | **P109**         | SPI(1) MOSI               |
| MISO       | D9 / MISO  | **P110**         | SPI(1) MISO               |
| NSS (CS)   | D4 / SDA   | **P006**         | `Pin.OUT`, init `1`       |
| RESET      | D2 / A2    | **P001**         | `Pin.OUT`, init `1`       |
| BUSY       | D3 / A3    | **P002**         | `Pin.IN`                  |
| DIO1 (IRQ) | D1 / A1    | **P000**         | `Pin.IN` + `IRQ_RISING` (IRQ0 канал) |
| DIO2/RXEN  | D5 / SCL   | P100             | **не свързваме** — управлява се от SX1262 |
| 3V3        | 3V3        | 3V3 рейл         | стабилен (≥150 mA peak)   |
| GND        | GND        | GND              | обща маса                 |

Бележки:
- **IRQ-capable пинове:** P000=IRQ0, P001=IRQ1, P002=IRQ2 — DIO1 на P000
  използва EXTI канал IRQ0 без конфликти.
- **ADC отнемане:** P000–P002 и P006 се използват като ADC входове в
  стандартния VK_RA4M2 пример. Докато LoRa тече, тези ADC канали не са
  достъпни.
- **P100 е и SPI(0) MISO** — затова не свързваме XIAO D5 към VK_RA4M2; и
  без това DIO2 е чисто вътрешен RF-switch сигнал на модула.
- **VK_RA4M2 = QFP100** (повече пинове от XIAO RA4M1 LQFP64), така че
  XIAO header-ът никога не може да се запоява директно — свързваме с
  жички (Dupont) към изведените пинове на VK_RA4M2.

## 3. Трак A — Raw LoRa (micropySX126X)

### A.1. Инсталация на драйвера

Вариант A (препоръчителен) — официалният `lora-sx126x`:
```
mpremote mip install lora-sx126x
```
Smoke import: `from lora import SX1262`.

Вариант B (алтернатива) — `ehong-tl/micropySX126X`:
1. Сваляне на `lib/_sx126x.py`, `lib/sx126x.py`, `lib/sx1262.py` от
   https://github.com/ehong-tl/micropySX126X.
2. Копиране в `/flash/lib` на VK_RA4M2 (или `examples/LoRa/lib/`).
3. Smoke import: `from sx1262 import SX1262`.

И двата варианта могат да съществуват едновременно — namespace-ите им се
различават (`lora.SX1262` срещу `sx1262.SX1262`).

### A.2. Bring-up скриптове (в `examples/LoRa/`)
- `01_id_probe.py` — reset → четене на DIO1/BUSY нива → четене на статус
  регистър → проверка че `BUSY` пада в рамките на 1 ms след reset.
- `02_lora_tx.py` — EU868 канал **868.1 MHz**, SF7, BW 125 kHz, CR 4/5,
  preamble 8, sync **0x12** (private), power **+14 dBm**, TCXO 1.8 V.
  Изпраща `b"hello %d" % i` веднъж в секунда. **Респектираме duty cycle
  1%** с `sleep_ms` ≥ 1 s между предавания.
- `03_lora_rx.py` — приемник на същия канал, печата RSSI и SNR.
- `04_lora_rx_irq.py` — non-blocking RX през `DIO1` IRQ + queue на
  пакетите към main loop.
- `05_ping_pong.py` — двупосочна латентност между два VK_RA4M2 борда (или
  VK_RA4M2 ↔ XIAO ESP32-S3 + Wio).

### A.3. Acceptance Trail A
- [ ] Reset последователност: BUSY pulse в първите 1 ms след падащ фронт.
- [ ] `setDio2AsRfSwitchCtrl(True)` без грешка → видим RF изход на спектрален
  анализатор при `setFrequency(868.1)`.
- [ ] TX→RX между два борда на 868.1 MHz: ≥1000 пакета без CRC грешка
  на разстояние 5 m в стая.
- [ ] DIO1 IRQ хваща ≥99% RX_DONE събития (1000 пакета).

## 4. Трак B — LoRaWAN end-node за SenseCAP

### B.1. Защо няма готова Python библиотека за нашата конфигурация
- Официалната `micropython-lib/lora` (Espressif sponsored) e **само PHY**;
  LoRaWAN MAC не е включен.
- `Pycom LoRa` стек е заключен за Pycom борд.
- `LoRaMac-node` (Semtech референтен C стек) изисква C-биндинги — тежко за
  скоп на този проект.

### B.2. Реалистични опции (по нарастваща сложност)

| # | Подход | Усилие | Риск |
|---|--------|--------|------|
| B-1 | **Минимален Class-A LoRaWAN на Python** върху `micropySX126X` (OTAA join + unconfirmed uplink + RX1/RX2) | ~3–5 дни | средно — AES-CMAC и frame counter persistence |
| B-2 | Порт на `tinyLoRa` (Adafruit CircuitPython, SX127x) към SX1262 | ~3–4 дни | средно — пренаписване на PHY част |
| B-3 | Порт на `LoRaMac-node` като MicroPython C-модул | ~7–10 дни | високо — много glue, тестове |

**Препоръка:** B-1. AES-128 е наличен през `cryptolib` (вградено в
MicroPython). Frame counter може да се пази в Data Flash (8 KB на
VK_RA4M2 — повече от достатъчно).

### B.3. Регистрация в SenseCAP Cloud
1. SenseCAP Console → **Devices → Add Device** → "Custom LoRaWAN Device".
2. Копирай `DevEUI` (8 байта), `JoinEUI` (понякога `AppEUI`, 8 байта),
   `AppKey` (16 байта).
3. Frequency Plan: **EU868**, Class-A, OTAA, LoRaWAN spec **1.0.3** (по-лесно
   от 1.1; SenseCAP поддържа и двете).
4. Регистрирай SenseCAP gateway-а под същия акаунт; уверявай се че е online.

### B.4. Минимален LoRaWAN MAC — структура

```
boards/VK_RA4M2/examples/LoRa/lorawan/
├── __init__.py
├── crypto.py        # AES-CMAC, AES-CTR врапери около cryptolib
├── frame.py         # MHDR/MIC/encrypt/decrypt на uplink/join/downlink
├── eu868.py         # канална таблица (8 канала + RX2 869.525 MHz SF12)
├── mac.py           # MAC автомат: idle → tx → rx1 → rx2 → idle
├── radio.py         # обвивка над micropySX126X (frequency, SF, BW, sync)
└── store.py         # persistence на DevAddr/FCntUp/FCntDown в data flash
```

Минимална функционалност за първо включване в SenseCAP:
- OTAA Join Request → Join Accept (RX1 след 5 s, RX2 след 6 s, sync `0x34`
  за LoRaWAN public).
- Unconfirmed Data Up (`MType=0x40`) с `FPort=1`, payload до 51 байта (DR0
  EU868).
- Игнор на downlink-овете (за първа версия).

Sync word **критично**: за LoRaWAN е `0x34` (`SX126X_SYNC_WORD_PUBLIC`).
`micropySX126X` по подразбиране е `SX126X_SYNC_WORD_PRIVATE = 0x12` — трябва
да се подаде `syncWord=0x34` в `begin()`.

### B.5. Acceptance Trail B
- [ ] OTAA Join успешен (видим в SenseCAP Console: "Join Accepted").
- [ ] 10 поредни uplink-а на FPort=1 видими в SenseCAP Cloud → Device
      → Last Data, FCnt се увеличава.
- [ ] RSSI на gateway-а ≥ −115 dBm в LoS на 50 m.
- [ ] Persistence: след reset на VK_RA4M2 не правим повторен Join, а
      продължаваме със стария DevAddr и съхранения FCntUp.
- [ ] Duty cycle 1% за SF7 (~36 ms ToA): автоматичен gap ≥ 3.6 s.

## 5. Етапи и времеви бюджет

| Фаза | Описание                                                       | Часове |
|------|----------------------------------------------------------------|--------|
| 0    | Поръчка/проверка на Wio-SX1262 Header Board + 868 MHz антена   |  1     |
| 1    | Свързване по таблицата в §2, smoke import                       |  1     |
| A.1  | `01_id_probe.py` — basic SPI/reset                              |  1     |
| A.2  | `02..03` TX+RX на два борда (868.1 MHz, SF7)                    |  2     |
| A.3  | `04` IRQ-based RX, `05` ping-pong + acceptance                  |  2     |
| B.1  | Скелет на `lorawan/` пакета + AES обвивки                       |  4     |
| B.2  | Join Request/Accept (OTAA), MIC валидация                       |  6     |
| B.3  | Unconfirmed uplink + EU868 канална ротация                      |  4     |
| B.4  | Persistence в Data Flash, RX1 windows                           |  3     |
| B.5  | Регистрация в SenseCAP, end-to-end приемане                     |  2     |
| 6    | Документация (BG) в `docs/renesas-ra/tutorial/`                 |  3     |
| **Σ**| —                                                              | **29** |

Без C промени в порта; цялата работа е Python + хардуерен bring-up.

## 6. Рискове

| Риск | Митигация |
|------|-----------|
| TCXO напрежение грешно (1.6 V вместо 1.8 V) → дрифт на честотата | Изричен `tcxoVoltage=1.8` във всеки пример |
| DIO2 RF switch не е активиран → отказва TX/RX над −10 dBm | `setDio2AsRfSwitchCtrl(True)` сразу след `reset()` |
| LoRaWAN sync word `0x12` (private) → пакети не достигат gateway | `syncWord=0x34` (`PUBLIC`) задължително за SenseCAP |
| EU868 duty cycle нарушение → ETSI несъответствие | На ниво MAC: per-канал last-airtime + блокиращ `sleep_ms` |
| Heap fragmentation при `import sx1262` | Frozen modules в Phase A.3 (`manifest.py` → `freeze(...)`) |
| Frame counter reset след reboot → SenseCAP refuse | Pers. в data flash след всеки uplink (RMW safe) |
| `cryptolib` AES-128 ECB не е вграден в build-а | Проверка в `mpconfigport.h`: `MICROPY_PY_UCRYPTOLIB = 1` |
| Конфликт P000..P002, P006 с ADC примери | Документация: при LoRa примерите тези ADC канали са изключени |

## 7. Препратки

- micropySX126X: https://github.com/ehong-tl/micropySX126X
- Wio-SX1262 Header Board pinout: https://github.com/meshtastic/firmware/issues/8409
- SX1262 datasheet (DIO2 RF switch, TCXO): Semtech DS.SX1261-2.W.APP rev 2.1
- LoRaWAN 1.0.3 specification, Regional Parameters EU868
- SenseCAP Console: https://sensecap.seeed.cc/
- ETSI EN 300 220-2 (EU868 SRD)
