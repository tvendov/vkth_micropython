## EK_RA4W1 BLE advertising verification (2026-01-14)

### Goal
Make MicroPython on EK_RA4W1 advertise over BLE so it is visible in a scanner (nRF Connect / Android Bluetooth scanner) by device name.

### How advertising is started in this port
- `import bluetooth` loads a frozen shim: `ports/renesas-ra/boards/EK_RA4W1/bluetooth.py`
- `BLE.gap_advertise(interval_us, ...)` maps to `renesas_ble.advertise(gap_name, interval_ms)`

### Test script
File: `ble_adv_test.py`
- enables BLE
- sets `gap_name`
- starts advertising
- keeps running

### Run steps
1. Flash EK_RA4W1 MicroPython build that has BLE enabled.
2. Open a UART/Serial terminal to the MicroPython REPL (115200 8N1).
3. Paste/run the contents of `ble_adv_test.py` in the REPL (use MicroPython paste mode if available).
4. Open nRF Connect (or Android Bluetooth scanner) and scan.
5. Expected result: device appears as `EK_RA4W1_MPY`.

### Build artifact location (local workspace)
- `ports/renesas-ra/build-EK_RA4W1/firmware.bin`
- `ports/renesas-ra/build-EK_RA4W1/firmware.hex`
- `ports/renesas-ra/build-EK_RA4W1/firmware.elf`

### Notes / troubleshooting
- If `import bluetooth` fails: BLE is not present in this firmware build.
- If advertising does not appear: confirm the firmware was built for BOARD=EK_RA4W1 with BLE enabled and that the device does not crash when BLE is enabled.
