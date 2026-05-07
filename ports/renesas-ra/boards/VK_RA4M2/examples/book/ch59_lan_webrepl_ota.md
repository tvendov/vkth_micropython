### Глава 59. LAN, WebREPL и OTA на VK_RA6M5 `[Напреднал]`

> **💡 Аналогия:** USB кабелът е като жица за домофон — стигаш само до едно работно място. Ethernet е като телефон — обаждаш се отвсякъде в мрежата. WebREPL е „телефон към интерпретатора"; OTA е „куриер, който носи нова прошивка по жицата вместо ти да я носиш с ръка".

---

#### 59.1. Карта на функциите

| Функция | Слой | Файл в порта |
|---|---|---|
| Hardware Ethernet (EDMAC + ICS1894 PHY) | FSP `r_ether` | `lib/fsp/ra/fsp/src/r_ether/` |
| lwIP TCP/IP стек | C | `lib/lwip/` + `mpnetworkport.c` |
| `network.LAN()` Python клас | C | `eth.c`, `network_lan.c` |
| Non-blocking init | C | `eth.c` (eth_start без busy-wait) |
| RX FIFO (8 слота) | C | `eth.c` (eth_rx_fifo) |
| WebREPL (websocket, REPL) | Python + C | `modwebrepl.c`, `webrepl.py` (frozen) |
| OTA flash helper | C + Python | `boards/VK_RA6M5/ota/` (opt-in) |

---

#### 59.2. Бърз старт

##### A. Свързване с LAN

```python
import network, time
lan = network.LAN()
lan.active(True)                  # връща за <1 ms

# Чакай DHCP във фон (kernel-ът обработва lwIP в PendSV)
while lan.ifconfig()[0] == '0.0.0.0':
    time.sleep_ms(100)

print('IP:', lan.ifconfig()[0])
```

##### B. Manual IP (без DHCP сървър)

```python
lan.active(True)
lan.ifconfig(('192.168.2.50', '255.255.255.0',
              '192.168.2.1', '8.8.8.8'))
```

##### C. HTTP клиент

```python
import socket
s = socket.socket()
s.connect(('1.1.1.1', 80))
s.send(b'GET / HTTP/1.0\r\nHost: 1.1.1.1\r\n\r\n')
print(s.recv(200))
s.close()
```

##### D. HTTPS (mbedTLS)

```python
import requests
r = requests.get('https://api.github.com/zen')
print(r.text)
```

##### E. WebREPL — REPL през браузър

```python
# В boot.py (вече направено в boards/VK_RA6M5/boot.py):
import webrepl
webrepl.start(password='vk6m5')  # max 9 знака!

# В браузъра на твоя PC:
#   1. Свали https://github.com/micropython/webrepl/archive/master.zip
#   2. Отвори webrepl-master/webrepl.html
#   3. Пиши ws://192.168.2.144:8266/   парола: vk6m5
```

##### F. WebREPL — file upload от командния ред

```bash
# webrepl_cli.py от https://github.com/micropython/webrepl
python webrepl_cli.py -p vk6m5 firmware.bin 192.168.2.144:/flash/firmware.bin
```

> **⚠️ Внимание:** Дестинацията **трябва** да е под `/flash/...`.  Кореновият
> `/` е VFS root (read-only); писане там пада с `ENODEV`.

**Тест с 1 MB файл (потвърждение, че оригиналният 300 KB лимит е премахнат):**
```
$ python webrepl_cli.py -p vk6m5 test1mb.bin 192.168.2.144:/flash/test1mb.bin
Remote WebREPL version: (1, 28, 0)
Sent 1048576 of 1048576 bytes        # ~65 s, ~17 KB/s

# На устройството:
>>> import os; os.stat('/flash/test1mb.bin')
(32768, 0, 0, 0, 0, 0, 1048576, ...)   # 1 048 576 bytes ✓

# RX FIFO статистики след transfer:
>>> network.LAN().config('rx_fifo')
(7, 2)                                 # high_water=7/8, dropped=2/4370 = 0.05%
```

---

#### 59.3. Какво НЕ се ползва за LAN data path

Често се пита „пише ли байт по байт?" — отговорът е **категорично не**:

| Транспорт | Кой го прави |
|---|---|
| MAC ↔ PHY | EDMAC (вградена в ETHERC периферия), CPU-цикли = 0 |
| Descriptor ↔ buffer | EDMAC bus master, не DTC, не DMAC |
| User buffer ↔ FSP buffer | `R_ETHER_Read`/`Write` правят CPU memcpy (~1500 B / 5 µs) |
| pbuf ↔ user buffer | `pbuf_take` / `pbuf_copy_partial` (~5 µs) |

DTC и DMAC каналите остават напълно свободни за DAC, ADC, SPI и т.н.

**Блокировки в горещ път (след `active(True)` връща):**

| Операция | Време |
|---|---|
| `socket.send()` | <20 µs |
| `socket.recv()` (non-blocking) | <20 µs |
| ETH ISR | ~1-3 µs (само memcpy в FIFO) |
| RX обработка в PendSV | ~30 µs (interruptable, не блокира audio ISR) |

`lan.active(True)` връща за **0-1 ms** — не блокира main thread по време на startup. DHCP retry-те се обработват в background от PendSV.

---

#### 59.4. RX FIFO архитектура

```
EDMAC хардуер
   │ DMA в g_ether0_buffer[0..7]  (FSP descriptor ring, 8 × 1.5 KB)
   ▼
ETH ISR (приоритет 12)
   │ R_ETHER_Read → копира в eth_rx_fifo[head]   (~1-3 µs)
   │ pendsv_schedule_dispatch(LWIP, pyb_lwip_poll)
   ▼
PendSV (приоритет 15, най-нисък)
   │ pyb_lwip_poll:
   │   eth_drain_rx() → за всеки слот:
   │      pbuf_alloc → pbuf_take → netif->input → ip_input → ...
   │   sys_check_timeouts() → DHCP/ARP/TCP retransmit
   │   R_ETHER_LinkProcess() → link state machine
   ▼
Python user code (lan, socket, urequests, ...)
```

**`lan.config('rx_fifo')` → `(high_water, dropped)`** — мониторинг:
- `high_water` ≤ 4 при нормална работа
- `high_water` 5-7 при stress тест (10× HTTP)
- `dropped` > 0 означава, че PendSV се забави > 8 пакета.

---

#### 59.5. WebREPL детайли

**Капани, които срещнах:**

| Симптом | Причина | Решение |
|---|---|---|
| `webrepl.start()` хвърля празен ValueError | Парола > 9 знака (`webrepl_passwd[10]` в `modwebrepl.c`) | Парола max 9 знака |
| `webrepl.start()` хвърля AttributeError за WLAN | webrepl.py итерира `network.WLAN(0)/(1)` за print URL; на този порт няма WLAN | Custom listener, не `webrepl.start()` |
| Connection accepted, after handshake → ConnectionReset | `setsockopt(SOL_SOCKET, 20, ...)` (ESP-only callback) не работи на този порт | `machine.Timer(-1, period=200)` poll за accept() |
| Не можеш да import-неш webrepl | `bundle-networking` не се pull-ва автоматично | `FROZEN_MANIFEST = $(BOARD_DIR)/manifest.py` в `mpconfigboard.mk` |

**`boot.py` template:** виж `boards/VK_RA6M5/boot.py`. Прави:
1. `network.LAN().active(True)`
2. Чака DHCP
3. Свърз listener на 0.0.0.0:8266
4. `machine.Timer` polling-loop accept-ва клиенти

**WebREPL клиент:** Има 3 варианта:
- Браузър: `webrepl.html` от https://github.com/micropython/webrepl
- CLI: `webrepl_cli.py` от същия repo
- mpremote: **не поддържа WebREPL** (само serial), но `mpremote cp` работи добре върху USB

---

#### 59.6. OTA — план и скелет

**Защо ти трябва OTA:**

- Потребителите ти инсталират платката в кутия — не могат да я свалят, за да flash-нат USB
- Поправка на бъг "by phone" (примерно през HTTPS download)
- Версионна миграция без service truck

**Карта на flash паметта (опционална, opt-in):**

```
0x00000000   Bootloader    64 KB    ← никога не се пипа от OTA
0x00010000   Active app    1 MB     ← MicroPython firmware
0x00110000   Staging       512 KB   ← новата прошивка идва тук
0x00190000   /flash FS     448 KB   ← същата като сега
```

**Файлове в `boards/VK_RA6M5/ota/`:**

| Файл | Какво прави |
|---|---|
| `bootloader.c` | Skeleton на ~200 реда: чете staging flag, swap-ва, jump към app |
| `vk_ra6m5_app.ld` | Linker script за app на offset `0x00010000` |
| `flash_glue.c` | C модул `_ota` с `erase_staging()`, `write()`, `commit()` |
| `ota.py` | High-level Python wrapper: `ota.write_bin('/flash/firmware.bin')` + `ota.commit()` |
| `README.md` | Workflow, integration steps, references |

**Workflow:**

```python
# 1. Качи новата прошивка (например през WebREPL):
#    webrepl_cli.py firmware_v2.bin :firmware_v2.bin
# 2. На самата платка:
import ota
ota.write_bin('/flash/firmware_v2.bin')   # programs staging
ota.commit()                               # sets flag, resets
# 3. Bootloader-ът на следващия boot вижда флага, copy-ва staging→active, boot-ва.
```

**Защо това е скелет, не цяло решение:**

- Bootloader-ът трябва да се build-не отделно като e2 studio FSP проект (или **MCUboot**)
- `flash_hp` FSP инстанцията трябва да се добави през Smart Configurator
- Image signature/hash verification липсва (за production задължително)
- Roll-back при failed boot не е реализиран

За production се препоръчва **MCUboot** — Renesas FSP има готова интеграция, добавя digital signature + hash verification.

---

#### 59.7. Препратки към реализацията

- `ports/renesas-ra/eth.c` — FSP r_ether glue, RX FIFO, диагностични броячи
- `ports/renesas-ra/network_lan.c` — Python `network.LAN` клас
- `ports/renesas-ra/mpnetworkport.c` — lwIP polling, PendSV dispatch
- `ports/renesas-ra/boards/VK_RA6M5/boot.py` — auto-LAN + WebREPL boot script
- `ports/renesas-ra/boards/VK_RA6M5/ota/` — OTA opt-in skeleton
- `ports/renesas-ra/boards/VK_RA6M5/ra_cfg/fsp_cfg/r_ether_cfg.h` — `USE_LINKSTA=0`, `LINK_PRESENT=0`
- `extmod/modwebrepl.c`, `extmod/modwebsocket.c` — generic MicroPython WS/WebREPL C-модули
- `lib/micropython-lib/micropython/net/webrepl/webrepl.py` — Python wrapper

#### 59.8. Mini-table на commit-ите

| Commit | Какво адресира |
|---|---|
| `fc018c9ca` | Native Ethernet LAN binding via FSP r_ether (network.LAN) |
| `46a3cb5b7` | Derive Ethernet MAC from MCU unique ID |
| `26b87feee` | Drive lwIP from PendSV; fix LAN end-to-end |
| `955ef5b49` | Make network.LAN().active(True) non-blocking |
| `02353115d` | RX FIFO + drain from PendSV; ETH ISR shrunk to ~1 us |
| `8ccc80784` | Enable WebREPL |
| `74b0886df` | OTA skeleton — bootloader, app linker, flash glue, Python helper |
