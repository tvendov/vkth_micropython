# Thermal 10 FPS Port Build Log

This file records every VK_RA6M3 port recompilation made for the dual thermal-camera project.

## 2026-08-29 05:37 +03:00 - Async I2C milestone, build 1

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`
- Intended change: add non-blocking RA RIIC `start/poll/cancel` support and expose `machine.I2CAsync` with a rooted destination buffer.
- Result: **FAILED** (`make` exit code 2).
- Compiler error: `machine_i2c.c:492: MICROPY_EVENT_POLL_HOOK undeclared` in the optional blocking `wait()` wrapper.
- Confirmed before failure: generated root pointers, `ra_i2c.c`, and the new async transaction structures compiled successfully.
- Firmware artifact: none; the linker did not run.
- Next action: replace the unsupported poll hook with the port-supported pending-event handler and recompile.

## 2026-08-29 05:42 +03:00 - Async I2C milestone, build 2

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`
- Change since build 1: replaced the unavailable `MICROPY_EVENT_POLL_HOOK` in `I2CAsync.wait()` with the port-supported `mp_handle_pending(true)` call.
- Result: **SUCCESS** (`make` exit code 0).
- Link report: `text=1413108`, `data=0`, `bss=647244`, total `2060352` bytes.
- `firmware.bin`: 1,413,092 bytes; SHA-256 `6cb1990ba7a1c77829bd01d11bfdb8290ef1c5e5f95b19f1135567169165e06c`.
- `firmware.hex`: 3,974,801 bytes; SHA-256 `d24b5d10713eeab08046e693b2237e02cf987347e4046fe925374b6b37d61b19`.
- `firmware.elf`: 15,591,232 bytes; SHA-256 `0250d5a6065dc0dba1b86206d08bca20b111f9250dc2239ee2a6eb1f5288f0e9`.
- Next action: visible J-Link reset, flash this exact firmware, then validate asynchronous RIIC timing and overlap on the board.

## 2026-08-29 05:56 +03:00 - Async I2C recovery, build 3

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Intended change: make `cancel()` drain a healthy transfer before forced recovery, add keyword arguments to `readinto()`, and reject wrapper reconstruction while active.
- Result: **FAILED** (`make` exit code 1).
- Compiler error: `machine_i2c.c:155` passed the stored SCL/SDA pin-object pointers to `ra_i2c_init()`, which requires numeric pin identifiers.
- Firmware artifact: none; the linker did not run.
- Next action: pass `self->bus->scl->pin` and `self->bus->sda->pin`, then perform another clean build.

## 2026-08-29 06:02 +03:00 - Async I2C recovery, build 4

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 3: the forced-recovery reinitialization now passes the numeric SCL/SDA pin identifiers.
- Result: **SUCCESS** (`make` exit code 0).
- Link report: `text=1413396`, `data=0`, `bss=647244`, total `2060640` bytes.
- `firmware.bin`: 1,413,380 bytes; SHA-256 `9a56b04fecd2b55cff10f63ee1dbe7e1a7b9b8ca79692f8925f6879c79dd09d3`.
- `firmware.hex`: 3,975,611 bytes; SHA-256 `e0791a893a261e162eef8ece15070ac79bef8ad01f9f3f355bcf1fcce3bf388f`.
- `firmware.elf`: 15,594,288 bytes; SHA-256 `a973a8c8baad25c6c85768e62acaefcedcc1b8cdbc47320919a3373290a033d5`.
- Next action: visible J-Link reset, BIN programming and verification, then repeat cancel/timeout/heap-lock/overlap hardware tests.

### Build 4 flash and hardware validation

- A separate visible J-Link reset was performed before programming.
- J-Link BIN programming reported `Program & Verify O.K.` and the explicit `verifybin` pass reported `Verify successful`.
- One 1664-byte MLX90640 read at 400 kHz starts in 99 us and completes during the 43.012 ms MicroPython native/FPU temperature calculation; measured overlapped total is 43.138 ms.
- The pointer-write + asynchronous read + polling sequence completed under `micropython.heap_lock()` and returned all 1664 bytes.
- Reconstructing `I2CAsync` during an active transfer raises `EBUSY` (16).
- Immediate `cancel()` drains the transfer in 33.803 ms, reports `ECANCELED` (125), and the next scan still finds `0x33`, `0x47`, and `0x68`.
- An intentional 1 ms timeout drains safely in 34.229 ms, reports `ETIMEDOUT` (110), and the next scan still finds all three devices.
- A NACK at `0x7e` reports `ENODEV` (19); the following scan remains healthy.
- Milestone status: **hardware validated** for interrupt-driven RIIC overlap, error handling, timeout/cancel recovery, and allocation-free normal operation.

## 2026-08-29 07:33 +03:00 - Python FPU dual-camera 10 FPS milestone

- Port recompilation: **none**. This milestone uses the already built and flashed build 4 firmware.
- Project boundary: sensor drivers, MLX90640 temperature calculation, interpolation, color conversion, and screen composition remain MicroPython code. The MLX90640 pixel kernel uses RA6M3 single-precision FPU instructions from `@micropython.asm_thumb`; it was not moved to a C module.
- Bus configuration: one physical RIIC/I2C controller at 400 kHz for AMG8833 (`0x68`) and MLX90640 (`0x33`). The device at `0x47` remains visible on the same bus.
- Screen structure: AMG8833 RAW above bilinear on the left; MLX90640 RAW above bilinear on the right. LVGL 9.4 draws the static layout once, then four complete RGB565 staging buffers are copied to the DIRECT GLCDC framebuffer after VSYNC.
- Synthetic 768-pixel MLX FPU kernel time: approximately `3.006 ms`.
- Real MLX frame comparison: exact two-subpage Python-native calculation `101.486 ms`; shared 768-pixel FPU kernel `2.500 ms`.
- Accuracy after one warm frame: maximum absolute difference `0.0007553102 deg C`; mean absolute difference `0.00007465484 deg C`.
- Twenty complete back-to-back visible frames: mean `53.491 ms`, equivalent processing capacity `18.69 FPS`.
- Five-second timer test: `50` frames in `5.011 s`, or `9.97 FPS`; `update_failed=False`.
- One-minute timer soak: `600` frames in `60.000 s`, or exactly `10.00 FPS`; `update_failed=False`.
- Heap stability during the one-minute soak: free heap after GC changed from `91664` to `91632` bytes (`-32` bytes).
- Allocation test: `APP.verify_no_alloc(10)` returned `0`; all ten complete read/compute/display cycles passed with the MicroPython heap locked.
- Post-test I2C scan: `[0x33, 0x47, 0x68]`.
- Visual validation: the real GLCDC framebuffer capture shows all four correctly positioned RAW/bilinear fields without overlap.
- Beginner documentation: `THERMAL_DUAL_RA6M3_10FPS_BG.md` and `THERMAL_DUAL_RA6M3_10FPS_BG.docx` were generated in `C:\Users\teodor\Desktop\stem\mpy_drivers`; all 10 DOCX pages were rendered and inspected.
- Milestone status: **hardware validated at 10 FPS for both sensors on one 400 kHz I2C bus**.

## 2026-08-29 15:56 +03:00 - Event-driven I2C completion, build 5

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Intended change: notify MicroPython exactly once when an RIIC asynchronous transfer reaches a terminal state, without calling Python from the hardware ISR.
- Implementation: the RIIC layer emits one C terminal hook; `machine.I2CAsync` uses one static scheduler node, one rooted persistent Python handler, and one static one-shot timeout watchdog per RIIC bus.
- API added: `I2CAsync.irq(handler)`; `handler(transfer)` runs after the C layer has finalized the result, released the bus, and removed the destination-buffer root.
- Result: **SUCCESS** (`make` exit code 0).
- Generated-root check: both `machine_i2c_async_buffer_roots[3]` and `machine_i2c_async_callback_roots[3]` are present in `build-VK_RA6M3/genhdr/root_pointers.h`.
- Link report: `text=1414056`, `data=0`, `bss=647420`, total `2061476` bytes.
- `firmware.bin`: 1,414,040 bytes; SHA-256 `95863f6ce3efb948170dc7d54a66d0ba52a9d25148e6f5bfbf43153e1f1cd346`.
- `firmware.hex`: 3,977,477 bytes; SHA-256 `cf48859faaeaa01e8b84257bf535dc5f615a104a586934db79e0f87d8cc3c930`.
- `firmware.elf`: 15,601,160 bytes; SHA-256 `9b68e70472a5366c2e50195f84490d768357bea1ff0e8a0d1c05c00854e721aa`.
- Flash status: not yet flashed at this point.
- Next action: visible J-Link reset, program and verify this exact BIN, then test success, NACK, cancel, timeout, exactly-once delivery, callback chaining, heartbeat progress, and heap-lock behavior.

### Build 5 flash and hardware validation

- A separate visible J-Link reset completed with exit code 0 before programming.
- Visible J-Link BIN programming completed with exit code 0; its log reports `Program & Verify` for the complete 1,414,040-byte image.
- An independent `verifybin` session read and compared exactly 1,414,040 bytes at `0x00000000` and completed with exit code 0.
- Event-driven 1664-byte MLX90640 read: callback result `1664`, `35` independent heartbeat iterations, `34.034 ms` from start to deferred Python handler, and exactly one callback.
- Callback chaining: the first handler started the second AMG8833 read; both returned `2` bytes and exactly two callbacks were observed.
- Error delivery: NACK produced `ENODEV` (`19`), cancel produced `ECANCELED` (`125`), and the static 1 ms watchdog produced `ETIMEDOUT` (`110`), each exactly once.
- Responsiveness: `cancel()` returned in `44 us`; the timeout handler arrived about `840 us` after transfer start in the measured run.
- Allocation test: pointer write + asynchronous read + scheduler notification + Python handler completed under `micropython.heap_lock()` with one callback and restored lock depth `0`.
- Stock `asyncio.ThreadSafeFlag` behavior: a waiting transfer task and an independent heartbeat task ran correctly (`35` heartbeat iterations), proving the desired await-message control flow.
- Stock asyncio allocation gate: both creation of `ThreadSafeFlag.wait()` and insertion into its `IOQueue` raised `MemoryError` under heap lock. Therefore stock asyncio is functionally suitable but does not meet the project's no-allocation-after-init rule.
- Post-test I2C scan: `[0x33, 0x47, 0x68]`.
- Review finding before production: scheduler completion must only gate transfers when `irq(handler)` is installed, so the legacy polling-only API remains compatible. This compatibility adjustment is assigned to build 6.

## 2026-08-29 16:12 +03:00 - Event-driven I2C compatibility, build 6

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 5: a static completion node is scheduled only when `I2CAsync.irq(handler)` is installed. Without a handler, `done()`, `result()`, and `wait()` retain their previous polling-only behavior and do not wait for a notification that nobody consumes.
- `cancel()` now finalizes the result immediately after the bounded hardware abort/reinitialization; with a handler, the already scheduled node still delivers exactly one deferred callback.
- Result: **SUCCESS** (`make` exit code 0).
- Link report: `text=1414064`, `data=0`, `bss=647420`, total `2061484` bytes.
- `firmware.bin`: 1,414,048 bytes; SHA-256 `fa7b243272302f3f68e4df4c6457c9f2ce7d2f8339d6496d302400307b2c930a`.
- `firmware.hex`: 3,977,493 bytes; SHA-256 `50521980e076b5959b970019631e3707d267c4e7e80b8ac2924e8db75d5b2927`.
- `firmware.elf`: 15,601,332 bytes; SHA-256 `7caabebe1747f9304e41956a0c5c4b7b7736bf2ef0ea611f82ab3c48fe25732b`.
- Flash status: not yet flashed at this point.
- Next action: visible J-Link reset, program and verify build 6, rerun the event-driven HIL matrix, then run a dedicated no-handler polling regression.

### Build 6 flash and hardware validation

- A new visible J-Link reset completed with exit code 0 before build 6 was programmed.
- Visible BIN programming completed with exit code 0; J-Link reports `Program & Verify` for the full image.
- Independent `verifybin` read and compared exactly 1,414,048 bytes at `0x00000000` and completed with exit code 0.
- Polling compatibility: with `irq(None)`, the first AMG8833 read completed through `done()/result()`, a second read started immediately, and `wait()` returned `2`; no hidden notification gate remained.
- Event-driven MLX90640 read: callback result `1664`, `35` independent heartbeat iterations, about `34.378 ms` start-to-handler time, exactly one callback.
- Callback chaining, NACK (`19`), cancel (`125`), 1 ms timeout (`110`), and heap-lock callback tests all passed exactly once.
- `cancel()` returned in `46 us`; measured timeout callback arrival was about `842 us` after transfer start.
- Stock asyncio message flow passed with `35` heartbeat iterations, while both no-allocation probes still correctly demonstrated that stock `ThreadSafeFlag.wait()` allocates.
- Final I2C scan: `[0x33, 0x47, 0x68]`.
- Reusable HIL test: `boards/VK_RA6M3/examples/i2c_async_notify_hil.py`.
- Milestone status: **hardware validated** for exactly-once completion notification, autonomous handler-mode timeout, callback chaining, polling compatibility, and allocation-free handler delivery.
