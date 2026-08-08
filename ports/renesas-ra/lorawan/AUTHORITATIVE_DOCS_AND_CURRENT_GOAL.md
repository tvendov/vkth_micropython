# Authoritative Documents And Current Goal

Date: 2026-05-20
Doc-Version: 1.0
Status: Primary authority contract; implementation gaps are tracked explicitly.

Цел: създаване на LoRaWAN драйвер за MicroPython RA4M2.

## Authority

Авторитетни за този LoRaWAN порт са само документите в:

`ports/renesas-ra/lorawan/`

Документи извън тази директория не определят текущата цел.

## Driver Goal

Драйверът трябва да даде на MicroPython програма тези възможности:

- създаване на `lorawan.Mac()`
- инициализация на LoRaWAN стека
- задаване на ключове за join
- join към LoRaWAN мрежа
- изпращане на uplink
- отваряне на receive window от таймер
- приемане на downlink от `DIO1`
- уведомяване на Python callback, който може да събуди `asyncio` приложение
- четене на получените данни чрез `recv()`
- еднократна доставка на LoRaWAN събитие към Python callback
- запазване на последния pending application downlink до прочитане чрез `recv()`
- видим отчет за failed `mp_sched_schedule(...)` / dropped callback delivery
- връщане в състояние на изчакване без активно задържане

## Active Code

Активният кодов път е:

- `lorawan/lorawan.mk`
- `lorawan/mod_lorawan.c`
- `lorawan/mac/LoRaMac.c`
- `lorawan/radio/sx126x/radio.c`
- `lorawan/boards/vk_ra4m2_sx126x/`

## Board Pin Contract

```text
+--------------------------+-----------------+----------+----------------------------------------------+
| SIGNAL                   | MICROPYTHON PIN | RA PORT  | MEANING                                      |
+--------------------------+-----------------+----------+----------------------------------------------+
| DIO1 radio interrupt     | P015            | P0_15    | SX126x interrupt request line.               |
| radio reset              | P001            | P0_1     | SX126x reset line.                           |
| radio busy               | P002            | P0_2     | SX126x busy status input.                    |
| radio chip select        | P206            | P2_6     | SX126x Serial Peripheral Interface select.   |
| radio-frequency switch 1 | P100            | P1_0     | RF switch enable; keep high while LoRa runs. |
| SPI3 serial clock        | P111            | P1_11    | Serial Peripheral Interface clock.           |
| SPI3 master input        | P110            | P1_10    | Radio-to-RA4M2 data line.                    |
| SPI3 master output       | P109            | P1_9     | RA4M2-to-radio data line.                    |
+--------------------------+-----------------+----------+----------------------------------------------+
```

`DIO1` means digital input/output line 1 on the SX126x radio. `SPI3` means Serial Peripheral Interface instance 3 in MicroPython, implemented by RA SCI9 on this board.

## Python Ownership Reference Pattern

The authority ownership pattern follows the proven `micropySX126X` style:

```python
SPI_BUS  = 3
PIN_SCK  = "P111"
PIN_MOSI = "P109"
PIN_MISO = "P110"
PIN_CS   = "P206"
PIN_RST  = "P001"
PIN_BUSY = "P002"
PIN_DIO1 = "P015"

# micropySX126X constructs SPI internally from the supplied pin names.
lora = LoRaWAN(
    spi_bus=SPI_BUS,
    clk=PIN_SCK,
    mosi=PIN_MOSI,
    miso=PIN_MISO,
    cs=PIN_CS,
    irq=PIN_DIO1,
    rst=PIN_RST,
    gpio_busy=PIN_BUSY,
)
```

The Python/MicroPython constructor creates/configures and owns `spi_bus`, `clk`, `mosi`, `miso`, `cs`, `irq`, `rst`, and `gpio_busy`. The C LoRaWAN board layer must not own, deinit, or privately reinitialize those resources.

## SPI Ownership

MicroPython owns the SPI3/SCI9 resource created from `spi_bus=3`, `clk=P111`, `mosi=P109`, and `miso=P110`. The LoRaWAN binding may keep GC-visible references needed for lifetime safety, but ownership and deinit lifecycle remain on the Python/MicroPython side.

The LoRaWAN radio transfer path follows the Renesas/Semtech board contract: CS is asserted, opcode/address/data are shifted byte-by-byte through the standard MicroPython `machine.SPI` protocol on the constructor-owned SPI object, then CS is released and BUSY is polled where upstream does so. The concrete port backend is an implementation detail, not the LoRaWAN ownership boundary.

The existing `machine.SPI` protocol boundary is synchronous and void-returning. A backend submit failure inside the port implementation is not directly observable by the SX126x wrapper; this is an accepted 1:1 wrapper limitation, and diagnostics observe later status/read symptoms instead of adding a second failable SPI layer.

While LoRaWAN is active, user Python code must not deinitialize the constructor-owned SPI resource and must not use the same LoRa pins from another SPI owner. Without private `machine_spi.c` access, LoRaWAN cannot prove the id/configuration or prevent later deinit.

Target design: no Radio/SPI async layer. The wrapper intentionally blocks exactly at the points the Renesas board layer blocks, using byte-by-byte `SpiInOut` semantics.

There is no SPI completion callback in the LoRaWAN authority design.

## LoRaMac Contract And RA4M2 Implementation

LoRaMac uses a synchronous radio contract: it calls `Radio.Rx(...)` or `Radio.Send(...)` and expects the radio operation result to be meaningful at the call boundary.

Target design: the VK_RA4M2 implementation must provide the same visible LoRaWAN result by preserving the original synchronous call boundary. It must not introduce a parallel Radio/SPI state machine.

The implementation must not report final radio success before the required SPI and BUSY work is actually complete.

## Runtime Contract

- Python/MicroPython creates and owns the external resources represented by `spi_bus`, `clk`, `mosi`, `miso`, `cs`, `irq`, `rst`, `gpio_busy`, and `timer`.
- The LoRaWAN binding keeps GC-visible references to those objects while active, but it does not own their deinit lifecycle.
- Timer expiry is armed through the public Python timer object's `init/deinit` methods; the expiry callback immediately enters the C LoRaWAN timer path and requests foreground LoRaMac service.
- The external `DIO1` interrupt requests foreground LoRaMac service through the
  Python-owned `Pin.irq`/`extint` hard rising-edge path; `mac/LoRaMac.c`
  remains the LoRaWAN state authority.
- `MacProcessNotify` requests foreground LoRaMac service; it does not create a second
  LoRaWAN state machine.
- `mac.process()` remains a manual service entry.
- If `mac.process()` reaches radio SPI, it completes the byte-by-byte transfer
  before returning, matching the Renesas board-layer contract.
- The Python callback surface is `set_event_callback(callback)`, using a MicroPython scheduled callback.
- There is no native `asyncio` Stream, Queue, or Event interface in the current binding.
- `asyncio` may be built by user code above the callback, but it does not drive receive timing.

## Current Implementation Gap

The status projection contract remains the design contract. Current code
implements several preconditions, but it still has wrapper-surface gaps:

- fixed: duplicate `lorawan_state_t` / transition helper removed from
  `mod_lorawan`; `status()` is now a binding snapshot/projection
- implemented: Python-owned SPI/timer/pin lifetime roots are present, and the
  public constructor follows the `micropySX126X` pin-name ownership pattern
  above
- implemented: SX126x production transfers use byte-by-byte calls through
  the constructor-owned MicroPython SPI object, matching the Renesas
  `SpiInOut` shape without taking private SPI ownership
- implemented: schedule failure/no-callback drops increment
  `event_drop_count`, exposed through `status()`
- not required: top-level `lorawan_radio_state_t radio_state` / saved LoRaMac
  resume descriptor; LoRaMac remains the only protocol FSM
- current 1:1 wrapper policy: no C-side event queue; events are delivered by
  one MicroPython scheduled callback attempt and failed delivery is counted
- current 1:1 wrapper policy: no receive queue; `recv()` reads the latest
  single pending application downlink (`rx_pending`)
- implemented: `status()` exposes the callback/drop delivery counter required
  by `PORT_REQUIREMENTS.md`
- gap: no Python wrapper with `asyncio.ThreadSafeFlag`

## Design Completion

Дизайнът се счита за завършен, когато са реализирани тези елементи:

```text
+-------+--------------------------------+----------------------------------------------+
| ORDER | ELEMENT                        | MEANING                                      |
+-------+--------------------------------+----------------------------------------------+
| 1     | LoRaMac state projection        | `status()` reports binding state without     |
|       |                                | duplicating upstream `MacCtx.MacState`.      |
| 2     | Radio/SPI transport             | Byte-by-byte Renesas/Semtech SPI semantics   |
|       |                                | with BUSY polling and no parallel MAC FSM.   |
| 3     | LoRaMac synchronous boundary    | Radio.Rx and Radio.Send return only after    |
|       |                                | required SPI/BUSY work is complete.          |
| 4     | Schedule/drop counters          | Counters record failed `mp_sched_schedule`   |
|       |                                | calls or missing callback delivery.          |
| 5     | asyncio wrapper                 | Python wrapper uses `asyncio.ThreadSafeFlag` |
|       |                                | to wake async user code safely.              |
| 6     | Optional queues                 | Python queue-style helpers may be added      |
|       |                                | after the core driver path is stable.        |
+-------+--------------------------------+----------------------------------------------+
```

Ring buffers are not part of the current authority contract. They may be added later as optional Python-level helpers only after the 1:1 wrapper path is stable.

## Document Order

Primary documents:

1. `AUTHORITATIVE_DOCS_AND_CURRENT_GOAL.md`
2. `PORT_REQUIREMENTS.md`
3. `LORAWAN_LIMITATIONS.md`
4. `LORAWAN_STATUS_PROJECTION_CONTRACT.md`
5. `LORAWAN_SEND_BINDING_PROJECTION_CONTRACT.md`
6. `LORAWAN_IMPLEMENTATION_PLAN_5_POINTS.md`
7. `LORAWAN_ARCH_ANALYSIS_AND_C_PUMP_PROPOSAL.md`
8. `README.md`

Derived current documents:

9. `ARCHITECT_DOC_ANALYSIS_V2_2026_05_20.md` (superseded notice)
10. `ARCHITECT_DOC_DIGEST_2026_05_20.md`
11. `LORAWAN_DOCS_COMPRESSED_MODEL.md`

Derived documents must match the primary documents. If a derived document and a primary document differ, the primary document is the source of truth.
