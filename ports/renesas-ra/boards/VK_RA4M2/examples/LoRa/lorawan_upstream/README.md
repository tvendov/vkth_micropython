# LoRaWAN end-node за VK_RA4M2 + Wio-SX1262 (TTN / ChirpStack EU868)

Class-A OTAA LoRaWAN устройство върху MicroPython. End-to-end: OTAA join
+ CFList → multi-channel (3..8 ch) round-robin TX → AES-CTR encrypted
uplinks → RX1/RX2 downlink с MAC dialogue + ADR (LinkADRReq реално
сменя SF/Pwr/ChMask) + app commands → WS2812 индикация.

Тестван и с **TTN single-channel gateway** (slot 868.1 SF7, EU_863_870_TTN
plan), и с **реален multi-channel gateway** (SenseCAP M2 + ChirpStack).
SCG fallback се активира с `EU868_DEFAULT_CHANNELS_HZ = [868_100_000]` +
`DEFAULT_RX1_DELAY_MS = 5000`.

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
VK_RA4M2 LoRaWAN end-node (EU868 multi-channel)
==================================================
ADR: DR5 (SF7) Pwr=idx0 (+14dBm) ChMask=0x07 (3 ch) RX1=1000ms NbTrans=1
Channels: 868.100, 868.300, 868.500
Radio OK
OTAA join (DevNonce=100, freq=868.100 MHz SF7)...
  CFList +867.100 MHz
  CFList +867.300 MHz
  CFList +867.500 MHz
  CFList +867.700 MHz
  CFList +867.900 MHz
  joined: DevAddr=260b8d37 RX1delay=1s channels=8
[FCnt=0 Unconfirmed ch=868.100 MHz DR5] temp=20.25°C relay=0 + FOpts=0d
  *** RX1 downlink RSSI=-49 SNR=12.5 FCnt=0 FPort=2
    *** UTC time from TTN: 2026-05-02 00:20:24.664
    relay → OFF (зелено)
    MAC cmds: [('DeviceTimeAns', '...'), ('LinkADRReq', '...')]
    LinkADR applied: DR=5 (SF7) Pwr=1 (+12dBm) ChMask=0xFF NbTrans=1
[FCnt=1 Unconfirmed ch=868.300 MHz DR5] temp=20.50°C relay=0 + FOpts=03070
[FCnt=2 Unconfirmed ch=868.500 MHz DR5] temp=20.75°C relay=0
[FCnt=3 Unconfirmed ch=867.100 MHz DR5] temp=21.00°C relay=0
...
```

### Всеки следващ boot (с запазена session)

```
ADR: DR5 (SF7) Pwr=idx1 (+12dBm) ChMask=0xFF (8 ch) RX1=1000ms NbTrans=1
Channels: 868.100, 868.300, 868.500, 867.100, 867.300, 867.500, 867.700, 867.900
Loaded session: DevAddr=260b8d37 FCnt=58
[FCnt=58 Unconfirmed ch=867.500 MHz DR5] temp=20.25°C relay=0
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

### MAC dialogue (auto-handled, EU868 multi-channel + ADR full path)

| Server cmd | Нашият отговор |
|-----------|---------------|
| `DevStatusReq` | `DevStatusAns(battery=255, margin=last_snr)` |
| `LinkADRReq` | `LinkADRAns(pwr_ack, dr_ack, chmask_ack)` — реално превключваме SF/Pwr/ChMask и persist-ваме (`/flash/lw_dr.dat`, `/flash/lw_pwridx.dat`, `/flash/lw_chmask.dat`, `/flash/lw_nbtrans.dat`) |
| `RXTimingSetupReq` | `RXTimingSetupAns` + сменяме RX1 delay → `/flash/lw_rx1delay.dat` |
| `NewChannelReq` | `NewChannelAns(both ack)` + добавяме freq в `active_channels` → `/flash/lw_channels.dat` |
| `DeviceTimeAns` | Decode → UTC print |
| `JoinAccept` CFList | До 5 extra канала се append-ват към `active_channels` (типично 867.1/3/5/7/9 за EU868) |
| `JoinAccept` RXDelay | Override-ва `rx1_delay_ms` при join |

### DeviceTimeReq (наша инициатива)

При `ASK_DEVICE_TIME_AT_START = True` пращаме DeviceTimeReq в FOpts на
първия uplink → TTN отговаря с DeviceTimeAns → принтваме UTC time.

### Confirmed uplinks

Всеки `CONFIRMED_EVERY`-ти uplink (default: 5) е Confirmed → device
проверява ACK bit в downlink и принтва "ACK YES/NO".

### Persistent state

| File | Съдържание |
|------|-----------|
| `/flash/lw_devnonce.dat`  | Last DevNonce (анти-replay) |
| `/flash/lw_session.dat`   | DevAddr (4) + NwkSKey (16) + AppSKey (16) |
| `/flash/lw_fcntup.dat`    | Last FCntUp |
| `/flash/lw_dr.dat`        | Current DR (LinkADRReq → SF map) |
| `/flash/lw_pwridx.dat`    | Current LoRaWAN power index (0..7) |
| `/flash/lw_chmask.dat`    | LinkADRReq ChMask (per-bit channel enable) |
| `/flash/lw_channels.dat`  | Extra channels от CFList/NewChannelReq (CSV Hz) |
| `/flash/lw_rx1delay.dat`  | RX1 delay (ms) — RXTimingSetupReq override |
| `/flash/lw_nbtrans.dat`   | NbTrans от LinkADRReq (за future confirmed retry) |
| `/flash/lw_txpower.dat`   | Manual TX power index (button cycle) |
| `/flash/lw_interval.dat`  | Manual uplink interval index (button cycle) |

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
EU868_DEFAULT_CHANNELS_HZ = [868_100_000, 868_300_000, 868_500_000]
RX2_FREQ_MHZ      = 869.525    # EU868 default RX2
RX2_SF            = 12
DEFAULT_DR        = 5          # SF7 (DR0=SF12, ..., DR5=SF7)
DEFAULT_PWR_IDX   = 0          # +14 dBm (idx 0..7 → 14..0 dBm)
DEFAULT_RX1_DELAY_MS = 1000    # ChirpStack default; TTN ползва 5000
CONFIRMED_EVERY   = 5          # 0=никога; 5=всеки 5-ти; 1=винаги
ASK_DEVICE_TIME_AT_START = True
```

### SCG fallback (TTN single-channel gateway)

За работа с TTN single-channel gateway (1 канал, SF7 only) →

```python
EU868_DEFAULT_CHANNELS_HZ = [868_100_000]   # само ch 0
DEFAULT_RX1_DELAY_MS      = 5000             # TTN
```

В този режим LinkADRReq за DR≠5 ще rejectne (SF mismatch с gateway-а),
а CFList добавените канали ще се изпускат на TX (gateway не слуша).

### Multi-channel (ChirpStack + SenseCAP M2 / реални gateway-и)

Default config — 3 mandatory ch с round-robin hop, CFList add-ва още до
5 канала ако сървърът ги изпрати. ADR работи end-to-end: server-ът
optimizira SF/Pwr спрямо link budget; device-ът се адаптира.

⚠ **EU868 1% duty cycle на g1 sub-band** — при default 10s интервал и
3 канала: ~0.15% / канал, well below limit. При 2s интервал — 0.77% /
канал, still below limit но близо до. SCG режим (1 канал) → 2s = 2.3%
violation; ползвай ≥10s в SCG mode.

## Setup за ChirpStack + SenseCAP M2

ChirpStack (https://www.chirpstack.io/) — open-source LoRaWAN Network
Server. SenseCAP M2 — реален 8-канален EU868 gateway (Semtech SX1302).

### 1. Регистрирай device в ChirpStack

Application → Add device:

| Поле | Стойност |
|------|---------|
| **Device profile** | Class A, **MAC version 1.0.3**, Region EU868 |
| **OTAA / ABP** | OTAA |
| **DevEUI** | същото като в Data Flash credentials |
| **JoinEUI / AppKey** | същите като в Data Flash credentials |
| **RX1 delay** | 1 s (ChirpStack default; ако смениш → device чрез RXTimingSetupReq |
| **ADR** | enabled |

### 2. Конфигурирай SenseCAP M2

M2 config-а (ChirpStack-Gateway-Bridge или Semtech UDP packet forwarder)
трябва да сочи към ChirpStack сървъра ти. Frequency plan = `EU868`
(8 channel sub-band 1 + downlink канал на 869.525 SF12).

### 3. Стартирай device-а — ще видиш в ChirpStack live frame log:

```
JoinRequest  on  868.1 MHz SF7   (ch 0 mandatory)
JoinAccept   с  CFList → +5 канала добавени
[FCnt=0 ch=868.500 MHz DR5] DeviceTime + LinkADR conversation
[FCnt=1 ch=868.300 MHz DR5] ...                  # round-robin hop
[FCnt=2 ch=868.100 MHz DR5] ...
[FCnt=3 ch=867.100 MHz DR5] ...                  # CFList added
...
```

ADR ще конвергира до оптимален DR/Pwr за 10-20 uplink-а.

## Troubleshooting

| Симптом | Причина | Поправка |
|---------|---------|----------|
| `OTAA join: no JoinAccept` | DevEUI/JoinEUI/AppKey грешни, или DevNonce ≤ предишен | Провери credentials, ресет `/flash/lw_devnonce.dat` |
| `DevNonce was used before` (TTN/ChirpStack log) | сървърът не е виждал по-малки nonce-ове | DevNonce минимум = 100, инкремент при retry |
| Никакъв downlink | Single-channel SF7 gateway? Антена? | Виж RSSI/SNR в console-а; провери gateway live data |
| `LinkADR rejected: dr_ack=0` | сървърът пита DR извън 0..5 (DR6=SF7BW250, DR7=FSK не поддържаме) | Disable DR6/DR7 в device profile-а |
| `LinkADR rejected: chmask_ack=0` | ChMaskCntl 1..5 или 7 — не поддържаме | За EU868 simple ChMaskCntl=0 трябва да е достатъчно (≤8 канала) |
| Round-robin hop иска канал, който gateway не слуша | partial coverage (например M2 само sub-band 1) | Disable канала в device profile-а → LinkADRReq ChMask ще го mask-не |
| WS2812 не светва | P500 power off, грешен pin | Виж `examples/ws2812_sci_test.py` |
| `MIC mismatch` в downlink parse | Session keys невалидни (rejoin) | `os.remove("/flash/lw_session.dat")` → ще rejoin-не |
| ADR не конвергира | Малко uplink-ове, или RSSI margin тесен | По-чест uplink (10s ≤ interval), или disable ADR в device profile |

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
