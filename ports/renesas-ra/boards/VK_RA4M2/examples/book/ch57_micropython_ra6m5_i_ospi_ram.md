### Глава 57. MicroPython на VK_RA6M5 и OSPI RAM `[Напреднал]`

> **💡 Аналогия:** Ако VK_RA4M2 е малък работен кабинет (128 KB RAM ≈ работно бюро), VK_RA6M5 е офис с голяма зала за съхранение: бюрото е малко по-голямо (512 KB SRAM) **плюс** отделен склад от 8 MB OSPI RAM. Малките инструменти стоят на бюрото (бързо), големите кутии със суровини — в склада (по-бавно, но 16× повече място).

---

#### 57.1. Какво различно има спрямо VK_RA4M2

| Свойство | VK_RA4M2 | VK_RA6M5 |
|---|---|---|
| Ядро | Cortex-M33 @ 100 MHz | Cortex-M33 @ **200 MHz** |
| Флаш | 512 KB | **2 MB** |
| Вградена RAM | 128 KB | **512 KB** |
| Външна RAM | няма | **8 MB OctaSPI RAM @ `0x68000000`** |
| Външна флаш за FS | няма (FS на /flash, ~448 KB) | **16 MB QSPI flash @ `0x60000000`** |
| Ethernet MAC | няма | **10/100 RMII (ICS1894 PHY)** |
| USB | Full-Speed device | **High-Speed device + host** |
| SD карта | няма (SoftSPI) | **SDHI hardware** |
| Hardware crypto (SCE) | основен (AES) | **разширен (AES, GCM, RSA, ECC, SHA, TRNG)** |

API-то на MicroPython е същото — `from machine import Pin/UART/SPI/I2C` работи идентично. Разликата е *колко* можеш да побереш в RAM-а и *какво* можеш да включиш в мрежа (LAN/HTTP/MQTT през lwIP, TLS през mbedTLS).

---

#### 57.2. Карта на паметта — разширен вариант

```
0x0000_0000 ┌──────────────────────┐
            │  Code flash (1.6 MB) │  firmware + .py файлове като константи
0x0019_0000 ├──────────────────────┤
            │  FS flash (448 KB)   │  /flash (вътрешна резерва, ако не ползваш QSPI)
0x0800_0000 ├──────────────────────┤
            │  Data flash (8 KB)   │  dataflash модул (същият като RA4M2)
0x2000_0000 ├──────────────────────┤
            │  SRAM (512 KB)       │  .data, .bss, стек, GC heap (primary)
0x2008_0000 ├──────────────────────┤
            │  ...                 │
0x6000_0000 ├──────────────────────┤
            │  QSPI flash (16 MB)  │  /flash при включен HAS_QSPI_FLASH
0x6800_0000 ├──────────────────────┤
            │  OSPI RAM (8 MB)     │  GC heap secondary  ← фокус на главата
0x7000_0000 └──────────────────────┘
```

Двете външни области са memory-mapped — четеш и пишеш с обикновени `*addr = val` както към вътрешна RAM. Контролерите (QSPI, OSPI) превеждат всеки достъп на серийни команди към чипа. Цена: **по-бавно от вътрешна SRAM** (грубо 5-10× при OSPI DOPI) и **необходима е инициализация** при boot (контролерът трябва да бъде превключен в memory-mapped DOPI режим преди да се ползва).

---

#### 57.3. OSPI RAM като втора област на garbage collector-а

Вградените 512 KB SRAM не стигат за големи аудио буфери, мрежови receive queues или Python списъци с десетки хиляди елементи. Затова MicroPython port-ът на VK_RA6M5 регистрира **8 MB OctaSPI RAM** като *втора* област на GC heap-а. Резултатът: `gc.mem_free()` показва ~8.5 MB веднага след boot, а единичен `bytearray` може да заеме почти **7.8 MB непрекъснато**.

**Стратегията е „малките в SRAM, големите в OSPI":**

- Allocator-ът сканира областите по реда на регистрация. SRAM е *първата*, OSPI е *втората*.
- Малък обект (str, dict, frame, parse-tree node) → намира място в SRAM → стои там.
- Голяма bytearray, която не се събира в свободното на SRAM → пада в OSPI като overflow.
- OSPI остава devствено докато не поискаш голяма памет → когато стане нужно, има пълни 8 MB на разположение.

##### Първа проверка след boot

```python
>>> import gc
>>> gc.collect()
>>> print(gc.mem_free(), "bytes free")
8503376 bytes free
```

> **📌 Важно:** Числото е *общо* свободно през двете области, не максимален непрекъснат блок. Можеш да имаш 8 MB free и въпреки това да не успееш да алокираш един блок от 8 MB — за това е следващата секция.

##### Колко големи буфери минават

```python
>>> b = bytearray(1 * 1024 * 1024)        # 1 MB → SRAM е малък, отива в OSPI
>>> hex(id(b))                             # header-ът обаче е в SRAM
'0x20018fa0'
>>> len(b)
1048576
>>> del b; gc.collect()
>>> b = bytearray(7800 * 1024)             # ~7.6 MB
>>> len(b)
7987200
```

На чист boot **измерен максимален contiguous block ≈ 7.8 MB**. По-голям клеква на ATB/FTB metadata в края на pool-а:

```python
>>> bytearray(7900 * 1024)                # 7.7 M → OK
>>> bytearray(7980 * 1024)                # 7.8 M → MemoryError
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
MemoryError: memory allocation failed, allocating 8171520 bytes
```

##### Защо след освобождаване 8 MB не се връщат

```python
>>> b = bytearray(2 * 1024 * 1024); del b; gc.collect()
>>> bytearray(7 * 1024 * 1024)            # FAIL!
MemoryError: memory allocation failed, allocating 7340032 bytes
```

GC-то на MicroPython е **non-moving mark-sweep**. То не премества живи обекти, не дефрагментира. Свободни блокове, които са *съседни*, изглеждат като един continuous пул (защото allocator-ът сканира линейно) — но щом между две свободни области има жив обект, те остават отделни.

В горния пример между ATB metadata-та (началото на OSPI) и края на 2 MB освободения регион е възможно да са се появили живи обекти от runtime-а (qstr, parse-tree fragments, lwIP буфери) — и max contiguous пада. **Затова големите алокации се правят рано, преди системата да е работила дълго.**

---

#### 57.4. Кога не пишете в OSPI RAM

OSPI е по-бавна. За горещ път (ISR, DSP loop, 1 MS/s ADC sampling) използвайте вътрешна SRAM. Конкретни случаи:

| Задача | Памет | Защо |
|---|---|---|
| Stack | SRAM | ISR-ите не понасят OSPI latency |
| DTC/DMAC ring буфери (audio, ADC) | SRAM | DMA-та чете директно, всеки cycle брои |
| Parse + compile | SRAM | стотици малки alloc-и, OSPI ще ги фрагментира |
| Голям WAV файл, прочетен от SD | OSPI ✓ | 7 MB чисто данни, ползват се на парчета |
| HTTP body buffer | OSPI ✓ | 1-5 MB JSON/HTML без проблем |
| FFT array на 128K точки | OSPI (с внимание) | работи, но 5-10× по-бавно от SRAM |
| Списък с 50 000 sensor reading-а | OSPI ✓ | не е критично за латентност |

> **⚠️ Cache coherency:** RA6M5 има I-cache и D-cache. OSPI memory-mapped регионът обикновено се третира като нормална памет — кешира се, CPU достъпи са кохерентни. *Ако* DMA пише в OSPI RAM (рядък сценарий — обикновено DMA пише в SRAM), след DMA трябва `SCB_InvalidateDCache_by_Addr(addr, len)` преди да четеш. За GC heap самият CPU достъп е безпроблемен.

---

#### 57.5. Как да тестваш сам

##### Скрипт за измерване на max contiguous

```python
# max_alloc_probe.py
import gc

def probe():
    gc.collect()
    free_at_start = gc.mem_free()
    # Двоично търсене на най-голям bytearray, който минава.
    lo, hi, best = 1024, free_at_start, 0
    while lo <= hi:
        mid = (lo + hi) // 2
        try:
            b = bytearray(mid)
            best = mid
            del b
            gc.collect()
            lo = mid + 4096
        except MemoryError:
            hi = mid - 4096
    return free_at_start, best

f, m = probe()
print("free:", f, "bytes,  max single block:", m, "bytes")
print("max single block:", m / (1024 * 1024), "MB")
```

##### Адрес → коя област

```python
def where(obj):
    addr = id(obj)
    if 0x20000000 <= addr < 0x20080000:
        return "SRAM"
    if 0x68000000 <= addr < 0x68800000:
        return "OSPI"
    return f"other (0x{addr:08x})"

x = "hello"
b = bytearray(64)
print(where(x))      # SRAM (qstr-ите винаги в SRAM)
print(where(b))      # SRAM — header-ът на bytearray
```

> Забележете: `id(b)` връща адреса на *header*-а на обекта, не на самия `bytes` буфер. Header-ите винаги са малки (~16 B) и попадат в SRAM. Самите buffer-и могат да са и в OSPI — не се вижда директно от Python.

---

#### 57.6. Какво да включиш в mpconfigboard.h, за да активираш OSPI heap

При port на собствена RA6M5 платка със същия (или подобен) Macronix MX25LM51245G OSPI чип:

```c
#define MICROPY_HW_HAS_OSPI_RAM     (1)

void board_init(void);
#define MICROPY_BOARD_EARLY_INIT()  board_init()
```

В `mpconfigport.h` (port-wide):
```c
#if defined(RA6M5) && MICROPY_HW_HAS_OSPI_RAM
#define MICROPY_GC_SPLIT_HEAP       (1)
#endif
```

В linker script (`vk_ra6m5.ld` или еквивалент) трябва:
```
OSPI_RAM (rwx)  : ORIGIN = 0x68000000, LENGTH = 0x00800000
...
.octa_ram (NOLOAD): { . = ORIGIN(OSPI_RAM) + LENGTH(OSPI_RAM); } > OSPI_RAM
_ospi_ram_start = ORIGIN(OSPI_RAM);
_ospi_ram_end   = ORIGIN(OSPI_RAM) + LENGTH(OSPI_RAM);
```

В `main.c` след `gc_init`:
```c
gc_init(MICROPY_HEAP_START, MICROPY_HEAP_END);
#if MICROPY_GC_SPLIT_HEAP && MICROPY_HW_HAS_OSPI_RAM
extern uint32_t _ospi_ram_start, _ospi_ram_end;
gc_add(&_ospi_ram_start, &_ospi_ram_end);
#endif
```

`board_init()` отваря OSPI controller-а през FSP API-то, превключва го в memory-mapped DOPI режим, прави един test write/read на `0x68000000` и излиза. Готово — `gc_add` може да обхожда region-а свободно.

---

#### 57.7. Често срещани капани

| Симптом | Причина | Решение |
|---|---|---|
| `gc.mem_free()` показва ~150 KB вместо 8 MB | `MICROPY_GC_SPLIT_HEAP` не е `1` или `gc_add` не се вика | проверете `mpconfigport.h` и `main.c` |
| HardFault на първия достъп до `0x68000000` | OSPI controller не е в memory-mapped mode | викайте `board_init()` от `MICROPY_BOARD_EARLY_INIT` |
| `0xFFFF...` или random data при четене | DOPI dummy cycles грешни, или PHY timing | проверете `g_ospi_ram0_read_settings` в `hal_data.c` |
| 6 MB alloc клеква при 8.5 MB free | fragmentation от предишни allocations | направете голямата алокация рано, преди мрежа/audio да е стартирала |
| Голяма `bytearray` работи бавно | data е в OSPI (5-10× по-бавна от SRAM) | за горещ път копирайте парче по парче в SRAM буфер |

---

#### 57.8. Препратки към реализацията

- `ports/renesas-ra/boards/VK_RA6M5/vk_ra6m5.ld` — linker, OSPI_RAM region и `_ospi_ram_*` символи
- `ports/renesas-ra/boards/VK_RA6M5/board_init.c` — `R_OSPI_Open` + memory-mapped DOPI mode
- `ports/renesas-ra/boards/VK_RA6M5/mpconfigboard.h` — `MICROPY_HW_HAS_OSPI_RAM` и `MICROPY_BOARD_EARLY_INIT`
- `ports/renesas-ra/main.c` (около ред 310) — `gc_init` + `gc_add`
- `ports/renesas-ra/mpconfigport.h` — `MICROPY_GC_SPLIT_HEAP` gate за RA6M5
- `py/gc.c` — генеричен mark-sweep (без местене на обекти)

> **Следваща стъпка** (Глава 58): Hardware AES крипто (SCE) — как се ускорява TLS на LAN, AES-CMAC за LoRaWAN и AES-CCM за Bluetooth Link Layer.
