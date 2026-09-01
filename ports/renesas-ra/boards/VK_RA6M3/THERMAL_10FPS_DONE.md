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

## 2026-08-29 17:15 +03:00 - DTC-backed RIIC receive, build 7

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Reason for the change: build 6 was interrupt-driven but still entered the RXI handler once per received byte. At 16 MLX90640 subpages per second, the 1664-byte RAM reads alone would cause 26,624 byte RX interrupts per second.
- Slave-I2C review: its hardware RX path confirms the working DTC source/destination model (`ICDRR` fixed to incrementing RAM, one-byte transfers, `TRANSFER_IRQ_END`). The master implementation follows the stricter Renesas FSP receive sequence instead of copying the 48-byte slave buffer logic.
- Master receive split: DTC transfers `N - 3` bytes. The CPU handles the initial dummy RXI, the DTC end RXI, and the final three RXIs that program WAIT, NACK and STOP.
- Error cleanup: NACK, arbitration loss, timeout, STOP, explicit cancel, bus-start failure and deinitialization all close the DTC activation before RIIC recovery continues.
- Diagnostic API: `I2CAsync.stats()` returns `(rxi_irq_count, dtc_transfer_count, dtc_bytes, dtc_fallback_count)` for the most recent transfer.
- HIL acceptance target for one 1664-byte MLX read: at most 6 RXI handler entries, exactly 1 DTC transfer, exactly 1661 DTC bytes, and 0 fallbacks.
- Result: **SUCCESS** (`make` exit code 0).
- Generated API check: `MP_QSTR_stats` is present; both asynchronous buffer and callback roots remain present in `root_pointers.h`.
- Link report: `text=1414656`, `data=0`, `bss=647500`, total `2062156` bytes.
- `firmware.bin`: 1,414,640 bytes; SHA-256 `cc8d83204e03d8bfcce3706440c5aa26b43136848bf4c4f6b06d36546522c48a`.
- `firmware.hex`: 3,979,158 bytes; SHA-256 `afecaff72001600c4b981810f7949f4d7a8371d28632a4844211964aa0285efd`.
- `firmware.elf`: 15,608,284 bytes; SHA-256 `55a65c9e28dbd2e4af754c8ee2f45b686756daba01141db92e7c4c5ce2b76b76`.
- Flash status: not yet flashed at this point. Build success alone does not prove that DTC consumed the RXI events.
- Next action: visible J-Link reset, program and independently verify this exact BIN, then run the DTC-count HIL test and the complete notification/error regression matrix.

### Build 7 flash and DTC hardware validation

- A separate visible J-Link reset completed with exit code 0 before programming. Its log identifies the Cortex-M4 and two successful normal reset operations.
- The following visible J-Link programming session completed with exit code 0 and reported `Program & Verify` for build 7.
- An independent visible `verifybin` session read and compared exactly 1,414,640 firmware bytes from `0x00000000` and completed with exit code 0.
- MLX90640 1664-byte read: callback result `1664`, about `34.042 ms`, `35` independent heartbeat iterations, and stats `(5, 1, 1661, 0)`.
- AMG8833 128-byte read: callback result `128`, `4` independent heartbeat iterations, and stats `(5, 1, 125, 0)`.
- Interpretation: both long reads entered the CPU RXI handler only five times. DTC moved every payload byte except the final three required for the RIIC WAIT/NACK/STOP sequence, with no fallback.
- Exactly-once callback chaining, NACK (`19`), cancel (`125`), 1 ms timeout (`110`), heap-lock callback delivery, polling compatibility and stock-asyncio behavior all passed again.
- The measured cancel call returned in `204 us` while DTC was active; the autonomous timeout callback arrived in about `843 us`.
- Final I2C scan: `[0x33, 0x47, 0x68]`.
- Milestone status: **hardware validated** for DTC-backed RIIC receive and deferred completion notification on both thermal sensor frame sizes.

## 2026-08-29 17:32 +03:00 - Bidirectional DTC RIIC transport, build 8

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Reason for the change: RX already used DTC, but master writes still entered TXI once per payload byte. The transport is now symmetric so sensor register pointers, commands and future long writes use DTC as well.
- Master transmit sequence: the first CPU TXI arms DTC immediately before sending the slave address; DTC then loads every payload byte into fixed destination `ICDRT`; the final TXI advances the existing TEI/STOP or repeated-START state machine.
- The implementation follows the Renesas FSP master timing and uses the slave-I2C DTC code only to confirm source/destination direction (`incrementing RAM -> fixed ICDRT`).
- New API: `I2CAsync.writefrom(address, buffer, stop=True, timeout_ms=...)` uses the same static action, timeout, rooted buffer and exactly-once completion callback as `readinto()`.
- `I2CAsync.stats()` now returns eight values: `(rxi_irq_count, rx_dtc_transfers, rx_dtc_bytes, rx_fallbacks, txi_irq_count, tx_dtc_transfers, tx_dtc_bytes, tx_fallbacks)`.
- HIL additions prepared in `examples/i2c_async_notify_hil.py`: four-byte MLX status-clear DTC write, callback-driven write/repeated-START/read, and a complete write/read callback chain while the MicroPython heap is locked.
- Result: **SUCCESS** (`make` exit code 0).
- Generated API check: `MP_QSTR_writefrom` is present; both asynchronous buffer and callback roots remain present in `root_pointers.h`.
- Link report: `text=1415204`, `data=0`, `bss=647596`, total `2062800` bytes.
- `firmware.bin`: 1,415,188 bytes; SHA-256 `aeab69ac401a5d743f71927c3a5bd6a5b782621eafaf9120939afe291fb7b7d3`.
- `firmware.hex`: 3,980,696 bytes; SHA-256 `4774efb870060525b4ba83884ed0e174ba00daca43f7b764b49b38e72b1635fa`.
- `firmware.elf`: 15,612,224 bytes; SHA-256 `0aa43f7d79885f3fe03c63e143ca5fc2cbeedad0f0b2e05afca1a8b27d7a7b72`.
- Flash status: not yet flashed at this point. Build success does not prove TX DTC activation or repeated-START behavior.
- Next action: separate visible J-Link reset, program and independent verify, then run the full bidirectional DTC HIL matrix.

### Build 8 flash and bidirectional DTC hardware validation

- A separate visible J-Link reset completed with exit code 0 before programming and reported two normal Cortex-M4 reset operations.
- The following visible programming session completed with exit code 0 and reported `Program & Verify` for build 8.
- An independent visible `verifybin` session read and compared exactly 1,415,188 bytes from `0x00000000` and completed with exit code 0. Its temporary command file was removed afterward.
- Four-byte MLX status-clear write: callback result `4`, one heartbeat iteration, and stats `(0, 0, 0, 0, 3, 1, 4, 0)`. DTC transferred every payload byte with no fallback; CPU TXI entries are bounded address/completion events rather than one interrupt per payload byte.
- Combined MLX transaction: DTC write of the two-byte status pointer with `stop=False`, deferred callback, repeated START, and asynchronous two-byte read all completed correctly (`write=2`, `read=2`) without polling.
- MLX90640 1664-byte read remained correct: callback result `1664`, about `34.044 ms`, `35` heartbeat iterations, and RX stats `(5, 1, 1661, 0)`.
- AMG8833 128-byte read remained correct: callback result `128`, `4` heartbeat iterations, and RX stats `(5, 1, 125, 0)`.
- The complete DTC write/repeated-START/read chain completed under `micropython.heap_lock()` with two callbacks, correct result sum `3`, one heartbeat iteration, and restored lock depth `0`.
- Polling compatibility, exactly-once callback chaining, NACK (`19`), cancel (`125`), 1 ms timeout (`110`), stock-asyncio message flow and the expected stock-asyncio allocation gate all passed.
- Measured cancel return: `41 us`; measured autonomous timeout callback arrival: about `810 us`.
- Final I2C scan: `[0x33, 0x47, 0x68]`.
- Milestone status: **hardware validated** for bidirectional DTC-backed RIIC transport, repeated START chaining, deferred notification and heap-locked execution.

## 2026-08-29 17:45 +03:00 - Allocation-free asynchronous error result, build 9

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Reason for the change: `I2CAsync.result()` is allocation-free on the normal success path, but reports NACK/timeout/cancel by constructing `OSError`. A production callback running under the no-allocation rule needs the same information without an exception object.
- New API: `I2CAsync.result_code()` returns the completed byte count or negative `errno` as a small immediate integer. If called before completion it returns `-EBUSY`; it does not raise.
- The standard `result()` API and all existing behavior remain unchanged for conventional exception-based code.
- HIL update: normal callbacks now use `result_code()`, and a dedicated missing-address read checks `-ENODEV` while the MicroPython heap is locked.
- Result: **SUCCESS** (`make` exit code 0).
- Generated API check: `MP_QSTR_result_code` and `MP_QSTR_writefrom` are present; both asynchronous GC roots remain present in `root_pointers.h`.
- Link report: `text=1415260`, `data=0`, `bss=647596`, total `2062856` bytes.
- `firmware.bin`: 1,415,244 bytes; SHA-256 `be8f0dc551dcc669f3ba463d3103bd74ec46d5dc236bd79dc1a2571f6c8f553a`.
- `firmware.hex`: 3,980,860 bytes; SHA-256 `2083a6e5eb823cae24551f9e5fc61dc6fb64be86c38327067048ab6decbf5909`.
- `firmware.elf`: 15,613,516 bytes; SHA-256 `c689526e56f64db11c02de686f8f7bb4c7bed05dca64582f9ed655ab6526f6b5`.
- Flash status: not yet flashed at this point.
- Next action: separate visible J-Link reset, program and independent verify, then rerun the complete bidirectional DTC HIL matrix including the heap-locked error path.

### Build 9 flash and allocation-free error validation

- A separate visible J-Link reset completed with exit code 0 and reported the Cortex-M4 plus two normal reset operations before programming.
- Visible programming completed with exit code 0 and reported `Program & Verify`.
- Independent visible `verifybin` read and compared exactly 1,415,244 bytes from `0x00000000` with exit code 0; its temporary command file was removed.
- New error-path result: `PASS heap_lock_error result_code -19`. A missing-address transfer delivered `-ENODEV` through the deferred callback while the MicroPython heap was locked, without constructing `OSError`.
- The heap-locked DTC write/repeated-START/read chain again completed with two callbacks and correct result sum `3`.
- TX DTC remained `(0, 0, 0, 0, 3, 1, 4, 0)` for the four-byte MLX command; MLX RX remained `(5, 1, 1661, 0)` and AMG RX remained `(5, 1, 125, 0)`.
- MLX callback latency was about `34.038 ms` with `35` heartbeat iterations. Cancel returned in `41 us`; the 1 ms timeout callback arrived in about `754 us` in this run.
- Polling compatibility, repeated START, exactly-once callback chaining, NACK, cancel, timeout, stock-asyncio message flow and the expected stock-asyncio allocation gate all passed.
- Final I2C scan: `[0x33, 0x47, 0x68]`.
- Milestone status: **hardware validated** for allocation-free success and error completion messages on the bidirectional DTC transport.

## 2026-09-01 09:35 +03:00 - LVGL 9.4 optional features, build 10

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Intended change: enable complex software gradients, matrix object transforms, Python font glyph callbacks, and Lottie backed by the internal ThorVG renderer.
- Build integration: add the bundled ThorVG C++ sources and route global C++ `new/delete` through `lv_malloc/lv_free`, avoiding a second allocator over the MicroPython GC region.
- Result: **FAILED** (`make` exit code 2) during qstr preprocessing, before C/C++ compilation and linking.
- Error: `lv_matrix.h: #error "LV_USE_FLOAT is required for lv_matrix"`.
- Root cause: the generic binding configuration defaults `MICROPY_FLOAT` to `0`, so `LV_USE_FLOAT` remained disabled even though this port builds MicroPython with single-precision floating point and the RA6M3 FPU.
- Firmware artifact: none; the linker did not run.
- Next action: set board-local `LV_USE_FLOAT=1` and repeat a clean `-j16` build.

## 2026-09-01 09:38 +03:00 - LVGL 9.4 optional features, build 11

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 10: enable board-local `LV_USE_FLOAT=1` for the matrix and vector APIs.
- Result: **FAILED** (`make` exit code 2) during qstr preprocessing, before object compilation and linking.
- Error: `boards/VK_RA6M3/lvgl_thorvg_port.cpp: fatal error: lvgl.h: No such file or directory`.
- Root cause: the Renesas include path exposes the public header as `lvgl/lvgl.h`, not as a top-level `lvgl.h`.
- Firmware artifact: none; the linker did not run.
- Next action: use the port-correct include path and repeat a clean `-j16` build.

## 2026-09-01 09:41 +03:00 - LVGL 9.4 optional features, build 12

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 11: include the binding header as `lvgl/lvgl.h` from the ThorVG allocator bridge.
- Reached before failure: qstr, module and root-pointer generation completed and normal C object compilation started. This proves only that preprocessing and table generation did not stop; it does not yet prove that the requested Python APIs were emitted, linked or usable.
- Result: **FAILED** (`make` exit code 2) while compiling the bundled Dave2D line renderer.
- Error: four expected `float` to `int32_t` clip-area conversions in `lv_draw_dave2d_line.c` were promoted to errors by the port-wide `-Werror=float-conversion` policy.
- Firmware artifact: none; the linker did not run.
- Next action: keep the diagnostic warning but disable `-Werror=float-conversion` only for bundled Dave2D objects, then repeat a clean `-j16` build.

## 2026-09-01 09:45 +03:00 - LVGL 9.4 optional features, build 13

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 12: retain Dave2D and keep its float-to-integer diagnostics visible, but stop treating that one upstream warning class as fatal for Dave2D objects only.
- Result: **FAILED** (`make` exit code 2) during LVGL C compilation, before ThorVG C++ compilation and linking.
- Error: `lv_sprintf_builtin.c` promotes `float` values to `double` because several upstream numeric literals have type `double`; the port-wide `-Werror=double-promotion` policy makes those promotions fatal.
- Interpretation boundary: reaching this point still does not prove that matrix, glyph-callback or Lottie APIs are present in the generated MicroPython binding.
- Dave2D status: still enabled and compiled; it has not been replaced by the software renderer. Runtime initialization remains unverified until an ELF is linked and tested on the board.
- Firmware artifact: none; the linker did not run.
- Next action: inspect the exact float typedefs and compiler flags, then apply the narrowest type-correct fix for `lv_sprintf_builtin.c` without disabling Dave2D or weakening warnings port-wide.

## 2026-09-01 09:50 +03:00 - LVGL 9.4 optional features, build 14

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 13: compile only `lv_sprintf_builtin.o` with `-fno-single-precision-constant`, because that upstream formatter intentionally uses `double` calculations. No warning class was disabled for that object.
- Result: **FAILED** (`make` exit code 1) during LVGL C compilation, before ThorVG C++ compilation and linking.
- Confirmed narrow result: `lv_sprintf_builtin.c` now compiles cleanly; the previous double-promotion failures there are removed without changing its source or precision.
- Error: `lv_label.c` passes `subject->value.float_v` to the variadic `lv_label_set_text_fmt()` function. The C language requires the default argument promotion from `float` to `double`, while the port treats every double promotion as fatal.
- Static follow-up: the same required variadic promotion also exists in `lv_span.c`.
- Dave2D status: its arc, border, fill, image, label, line, mask, triangle and utility sources all compiled in this build. Runtime activation is still unverified because no ELF exists yet.
- Firmware artifact: none; the linker did not run.
- Next action: retain the warning, but make it non-fatal only for the two upstream label/span objects where C mandates the variadic promotion, then repeat the clean `-j16` build.

## 2026-09-01 09:56 +03:00 - LVGL 9.4 optional features, build 15

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 14: make the mandatory C default-argument promotion non-fatal only for `lv_label.o` and `lv_span.o`.
- Result: **FAILED** (`make` exit code 1) while compiling the generated `build-VK_RA6M3/lvgl/lv_mpy.c`; ThorVG C++ compilation and linking were not reached.
- Confirmed narrow result: both upstream label/span sources compiled and retained their visible double-promotion warnings.
- Binding error 1: the generator emits invalid C declarators for the two-dimensional `float m[3][3]` member of `lv_matrix_t` (`static float [3] *...`). Therefore matrix support is not yet exposed correctly even though table generation completed.
- Binding error 2: the generated Lottie type references `lv_lottie_class`, but LVGL 9.4 defines that global in `lv_lottie.c` without declaring it in the public `lv_lottie.h` header.
- Corrected proof boundary: qstr/module/root-pointer completion does not validate generated binding C. Build 15 is the first direct compilation proof and it fails.
- Dave2D status: all bundled Dave2D sources compiled again. It remains enabled; runtime activation is still unverified because linking was not reached.
- Firmware artifact: none.
- Next action: fix the generator's multidimensional-array declaration handling and provide a narrow public declaration bridge for the existing Lottie class symbol, then repeat a clean `-j16` build.

## 2026-09-01 10:06 +03:00 - LVGL 9.4 optional features, build 16

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 15: generate nested-array element typedefs and row copies in the binding generator; compile `lv_mpy.o` with a board-local declaration of the existing `lv_lottie_class` symbol.
- Result: **FAILED** (`make` exit code 1) while compiling generated `lv_mpy.c`; ThorVG C++ compilation and linking were not reached.
- Confirmed progress: the generated matrix declarations are now valid C (`typedef float ...[3]`), the `m[3][3]` read/write converters are emitted, and the generated Lottie wrapper resolves its class declaration.
- Error: the new temporary element typedef retained source `const` qualifiers for several ordinary one-dimensional input arrays. The converter then attempted to fill read-only temporary elements, producing assignment errors and const-discard diagnostics.
- Root cause: the old conversion path intentionally used an unqualified temporary element type; the new typedef must preserve that behavior before adding multidimensional shape.
- Dave2D status: all bundled Dave2D sources compiled again and remain enabled. Runtime activation remains unverified because no ELF was linked.
- Firmware artifact: none.
- Next action: remove qualifiers from the temporary conversion typedef, while leaving the public function signatures unchanged, then repeat a clean `-j16` build.

## 2026-09-01 10:13 +03:00 - LVGL 9.4 optional features, build 17

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 16: remove qualifiers only from the binding generator's temporary array-element typedef; public LVGL argument qualifiers remain unchanged.
- Result: **FAILED** (`make` exit code 1) at the final link step. All generated binding C, LVGL C, bundled Dave2D, FSP Dave2D driver and selected ThorVG C++ objects compiled.
- Binding progress: generated matrix nested-array converters and the Lottie class wrapper compiled successfully. This is compile-time proof only; no usable ELF was produced.
- Link error 1: adding the complete target `libstdc++.a` pulled exception, locale, I/O and system-error runtime objects that require unsupported hosted C library symbols such as `malloc`, `free`, `fopen`, `setlocale` and `__dso_handle`.
- Link error 2: LVGL vector code also needs the math library symbols `lroundf`, `lrintf` and `ceil`; the current library ordering does not resolve them.
- Memory result: the incomplete link reports a 1,192-byte RAM overflow where `.stack_dummy` crosses the fixed framebuffer boundary. This is not yet a final firmware-size result because the current C++ runtime selection is incorrect.
- Precision issue found during C++ compilation: the port-wide `-fsingle-precision-constant` flag changes ThorVG/RapidJSON constants intended as `double`, including powers outside the float range. ThorVG objects need their upstream double literals preserved.
- Dave2D status: all Dave2D draw and FSP driver objects compiled and remained part of the attempted link. It has not been disabled or replaced; runtime activation remains unverified until a valid ELF is linked and tested.
- Firmware artifact: none; final link failed.
- Next action: use a freestanding, minimal C++ support set for ThorVG, preserve ThorVG double literals, resolve math/runtime symbols with the port's supported libraries, and repeat a clean `-j16` build.

## 2026-09-01 10:29 +03:00 - LVGL 9.4 optional features, build 18

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 17: omit the unused ThorVG SVG/iostream loader for VK_RA6M3; keep the Lottie JSON loader; route Lottie file reads through `lv_fs_*`; use `lv_free` for ThorVG allocations made by `lv_malloc/lv_realloc`; preserve ThorVG double literals; add exception-free C++ failure bridges; link only referenced members from `libstdc++`, `libc_nano`, `libm` and `libgcc` as one archive group.
- Result: **FAILED** (`make` exit code 1) at the final link step. All requested C, generated binding C, Dave2D/FSP and reduced ThorVG C++ objects compiled.
- Confirmed progress: the previous iostream/locale/exception dependency chain is absent, and the linker no longer reports a RAM overflow or framebuffer overlap.
- Remaining link errors: only the six freestanding newlib syscall hooks `_close`, `_fstat`, `_isatty`, `_lseek`, `_read` and `_write` are unresolved.
- File-path architecture: Lottie file loading no longer uses these newlib calls; it uses the registered LVGL file-system driver, allowing a MicroPython-backed `lv_fs` drive to read `/flash` files.
- Dave2D status: all LVGL Dave2D objects and the full FSP Dave2D driver compiled. The final link still failed, so runtime activation remains unverified.
- Firmware artifact: none; final link failed.
- Next action: add the toolchain's freestanding `libnosys` syscall stubs to the existing archive group and repeat a clean `-j16` build.

## 2026-09-01 10:36 +03:00 - LVGL 9.4 optional features, build 19

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 18: add the target toolchain's freestanding `libnosys.a` to the existing C/C++ archive group; no source-level syscall implementation or hosted file-I/O path was added.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and `firmware.elf`, `firmware.hex` and `firmware.bin` were generated.
- Size summary: `text=1606840`, `data=0`, `bss=648068`, total `2254908` bytes (`0x22683c`). The large BSS value includes the port's statically reserved display memory and must be interpreted from the linker map, not as MicroPython heap usage alone.
- Link diagnostics: the expected `libnosys` warnings for `_close`, `_fstat`, `_isatty`, `_lseek`, `_read` and `_write` remain visible, plus the existing RWX load-segment warning. They are warnings, not unresolved symbols.
- Binding evidence: generated matrix nested-array converters, glyph-descriptor callback support and Lottie wrappers compiled into the firmware.
- Dave2D evidence: the LVGL Dave2D draw objects and the FSP Dave2D driver compiled and linked while `LV_USE_DRAW_DAVE2D=1`; Dave2D was not replaced by the software renderer. Actual board-side initialization and draw dispatch still require static ELF/source tracing and HIL verification.
- ThorVG/Lottie evidence: the reduced embedded ThorVG/Lottie object set compiled and linked without the desktop SVG/iostream loader; Lottie file input uses `lv_fs_*`.
- Proof boundary: this is a successful firmware build milestone only. No flash, board startup, Dave2D runtime dispatch or visual feature test has been performed for build 19 yet.
- Next action: inspect the final ELF/map and generated Python API, add a complete HIL script, clean generated logs, commit the exact milestone, then perform the required visible J-Link reset before programming.

### 2026-09-01 10:53 +03:00 - Build 19 static audit and prepared HIL

- Firmware binary: `1,606,824` bytes, SHA-256 `6B413B2F962360FFD8B5ADD2968421EBD6FEBF57FF1D5ED5865F778F0A591AB2`.
- Flash layout: application FLASH is `0x1c0000` bytes; the binary leaves `228,184` bytes before the separate `FLASH_FS` region.
- RAM layout from `firmware.map`: fixed MicroPython heap `0x47000` (`290,816` bytes), fixed stack `0x4000` (`16,384` bytes), framebuffer `0x3fc00` (`261,120` bytes), and `0xaf8` (`2,808` bytes) between the stack top and framebuffer origin. The linker assertions for heap/stack and framebuffer separation passed.
- Final machine-code trace: `lv_init` directly branches to `lv_draw_sw_init` and then to `lv_draw_dave2d_init` at `0x000a3cb4`. This proves that Dave2D initialization is present in the executable initialization path, not merely in an unreferenced object.
- Final symbol trace: `lv_draw_dave2d_dispatch`, `_dave2d_evaluate`, `d2_opendevice`, `d2_executerenderbuffer`, the DRW ISR, matrix functions and all five Lottie entry points are retained in `firmware.elf`.
- Runtime proof mechanism: the existing `machine.LCD.drw_stats()` returns DRW interrupt/allocation counters. The new HIL script compares the interrupt count before and after a frame containing only Dave2D-supported fill, border and label tasks.
- Complete HIL files prepared: `examples/lvgl_optional_features_dave2d_hil.py` and `examples/lv_example_lottie_approve.json`. Host syntax and JSON structure checks pass; neither has run on the board yet.
- Renderer boundary: complex linear/radial/conical gradients intentionally use LVGL software fallback. Dave2D remains the preferred draw unit for supported ordinary fills, borders, labels, lines, arcs and image formats; ThorVG handles Lottie vector rasterization.
- Nested milestone commits: LVGL `9d7f9eaa80f88964733b727a77adec055191a72e`; binding generator plus LVGL pointer `55837cddd48c4aa373979cc05b424edc4844066f`.
- Generated build logs 10 through 19 and the regenerated `lextab.py` ordering noise were removed from the worktree; no firmware source change was discarded.
- Proof boundary remains unchanged: static inspection is not board execution. Flash and HIL follow only after the parent port commit.
