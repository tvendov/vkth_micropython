# LoRaWAN Port Requirements

Date: 2026-05-21
Doc-Version: 1.0
Status: Primary authority contract; implementation gaps are tracked explicitly.

## Purpose

Create a LoRaWAN driver for MicroPython on RA4M2 without duplicating the
upstream LoRaMac protocol stack.

## Required Python Interface

The driver must support these MicroPython entries:

```text
lorawan.Mac(...)
lorawan_init()
init_defaults()
set_keys(deveui, joineui, appkey)
join()
send(port, payload)
recv()
status()
is_joined()
set_event_callback(callback)
dio1_isr(pin)
process()
```

`process()` is a manual foreground service entry. Normal join/send/receive
timing must not depend on a Python polling loop.

No raw radio/SPI diagnostic helper is part of the public binding surface.

## Board Pin Requirements

```text
+--------------------------+-----------------+----------+----------------------------------------------+
| SIGNAL                   | MICROPYTHON PIN | RA PORT  | REQUIREMENT                                  |
+--------------------------+-----------------+----------+----------------------------------------------+
| DIO1 radio interrupt     | P015            | P0_15    | Configure as rising hard interrupt.          |
| radio reset              | P001            | P0_1     | Python-created pin; board layer toggles it.  |
| radio busy               | P002            | P0_2     | Polled status input, not ICU interrupt.      |
| radio chip select        | P206            | P2_6     | Active radio chip-select signal.             |
| radio-frequency switch 1 | P100            | P1_0     | High while LoRa radio path is used.          |
| SPI3 serial clock        | P111            | P1_11    | SPI clock.                                   |
| SPI3 master input        | P110            | P1_10    | Radio-to-RA4M2 data line.                    |
| SPI3 master output       | P109            | P1_9     | RA4M2-to-radio data line.                    |
+--------------------------+-----------------+----------+----------------------------------------------+
```

`SPI3` is the MicroPython SPI instance implemented by RA SCI9 on this board.

## Python Resource Ownership

The public ownership pattern follows the `micropySX126X` style:

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

The constructor creates/configures the MicroPython SPI and pin resources.
MicroPython/Python owns `spi_bus`, `clk`, `mosi`, `miso`, `cs`, `irq`, `rst`,
and `gpio_busy`.

The C LoRaWAN board layer may keep lifetime references and use the agreed pins,
but it must not privately create, own, deinitialize, or reinitialize duplicate
SPI/pin objects.

While LoRaWAN is active, user code must not call `spi.deinit()` on the owned SPI
resource and must not use another SPI owner on the same radio pins. The C
binding cannot reliably detect that violation without private `machine_spi.c`
access.

## SPI And BUSY Requirements

- SX126x transfers use the constructor-owned MicroPython SPI object
  byte-by-byte, matching the Renesas `SpiInOut` board sources.
- The Renesas RA `machine.SPI` backend is a void synchronous transfer
  boundary; submit failures inside that backend are an accepted 1:1 wrapper
  limitation and are surfaced only indirectly through later radio
  status/read diagnostics.
- The radio hot path must not build a batched async layer.
- CS remains asserted across the opcode/address/data byte sequence.
- `BUSY` on P002 is a polled status signal. It is not an ICU interrupt source.
- BUSY polling follows the same synchronous board-layer boundary as upstream.
- `Radio.Rx(...)` and `Radio.Send(...)` must not report final success before
  required SPI/BUSY work is complete.

## Timing Requirements

- Python creates and passes the timer object.
- LoRaWAN arms/disarms that timer only through the public timer methods.
- Timer expiry immediately enters the C timer service path.
- The binding arms the Python-owned DIO1 pin through the public
  `Pin.irq`/`extint` path with `hard=True` and rising-edge trigger.
- The DIO1 callback body enters the C radio interrupt service path.
- Python receives results through scheduled callbacks; Python does not open RX1
  or RX2 windows.

## State Authority Requirements

`mac/LoRaMac.c` is the only LoRaWAN protocol state machine.

The binding must not implement or store a parallel LoRaWAN state machine.

Forbidden in `mod_lorawan.c`:

```text
lorawan_state_t
self->state
LORAWAN_STATE_* transition helper
join/send/RX-window transition table
copied MacCtx.MacState lifecycle bits
```

Allowed in `mod_lorawan.c`:

```text
Python object lifetime flags
Python SPI/timer/pin/callback roots
pending API request metadata
last event delivery metadata
single pending receive payload
status() projection labels
```

Allowed in the board layer:

```text
chip-select state
BUSY polling phase
DIO1 edge latch
transport-local guards
```

`status()` is a binding snapshot/projection. It may expose derived labels such
as joined, mac_busy, rx_pending, spi_pinned, and state_authority, but those
labels must be derived from LoRaMac public APIs, callbacks, MIB values,
binding-local facts, and board transport facts.

## Foreground Service Requirements

- `lorawan_pump_request_dio1()` requests bounded foreground LoRaMac service.
- `mod_lorawan.c` coordinates calls to public LoRaMac service entry points.
- `mod_lorawan.c` is not a second LoRaWAN protocol scheduler.
- Foreground service execution must have a re-entry guard.
- Foreground service execution may enter synchronous Renesas-compatible radio SPI through
  LoRaMac public calls.
- Foreground service execution must not hide a second LoRaWAN protocol scheduler.
- Timer, DIO1, and MacProcessNotify may request service.
- Hard interrupt paths must set C facts and return quickly.

## Python Notification Requirements

- C events are delivered through `set_event_callback(callback)`.
- LoRaWAN confirm/indication/error events are delivered through one
  `mp_sched_schedule(...)` attempt to the user callback.
- If scheduling fails or no callback is registered, a drop counter increments.
- The C binding does not expose native `asyncio` Stream, Queue, or Event.
- A Python wrapper may use `asyncio.ThreadSafeFlag` above the C callback.
- `recv()` remains synchronous at the C binding level.

## Design Completion Requirements

```text
+-------+--------------------------------+----------------------------------------------+
| ORDER | REQUIRED PART                  | REQUIRED BEHAVIOR                            |
+-------+--------------------------------+----------------------------------------------+
| 1     | LoRaMac status projection      | Report derived labels without duplicate FSM. |
| 2     | Radio/SPI transport             | Byte-by-byte Renesas/Semtech SPI semantics. |
| 3     | LoRaMac synchronous boundary    | Return after required SPI/BUSY work is done. |
| 4     | Schedule/drop counters          | Report scheduler/callback delivery drops.    |
| 5     | Python asyncio wrapper          | Wake async code via asyncio.ThreadSafeFlag.  |
| 6     | Optional queues                 | Add higher-level helpers after core works.   |
+-------+--------------------------------+----------------------------------------------+
```

## Driver Acceptance

The driver is acceptable when these work without a second protocol FSM:

```text
stack initialization
key setup
join request and join confirm
uplink request and uplink confirm
receive window 1
receive window 2
downlink indication
Python notification
recv() readout
error notification and retry path
status() projection from real LoRaMac/binding/transport facts
schedule failure and callback/drop counters
async Python wake through asyncio.ThreadSafeFlag wrapper
```

## Code Placement

- Binding code stays in `mod_lorawan.c`.
- Board-specific radio/timer/interrupt transport code stays in
  `boards/vk_ra4m2_sx126x/`.
- Vendor LoRaMac core changes stay minimal and diagnostic-only when possible.
