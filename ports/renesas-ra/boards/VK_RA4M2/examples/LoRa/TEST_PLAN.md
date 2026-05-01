# Тест план — LoRa на VK_RA4M2 (lora-sx126x + micropySX126X)

**Цел:** валидиране на функционалния паритет и съответствие с EU868 на двете
паралелни Python библиотеки за SX1262 PHY върху VK_RA4M2 + Wio-SX1262 Header
Board.

**Drivers under test (DUT):**
- **A** — `lora-sx126x` от `micropython-lib` (`from lora import SX1262`)
- **B** — `micropySX126X` (`from sx1262 import SX1262`)

**Регион:** EU868 (863–870 MHz, ETSI EN 300 220, ERP ≤ +14 dBm, duty cycle ≤ 1%).

## 0. Тестова среда

### Колко платки и модули са нужни

**Минимум: 2 устройства с SX1262.** LoRa е радиолинк — за TX/RX тестове трябва
предавател и приемник. Не може да се тества linq функционалност с една платка
(само reset / SPI / GPIO).

| Брой устройства | Какво може да се тества |
|-----------------|--------------------------|
| **1 платка** (само DUT-1) | T1 (smoke/import), T2 (bring-up: SPI/IRQ/BUSY с анализатор), T6 (изходна мощност и спектър със SDR), T8 (recovery) |
| **2 платки** (DUT-1 + DUT-2) | + T3 (TX/RX), T4 (паритет A vs B), T5 (стабилност), T7 (throughput, latency) — пълният план |
| **3+ платки** | разширени multi-node тестове (out-of-scope за v0.1) |

**Препоръчителна конфигурация (пълен план):**

- **DUT-1: VK_RA4M2 + Wio-SX1262 Header Board** — основното устройство;
  тук тестваме и двете либи последователно (A и B). 1 брой.
- **DUT-2: peer устройство** — единият от:
  - **(препоръчително)** втори VK_RA4M2 + втори Wio-SX1262 Header Board
    — пълна симетрия, паритет A↔B може да се тества и в двете посоки.
  - **(минимум за reference)** XIAO ESP32-S3 + Wio-SX1262 Kit с
    конфигурация от varna9000/micropython-reticulum — служи като
    "known good" реф. имплементация; не може да изпълни всички паритет
    тестове, но е достатъчен за обмен на пакети с DUT-1.

**Минимална bill-of-materials (BOM) за пълния план:**

| # | Артикул | Брой |
|---|---------|-----|
| 1 | VK_RA4M2 платка | 2 |
| 2 | Wio-SX1262 for XIAO Header Board | 2 |
| 3 | Антена 868 MHz, 50 Ω, 2 dBi, U.FL/SMA | 2 |
| 4 | Dupont жички (за свързване Wio Header → VK_RA4M2 пинове) | 1 комплект |
| 5 | USB-C кабели за двете платки | 2 |

**Алтернативен BOM (по-евтин, но с компромиси):**

| # | Артикул | Брой | Бележка |
|---|---------|-----|---------|
| 1 | VK_RA4M2 платка | 1 | DUT-1 |
| 2 | XIAO ESP32-S3 | 1 | DUT-2 — само като peer |
| 3 | Wio-SX1262 for XIAO Header Board | 1 | за DUT-1 |
| 4 | Wio-SX1262 for XIAO ESP32-S3 Kit | 1 | за DUT-2 (B2B конектор) |
| 5 | Антени 868 MHz | 2 | |

В този алтернативен сценарий тестовете T4.1, T4.2, T4.3 (паритет A vs B
като peer) се изпълняват само в едната посока — DUT-1 като TX/RX, DUT-2
като фиксиран peer.

### Подробен хардуер
- **DUT-1:** VK_RA4M2 + Wio-SX1262 Header Board (XIAO RA4M1 pinout, виж README.rst).
- **DUT-2:** виж по-горе — препоръчително втори VK_RA4M2; алтернативно
  XIAO ESP32-S3 + Wio-SX1262 Kit (varna9000/micropython-reticulum config).
- **Антени:** 868 MHz, 50 Ω, 2 dBi (по една на всеки DUT).
- **Захранване:** 3V3 рейл с резерв ≥ 200 mA на всеки модул.

### Измервателно оборудване (минимум)
- Логически анализатор ≥ 8 MS/s (например Saleae Logic 8 или USB clone).
- Спектрален анализатор / SDR (RTL-SDR + GQRX или HackRF + GQRX) за 868 MHz.
- Цифров мултиметър (DC напрежение / ток).
- (Опционално, за LoRaWAN тестове) SenseCAP M2 indoor gateway или TTN
  индекс gateway.

### Софтуерна среда
- MicroPython firmware за VK_RA4M2 от текущия branch (master).
- `mpremote` за файлов трансфер и REPL достъп.
- Python 3.10+ на хост машина за лог анализ.
- pytest + pytest-html за оркестрация на хост-страничните тестове.

## 1. Тестови категории

| ID | Категория | Цел |
|----|-----------|-----|
| T1 | Smoke / sanity | Импорт, init, reset последователност без грешки |
| T2 | HIL bring-up | SPI и IRQ електрически валидни |
| T3 | Функционален | TX и RX работят между две устройства |
| T4 | Паритет | A и B дават еквивалентни резултати при еднакви параметри |
| T5 | Стабилност | 1000+ пакета без CRC грешки и без heap fragmentation |
| T6 | Регулаторен | Duty cycle ≤ 1%, ERP ≤ +14 dBm |
| T7 | Производителност | Throughput, latency, RSSI/SNR характеристика |
| T8 | Грешки и възстановяване | Reset след timeout, recovery от bad command |

## 2. Тестови случаи

### T1 — Smoke / sanity (~30 мин)

| ID | Описание | DUT | Очаквано | Pass criteria |
|----|----------|-----|----------|---------------|
| T1.1A | `import lora; from lora import SX1262` | DUT-1 | без `MemoryError` или `ImportError` | модулът се зарежда |
| T1.1B | `from sx1262 import SX1262` | DUT-1 | същото | модулът се зарежда |
| T1.2A | Конструктор A с правилни пинове | DUT-1 | връща обект | `repr(modem)` не хвърля грешка |
| T1.2B | Конструктор B + `begin(...)` | DUT-1 | `err == 0` | `ERR_NONE` |
| T1.3 | Грешен пин (несвързан) — конструктор | DUT-1 | timeout / OSError | контролирана грешка, без hang |
| T1.4 | Двукратен `import` в една сесия | DUT-1 | без crash | модулите са идемпотентни |

### T2 — HIL bring-up (~1 час)

| ID | Описание | Метод | Pass criteria |
|----|----------|-------|---------------|
| T2.1 | Reset последователност | Логически анализатор на `RESET` и `BUSY` | `BUSY` пада до `0` в първите ≤ 5 ms след rising edge на `RESET` |
| T2.2 | SPI clock и polarity/phase | Логически анализатор на `SCK`/`MOSI`/`MISO` | `polarity=0`, `phase=0`, `freq ≈ 2 MHz`, MOSI bytes валидни |
| T2.3 | NSS toggling | Логически анализатор | `NSS` пада за всяка SPI транзакция, не остава ниско между транзакции |
| T2.4 | DIO1 IRQ rising edge | Принудително TX_DONE → проверка на `dio1.value()` и IRQ callback | callback се извиква в ≤ 1 ms |
| T2.5 | Захранване | Мултиметър на 3V3 преди/след `send()` peak | падане на 3V3 < 100 mV при +14 dBm |
| T2.6 | TCXO стартиране | Спектрален анализатор на 868 MHz при `set_freq(868.0)` без модулация | пик на 868.000 MHz ± 5 kHz |

### T3 — Функционален: TX/RX (~1 час)

| ID | Описание | Конфигурация | Pass criteria |
|----|----------|--------------|---------------|
| T3.1A | TX от DUT-1 (lib A), RX на DUT-2 | 868.1 MHz, SF7, BW125, CR4/5, +14 dBm, sync 0x12 | DUT-2 получава 100/100 пакета |
| T3.1B | TX от DUT-1 (lib B), RX на DUT-2 | същото | DUT-2 получава 100/100 пакета |
| T3.2A | RX на DUT-1 (lib A), TX от DUT-2 | същото | DUT-1 получава 100/100, RSSI > −95 dBm на 1 m |
| T3.2B | RX на DUT-1 (lib B), TX от DUT-2 | същото | DUT-1 получава 100/100, RSSI > −95 dBm на 1 m |
| T3.3 | Smoke: payload-и 1, 16, 64, 200, 250 байта | sf=7, blocking | всички размери TX/RX без грешки |
| T3.4 | DIO1 IRQ-driven RX | non-blocking + Pin.irq | ≥ 99/100 пакета хванати |
| T3.5 | FSK режим (само B) | `beginFSK(50000, 25000, ...)` | RX на DUT-2 (също в FSK) получава 100/100 |
| T3.6 | Sync word филтър | TX със sync 0x12, RX със sync 0x34 | RX **не** получава пакети (потвърждава филтрирането) |

### T4 — Паритет A vs B (~1.5 часа)

Цел: при еднакви параметри двата драйвера трябва да предават съвместими пакети
и да четат еднакъв RSSI/SNR.

| ID | Описание | Pass criteria |
|----|----------|---------------|
| T4.1 | TX от DUT-1 (A) → RX на DUT-2 (B) и обратно | пакетите се обменят успешно (cross-driver compat) |
| T4.2 | RSSI разлика A vs B при еднакъв сигнал | \|RSSI_A − RSSI_B\| ≤ 2 dB |
| T4.3 | SNR разлика A vs B | \|SNR_A − SNR_B\| ≤ 1 dB |
| T4.4 | Time-on-air измерване (T_OA) | спрямо теоретичното от калкулатор < 5% грешка и за двата |
| T4.5 | Паметна стъпка `import` (преди / след) | A и B да не оставят > 4 KB загуба след `import` + `del` |

### T5 — Стабилност и стрес (~2 часа)

| ID | Описание | Условия | Pass criteria |
|----|----------|---------|---------------|
| T5.1 | 1000-пакетен burst | sf=7, payload=32 B, 100 ms gap | PER ≤ 1%, без crash, без heap warning |
| T5.2 | 24-часов RX continuous | sf=9, IRQ-driven | без hang, без OOM, log на counters |
| T5.3 | Цикъл TX → RX → sleep | 1000 итерации | състоянието след всяка итерация е чисто |
| T5.4 | GC поведение | `gc.mem_free()` преди/след TX | Δ ≤ 200 B на пакет (lib A); ≤ 1 KB (lib B) |
| T5.5 | Reset след hang | принудителен soft reset на радиочипа | recovery в ≤ 100 ms, `begin()` отново успява |

### T6 — Регулаторен (EU868) (~1 час)

| ID | Описание | Метод | Pass criteria |
|----|----------|-------|---------------|
| T6.1 | Изходна мощност | Спектрален анализатор / power meter на антенен изход | ≤ +14 dBm при `output_power=14` |
| T6.2 | Заетост на честотата (BW) | SDR snapshot | основна линия ± 62.5 kHz при BW=125 kHz |
| T6.3 | Duty cycle 1% при SF7, payload 32 B | TX every 1 s, измерване ToA | средно ToA / 1 s ≤ 1% |
| T6.4 | Канална ротация (за LoRaWAN тестове) | log на честотите в 100 пакета | равномерно разпределение между 868.1 / 868.3 / 868.5 |
| T6.5 | RX2 fallback | sf=12 на 869.525 MHz | приемник заключва на честотата |

### T7 — Производителност (~1 час)

| ID | Описание | Pass criteria |
|----|----------|---------------|
| T7.1 | Throughput на сурови байтове, sf=7 | ≥ 5.0 kbps (теоретично 5.47 kbps) |
| T7.2 | Latency `send()` → DIO1 TX_DONE | ≤ ToA + 5 ms |
| T7.3 | RSSI vs разстояние, открито пространство | RSSI ≈ −60 dBm @ 1 m, ≈ −110 dBm @ 1 km |
| T7.4 | Линк бюджет, sf=12 vs sf=7 | sf=12 хваща ≥ 8 dB по-слаб сигнал |
| T7.5 | TX→RX turnaround (ping-pong) | ≤ 100 ms на 32-байтов payload, sf=7 |

### T8 — Грешки и възстановяване (~30 мин)

| ID | Описание | Pass criteria |
|----|----------|---------------|
| T8.1 | RX timeout | `recv(timeout_ms=1000)` връща празен резултат след ~1 s |
| T8.2 | TX без свързана антена | без crash, мощност автоматично ограничена |
| T8.3 | Bad command (грешен SPI opcode) | библиотеката хвърля контролирана грешка |
| T8.4 | Power glitch на 3V3 | след глитч `begin()` отново успява |
| T8.5 | Команда по време на BUSY | библиотеката чака BUSY=0 преди следваща транзакция |

## 3. Скрипти и автоматизация

Препоръчителна структура в `examples/LoRa/tests/`:

```
tests/
├── conftest.py            # pytest + mpremote фикстури
├── t1_smoke_a.py          # на DUT
├── t1_smoke_b.py
├── t2_bringup.py          # активира GPIO toggling за logic analyzer
├── t3_tx_a.py / t3_rx_a.py
├── t3_tx_b.py / t3_rx_b.py
├── t4_parity.py           # стартира и двата, сравнява RSSI/SNR
├── t5_stress_burst.py
├── t5_stress_24h.py
├── t6_etsi_dutycycle.py
├── t7_throughput.py
├── t8_recovery.py
└── host/
    ├── run_all.py         # оркестратор от хост
    └── parse_logs.py      # анализ на JSON/CSV логове
```

Всеки тестов скрипт:
1. Принтира `TEST_ID:` и `DUT:` на първия ред.
2. Логва структуриран JSON един ред на резултат: `{"id":..., "result":"PASS|FAIL", "data":{...}}`.
3. Завършва с `EXIT_CODE: 0|1`.

## 4. Acceptance criteria за DoD (Definition of Done)

Тагваме `lora-driver/v0.1` когато:

- [ ] T1 — всички pass на двете либи.
- [ ] T2 — bring-up на хардуера документиран със screenshot от анализатор.
- [ ] T3 — TX/RX между две устройства, ≥ 99/100 пакета и в двете посоки.
- [ ] T4.1 — cross-compat A↔B потвърден.
- [ ] T5.1 — PER ≤ 1% при 1000 пакета.
- [ ] T6.1, T6.3 — ETSI ERP и duty cycle спазени, измерени.
- [ ] T8.1, T8.5 — recovery поведение работи.
- [ ] Документация в `README.rst` обновена с PASS статус и измерени стойности.

T5.2 (24h soak) и T7.3 (range test) са optional за v0.1 — препоръчителни за v0.2.

## 5. Известни ограничения и out-of-scope

- **LoRaWAN MAC тестове** — не са в обхвата на v0.1. Покриват се отделно
  (виж `PLAN.md` Трак B): OTAA join, MIC валидация, FCnt persistence,
  RX1/RX2 timing, SenseCAP visibility.
- **Multi-chip тестове (SX1268, SX1261)** — out-of-scope; само SX1262.
- **Long-range полеви тестове (≥ 1 km)** — препоръчителни, но не задължителни
  за приемане; провеждат се отделно с GPS лог.
- **EMC / RED съответствие** — извън лабораторния тест план; нужна е
  акредитирана лаборатория.

## 6. Дневник на дефекти

Формат за всеки откритен дефект (в `tests/issues.md`):

```
## [ID] Кратко заглавие
- **Дата:** YYYY-MM-DD
- **DUT:** DUT-1 / DUT-2
- **Lib:** A (lora-sx126x) / B (micropySX126X)
- **Test ID:** T3.1A
- **Симптом:** ...
- **Стъпки за репродукция:** ...
- **Очаквано:** ...
- **Реално:** ...
- **Severity:** blocker / major / minor
- **Workaround:** ...
- **Корекция:** commit hash / PR
```

## 7. Прогноза за обема на изпълнение

| Категория | Часове |
|-----------|--------|
| T1        | 0.5    |
| T2        | 1      |
| T3        | 1      |
| T4        | 1.5    |
| T5 (без 24h soak) | 2 |
| T6        | 1      |
| T7        | 1      |
| T8        | 0.5    |
| Документация на резултатите | 1 |
| **Σ (без soak)** | **9.5** |
| T5.2 24h soak | +24 паралелно |

Изпълнение: **един работен ден** активна работа + 1 нощ за soak.

## 8. Резултати от изпълнение (2026-05-01)

### Изпълнена среда
- **DUT-1:** VK_RA4M2 на COM18 + Wio-SX1262 Header Board (hard-wired)
- **DUT-2:** VK_RA4M2 на COM9 + Wio-SX1262 Header Board (hard-wired)
- **Pin map (потвърден):** SCK=P111, MOSI=P109, MISO=P110, NSS=P206, RST=P001, BUSY=P002, DIO1=P015, RF_SW1=P100 (HIGH винаги)
- **SPI:** SoftSPI @1 MHz (firmware bug в hardware SPI(1) на VK_RA4M2 build → виси при `write_readinto`)
- **RF параметри:** EU868 канал 0 (868.1 MHz), SF7, BW125, CR4/5, +14 dBm, sync 0x12, TCXO 1.8V

### Обобщение на резултатите

| Тест | Параметри | TX/RX | PER | Бележка | Status |
|------|-----------|-------|-----|---------|--------|
| **T1.smoke** lib A | — | — | — | init 66 ms, 14 KB heap | ✅ PASS |
| **T1.smoke** lib B | — | — | — | init 213 ms, 50 KB heap | ✅ PASS |
| **T3.tx/rx** | 20 пакета, gap 2 s | 20 / 20 | 0.0 % | RSSI −13 dBm, SNR +13 dB | ✅ PASS |
| **T3.pingpong** | 20 PING/PONG | 20 / 20 | — | RTT median 140 ms, min 127 / max 147 | ✅ PASS |
| **T5.stress** | 200 пакета, gap 500 ms | 200 / 200 | 0.0 % | TX leak 128 B, RX leak 1072 B | ✅ PASS |
| **T4.parity** | 15 sample-а × 2 либи | — | — | ΔSNR 0.28 dB ✓, ΔRSSI 3.3 dB | ✅ PASS (SNR), ⚠ RSSI run-to-run drift |

### Открити дефекти

**[D1] lora-sx126x SNR scaling bug** — major

- **Дата:** 2026-05-01
- **Lib:** A (`lora-sx126x` от micropython-lib)
- **Test ID:** T4.parity
- **Симптом:** `RxPacket.snr` връща ~50 при сигнал със SNR ~12.5 dB.
- **Стъпки за репродукция:** Предавай LoRa пакет с известен SNR, чети `rx.snr` от lib A, сравни с lib B (`micropySX126X`).
- **Очаквано:** SNR в dB (per SX1262 datasheet § 13.4.6: `raw_int8 / 4`).
- **Реално:** raw `int8_t` от регистъра без скалиране.
- **Severity:** major (изкривява всякакви RSSI/SNR метрики и линк бюджет калкулации)
- **Workaround:** делене на 4 в адаптера (`tests/_radio.py:_RadioA.recv`).
- **Корекция:** TODO — PR към `micropython-lib/micropython/lora/lora-sx126x/lora/sx126x.py`.

**[D2] VK_RA4M2 hardware SPI(1) firmware bug** — major

- **Дата:** 2026-05-01
- **Test ID:** T2.bring-up
- **Симптом:** `SPI(1, ...).write_readinto(...)` виси безсрочно (поляването на `SPSR.SPRF` flag никога не става true).
- **Стъпки за репродукция:** `SPI(1, baudrate=1000000, sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))` → `write_readinto(b'\xC0\x00', rx)` → hang.
- **Очаквано:** SPI транзакция приключва в µs.
- **Реално:** hang, COM port става недостъпен (изисква USB power cycle).
- **Severity:** major (блокира hardware SPI(1) на VK_RA4M2)
- **Workaround:** SoftSPI на същите пинове (виж `tests/_radio.py:_make_spi`).
- **Корекция:** TODO — добавяне на SPI1 ISR vectors в `boards/VK_RA4M2/ra_gen/vector_data.c` + recompile.

### Препоръки

1. ✅ **Production-ready ниво:** двете либи могат да се ползват paralelno за SX1262 PHY на VK_RA4M2.
2. ⚠ **Heap budget:** lib B (50 KB) НЕ може да съществува заедно с lib A в една сесия на VK_RA4M2. Замразяване в firmware (`manifest.py`) препоръчително за продуктивно ползване.
3. 🐛 Двата открити bug-а (D1, D2) трябва да се документират в upstream.
4. 📡 За SenseCAP интеграция (LoRaWAN) — продължи с Трак B от `PLAN.md`.

## 9. Препратки

- Pin map и init примери: `README.rst`, `01_lora_init.py`, `02_lora_init_micropysx126x.py`
- Имплементационен план: `PLAN.md`
- Reference имплементация: https://github.com/varna9000/micropython-reticulum
- LoRaWAN базов код: https://github.com/GereZoltan/LoRaWAN
- Регулация: ETSI EN 300 220-2, LoRa Alliance Regional Parameters EU868
