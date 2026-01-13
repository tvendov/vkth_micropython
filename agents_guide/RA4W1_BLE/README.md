# RA4W1 BLE Integration

This directory contains documentation and session notes for integrating native BLE support into the Renesas RA4W1 MicroPython port.

## Quick Links

- **[Integration Plan](INTEGRATION_PLAN.md)** - Overall architecture and implementation phases
- **[Event Queue Design](EVENT_QUEUE_DESIGN.md)** - IRQ-safe event handling system
- **[FSP Development Guide](FSP_DEVELOPMENT_GUIDE.md)** - Working with Renesas FSP BLE stack
- **[Latest Session](SESSION_2026-01-07.md)** - Current progress and next steps

## Status

**Phase 3 Complete** - Infrastructure implemented, ready for FSP BLE library integration.

### Completed
- ✅ Event queue system (lock-free ring buffer)
- ✅ BLE wrapper layer (stub API)
- ✅ MicroPython module (`renesas_ble`)
- ✅ Build system integration
- ✅ Documentation

### Next Steps
1. Obtain FSP BLE library
2. Implement FSP callbacks in `ra_ble.c`
3. Integrate event processing in main loop
4. Build and test on hardware

## Implementation Files

```
ports/renesas-ra/
├── ble/
│   ├── ra_ble_events.h/c    # Event queue system
│   └── ra_ble.h/c           # BLE wrapper layer
└── modble_renesas.h/c       # MicroPython module
```

## Build

```bash
cd ports/renesas-ra
make BOARD=EK_RA4W1 MICROPY_HW_ENABLE_BLE=1
```

## Python API Example

```python
import renesas_ble as ble

# Initialize BLE
ble.active(True)

# Start advertising
ble.advertise("RA4W1", interval_ms=100)

# Register callbacks
def on_connect(conn_handle):
    print(f"Connected: {conn_handle}")

ble.on("connect", on_connect)

# Send notification
ble.notify(conn_handle, attr_handle, b"Hello")

# Get statistics
stats = ble.get_stats()
print(f"Events: {stats}")
```

## References

- [Renesas FSP](https://github.com/renesas/fsp)
- [RA4W1 Datasheet](https://www.renesas.com/us/en/products/microcontrollers-microprocessors/ra-cortex-m-mcus/ra4w1-group)
- [MicroPython BLE](https://docs.micropython.org/en/latest/library/bluetooth.html)

