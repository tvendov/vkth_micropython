# renesas-ra LoRaWAN — C stack (Renesas) skeleton

Този каталог съдържа MicroPython C-модул `lorawan`, който ще обвие
**Renesas LoRa-based Wireless Software Package (LoRaMac-node fork)**
като алтернатива на текущия pure-Python стек във
`boards/VK_RA4M2/examples/LoRa/lorawan_upstream/`.

## FSP target — задължително 4.4.0 (без upgrade)

Този порт компилира срещу **FSP 4.4.0**, същата версия която MicroPython
renesas-ra ползва за всички останали драйвери (`lib/fsp/ra/fsp/inc/fsp_version.h`).
Renesas LoRa Software Package v4.90 е *генериран* срещу FSP 6.2.0, но ние
**НЕ обновяваме FSP** в MicroPython дървото. Стратегията:

1. Копираме като black-box само portable C код от LoRa пакета
   (`mac/`, `mac/region/`, `radio/sx126x/`, `radio/region/`, `peripherals/soft-se/`)
   — нито един от тези файлове не извиква FSP API директно, говори с
   `glue/*-board.c` слой който ние пишем.
2. **Изхвърляме** всичко FSP-version-specific от пакета:
   `samples/project/src/boards/ra2*`, `system/uart.c`, `system/gpio.c`,
   `system/spi.c`, `system/flash/cflash*`, `system/at/`.
3. **Glue слоят** (`glue/`) е писан срещу FSP 4.4 API-та които вече се ползват
   в порта — обвивки `ra_sci_spi.c`, `ra_timer.c`, `ra_dac.c`, `ra_storm_adc.c`,
   `R_FLASH_HP` BGO. Никакви директни FSP calls в LoRa дървото освен през тях.

Това означава: ако някой ден FSP се обнови до 6.x, единственият код който
може да се счупи е в `glue/` — а той е малък и изолиран.

## Build switch

Един параметър избира кой LoRa стек се компилира. Дефинира се в
`boards/<BOARD>/mpconfigboard.mk`:

```make
MICROPY_HW_LORA_STACK = python    # (default ако MICROPY_HW_ENABLE_LORA=1)
MICROPY_HW_LORA_STACK = renesas   # този C-модул
MICROPY_HW_LORA_STACK = none      # без LoRa
```

| Стойност  | Какво се прави                                                                                             |
|-----------|------------------------------------------------------------------------------------------------------------|
| `python`  | Freeze на `_upstream/sx126x.py` + `LoRaWAN/*.py`, `modaes_cmac.c`, axTLS AES.                              |
| `renesas` | Build на `lorawan/*.c` (Renesas LoRaMac + sx126x C драйвер + soft-se), expose `lorawan` Python модул.      |
| `none`    | Нищо LoRa не се включва (~44 KB по-малко flash).                                                           |

Backward-compat: ако `MICROPY_HW_LORA_STACK` не е зададен и
`MICROPY_HW_ENABLE_LORA=1` → автоматично `python`. Ако и двете не са
зададени → `none`.

## Каталог

```
lorawan/
├── lorawan.mk           ← Makefile fragment, включва се само ако stack=renesas
├── mod_lorawan.c        ← MicroPython binding (`lorawan.Mac` клас)
├── mod_lorawan.h
├── glue/
│   ├── sx126x_board.c   ← SX126x SPI/GPIO/IRQ glue (RA4M2 SCI9 SPI3 + DTC)
│   ├── sx126x_board.h
│   ├── timer_board.c    ← AGT4 (MAC tick) + AGT5 (RX window) glue
│   ├── timer_board.h
│   ├── dma_board.c      ← DMAC7 резервация (опционален upgrade за burst SPI)
│   ├── dma_board.h
│   ├── nvm_board.c      ← R_FLASH_HP BGO write callback glue
│   └── nvm_board.h
├── mac/                 ← (празен) → копие на Renesas samples/project/src/mac/
├── radio/               ← (празен) → копие на samples/project/src/radio/sx126x/
├── soft_se/             ← (празен) → копие на peripherals/soft-se/
└── system/              ← (празен) → копие на system/{delay,timer,systime,fifo}.c
```

## Hardware ресурси, резервирани от стека (на VK_RA4M2)

| Ресурс    | Употреба                                            | Източник на резервацията                  |
|-----------|------------------------------------------------------|-------------------------------------------|
| SPI3/SCI9 | SX126x command + payload                            | `mp_machine_spi(3)` ползва `ra_sci_spi`   |
| DTC TX/RX | SCI9_TXI / SCI9_RXI (динамично, per-transfer)        | `ra_sci_spi.c` (без промяна)              |
| AGT4      | LoRaMac 1 ms tick + RX1/RX2 window arming            | `glue/timer_board.c`                      |
| AGT5      | TX timeout / retransmit / ACK timeout                | `glue/timer_board.c`                      |
| DMAC7     | (опц.) burst SPI > 256 B, RX continuous              | `glue/dma_board.c`                        |
| ICU pin IRQ | DIO1 (radio IRQ), BUSY (radio ready) — falling/rising | `glue/sx126x_board.c`                  |
| Data flash | NVM persistence (DevNonce, FCnt, sessions)          | `glue/nvm_board.c` (R_FLASH_HP BGO)       |

## Phase status

- **Phase 0** — ✅ skeleton committed (this directory)
- **Phase 1** — radio HAL (SX126x board glue + DIO1/BUSY IRQ)
- **Phase 2** — timer service (AGT4/AGT5 + resumable Radio FSM)
- **Phase 3** — LoRaMac compile + scheduler glue
- **Phase 4** — OTAA join към TTN
- **Phase 5** — Uplink/Downlink + ADR + DutyCycle
- **Phase 6** — Cleanup + docs

Виж пълния phase-plan по-долу в обсъждането на проекта.
