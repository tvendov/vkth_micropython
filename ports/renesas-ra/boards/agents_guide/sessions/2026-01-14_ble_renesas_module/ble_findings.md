## Session 2026-01-14: EK_RA4W1 BLE (renesas_ble) implementation notes

### What I located

- MicroPython module: `ports/renesas-ra/modble_renesas.c`
  - Registers as `renesas_ble` via `MP_REGISTER_MODULE(MP_QSTR_renesas_ble, ...)`.
  - Exposes: `active(state)`, `advertise(name, interval_ms)`, `stop_advertise()`, `on(event, cb)`,
    `notify(conn, attr, data)`, `indicate(conn, attr, data)`, `disconnect(conn)`, `get_stats()`.

- FSP BLE wrapper layer: `ports/renesas-ra/ble/ra_ble.c` + `ports/renesas-ra/ble/ra_ble.h`
  - Provides `ra_ble_init/deinit`, GAP start/stop adv, disconnect, GATTS notify/indicate,
    device-name storage, get-address, and an event pump.

- FSP-generated / stack configuration data: `ports/renesas-ra/ble/ra_ble_config.c`
  - Contains heap/buffer config for the Compact BLE stack and a minimal GATT DB (Device Info).

### Advertising path (high level)

1. Python user code typically calls `import renesas_ble` or `import bluetooth`.
2. `bluetooth.py` (EK_RA4W1 shim) maps `BLE.gap_advertise()` to `renesas_ble.advertise(name, interval_ms)`.
3. `modble_renesas.c` builds legacy ADV + scan-response payloads (Flags + name) and calls:
   - `ra_ble_gap_set_device_name(name, ...)`
   - `ra_ble_gap_start_advertising(&adv_params)`
4. `ra_ble.c` configures advertising via FSP calls:
   - `R_BLE_Open()`, `R_BLE_GAP_Init()`, `R_BLE_GATT_Init()`, `R_BLE_GATTS_Init()`
   - `R_BLE_GAP_SetAdvParam(...)`, `R_BLE_GAP_SetAdvSresData(...)`, `R_BLE_GAP_StartAdv(...)`

### Notes / gotchas

- The EK_RA4W1 board sources reserve IRQ 8 for BLE (see generated `vector_data.h`):
  - `Reserved0_IRQn = 8` with comment `/* Reserved for BLE Interrupt */`.
  - So if you see “reserved IRQ8”, that is expected when BLE is enabled.

- The Renesas BLE manual PDF we looked at is not text-searchable for symbols like
  `R_BLE_GAP_StartAdv` (table of contents is searchable, API names are not).

### Likely next improvements (if needed)

- `bluetooth.py` currently ignores `adv_data`/`resp_data` arguments and only uses `gap_name`.
  If you want raw ADV/RESP payload control from Python, we can extend `renesas_ble.advertise()`
  to accept optional `adv_data` and `resp_data` (bytes) and pass them through to `ra_ble_gap_start_advertising()`.
