# LoRaWAN end-node за VK_RA4M2 + Wio-SX1262 + TTN

Class-A OTAA LoRaWAN устройство върху MicroPython, регистрирано в **The
Things Network** (EU868). End-to-end: OTAA join → AES-CTR encrypted uplinks
→ RX1/RX2 downlink с MAC dialogue + app commands → WS2812 индикация.

## Какво има тук

```
lorawan_upstream/
├── lorawan_app.py            ← главният end-node app (production-ready)
├── LoRaConfig_TTN.py         ← OTAA credentials (DevEUI/JoinEUI/AppKey)
├── ttn_payload_formatter.js  ← TTN Console JavaScript (uplink + downlink)
├── LoRaWAN/                  ← MAC-слой helpers (AES_CMAC, FHDR, и т.н.)
│   ├── AES_CMAC.py           ←  pure-Python CMAC (за MIC)
│   ├── maes.py               ←  cryptolib AES wrapper
│   └── ...                   ←  MType/Direction/payload classes
├── radio/                    ← micropySX126X PHY driver (vendored)
│   ├── sx1262.py
│   └── _sx126x.py
├── join_minimal.py           ← демо: само OTAA join (debug)
├── full_join_uplink.py       ← демо: join + 1 uplink (debug)
└── README.md                 ← този файл
```

## Различия от reference (`ehong-tl/micropySX126X`)

`ehong-tl/micropySX126X` е **само PHY** — `sx.send(bytes)` / `sx.recv()` за
точка-точка LoRa между две устройства. Няма LoRaWAN. Examples-ите там са:

| micropySX126X example | Нашия еквивалент |
|----------------------|-------------------|
| `TX/main.py` | `tests/t3_tx.py` (raw TX, точка-точка) |
| `RX/main.py` | `tests/t3_rx.py` (raw RX) |
| `Ping Pong/main.py` | `tests/t3_pingpong.py` |
| `TX (non blocking)` | (не покрит — не ни трябва за LoRaWAN) |
| — | **`lorawan_app.py`** (LoRaWAN end-node, OTAA, RX1/RX2, MAC, persistence) |

Накратко — взехме PHY-я (`radio/sx1262.py`) и добавихме целия LoRaWAN MAC
слой отгоре (изграден от `GereZoltan/LoRaWAN` + наши patch-ове).

| Слой | Reference | Нашата добавка |
|------|-----------|---------------|
| PHY (SX1262) | ✅ от `ehong-tl/micropySX126X` | vendored като `radio/sx1262.py` |
| LoRaWAN MAC build/parse | ❌ | `LoRaWAN/*.py` (vendored + patched) |
| AES-CMAC за MIC | ❌ | `LoRaWAN/AES_CMAC.py` (pure-Python) |
| AES-128 ECB/CTR | ❌ | `cryptolib` (axTLS, firmware) + `maes.py` wrapper |
| OTAA join | ❌ | `lorawan_app.py` |
| Session key derive | ❌ | `lorawan_app.py` |
| RX1 / RX2 windowing | ❌ | `listen_rx1` / `listen_rx2` |
| MAC commands | ❌ | DevStatusReq/Ans, LinkADRReq/Ans, RXTimingSetup, DeviceTimeReq/Ans |
| Persistent state | ❌ | DevNonce + FCntUp + session keys в `/flash/` |
| Confirmed uplink + ACK | ❌ | `confirmed=True` режим |
| TTN payload formatter | ❌ | `ttn_payload_formatter.js` (uplink + downlink) |
| App downlink → relay | ❌ | WS2812 LED on/off от FPort=2 |

## Hardware setup

VK_RA4M2 + Wio-SX1262 Header Board, твърдо свързани:

```
SX1262             VK_RA4M2 pin
──────             ────────────
SCK                P111   (SPI(1) CLK)
MOSI               P109   (SPI(1) MOSI)
MISO               P110   (SPI(1) MISO)
NSS  (CS)          P206
DIO1 (IRQ)         P015
RESET              P001
BUSY               P002
RF_SW1 (PE4529)    P100   (HIGH = enable RF path)
TCXO               (1.8 V, useRegulatorLDO=False)
```

WS2812 (за relay индикация):
```
DIN                P112   (SCI2 TX)
VCC enable         P500   (трябва да е HIGH)
```

## Setup за TTN (стъпка по стъпка)

### 1. Регистрирай device в TTN Console

https://eu1.cloud.thethings.network → Application → **Add end device** →
"manual entry":

| Поле | Стойност |
|------|---------|
| **Frequency plan** | `EU_863_870_TTN` |
| **LoRaWAN version** | `MAC V1.0.3` |
| **Activation mode** | `OTAA` |
| **Device class** | `Class A` |

TTN ще генерира `DevEUI` (или ти го въвеждаш), `JoinEUI` и `AppKey`.

### 2. Попълни `LoRaConfig_TTN.py`

Замени placeholder-ите с реалните 16/32 hex char стойности (MSB ред —
както ги показва TTN Console):

```python
class LoRaConfig:
    DevEUI  = [0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x07, 0x74, 0x16]   # 8 bytes
    JoinEUI = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]   # 8 bytes
    AppKey  = [0xDC, 0x2E, 0xC6, 0x45, ..., 0xD9]                # 16 bytes
```

### 3. Инсталирай TTN payload formatter

В TTN Console → Application → **Payload formatters** →
- **Uplink formatter**: copy-paste от `ttn_payload_formatter.js` (функция `decodeUplink`)
- **Downlink formatter**: copy-paste от същия файл (функции `encodeDownlink` + `decodeDownlink`)

### 4. Качи Python файловете на устройството

```bash
mpremote connect COM18 cp lorawan_app.py :/flash/lib/
mpremote connect COM18 cp LoRaConfig_TTN.py :/flash/lib/
# (LoRaWAN/* и radio/* вече са frozen в firmware-а — виж manifest.py)
```

### 5. Стартирай

```bash
mpremote connect COM18 run lorawan_app.py
```

Или сложи `import lorawan_app; lorawan_app.main()` в `main.py` за auto-start.

## Очаквано поведение

### Първи boot (без запазена session)

```
==================================================
VK_RA4M2 LoRaWAN end-node (TTN, EU868 SF7)
==================================================
Radio OK
OTAA join (DevNonce=100)...
  joined: DevAddr=260b8d37
[FCnt=0 Unconfirmed] temp=20.25°C button=1 relay=0 + FOpts=0d
  *** RX1 downlink RSSI=-49 SNR=12.5 FCnt=0 FPort=2
    *** UTC time from TTN: 2026-05-02 00:20:24.664
    relay → OFF (зелено)
    MAC cmds: [('DeviceTimeAns', '...'), ('LinkADRReq', '...')]
    queued MAC ans for next uplink: 0300
[FCnt=1 Unconfirmed] temp=20.50°C button=0 relay=0 + FOpts=0300
...
```

### Всеки следващ boot (с запазена session в `/flash/lw_session.dat`)

```
Loaded session: DevAddr=260b8d37 FCnt=58
[FCnt=58 Unconfirmed] temp=20.25°C button=1 relay=0
...
```

OTAA join не се повтаря — продължава от запазената сесия + последния FCnt.

## Какво прави `lorawan_app.py`

### Uplink (FPort=2, всеки `UPLINK_INTERVAL_S` секунди)

3-byte payload:
```
bytes 0-1   temp     int16 BE × 100  (sim sweep 20.00..25.00 °C)
byte 2      button   uint8 (sim toggle: 10s on, 10s off)
```

### Downlink handling

| FPort | Какво очаква |
|-------|-------------|
| 0 | MAC commands (FRMPayload encrypted с NwkSKey) |
| 2 | App command: `byte[0]` = 0/1 → `set_relay(off/on)` → WS2812 зелен/червен |
| други | Печата decoded text/hex |

### MAC dialogue (auto-handled)

| Server cmd | Нашият отговор |
|-----------|---------------|
| `DevStatusReq` | `DevStatusAns(battery=255, margin=last_snr)` |
| `LinkADRReq` | `LinkADRAns(0, 0, 0)` — отхвърляме (single-channel SF7 gateway) |
| `RXTimingSetupReq` | `RXTimingSetupAns` (empty) |
| `DeviceTimeAns` | Decode → UTC print |

### DeviceTimeReq (наша инициатива)

При `ASK_DEVICE_TIME_AT_START = True` пращаме DeviceTimeReq в FOpts на
първия uplink → TTN отговаря с DeviceTimeAns → принтваме UTC time.

### Confirmed uplinks

Всеки `CONFIRMED_EVERY`-ти uplink (default: 5) е Confirmed → device
проверява ACK bit в downlink и принтва "ACK YES/NO".

### Persistent state

| File | Съдържание |
|------|-----------|
| `/flash/lw_devnonce.dat` | Last DevNonce (анти-replay) |
| `/flash/lw_session.dat` | DevAddr (4) + NwkSKey (16) + AppSKey (16) |
| `/flash/lw_fcntup.dat` | Last FCntUp |

## Тестване от TTN Console

### Изпрати downlink → relay

**Messaging → Downlink** →
- FPort = `2`
- Insert mode = `Bytes` → `01` (relay ON, червено) или `00` (OFF, зелено)
- Confirmed downlink = unchecked
- **Schedule downlink**

В рамките на 1-2 uplink цикъла (max ~20s) LED-ът сменя цвят и в REPL-а
ще видиш:
```
relay → ON (червено)
```

### Изпрати JSON downlink (минава през encoder)

**Insert mode = JSON**:
```json
{"relay": "on"}
```
или `{"relay": 1}`, `{"relay": true}`, `{"relay": "off"}`.

## Конфигурация (top-of-file константи в `lorawan_app.py`)

```python
UPLINK_INTERVAL_S = 60     # за production, 10 е test-only (TTN FUP!)
FREQ_MHZ          = 868.1
SF                = 7
RX2_FREQ_MHZ      = 869.525  # EU868 default RX2
RX2_SF            = 12
CONFIRMED_EVERY   = 5        # 0=никога; 5=всеки 5-ти; 1=винаги
ASK_DEVICE_TIME_AT_START = True
```

⚠ **TTN Fair Use Policy** = 30 s airtime/device/day. При SF7 + 6-byte
payload и `UPLINK_INTERVAL_S = 10`, дневен airtime ≈ 484 s → **16× над
FUP**. За дългосрочна работа: 5+ минути interval.

## Troubleshooting

| Симптом | Причина | Поправка |
|---------|---------|----------|
| `OTAA join: no JoinAccept` | DevEUI/JoinEUI/AppKey грешни, или DevNonce ≤ предишен | Провери credentials, ресет `/flash/lw_devnonce.dat` |
| `DevNonce was used before` (TTN log) | TTN не е виждал по-малки nonce-ове | DevNonce минимум = 100, инкремент при retry |
| Никакъв downlink | Single-channel SF7 gateway? Антена? | Виж RSSI/SNR в TTN; провери gateway live data |
| WS2812 не светва | P500 power off, грешен pin | Виж `examples/ws2812_sci_test.py` |
| `MIC mismatch` в downlink parse | Session keys невалидни (rejoin) | `os.remove("/flash/lw_session.dat")` → ще rejoin-не |

## Лицензи

- `radio/*` — MIT (от `ehong-tl/micropySX126X`, виж `LICENSE_micropySX126X`)
- `LoRaWAN/*` (без `maes.py`) — MIT (от `GereZoltan/LoRaWAN`, виж `LICENSE`)
- `LoRaWAN/maes.py` — нашата добавка (`cryptolib` wrapper)
- `lorawan_app.py`, `LoRaConfig_TTN.py`, `ttn_payload_formatter.js` — нашата работа

## Reference

- TTN docs: https://www.thethingsindustries.com/docs/
- LoRaWAN spec 1.0.3: https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-3/
- SX1262 datasheet: Semtech
- Wio-SX1262 board: https://wiki.seeedstudio.com/xiao_nrf52840_&_wio_SX1262_kit_for_meshtastic/
