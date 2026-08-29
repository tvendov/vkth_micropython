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
