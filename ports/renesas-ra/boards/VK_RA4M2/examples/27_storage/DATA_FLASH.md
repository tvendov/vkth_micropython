# DATA_FLASH — VK_RA4M2 предложение за data-flash оформление и запис

## Цел
- Всеки адрес е точно специфициран.
- **Никакви креденшъли в кодовата флаш.** AppKey/DevEUI/JoinEUI живеят само в data flash; firmware-ът не съдържа ключове (нито C `#define`, нито Python източник).
- Записът в флаша спира да става на всеки uplink — броячите живеят в retained RAM (Software Standby resume). Флашът се пише рядко.

## Хардуер
- Data flash: **0x08000000 – 0x08001FFF**, 8192 B
- Erase блок: **64 B** (128 блока) · Write единица: **4 B**
- Издръжливост: ~100 000 erase/блок

---

## Адресна карта

| Регион | Блокове | Адреси | Размер | Предназначение |
|--------|---------|--------|--------|----------------|
| **CRED** | 0 | `0x08000000 – 0x0800003F` | 64 B | Креденшъли (LWCR). Пишат се веднъж при provisioning. Само за четене от стека. |
| **NVM_A** | 1–32 | `0x08000040 – 0x0800083F` | 2048 B | LoRaMac NVM blob, копие A (ping-pong) |
| **NVM_B** | 33–64 | `0x08000840 – 0x0800103F` | 2048 B | LoRaMac NVM blob, копие B (ping-pong) |
| **NONCE** | 65–66 | `0x08001040 – 0x080010BF` | 128 B | DevNonce монотонен журнал (append) |
| **CONFIG** | 67 | `0x080010C0 – 0x080010FF` | 64 B | Устройствен конфиг: `interval_s` (от Grafana) + timestamp на последния запис |
| **APP** | 68–127 | `0x08001100 – 0x08001FFF` | 3840 B | Свободно за приложението (`dataflash`) |

Сумарно: 1 + 32 + 32 + 2 + 1 + 60 = 128 блока = 8192 B.

NVM blob е ~1.35 KB + 32 B хедър → побира се в 2048 B банка с резерв.

### CONFIG запис (block 67, `0x080010C0`)

| Поле | Адрес | Размер | Бележка |
|------|-------|--------|---------|
| Magic | `0x080010C0` | 4 B | валидност |
| `interval_s` | `0x080010C4` | 4 B | интервал на обаждане (сек), задава се от Grafana по downlink |
| `last_write_ts` | `0x080010C8` | 4 B | RTC timestamp на последния 24-ч запис |
| CRC16 | `0x080010CC` | 2 B | над предходните |

### CRED запис (LWCR, 44 от 64 B)

| Поле | Адрес | Размер |
|------|-------|--------|
| Magic `"LWCR"` | `0x08000000` | 4 B |
| Версия `0x02` | `0x08000004` | 1 B |
| Reserved | `0x08000005` | 1 B |
| DevEUI (MSB) | `0x08000006` | 8 B |
| JoinEUI (MSB) | `0x0800000E` | 8 B |
| **AppKey (MSB)** | `0x08000016` | 16 B |
| **device_number** (uint32 BE) | `0x08000026` | 4 B |
| CRC16-CCITT | `0x0800002A` | 2 B |

`device_number` е номерът на устройството — пише се при provisioning, чете се
от reader-а, и устройството го влага в uplink payload-а, за да се вижда в
Grafana (device → gateway → ChirpStack → MQTT → mqtt_bridge → DB → Grafana).
Версия `0x02`, CRC над bytes 0..41. **CRED се чете от Python** (`read_credentials.py`
логиката) и ключовете отиват в стека през `mac.set_keys(...)` — няма C парсер.

### NVM blob хедър (в началото на активната банка)
`valid_magic` (4 B) + 7×`uint16` размери на контекстите (mac/region/crypto/se/cmds/classb/cq) + padding → 32 B. После слепените контексти.

`valid_magic` се пише **последен** (4-байтов атомарен word) → банката е валидна само ако записът е завършил докрай (power-loss safe).

### NONCE журнал
Append на 4-байтови записи `{DevNonce}` в блок 65, после блок 66. Когато се напълнят → erase двата + рестарт. Активен = последният непразен запис.

---

## Политика на запис

| Събитие | Действие | Флаш |
|---------|----------|------|
| uplink + sleep/wake | броячите в retained RAM | **0 записа** |
| `mac_nvm_context_change()` | вдига `nvm_dirty = true` (само флаг) | 0 |
| на 24 часа | запиши неактивната NVM банка + CONFIG, превключи | 1 банка |
| join завършен | append DevNonce в NONCE журнала + запис банка | малко |
| cold boot (загуба на ток) | restore от валидната банка + DevNonce; **FCntUp += margin** | 0 |

---

## Стратегия за брояча

Броячът се мени при всеки uplink, но флашът се пипа **рядко**. Между записите истината живее в retained RAM (Software Standby resume). Флашът е само backup срещу пълна загуба на ток.

### 1. Нормална работа (между записите)
- Всеки uplink: `FCntUp++` **само в RAM**. Нула флаш.
- Deep sleep → RTC wake → RAM е жива, броячът продължава. Нула флаш.
- `mac_nvm_context_change()` само вдига `nvm_dirty = true`.

### 2. Записът — 1 път на 24 часа, **през Python**
- **Самият запис в флаша се прави от Python**, не от C. C само сериализира.
  Разделение:
  - C: `mac.nvm_blob()` → връща **свежи `bytes` (копие, не view)** + **version байт** отпред; `mac.nvm_restore_blob(bytes)` (MibSet) → **проверява version байта** преди MibSet и **връща статус** (joined / keys-only); `mac.advance_fcnt(N)` за margin-а.
  - Python (на 24 ч): ако `nvm_dirty` → взима blob от C → пише го в неактивната NVM банка през region-aware `dataflash` (ping-pong) → пише CONFIG (`interval_s` + timestamp) → `valid_magic` последно (атомарно) → чисти `nvm_dirty`.
- Съдържание: version + `{FCntUp, FCntDown, DevNonce, session keys, channels}` + CONFIG.
- **(Q5)** `mac.nvm_blob()` прави `assert blob_len ≤ 2048` → fail loud, без truncate. Текущ blob ~1.38 KB → 33% резерв.
- **(Q2)** MIB_NVM_CTXS е raw struct памет → version байтът пази от тихо invalidиране при stack upgrade (стар blob се отхвърля, не се подава на MibSet).
- Износване: 365 записа/година → ping-pong → ~182 erase/банка/година → **~500+ години**.

### 3. Cold boot (загуба на ток, RAM изтрита)
Строг ред (Q3 — **задължителен**):
```
1. blob = прочети валидната NVM банка (Python)
2. status = mac.nvm_restore_blob(blob)        # MibSet презаписва целия crypto ctx вкл. FCntUp
3. mac.advance_fcnt(N)                          # ЕДВА СЕГА — иначе MibSet го трие
```
- `advance_fcnt` мени FCntUp **само** през crypto setter-а: `LoRaMacCryptoGetUplinkFCnt()` +N → `LoRaMacCryptoSetUplinkFCnt()` (единственото authoritative копие; TX чете оттам). НЕ пипа `FCntList.FCntUp` директно.
- `advance_fcnt` **no-op-ва (error)** ако се извика преди успешен `nvm_restore_blob`.
- `status` от restore: ако е keys-only (не joined) → Python решава re-join.

**(Q4 — КОРИГИРАНО) Margin = МАКСИМУМЪТ, не `elapsed/текущ_интервал`:**

Интервалът може да е бил сменен в незаписания прозорец (напр. **10 s → 360 s** от Grafana). Ако смятаме по текущия интервал (360 s), подценяваме кадрите, които реално са пратени на 10 s → броячът изостава → дроп. Затова адвансваме с **максимално възможните uplink-и**, т.е. по **най-бързия** интервал:

```
N_MAX = 86400 / MIN_INTERVAL_S     # цял ден при най-малкия позволен интервал
FCntUp += N_MAX                     # винаги максимумът, независимо от текущия интервал
```

- `MIN_INTERVAL_S` = долната граница (Grafana диапазон 10–1275 s → MIN = 10 s) → `N_MAX = 8640`.
- Прозорецът е ≤ 24 ч (пишем 1×/ден), значи реалните кадри ≤ `N_MAX` при всеки случай → винаги сме отгоре.
- **Не зависи от RTC/elapsed/текущ интервал** — затова е имунен към смяна на интервала.
- Цената: ~8640 „изгорени" FCnt на reset (32-бит брояч → ~500k reset-а) — пренебрежимо.

**Cap:** ако някога `N_MAX ≥ 16384` (MAX_FCNT_GAP) → **re-join** вместо advance (нова сесия нулира броячите). При MIN=10 s, `8640 < 16384` → OK.

Гарантира `FCntUp > последно видяно от мрежата` при всякаква смяна на интервала → нула дропнати кадъра.

### 4. Ограничение
`N_MAX = 86400 / MIN_INTERVAL_S` трябва да е `< 16384` (MAX_FCNT_GAP, EU868) → `MIN_INTERVAL_S ≥ 6 s`. EU868 duty cycle така или иначе налага ≥ ~6 s.

| MIN_INTERVAL | N_MAX = max/ден | OK? |
|---|---|---|
| 10 s | 8640 | ✅ |
| 6 s | 14400 | ✅ (под лимита) |
| 5 s | 17280 | ❌ над 16384 → трябва re-join |

### Поток (резюме)
```
uplink      → FCntUp++ в RAM                    (нула флаш)
на 24 часа  → запис на банка + CONFIG
sleep/wake  → RAM пази брояча                   (нула флаш)
cold boot   → restore_blob → advance_fcnt(N_MAX)  (N_MAX = 86400/MIN_INTERVAL_S, max/ден)
```

### Архитектурни инварианти (от arch ревю)
- **Един flash thread.** `NvmDataMgmtStore` се достига САМО през `mac.nvm_store()` на Python thread-а — **никога от ISR/timer/callback**. Споделеният `g_flash0` няма mutex; единственият writer е този път. (Реалната опасност, не теоретична.)
- **GC-safe blob.** `nvm_restore_blob` копира подадените `bytes` в статичния `s_nvm` **преди return** — не задържа Python buffer pointer. `nvm_blob` връща свежо копие, не view в статична C памет.
- **Bounds check (Q1).** `dataflash.region("APP")` запис прави runtime проверка `offset+len ≤ край на регион` → отказва, ако би прелял в NVM_A. (Евтино; покрива единствения реален провал — грешен APP offset трие банка.)
- **Cache.** RA4M2 няма D-cache → FCACHE disable/DSB/ISB в `dflash_lwnvm.c` стига; без допълнителен барьер за Python read пътя.
- **Power-loss.** Crash между `nvm_restore_blob` и първи TX → следващ boot просто re-advance-ва (монотонно, само напред) → безопасно.

### Партишън guard (`moddataflash.c`)
Тъй като писането в флаша е през Python, `dataflash` е **region-aware**:

- **Default (без регион)** → само **APP** (`0x08001100–0x08001FFF`). Това вижда приложението; `dataflash.write/erase` тук не може да стигне CRED/NVM/NONCE/CONFIG.
- **Region-scoped** → `dataflash.region("NVM_A" | "NVM_B" | "CONFIG" | "NONCE" | "CRED")` връща view, ограничен до съответния регион. Ползва се само от LoRaWAN Python слоя (24-ч запис, restore) и от provisioning — не от произволен app код.

Следствие: голото `dataflash.write(0, ...)` → APP offset 0 = `0x08001100`, **не** `0x08000000`. CRED/NVM се пишат само през явно именуван регион.

---

## Provisioning на CRED — през Python, region-scoped

CRED се пише от Python през **именувания регион**:

```
cred = dataflash.region("CRED")
cred.erase_block(0); cred.write(0, lwcr_record)
```

- `lwcr_record` е 44-байтовият v2 LWCR (magic+version+EUI+AppKey+**device_number**+CRC16).
- Записва само block 0 (`0x08000000`). Голото `dataflash` (APP) не може да стигне дотам.
- Region-достъпът е по конвенция: само provisioning и LoRaWAN слоят го ползват. Тъй като писането е Python, твърд hardware guard срещу „друг Python код" няма — защитата е, че **default** `dataflash` е APP-only и трябва изричен `region("CRED")`, за да се пипне.

`provision_credentials.py` ползва `dataflash.region("CRED")`; ключовете идват runtime.

---

## Правило: нула креденшъли в кодовата флаш
- Без ключове в C (`#define`, масиви) и без ключове в Python източник (напр. `LoRaConfig_*.py`).
- **`provision_credentials.py` НЕ хардкодва ключове и НЕ се freeze-ва във firmware.** Ключовете идват по време на изпълнение — argv / `input()` / отделен `secrets.*` файл, който е в `.gitignore` и не влиза в образа.
- На boot **Python** чете CRED (`0x08000000`), парсва record-а и подава ключовете през `mac.set_keys(...)`. Няма C credential reader.
- Ако CRED е празен/невалиден → устройството **отказва join** (няма fallback към компилирани ключове).

---

## Какво се променя в кода
1. Един общ хедър `dataflash_partition.h` с всички адреси = единствен източник на истината (C и Python).
2. `moddataflash.c` → region-aware: default = APP; `dataflash.region(name)` за NVM/CONFIG/NONCE/CRED.
3. `provision_credentials.py` → пише CRED v2 (с `device_number`) през `dataflash.region("CRED")`; ключовете runtime.
4. C сериализация: `mac.nvm_blob()` → version байт + blob, свежо копие, `assert ≤2048`; `mac.nvm_restore_blob(bytes)` → проверява version, MibSet, връща статус (joined/keys-only); `mac.advance_fcnt(N)`.
5. `mac_nvm_context_change()` → само `nvm_dirty = true` (без флаш). **Инвариант:** `NvmDataMgmtStore` само през `mac.nvm_store()` на Python thread, никога ISR.
6. **Python 24-часов писач**: ако `nvm_dirty` → blob от C → запис в NVM банка + CONFIG през region-aware `dataflash` → чисти флага.
7. CONFIG: `interval_s` (от Grafana downlink, за sleep cadence + Grafana дисплей) + `last_write_ts` (диагностика). Margin-ът НЕ зависи от тях.
8. **Python restore при boot (строг ред):** чете NVM банка → `mac.nvm_restore_blob()` → **после** `mac.advance_fcnt(N_MAX)` (`N_MAX = 86400/MIN_INTERVAL_S`) през crypto setter (`LoRaMacCryptoGet/SetUplinkFCnt`); no-op ако restore не е успял. Ако `N_MAX ≥ 16384` → re-join вместо advance.
9. NONCE журнал за DevNonce (Python, region-scoped).
10. `device_number` в CRED v2 → Python (`read_credentials.py`) го парсва; приложението го влага в uplink payload-а за Grafana. (C credential reader премахнат.)
11. Банки 2048 B по картата горе; `region("APP")` запис прави bounds check срещу преливане.

## Ред на изпълнение
1. `dataflash_partition.h` + region-aware `moddataflash.c` + provisioning през `dataflash.region("CRED")` (взаимно свързани).
2. C blob API (`nvm_blob`/`nvm_restore_blob`/`advance_fcnt`) + `nvm_dirty`.
3. Python 24-часов писач + Python restore.
4. CONFIG регион + `interval_s` от Grafana + FCntUp дневен margin.
5. NONCE журнал; преоразмеряване на банките; `device_number` в CRED v2.

## Тест — margin след reset

Целта: да се потвърди, че след reset броячът е адвансиран с фиксирания максимум `N_MAX`.

1. Provision (с `device_number`) + join.
2. Тригни записа ръчно (тестов хук) → в банката се записва текущ `FCntUp = F0`.
3. Прати още uplink-и (RAM брояч расте над F0, флашът НЕ се пипа).
4. **(ключов сценарий)** смени интервала 10 s → 360 s по downlink, прати още.
5. **Reset устройството** (hard reset → RAM изтрита, cold boot).
6. След reset прочети `FCntUp` (през diag/REPL).

**Очаквано:** `FCntUp_след_reset == F0 + N_MAX`, където `N_MAX = 86400 / MIN_INTERVAL_S` (= 8640 при MIN=10 s) — **независимо** от това, че текущият интервал е 360 s.

Проверки:
- разликата спрямо `F0` == `N_MAX` (не `86400/360`) → максимумът работи, имунен към смяна на интервала;
- `N_MAX < 16384` → няма десинхрон;
- мрежата приема следващите uplink-и (няма drop), вкл. след смяната 10→360 s;
- ред: restore преди advance (ако се размени, MibSet трие advance-а — REPL регресия).
