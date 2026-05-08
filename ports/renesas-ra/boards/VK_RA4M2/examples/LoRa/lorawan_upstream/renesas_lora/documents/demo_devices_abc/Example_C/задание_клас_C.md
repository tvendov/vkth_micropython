# Задание: LoRaWAN Class C — Instant-Response Controller (VK_RA4M2)

**Проект:** LoRaWAN Live Demo
**Хардуер:** Vekatech VK_RA4M2 + SX1262
**Клас:** LoRaWAN Class C (LoRaWAN 1.0.4, EU868)
**Роля:** Instant-response controller — демонстрира < 1 s downlink латентност, async RX

## OTAA Credentials (server-side ALREADY configured ✓)

| Поле | Стойност |
|------|----------|
| **DevEUI** | `70B3D57ED0070003` |
| **JoinEUI** (AppEUI) | `0000000000000000` (all zeros) |
| **AppKey** | `202CB141A5842931F99C0C1DDFE70D68` |
| **Tenant ID** | `1cfe244f-156a-4b17-afd6-bf563b7ca173` |
| **Application ID** | `afa9488a-a5e5-4ab7-b20d-185e807ad5ac` |
| **Profile name** | `class-C-demo-profile` |
| **Profile ID** | `6eaba778-5bfd-44a1-a451-00011a4920bf` |

> **Note за byte order:** LoRaWAN spec изисква DevEUI/JoinEUI **little-endian** в JoinRequest frame; AppKey остава MSB-first. `lorawan_async` обикновено приема hex strings и обръща ред автоматично — провери в lib-а ако MIC fail-не.

---

## Hardware

### Компоненти

| Peripheral | Pin / Bus | Notes |
|------------|-----------|-------|
| WS2812 DATA | `P112` | Single pixel, RGB (channels=3) |
| WS2812 PWR  | `P500` | `Pin.OUT, value=1`; sleep 100 ms преди init |
| AHT20 SCL   | `P301` | SoftI2C, 100 kHz |
| AHT20 SDA   | `P302` | I2C addr 0x38 |
| SX1262 radio | (built-in, SPI internal) | Фиксиран от board layout — не менги |
| Reset btn   | `P206` | `Pin.IN, PULL_UP` — persistence test |
| USER BUTTON | `P212` | `Pin.IN, PULL_UP` — manual uplink trigger |

> **Бележка:** AHT20 ползва SoftI2C на P301/P302. Ако demo setup-ът споделя тези pins с OLED или друга периферия (напр. ham_ctcss.py), увери се че няма адресен конфликт на bus-а. За чисто LoRa demo тези pins са свободни.
>
> Reference implementation: `ports/renesas-ra/boards/VK_RA4M2/examples/project_ham_tone_generator/ham_ctcss.py` — клас `RgbStatus`

### Wiring

```
3V3 ──┬── AHT20 VCC       GND ──── AHT20 GND
      └── 4.7kΩ ─┬─ P301 (SCL)
                 └─ P302 (SDA)

P500 ─── WS2812 VDD (power enable, HIGH)
GND  ─── WS2812 GND
P112 ─── WS2812 DIN  (3.3 V logic — директно, без level-shifter)
```

### WS2812 MicroPython API

```python
from machine import WS2812, Pin
import time

# Color constants (унифицирани за всички 3 класа)
RGB_OFF      = (0, 0, 0)
RGB_BOOT     = (40, 20, 0)   # boot / loading
RGB_JOIN     = (60, 0, 0)    # joining (red)
RGB_JOINED   = (0, 40, 0)    # joined / idle / Class C listening (green)
RGB_TX       = (0, 0, 60)    # uplink TX (blue)
RGB_RX       = (60, 60, 0)   # RX window open (yellow)
RGB_DOWNLINK = (60, 0, 60)   # downlink received (magenta)
RGB_ERROR    = (60, 0, 40)   # error (red-magenta)

class RgbStatus:
    def __init__(self, pin_name="P112", power_pin_name="P500"):
        self.power = Pin(power_pin_name, Pin.OUT, value=1)
        time.sleep_ms(100)
        self.strip = WS2812(pixel_count=1, pin=Pin(pin_name), channels=3)
        self._cur = None
        self.set(RGB_BOOT)

    def set(self, color):
        if color == self._cur:
            return
        self._cur = color
        self.strip[0] = color
        self.strip.write()

    def force(self, color):
        self._cur = color
        self.strip[0] = color
        self.strip.write()

    def flash(self, color, ms=100, times=2):
        prev = self._cur
        for _ in range(times):
            self.force(color); time.sleep_ms(ms)
            self.force(RGB_OFF); time.sleep_ms(ms)
        if prev is not None:
            self.force(prev)
```

### WS2812 цветова схема

| Цвят    | Tuple           | Значение                                |
|---------|-----------------|-----------------------------------------|
| Off     | `(0,0,0)`       | —                                       |
| Orange  | `(40,20,0)`     | Boot / loading state                    |
| Red     | `(60,0,0)`      | Joining / not joined                    |
| Green   | `(0,40,0)`      | Class C active — continuous RX (слушам) |
| Blue    | `(0,0,60)`      | TX in progress (uplink)                 |
| Yellow  | `(60,60,0)`     | RX1 window след uplink                  |
| Magenta | `(60,0,60)`     | Downlink received — команда изпълнена   |
| Red-mag | `(60,0,40)`     | Error                                   |

**Ключово визуално послание:** Зеленото LED гори постоянно = "Слушам СЕГА".

### RX2 DR — критично за латентност

Default EU868 RX2 = DR0 (SF12BW125) → airtime ~991 ms → "instant" demo е невъзможен.
**Задължително:** Смени RX2 DR на DR5 (SF7BW125) → airtime ~46 ms → end-to-end < 300 ms.
Конфигурирай и в device profile, и в firmware (`mac.set_rx2(freq=869525000, dr=5)`).

---

## Payload Schema (uplink, fPort=12)

Базова 6-байтова схема + Class C специфични полета:

```
Byte  0–1  int16_LE   temp_c100             температура × 100
Byte  2    uint8      hum_p2                влажност × 2
Byte  3–4  uint16_LE  battery_mv            напрежение mV
Byte  5    uint8      flags                 bit0=class_c_active, bit1=adr, bit2=sensor_ok
Byte  6    uint8      dl_count              async downlinks от последния uplink
Byte  7–8  uint16_LE  last_dl_latency_ms    измерена латентност (0=нямало downlink)
Total 9 байта
```

`last_dl_latency_ms` е "wow" полето — аудиторията вижда измерената латентност директно в ChirpStack decoded payload.

### Downlink (fPort=22 — control; fPort=23 — config)

```
fPort=22:
  0x01  rgb_set   (byte[1]=R, byte[2]=G, byte[3]=B)
  0x02  rgb_off
  0x03  blink     (byte[1]=count, byte[2]=R, byte[3]=G, byte[4]=B)
  0x04  status_now (trigger immediate uplink)

fPort=23:
  0x01 <min>  set_uplink_interval (минути; 0=спри)
  0x02 <dr>   set_rx2_dr (0–5)
  0x03        force_rejoin
```

---

## Async Downlink ISR Model

```
SX1262 DIO1 IRQ → C ISR handler
                → копира frame в static RX buffer (GC-safe)
                → mp_sched_schedule(rx_callback, data)
                → ISR returns (< 10 µs)

Python scheduler → rx_callback(data)
                 → декодира LoRaWAN frame
                 → извиква user on_downlink(port, payload)
                 → сменя WS2812 цвят (magenta)
```

Максимален scheduling delay: ~1 scheduler tick (~10 ms при 100 Hz ticker). Static buffer е задължителен за GC safety — не предавай `bytearray` директно от ISR.

---

## Test Scenarios

### C-1: OTAA Join + Class C активация
**Pre-conditions:** DevEUI регистриран с `supports_class_c=true`
**Steps:** Boot → JOIN_ACCEPT → `mac.set_class('C')` → RGB green постоянно
**PASS:** ChirpStack device details показва class=C; RGB зелено гори непрекъснато
**FAIL:** class=A в ChirpStack → `set_class('C')` не работи или profile грешен

### C-2: Async downlink без uplink trigger (главен "wow" тест)
**Pre-conditions:** Class C активен; RGB green гори
**Steps:** Засечи времето → enqueue fPort=22, hex: `01 00 00 FF` (rgb_set blue) → наблюдавай RGB
**PASS:** RGB сменя цвят в < 1 s; `last_dl_latency_ms` < 1000 в следващия uplink payload
**FAIL:** RGB реагира след > 1 s → RX2 DR е SF12 (виж RX2 DR секцията по-горе)

### C-3: Latency в uplink payload
**Pre-conditions:** C-2 минат
**Steps:** Изчакай следващия periodic uplink → ChirpStack decoded payload
**PASS:** `last_dl_latency_ms` видим и < 700; `dl_count` = 1
**FAIL:** `last_dl_latency_ms` = 0 → firmware не измерва

### C-4: Continuous RX след uplink (не губи downlinks)
**Pre-conditions:** Class C активен; 60 s uplink cycle
**Steps:** Enqueue downlink точно по времето на TX + RX1 прозорец → провери дали пристига
**PASS:** Downlink получен след RX2; RGB мига magenta
**FAIL:** Downlink изгубен → firmware не се връща в continuous RX след uplink

### C-5: Class C vs Class B латентност (визуална демонстрация)
**Pre-conditions:** Board B и Board C активни едновременно
**Steps:** Enqueue downlink на двете едновременно; наблюдавай RGB на двете дъски
**PASS:** C реагира в < 300 ms; B реагира в ≤ 10 s — разликата видима за аудиторията
**FAIL:** Двете реагират почти едновременно → Class distinction не работи

---

## Demo Flow

1. **Power up** → RGB orange (boot) → RGB red (joining) → RGB green постоянно (Class C active, listening)
2. **Изчакай uplink** → ChirpStack Live frames показва decoded payload с temp, hum
3. **Enqueue rgb_set blue** (fPort=22, `0100 00FF`) → RGB сменя цвят МИГНОВЕНО (< 300 ms)
4. **Покажи `last_dl_latency_ms`** в следващия uplink decoded payload — числото за аудиторията
5. **Повтори 2–3 пъти** с различни цветове за визуален ефект

Coментар за аудиторията: "REST API → ChirpStack → MQTT → gateway → radio → MCU → LED — всичко за < 200 ms."

---

## ChirpStack Config

### Device Profile (SQL)

```sql
INSERT INTO device_profile (
    id, tenant_id, name,
    mac_version, reg_params_revision, region,
    supports_otaa, supports_class_b, supports_class_c,
    class_c_params, adr_algorithm_id, uplink_interval,
    payload_codec_runtime, payload_codec_script,
    created_at, updated_at
) VALUES (
    gen_random_uuid(),
    '1cfe244f-156a-4b17-afd6-bf563b7ca173',
    'class-C-demo-profile',
    'LORAWAN_1_0_4', 'RP002_1_0_3', 'EU868',
    true, false, true,
    '{"class_c_timeout":0}'::jsonb,
    'default', 60,
    'JS', $codec$
CODEC_PLACEHOLDER
$codec$,
    NOW(), NOW()
);
```

### Device + Keys (SQL — ВЕЧЕ изпълнено на server-а)

```sql
INSERT INTO device (dev_eui, application_id, device_profile_id, name, created_at, updated_at)
VALUES (decode('70B3D57ED0070003','hex'),
        'afa9488a-a5e5-4ab7-b20d-185e807ad5ac',
        (SELECT id FROM device_profile WHERE name='class-C-demo-profile'),
        'class-C-demo', NOW(), NOW());

INSERT INTO device_keys (dev_eui, nwk_key, app_key, created_at, updated_at)
VALUES (decode('70B3D57ED0070003','hex'),
        decode('202CB141A5842931F99C0C1DDFE70D68','hex'),
        decode('202CB141A5842931F99C0C1DDFE70D68','hex'),
        NOW(), NOW());
```

### Verification — текущо state на server-а (08-May-2026)

```
device:           class-C-demo
dev_eui:          70b3d57ed0070003
join_eui:         0000000000000000
enabled_class:    C
profile:          class-C-demo-profile
mac_version:      1.0.4
supports_class_c: true
adr_algorithm:    default
codec_runtime:    JS (1400 chars paste-able)
uplink_interval:  60s
app_key:          202cb141a5842931f99c0c1ddfe70d68
```

### Firmware boot snippet (sample)

```python
import lorawan_async
from machine import Pin, SoftI2C, WS2812
import time

# WS2812 status LED (от ham_ctcss.py reference)
power = Pin("P500", Pin.OUT, value=1); time.sleep_ms(100)
strip = WS2812(pixel_count=1, pin=Pin("P112"), channels=3)
def rgb(r, g, b):
    strip[0] = (r, g, b); strip.write()

rgb(40, 20, 0)                                    # boot orange

# AHT20 sensor I2C
i2c = SoftI2C(scl=Pin("P301", Pin.OPEN_DRAIN),
              sda=Pin("P302", Pin.OPEN_DRAIN), freq=100000)
# AHT20 addr=0x38 — read 7B trigger, 7B data; виж datasheet за sequence

# LoRaWAN
mac = lorawan_async.LoRaWAN(
    deveui  = "70B3D57ED0070003",
    joineui = "0000000000000000",
    appkey  = "202CB141A5842931F99C0C1DDFE70D68",
    region  = "EU868",
)

rgb(60, 0, 0)                                     # red — joining
await mac.join()
rgb(0, 40, 0)                                     # green — joined

mac.set_class('C')                                # CRITICAL: switch to Class C
mac.set_rx2(freq=869525000, dr=5)                 # CRITICAL: SF7 RX2 за low latency

# Continuous RX active — board listens always; on_downlink fires async
```

### Downlink (curl)

```bash
# RGB blue ON — главна демо команда
curl -s -X POST \
  -H "Grpc-Metadata-Authorization: Bearer $CS_API_TOKEN" \
  -H "Content-Type: application/json" \
  http://192.168.2.140:8080/api/devices/70b3d57ed0070003/queue \
  -d '{"queueItem":{"confirmed":false,"data":"AQAA/w==","fPort":22}}'
# AQAA/w== = base64([0x01,0x00,0x00,0xFF]) = rgb_set R=0 G=0 B=255

# RGB off
curl -s -X POST \
  -H "Grpc-Metadata-Authorization: Bearer $CS_API_TOKEN" \
  -H "Content-Type: application/json" \
  http://192.168.2.140:8080/api/devices/70b3d57ed0070003/queue \
  -d '{"queueItem":{"confirmed":false,"data":"Ag==","fPort":22}}'
# Ag== = base64([0x02]) = rgb_off
```

### Downlink alternative (SQL — без API token)

```bash
# RGB blue, директно в DB queue (fallback ако няма API key):
ssh vkrz@192.168.2.140 "sudo -u postgres psql -d chirpstack -c \"\\
INSERT INTO device_queue_item (id, dev_eui, created_at, f_port, confirmed, data, is_pending, is_encrypted) \\
VALUES (gen_random_uuid(), decode('70b3d57ed0070003','hex'), now(), 22, false, decode('010000FF','hex'), false, false);\""
# 010000FF = rgb_set R=0 G=0 B=255 (blue)
```

### ChirpStack UI

| Изглед | URL |
|--------|-----|
| Device | http://192.168.2.140:8080/#/tenants/1cfe244f-156a-4b17-afd6-bf563b7ca173/applications/afa9488a-a5e5-4ab7-b20d-185e807ad5ac/devices/70b3d57ed0070003 |
| Live frames | (горния URL) `+ /events` |
| Device profile | Tenants → ChirpStack → Device profiles → `class-C-demo-profile` |

### MQTT monitor с латентност

```bash
mosquitto_sub -h 192.168.2.140 -p 1883 \
  -t "application/afa9488a-a5e5-4ab7-b20d-185e807ad5ac/device/70b3d57ed0070003/event/up" \
  -v | jq -r '
    "\(.time[0:19]) UP fCnt=\(.fCnt) dl_lat=\(.object.last_dl_latency_ms // 0)ms dl_count=\(.object.dl_count // 0)"
  '
```

---

## Codec

### JavaScript (ChirpStack default)

```javascript
function decodeUplink(input) {
    if (input.fPort !== 12 || input.bytes.length < 6)
        return { errors: ["bad fPort or short payload"] };
    var b = input.bytes;
    var lat = b.length >= 9 ? (b[7] | (b[8] << 8)) >>> 0 : 0;
    return { data: {
        temperature:        ((b[0] | (b[1] << 8)) << 16 >> 16) / 100.0,
        humidity:           b[2] / 2.0,
        battery_mv:         (b[3] | (b[4] << 8)) >>> 0,
        flags: {
            class_c_active: (b[5] & 0x01) !== 0,
            adr:            (b[5] & 0x02) !== 0,
            sensor_ok:      (b[5] & 0x04) !== 0
        },
        dl_count:           b[6] !== undefined ? b[6] : 0,
        last_dl_latency_ms: lat
    }};
}

function encodeDownlink(input) {
    var d = input.data;
    if (d.command === "rgb_set")
        return { bytes: [0x01, d.r||0, d.g||0, d.b||0], fPort: 22 };
    if (d.command === "rgb_off")
        return { bytes: [0x02], fPort: 22 };
    if (d.command === "blink")
        return { bytes: [0x03, d.count||3, d.r||0, d.g||0, d.b||255], fPort: 22 };
    if (d.command === "status_now")
        return { bytes: [0x04], fPort: 22 };
    if (d.command === "set_interval")
        return { bytes: [0x01, d.minutes||1], fPort: 23 };
    if (d.command === "set_rx2_dr")
        return { bytes: [0x02, d.dr||5], fPort: 23 };
    if (d.command === "force_rejoin")
        return { bytes: [0x03], fPort: 23 };
    return { errors: ["unknown command"] };
}
```

### Python (алтернативен)

```python
def decode_uplink(input):
    b = input["bytes"]
    if input["fPort"] != 12 or len(b) < 6:
        return {"errors": ["bad fPort or short payload"]}
    temp = int.from_bytes(b[0:2], "little", signed=True) / 100.0
    lat  = int.from_bytes(b[7:9], "little") if len(b) >= 9 else 0
    return {"data": {
        "temperature":        temp,
        "humidity":           b[2] / 2.0,
        "battery_mv":         int.from_bytes(b[3:5], "little"),
        "flags": {
            "class_c_active": bool(b[5] & 0x01),
            "adr":            bool(b[5] & 0x02),
            "sensor_ok":      bool(b[5] & 0x04),
        },
        "dl_count":           b[6] if len(b) > 6 else 0,
        "last_dl_latency_ms": lat,
    }}

def encode_downlink(input):
    d = input["data"]
    cmd = d["command"]
    if cmd == "rgb_set":
        return {"fPort": 22, "bytes": [0x01, d.get("r",0), d.get("g",0), d.get("b",0)]}
    if cmd == "rgb_off":
        return {"fPort": 22, "bytes": [0x02]}
    if cmd == "blink":
        return {"fPort": 22, "bytes": [0x03, d.get("count",3),
                                       d.get("r",0), d.get("g",0), d.get("b",255)]}
    if cmd == "status_now":
        return {"fPort": 22, "bytes": [0x04]}
    if cmd == "set_interval":
        return {"fPort": 23, "bytes": [0x01, d.get("minutes", 1)]}
    if cmd == "set_rx2_dr":
        return {"fPort": 23, "bytes": [0x02, d.get("dr", 5)]}
    if cmd == "force_rejoin":
        return {"fPort": 23, "bytes": [0x03]}
    return {"errors": ["unknown command"]}
```
