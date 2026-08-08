# renesas-ra LoRaWAN

Date: 2026-05-20
Doc-Version: 1.0
Status: Primary authority contract; implementation gaps are tracked explicitly.

Цел: създаване на LoRaWAN драйвер за MicroPython RA4M2.

## Authority

Само документите в `ports/renesas-ra/lorawan/` са авторитетни.

## Files

- `lorawan.mk` builds the LoRaWAN port files.
- `mod_lorawan.c` exposes `lorawan.Mac()` to MicroPython.
- `mac/LoRaMac.c` contains the imported LoRaWAN stack.
- `radio/sx126x/radio.c` connects LoRaMac to the radio driver.
- `boards/vk_ra4m2_sx126x/` contains the RA4M2 and SX126x board code.

## VK_RA4M2 LoRa Pins

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

## SPI Ownership

The Python/MicroPython ownership pattern follows the proven `micropySX126X` constructor style:

```python
SPI_BUS  = 3
PIN_SCK  = "P111"
PIN_MOSI = "P109"
PIN_MISO = "P110"
PIN_CS   = "P206"
PIN_RST  = "P001"
PIN_BUSY = "P002"
PIN_DIO1 = "P015"

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

The constructor creates/configures and owns `spi_bus`, `clk`, `mosi`, `miso`, `cs`, `irq`, `rst`, and `gpio_busy`. The LoRaWAN C board layer keeps only the lifetime references it needs and must not own, deinit, or privately reinitialize those resources.

The LoRaWAN radio transfer path follows the Renesas/Semtech board shape: CS is asserted, opcode/address/data are shifted byte-by-byte through the constructor-owned MicroPython `machine.SPI` object, CS is released, and BUSY is polled where upstream waits. LoRaWAN does not own or call private SPI backend state directly.

While LoRaWAN is active, user Python code must not deinitialize the constructor-owned SPI resource and must not use another SPI owner on the same pins.

The target LoRaWAN SPI path is intentionally synchronous at the same boundary as the Renesas board layer. It does not add a separate async Radio/SPI layer.

LoRaMac keeps a synchronous radio contract: `Radio.Rx(...)` and `Radio.Send(...)` must have meaningful results when they return. The VK_RA4M2 wrapper preserves that contract.

## Runtime

The runtime path is:

`MicroPython -> LoRaWAN binding -> LoRaMac -> radio -> SX126x -> RA4M2 peripherals`

Python creates and owns the timer object passed to the LoRaWAN constructor as `timer=...`. Timer expiry, `DIO1`, and `MacProcessNotify` enter LoRaWAN work through the foreground service path in `mod_lorawan.c`.

If a foreground service step reaches radio SPI through LoRaMac, that SPI work completes before the call returns, matching the Renesas board-layer contract.

Python receives events through `set_event_callback(callback)`. The binding uses MicroPython scheduled callback delivery. It does not expose a native `asyncio` Stream, Queue, or Event.

`asyncio` user code may wrap that callback and read data with `recv()`, but it does not drive receive timing.

Current limits: there is no `await mac.recv()`, no native `asyncio.Queue`, no native `asyncio.Event`, and no stream interface.

Completion also requires visible counters for failed `mp_sched_schedule(...)` calls and callback/drop delivery.

## Design Completion

The driver design is complete when these parts exist in this order:

```text
+-------+--------------------------------+----------------------------------------------+
| ORDER | PART                           | PURPOSE                                      |
+-------+--------------------------------+----------------------------------------------+
| 1     | LoRaMac state projection        | Report derived binding labels; do not       |
|       |                                | duplicate `MacCtx.MacState`.                |
| 2     | Radio/SPI transport             | Byte-by-byte Renesas/Semtech SPI semantics. |
| 3     | LoRaMac synchronous boundary    | Return after required SPI/BUSY work is done. |
| 4     | Schedule/drop counters          | Report failed scheduling/callback delivery.  |
| 5     | asyncio wrapper                 | Wake async Python through                    |
|       |                                | `asyncio.ThreadSafeFlag`.                    |
| 6     | Optional queues                 | Add higher-level queue helpers later.        |
+-------+--------------------------------+----------------------------------------------+
```

`LoRaMac state projection` means `status()` derives labels from LoRaMac public
APIs, callbacks, MIB values, binding flags, and transport flags. It is not a
stored `lorawan_state_t` protocol FSM in `mod_lorawan.c`.

`Optional queues` means Python-level helpers above the C binding; the current C binding does not add event or receive queues.

`asyncio.ThreadSafeFlag` means the MicroPython asyncio object that can be set from callback context and awaited by an async task.

## Authority Documents

The LoRaWAN authority and status projection contract is in
`LORAWAN_STATUS_PROJECTION_CONTRACT.md`.

The send binding projection contract is in `LORAWAN_SEND_BINDING_PROJECTION_CONTRACT.md`.

The 5-point implementation plan is in `LORAWAN_IMPLEMENTATION_PLAN_5_POINTS.md`.

Current design limitations are in `LORAWAN_LIMITATIONS.md`.
