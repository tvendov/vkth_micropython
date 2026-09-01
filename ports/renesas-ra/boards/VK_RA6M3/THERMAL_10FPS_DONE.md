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

## 2026-09-01 11:55 +03:00 - LVGL 9.4 audit corrections, build 20

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 19: validate exact fixed-array dimensions and iterator counts in both binding generators; add board-local `__cxa_pure_virtual` and `__cxa_deleted_virtual` failure bridges; strengthen the optional-feature HIL assertions; remove the broad `-Wno-error` from all ThorVG C++ objects.
- Result: **FAILED** (`make` exit code 1) during ThorVG C++ compilation, before linking.
- Generator result: LVGL generation, qstr, module and root-pointer generation completed, and generated `lv_mpy.c` compiled. The new nested fixed-array checks therefore pass generation and C compilation.
- Exact errors: `tvgAnimation.cpp` compares `float` values with upstream double literals `0.0` and `1.0`; `tvgLottieBuilder.cpp` passes a float expression through `sqrt()`. The port-wide `-Werror=double-promotion` policy makes these expected upstream promotions fatal.
- Warning-policy result: removing broad `-Wno-error` exposed one concrete warning class. No unrelated ThorVG warning class has been suppressed.
- Dave2D status: the LVGL Dave2D sources and the FSP Dave2D driver compiled with `LV_USE_DRAW_DAVE2D=1`; the existing Dave2D-only `-Wno-error=float-conversion` remained narrow and visible.
- Firmware artifact: none; the linker did not run.
- Next action: make only `double-promotion` non-fatal for the selected ThorVG objects, retain every other warning as fatal, then repeat a clean `-j16` build.

## 2026-09-01 12:02 +03:00 - LVGL 9.4 audit corrections, build 21

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 20: make `double-promotion` non-fatal only for `tvgAnimation.o` and `tvgLottieBuilder.o`; all other ThorVG warnings remained fatal.
- Result: **FAILED** (`make` exit code 1) during ThorVG C++ compilation, before linking.
- Confirmed narrow result: the two Build 20 diagnostics remained visible as warnings and no longer stopped compilation.
- New exact errors: `tvgLottieInterpolator.cpp` compares a float slope with the double literal `0.0`; `tvgLottieParser.cpp` intentionally converts a JSON float to `int8_t` and passes four float coordinates to variadic `snprintf`, where C++ requires promotion to double.
- Interpretation: extending warning waivers object by object would hide type intent across the Lottie loader. The safer correction is to make the intended types explicit in these four upstream expressions and remove the temporary ThorVG waiver entirely.
- Dave2D status: LVGL Dave2D and the complete FSP Dave2D driver compiled again and remained enabled.
- Firmware artifact: none; the linker did not run.
- Next action: use float literals and `sqrtf` for float calculations, explicit `int8_t` conversion for the parsed byte, and explicit double arguments for variadic formatting; then run another clean `-j16` build with all ThorVG warnings fatal.

## 2026-09-01 12:09 +03:00 - LVGL 9.4 audit corrections, build 22

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 21: replace float comparisons with float literals, use `sqrtf` for the Gaussian-blur calculation, add the explicit parsed-byte conversion and explicitly promote the four variadic `snprintf` coordinates; remove the temporary ThorVG warning waiver.
- Result: **FAILED** (`make` exit code 1) during ThorVG C++ compilation, before linking.
- Confirmed progress: every Build 21 diagnostic was resolved and the corrected parser/interpolator sources compiled with all ThorVG warnings fatal.
- Remaining exact error: `tvgLottieBuilder.cpp` passes the float result of `sqrtf(effect->blurness(frameNo))` to the overloaded `Scene::push(SceneEffect::GaussianBlur, ...)` interface whose matching parameter is `double`; `-Werror=double-promotion` requires that API-boundary conversion to be explicit.
- Warning-policy result: there is still no broad or object-specific ThorVG warning waiver.
- Dave2D status: LVGL Dave2D and the complete FSP Dave2D driver compiled again with `LV_USE_DRAW_DAVE2D=1`; Dave2D remains enabled.
- Firmware artifact: none; the linker did not run.
- Next action: inspect the exact `Scene::push` declaration, express its expected Gaussian-blur argument type explicitly at the call site, then repeat a clean `-j16` build.

## 2026-09-01 12:15 +03:00 - LVGL 9.4 audit corrections, build 23

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 22: explicitly promote the Gaussian-blur sigma to `double` at the `Scene::push(SceneEffect, ...)` variadic boundary, matching the receiver's `va_arg(args, double)` contract while retaining `sqrtf` for the calculation.
- Result: **FAILED** (`make` exit code 1) during a later ThorVG C++ source, before linking.
- Confirmed progress: `tvgLottieBuilder.cpp`, `tvgLottieInterpolator.cpp` and `tvgLottieParser.cpp` all compiled with every ThorVG warning fatal; the Gaussian-blur API boundary is now type-explicit.
- Remaining exact error: `tvgStr.cpp` multiplies a `float scale` by the double literal `1E8`, causing both `double-promotion` and `float-conversion` diagnostics under `-Werror`.
- Dave2D status: LVGL Dave2D and the complete FSP Dave2D driver compiled and remain enabled with the existing Dave2D-only clip-coordinate warning exception.
- Firmware artifact: none; the linker did not run.
- Next action: express the scale literal as the intended `float` constant `1E8f`, then repeat a clean `-j16` build.

## 2026-09-01 12:21 +03:00 - LVGL 9.4 audit corrections, build 24

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 23: replace the lone double scale literal in `tvgStr.cpp` with the intended `float` literal `1E8f`; no warning waiver was added.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Warning-policy result: every selected ThorVG C++ object compiled with all enabled warnings fatal. The temporary object-specific `double-promotion` waiver and the earlier broad ThorVG `-Wno-error` are absent.
- Size summary: `text=1607624`, `data=0`, `bss=648068`, total `2255692` bytes (`0x226b4c`). `firmware.bin` is `1,607,608` bytes with SHA-256 `C3CAD3E8D875329127FB60FBC12BD8BA8812B38BDB68185BE725787055E4EB4B`.
- Binding result: regenerated `lv_mpy.c` compiled with the new exact fixed-array dimension and iterator-count guards.
- Dave2D status: LVGL Dave2D, the full FSP Dave2D driver and the board Dave2D port compiled and linked with `LV_USE_DRAW_DAVE2D=1`; Dave2D remains enabled.
- Link diagnostics: the existing six `libnosys` syscall warnings and RWX LOAD-segment warning remain visible. Their retained-symbol cause must be checked in the final ELF/map before this build is accepted for flashing.
- Proof boundary: this is a successful clean build only. No Git milestone commit, flash, startup or HIL execution has yet been performed for build 24.
- Next action: audit the final ELF/map for C++ exception, allocation and syscall dependencies; verify Dave2D and optional-feature symbols; then either remove any unsafe runtime pull or commit the verified static milestone.

## 2026-09-01 12:28 +03:00 - LVGL 9.4 freestanding ThorVG runtime, build 25

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 24: explicitly instantiate `std::basic_string<char>` in the board C++ bridge with `_GLIBCXX_EXTERN_TEMPLATE=-1` and the target's existing `-fno-exceptions` flags, so ThorVG does not use the exception-enabled prebuilt `libstdc++` string-instantiation object.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1577300`, `data=0`, `bss=647716`, total `2225016` bytes (`0x21f378`). `firmware.bin` is `1,577,284` bytes with SHA-256 `9BB271A36C5B1D307FA81A772800D94CD788F4FC958BC63F72A3CEB7214954C5`.
- Delta from build 24: `text` decreased by `30,324` bytes, `bss` decreased by `352` bytes and the binary decreased by `30,324` bytes.
- Link diagnostics: all six previous `libnosys` syscall warnings disappeared. Only the port's existing RWX LOAD-segment warning remains.
- Dave2D status: LVGL Dave2D, the full FSP Dave2D driver and the board Dave2D port compiled and linked with `LV_USE_DRAW_DAVE2D=1`; Dave2D remains enabled.
- Proof boundary: this is a successful clean build. The expected removal of `string-inst.o`, exception ABI, demangler, newlib allocators and syscall stubs still requires direct ELF/map verification before commit or flash.
- Next action: run the exact symbol and archive-inclusion audit, verify retained Dave2D/Lottie/matrix entry points and memory layout, then commit the static milestone if all checks pass.

## 2026-09-01 12:36 +03:00 - LVGL 9.4 allocation-free support runtime, build 26

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 25: provide a board-local deterministic allocation-free `rand`/`srand` implementation for ThorVG's Lottie text-selector seed; replace the RLE renderer's single `setjmp`/`longjmp` cell-pool overflow escape with an explicit `outOfCells` status that returns the same `-1` result to the existing band-reduction path.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1572725`, `data=0`, `bss=647672`, total `2220397` bytes (`0x21e16d`). `firmware.bin` is `1,572,712` bytes with SHA-256 `8FA02F7FE8A75D02FA1FF6E3CBFB26B693782501EC6F398AA1A6EEBABC2587BF`.
- Delta from build 25: `text` decreased by `4,575` bytes, `bss` decreased by `44` bytes and the binary decreased by `4,572` bytes.
- Runtime semantics: RLE cell-pool exhaustion still returns `-1` to the caller, which halves the render band and retries; no out-of-bounds cell is created. The replacement random sequence remains deterministic after boot, matching the unseeded embedded C-library behavior without allocating reentrancy state.
- Dave2D status: LVGL Dave2D, the full FSP Dave2D driver and the board Dave2D port compiled and linked with `LV_USE_DRAW_DAVE2D=1`; Dave2D remains enabled.
- Link diagnostics: no `libnosys` syscall warning was emitted. The link output shown for this build also contained no RWX LOAD-segment warning.
- Proof boundary: this is a successful clean build. Exact absence of exception, allocator, syscall and unwind symbols and the retained optional-feature entry points still require the following ELF/map audit before commit or flash.
- Next action: complete the exact symbol, archive-inclusion, call-path and memory-layout audit; then commit the verified static milestone before the required visible J-Link reset and flash.

## 2026-09-01 12:43 +03:00 - LVGL 9.4 link without libnosys, build 27

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 26: remove `libnosys.a` from the final C++ archive group. This makes any accidental dependency on its syscall stubs a link failure instead of silently retaining placeholders.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin` without `libnosys.a`.
- Size summary: `text=1572725`, `data=0`, `bss=647672`, total `2220397` bytes (`0x21e16d`). `firmware.bin` is `1,572,712` bytes with SHA-256 `8FA02F7FE8A75D02FA1FF6E3CBFB26B693782501EC6F398AA1A6EEBABC2587BF`.
- Reproducibility: size and SHA-256 are byte-for-byte identical to build 26, confirming that `libnosys.a` contributed no bytes to that image.
- Dave2D status: LVGL Dave2D, the full FSP Dave2D driver and the board Dave2D port compiled and linked with `LV_USE_DRAW_DAVE2D=1`; Dave2D remains enabled.
- Link diagnostics: no syscall warning and no RWX LOAD-segment warning were emitted.
- Proof boundary: this is a successful clean build. The final Build 27 ELF/map audit and static Python checks follow before any commit or flash.
- Next action: repeat the exact symbol, archive-inclusion, segment, call-path and generated-binding checks against Build 27; then commit the verified static milestone.

## 2026-09-01 12:49 +03:00 - Build 27 final static audit

- Forbidden-runtime audit: the final ELF contains none of the exact C++ exception, catch, demangler, personality, ARM unwind, C/newlib allocator or syscall symbols selected for this audit. The similarly named MicroPython bytecode/native unwind helpers are application code, not C++ unwind runtime dependencies.
- C++ allocation path: disassembly proves the retained board-local `operator new(unsigned int)` calls `lv_malloc` and raises the MicroPython memory error bridge on failure; `operator delete(void *, unsigned int)` branches directly to `lv_free`.
- Archive audit: the linker map contains none of the selected prebuilt `string-inst`, exception, demangler, libc `rand`/`setjmp`/allocator/syscall, libgcc unwind or `libnosys` archive members.
- Dave2D call path: disassembly of `lv_init()` calls `lv_draw_sw_init()` and then `lv_draw_dave2d_init()`. The retained Dave2D dispatch calls `d2_selectrenderbuffer()`, `d2_executerenderbuffer()` and `d2_flushframe()`; `drw_int_isr`, `d2_opendevice` and the board dispatch are present in the ELF.
- Optional-feature symbols: matrix identity/multiply/rotate/inverse and all four requested Lottie source/buffer entry points are present. The generated binding contains exact nested fixed-array dimension guards and iterator overrun/underrun guards.
- Floating-point ABI: ELF attributes report `VFPv4-D16`, single-precision hard-float use and VFP argument registers. `LV_USE_FLOAT=1`, `LV_USE_DRAW_DAVE2D=1`, `LV_USE_THORVG_INTERNAL=1` and `LV_USE_LOTTIE=1` are active in the board configuration.
- Segment permissions: executable LOAD segments are `R E`; writable RAM/framebuffer LOAD segments are `RW`; there is no RWX LOAD segment.
- Memory layout: application flash is `0x00000000..0x001bffff`; the last initialized byte ends at `0x0017ff67`, leaving `0x40098` (`262,296`) bytes before `FLASH_FS`. The MicroPython heap is `0x47000` (`290,816`) bytes, the stack is `0x4000` (`16,384`) bytes, the stack-to-framebuffer guard gap is `0xc88` (`3,208`) bytes and the framebuffer is `0x3fc00` (`261,120`) bytes.
- Static test result: Python syntax passed for both modified generators and the HIL script; the bundled Lottie JSON parsed successfully (`5,139` bytes); all three repositories pass `git diff --check`.
- Artifact identity: Build 27 `firmware.bin` remains `1,572,712` bytes with SHA-256 `8FA02F7FE8A75D02FA1FF6E3CBFB26B693782501EC6F398AA1A6EEBABC2587BF`.
- Proof boundary: compilation, linking and static artifact inspection are complete. No Git milestone commit, board flash, startup observation or HIL execution has yet been performed for Build 27.
- Next action: commit the three nested Git scopes in dependency order, perform the required visible J-Link reset, flash and independently verify this exact binary, then run the HIL script only if the board reaches a usable MicroPython REPL.

## 2026-09-01 13:42 +03:00 - Build 27 flash, startup and HIL diagnosis

- Git milestone before flashing: LVGL `40b3312e71930941941f3a7e300acd826f04a745`, binding generator and LVGL pointer `a8caee46dcc4281592fcfa2f64eee7e615e0a8be`, parent port `cdba463468fcc045516dc2816110b02fd941ba8b`.
- The required visible J-Link reset completed successfully before programming. Build 27 `firmware.bin` was then programmed through a visible J-Link Commander window and checked with a separate `verifybin` operation.
- Programmed artifact identity: `1,572,712` bytes, SHA-256 `8FA02F7FE8A75D02FA1FF6E3CBFB26B693782501EC6F398AA1A6EEBABC2587BF`.
- Board startup: MicroPython reported `d1c45443ce-dirty on 2026-09-01; VK-RA6M3 with RA6M3` on COM18 after the required visible reset.
- HIL transport: the optional-feature test and Lottie JSON were copied to `/flash`; board-side file sizes and hashes matched the host files before execution.
- Runtime proof: the Dave2D-supported fill/border/label frame increased the DRW interrupt count; complex linear, radial and conical gradients rendered and passed their framebuffer checks.
- Matrix API proof: a 3-by-3 floating-point matrix could be created, stored and read back exactly enough for the test. This proves the generated binding and storage contract, not transformed rendering.
- Image-transform control: a generated RGB565 `lv.image` changed the framebuffer signature after rotation and scaling while the DRW interrupt count increased. Dave2D texture mapping and its hardware execution path therefore work.
- Isolated failure: an object transformed through LVGL's off-screen `LV_DRAW_TASK_TYPE_LAYER` path did not change the framebuffer. The layer task was not claimed or executed by the Renesas Dave2D draw unit; the header declared `lv_draw_dave2d_layer()`, but no implementation or active dispatch existed.
- Proof boundary: Build 27 remains a verified booting Dave2D firmware, but the full optional-feature HIL did not pass because the LVGL layer-composition path was incomplete. No claim is made that arbitrary `lv_draw_task_t.matrix` transforms are supported by Dave2D.
- Next action: implement only the missing Dave2D layer task by reusing the already proven Dave2D image renderer, then repeat a clean build, static audit, visible flash and the complete HIL.

## 2026-09-01 13:42 +03:00 - Dave2D layer composition, build 28

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 27: allow the Renesas Dave2D evaluator to claim supported `LV_DRAW_TASK_TYPE_LAYER` sources; execute the layer task; implement `lv_draw_dave2d_layer()` by replacing the layer source with its allocated draw buffer and passing the copied image descriptor to the existing Dave2D image renderer.
- Scope: Dave2D remains enabled and is used for the new path. No software-only replacement, matrix emulation or broad warning suppression was introduced.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1572789`, `data=0`, `bss=647672`, total `2220461` bytes (`0x21e1ad`). `firmware.bin` is `1,572,776` bytes with SHA-256 `6678A149BFEEBEB2FAE843972F7B6B7E357AA83AD9EBF0F5B750711A88F6591F`.
- Delta from build 27: `text` and `firmware.bin` increased by only `64` bytes; `data` and `bss` are unchanged.
- Machine-code proof: `lv_draw_dave2d_layer` is retained at `0x000330b0`; its disassembly checks the layer and draw-buffer pointers, copies the image descriptor, substitutes the layer draw buffer and calls `lv_draw_dave2d_image` at `0x00033098`.
- Dave2D proof: `_dave2d_evaluate`, `lv_draw_dave2d_dispatch`, `lv_draw_dave2d_layer`, `d2_opendevice`, `d2_executerenderbuffer`, `drw_int_isr` and the board DRW ISR are all retained in the final ELF.
- Optional-feature proof: matrix identity/multiply/rotate/inverse and both Lottie source entry points remain retained in the final ELF.
- Runtime audit: none of the selected C++ exception/catch/personality, ARM unwind, C/newlib allocator, syscall, `setjmp` or `longjmp` symbols are present. No selected exception, allocator, syscall, unwind, `string-inst` or `libnosys` archive member appears in the linker map.
- ABI and segments: the ELF uses VFPv4-D16, single-precision hard-float and VFP argument registers. Executable LOAD segments are `R E`; writable segments are `RW`; no LOAD segment is RWX.
- Warning boundary: the existing Dave2D float-to-integer clip-coordinate diagnostics for line and triangle remain visible and non-fatal only under the previously recorded Dave2D-specific policy. They are unrelated to the new layer function.
- Proof boundary: Build 28 has passed clean compilation, linking and static artifact inspection only. It has not yet been committed, flashed or executed on the board.
- Next action: commit this static milestone in dependency order, perform the required visible J-Link reset, program and independently verify this exact SHA-256 artifact, then rerun the complete HIL.

## 2026-09-01 14:17 +03:00 - Build 28 flash, Dave2D layer proof and ThorVG fault isolation

- Git milestone before flashing: LVGL `79815d29bc4ffe38fade867dae2ff8baef0fab22`, binding/LVGL pointer `0f86a07`, parent port `32298adde`.
- The required visible J-Link reset completed before programming. A visible flash operation and a separate verification operation both succeeded; exactly `1,572,776` bytes were read back from address zero and matched Build 28 SHA-256 `6678A149BFEEBEB2FAE843972F7B6B7E357AA83AD9EBF0F5B750711A88F6591F`.
- Board startup reached the MicroPython 1.28 preview REPL on COM18. Board-side copies of the HIL script and `lv_example_lottie_approve.json` matched the host artifacts before execution.
- Corrected layer trigger: LVGL 9.4 defines `LV_OPA_MAX` as `253`; opacity `254` still selected the direct image path. Using opacity `252` forced `LV_DRAW_TASK_TYPE_LAYER`. The framebuffer signature changed from `1732771868` to `424709893` and the DRW interrupt counter increased, proving the new Dave2D layer implementation on hardware.
- The complex gradients, matrix storage, transformed image/layer output and custom glyph callback all passed. Dave2D remained enabled and active for its supported primitives and image/layer composition paths.
- The feature scene was deleted and `gc.collect()` recovered approximately `242 KiB` before Lottie allocation. Lottie construction, data-source assignment, render-buffer assignment and animation refresh returned successfully; the board then entered HardFault during `lv.refr_now(display)`.
- Fault evidence: `IPSR=3`, `CFSR=0x01000000` (`UNALIGNED`) and `HFSR=0x40000000` (`FORCED`). The stacked PC `0x000c51c0` resolves to `tvg::Array<unsigned long>::push()` at `tvgArray.h:62`; LR `0x000c5231` resolves to `_outlineEnd()` at `tvgSwShape.cpp:49`.
- Lifetime evidence: ThorVG's static `globalMpool` pointer still pointed at `0x1fff6b60`, but that block began with `0x00001413`. Decimal `0x1413` is `5,139`, exactly the Lottie JSON byte length. The pool had been reclaimed after `gc.collect()` and reused by the Python JSON object.
- Root cause boundary: the LVGL allocator maps to MicroPython `m_malloc`, while ThorVG retained the software-renderer pool and task scheduler only through C++ static pointers. Those pointers are not MicroPython GC roots and are not reachable from `mp_lv_roots` before the first ThorVG canvas exists. This is separate from the already-correct `mp_lv_roots` registration and from Dave2D execution.
- Next action: root the two long-lived ThorVG engine allocations in `MP_STATE_PORT`, then repeat a clean Build 29, static audit, visible reset/flash/verify and the same GC-before-Lottie HIL sequence.

## 2026-09-01 14:25 +03:00 - ThorVG engine GC roots, build 29

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 28: register a two-slot `vk_ra6m3_thorvg_roots` array in MicroPython VM state; root/unroot ThorVG's shared software-renderer memory pool and task scheduler through optional LVGL configuration hooks; clear both slots during the board LVGL GC lifecycle.
- Scope: this is a narrow lifetime fix for engine allocations held only by C++ static pointers. The Dave2D evaluator, dispatch, layer renderer and hardware driver remain enabled and unchanged. No global LVGL allocation list or permanent heap lock was introduced.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1572861`, `data=0`, `bss=647672`, total `2220533` bytes (`0x21e1f5`). `firmware.bin` is `1,572,848` bytes with SHA-256 `9D1732F0F4D5FB65B3D5EBB55F53EA851B7F74A9D180E7B65AF97E837E528475`.
- Delta from build 28: `text` and `firmware.bin` increased by `72` bytes; `data` and `bss` are unchanged.
- Generated-root proof: `build-VK_RA6M3/genhdr/root_pointers.h` contains `void *vk_ra6m3_thorvg_roots[2]` together with the existing `mp_lv_roots` and Dave2D allocation root.
- Machine-code proof: `SwRenderer::init()` stores the new pool then calls `vk_ra6m3_thorvg_gc_root_set(0, pool)`; `_termEngine()` frees the pool, clears the static pointer and calls the same setter with null. `TaskScheduler::init/term()` perform the equivalent operations for slot 1.
- Dave2D proof: `lv_draw_dave2d_init`, `lv_draw_dave2d_layer`, `d2_executerenderbuffer`, `drw_int_isr` and the board ISR remain retained in the final ELF.
- Runtime dependency audit: the case-sensitive exact scan found zero selected C++ exception/catch/personality, ARM unwind, C/newlib allocator, syscall, `setjmp` or `longjmp` symbols and zero selected archive members. MicroPython's own bytecode/native helpers whose names contain lowercase `unwind` are not C++ unwind runtime dependencies and were excluded explicitly.
- ABI and segments: ELF attributes remain `VFPv4-D16`, single-precision hard-float and VFP argument registers. Executable LOAD segments are `R E`, writable segments are `RW`, and no LOAD segment is RWX.
- Static input checks: the complete HIL script parses as Python and the bundled Lottie JSON parses successfully at exactly `5,139` bytes. All three Git scopes pass `git diff --check`.
- Proof boundary: Build 29 has passed clean compilation, linking and static artifact inspection only. It has not yet been committed, flashed or run on the board.
- Next action: review and commit the exact nested Git scopes in dependency order, then perform the required visible J-Link reset, flash, independent verify and full GC-before-Lottie HIL.

## 2026-09-01 14:34 +03:00 - Idempotent LVGL GC initialization, build 30

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Review finding after build 29: upstream `lv_init()` invokes `LV_GC_INIT()` before it checks the existing initialized flag. Clearing the ThorVG slots or replacing `mp_lv_roots` in that hook would therefore unroot live engine state during a repeated `lv.init()` call.
- Change since build 29: `vk_ra6m3_lvgl_gc_init()` now allocates `lv_global_t` only when `mp_lv_roots` is null and always synchronizes the existing pointer into `MP_STATE_VM(mp_lv_roots)`. It does not alter live ThorVG slots. Deinitialization still nulls both global-context copies and both ThorVG roots after normal engine teardown.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1572861`, `data=0`, `bss=647672`, total `2220533` bytes (`0x21e1f5`). `firmware.bin` is `1,572,848` bytes with SHA-256 `F6B5D026FFCDAFE1B1B4BDE50C54E0C2BC20D997A6403F6869EE249E31BFC7A4`.
- Delta from build 29: section and binary sizes are unchanged; the SHA-256 differs because the GC-init control flow changed.
- Machine-code proof: `vk_ra6m3_lvgl_gc_init()` tests the static `mp_lv_roots`, calls `m_malloc0` only on the null branch, then writes the selected pointer to the VM root field. `vk_ra6m3_lvgl_gc_deinit()` clears the global pointer, VM pointer and both adjacent ThorVG slots.
- Root and renderer proof: the generated table still contains `vk_ra6m3_thorvg_roots[2]`; the pool/scheduler root setter call sites and all selected Dave2D init/layer/driver/ISR symbols remain retained.
- Runtime dependency audit: zero selected exception, catch, personality, unwind, newlib allocator, syscall, `setjmp` or `longjmp` symbols and zero selected archive members. Hard-float VFPv4-D16 attributes and non-RWX LOAD permissions are unchanged.
- Static input checks: the HIL script and 5,139-byte Lottie JSON parse successfully; all three nested Git scopes pass `git diff --check`.
- Proof boundary: Build 30 has passed clean compilation, linking and static inspection. It has not yet been committed, flashed or run on hardware.
- Next action: commit the exact LVGL, binding-pointer and parent-port scopes in dependency order, then perform visible reset, flash, independent verify and both repeated-init and full GC-before-Lottie HIL tests.

## 2026-09-01 14:46 +03:00 - Build 30 hardware result and ThorVG stack diagnosis

- Git milestone before flashing: LVGL `d6de1b52541c8cfd02505334ff6abb36afa11de5`, binding/LVGL pointer `4193d7116ab16f702a3c3b06394818b436ed6bbf`, parent port `c384470e645d4200c2f3b09c6b3c117668284102`.
- A visible J-Link reset, visible programming operation and independent `verifybin` all completed successfully. Exactly `1,572,848` bytes were verified at address zero for Build 30 SHA-256 `F6B5D026FFCDAFE1B1B4BDE50C54E0C2BC20D997A6403F6869EE249E31BFC7A4`.
- Repeated-initialization HIL passed after `gc.collect()`: two additional `lv.init()` calls preserved the same display and screen, and a forced refresh completed. This confirms that the idempotent GC-init hook does not discard live LVGL or ThorVG roots.
- Full HIL passed Dave2D interrupt activity, complex gradients, matrix storage, transformed layer rendering and the custom glyph callback. After the feature scene was released and garbage-collected, approximately `241 KiB` remained free.
- The previous ThorVG use-after-free did not recur. Lottie creation, source assignment, render-buffer assignment and animation refresh all returned successfully, proving that the new engine roots survived GC pressure.
- The board then entered NMI during the forced Lottie display refresh. `NMISR=0x00001000` identifies `BSP_GRP_IRQ_MPU_STACK`; `CFSR=0` and `HFSR=0`, so this is a stack-monitor event rather than a CPU HardFault.
- The exception PC resolves to `rleRender()` in `tvgSwRle.cpp`; its generated prologue reserves `18,096` bytes by itself. The VK_RA6M3 main stack is only `16,384` bytes, and the caller had already consumed about `3,888` bytes.
- Source cause: `rleRender()` places a fixed `16,384`-byte cell pool on the stack. Its existing overflow path can split a render band and retry, so the pool can be reduced without replacing ThorVG or Dave2D.
- Selected correction for Build 31: keep the upstream-compatible `16,384`-byte default, expose it as `LV_THORVG_SW_RLE_RENDER_POOL_SIZE`, and set only VK_RA6M3 to `4,096` bytes. This adds no refresh-time allocation and preserves the MicroPython heap and the 16 KiB main stack.
- Proof boundary: Build 30 validates the GC-root and repeated-init fixes, but the complete optional-feature HIL is not yet successful because of the independently proven ThorVG RLE stack overflow.
- Next action: clean-build Build 31, verify the reduced machine-code stack frame and all prior static contracts, commit the milestone, then repeat visible reset, flash, independent verify and the complete HIL.

## 2026-09-01 14:54 +03:00 - Configurable ThorVG RLE stack pool, build 31

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Change since build 30: expose ThorVG's fixed RLE worker array as `LV_THORVG_SW_RLE_RENDER_POOL_SIZE`, retain the `16,384`-byte default for other targets, and configure only VK_RA6M3 for `4,096` bytes.
- Allocation behavior: the RLE worker remains an automatic stack object. No per-refresh heap allocation, global shared scratch buffer or renderer replacement was introduced. The existing band-splitting overflow path remains active for complex outlines.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1572861`, `data=0`, `bss=647672`, total `2220533` bytes (`0x21e1f5`). `firmware.bin` is `1,572,848` bytes with SHA-256 `2DE3CC709EC37C15EE1F62CB611C8A1C1208C1AB4BA760C188351620623E481C`.
- Delta from build 30: section sizes and binary length are unchanged; the SHA-256 differs because the generated `rleRender()` constants and stack offsets changed.
- Stack-frame proof: the ARM prologue now pushes `36` bytes, subtracts `5,760` bytes and then `12` bytes, for a total frame of `5,808` bytes. It also loads the configured pool size as the immediate value `4,096`. Build 30 reserved `18,096` bytes in the same function.
- Stack headroom: with approximately `3,888` bytes already used by the observed caller chain, the previous path required about `21,984` bytes and crossed the `16,384`-byte stack limit. The new corresponding estimate is about `9,696` bytes, leaving roughly `6,688` bytes for nested calls and interrupt entry.
- Dave2D proof: `lv_init()` still calls `lv_draw_sw_init()` and then `lv_draw_dave2d_init()`. The Dave2D dispatch/layer functions, `d2_opendevice`, render-buffer functions, `drw_int_isr` and the board DRW ISR remain retained.
- ThorVG/Lottie proof: both engine-root slots remain in the generated root table; their setter and the selected Lottie source/buffer entry points remain retained.
- Runtime dependency audit: zero selected exception, catch, personality, unwind, C/newlib allocator, syscall, `setjmp` or `longjmp` symbols and zero selected archive members. The ELF remains VFPv4-D16 single-precision hard-float with VFP argument registers.
- Segment and input checks: no LOAD segment is RWX. The HIL Python source parses successfully, and the Lottie JSON parses successfully at exactly `5,139` bytes. All three Git scopes pass `git diff --check`.
- Proof boundary: Build 31 has passed clean compilation, linking and static artifact inspection only. It has not yet been committed, flashed or executed on hardware.
- Next action: commit the exact LVGL, binding-pointer and parent-port changes in dependency order, perform the required visible J-Link reset, flash and independent verify, then rerun the complete GC-before-Lottie HIL.

## 2026-09-01 15:09 +03:00 - Build 31 flash and LVGL callback re-entry diagnosis

- Git milestone before flashing: LVGL `dccff2ee12ea1431ed36d6fbafb4f4d7bddcc5d5`, binding/LVGL pointer `370ea21acece341e6663b18ff9292ea7ac254c27`, parent port `a8c268ded3790a99e5459d520356a22e1e4dd5fe`.
- The required visible J-Link reset, visible programming operation and separate `verifybin` operation all completed successfully. Exactly `1,572,848` bytes were verified at address zero for Build 31 SHA-256 `2DE3CC709EC37C15EE1F62CB611C8A1C1208C1AB4BA760C188351620623E481C`.
- Board startup reached the MicroPython 1.28 preview REPL on COM18 with `282,608` bytes free. Board-side copies of the `18,953`-byte HIL script and `5,139`-byte Lottie JSON matched their host SHA-256 hashes.
- HIL passed Dave2D interrupt activity, all three complex gradients, matrix storage and transformed Dave2D layer output. It then stopped during `font_refresh_begin` while rendering a label through the MicroPython glyph callback.
- Fault exclusion: `IPSR=0`, `CFSR=0` and `HFSR=0`; the CPU continued executing at approximately 120 million cycles per second. This was an active loop, not NMI, HardFault or a stopped core.
- Draw-state evidence: the first LVGL draw task remained `LV_DRAW_TASK_TYPE_LABEL`, `IN_PROGRESS`, owned by the Dave2D draw unit. The Dave2D unit still referenced that task while its internal task list and draw pressure were empty; a second LVGL task remained waiting.
- Stack-symbolization evidence: the original `lv_draw_dave2d_dispatch()` frame was still active in the glyph-render callback. Inside that callback, `mp_handle_pending_internal()` ran a scheduled `lv.task_handler()`, which entered `lv_refr_now()` and draw dispatch recursively on the same LVGL C stack.
- Source cause: the board's `lv_utils.py` deliberately called `lv.task_handler()` after three blocked scheduled ticks even when `lv._nesting.value != 0`. Its assumption that a scheduled handler could never run on an active LVGL C stack is disproved by the captured Build 31 stack.
- Related binding defect: generated callback wrappers increment `_nesting` before `mp_call_function_n_kw()` but skip the decrement when a Python exception unwinds through NLR. This historical leak motivated the unsafe timeout workaround.
- Selected correction for Build 32: make the generated Python-call helper restore `_nesting` on both normal and NLR exits, and require `lv_utils.py` to return whenever `_nesting` is nonzero. Dave2D remains enabled and unchanged.
- Proof boundary: Build 31 verifies the reduced ThorVG RLE stack frame and reaches the font scene without the previous stack NMI. The full HIL is not complete because of the separately proven callback re-entry loop.

## 2026-09-01 15:19 +03:00 - NLR-safe LVGL callbacks, build 32

- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16`.
- Changes since build 31: route every generated LVGL-to-Python callback through one NLR-protected helper; decrement `_nesting` on both normal return and Python-exception propagation; remove the `_nest_stuck` timeout and never invoke `lv.task_handler()` while `_nesting` is nonzero; add a HIL probe that raises from a synchronous tree-walk callback and checks that `_nesting` returns to zero.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1571725`, `data=0`, `bss=647672`, total `2219397` bytes (`0x21dd85`). `firmware.bin` is `1,571,712` bytes with SHA-256 `3FA33DD9E5C0AAC0836A8B097D935EF735B2707EBC93AB724B0AB7C7D1E4077C`.
- Delta from build 31: `text` and the binary decreased by `1,136` bytes because the frozen Python timeout/recovery branch was removed; `data` and `bss` are unchanged.
- Generated-binding proof: the generated file contains one `_nesting++`, two `_nesting--` paths and 57 calls from wrappers to `mp_lv_call_function_n_kw()`; the only three direct `mp_call_function_n_kw()` sites are the two object-construction paths and the protected helper itself.
- Machine-code proof: the helper increments `_nesting`, calls `nlr_push()`, and on the exception branch decrements `_nesting` before `nlr_jump()`. On success it calls `mp_call_function_n_kw()`, `nlr_pop()`, then decrements `_nesting` before returning. The glyph and tree-walk callback wrappers both call this helper.
- Dave2D proof: `lv_init()` calls `lv_draw_sw_init()` and then `lv_draw_dave2d_init()`. The Dave2D dispatch/layer entry points, `d2_opendevice`, `d2_executerenderbuffer`, `d2_flushframe`, `drw_int_isr` and the board DRW ISR remain retained.
- ThorVG and root proof: `rleRender()` still reserves the reduced `5,808`-byte frame and loads the configured `4,096`-byte pool size. The generated root table contains `mp_lv_roots` and both `vk_ra6m3_thorvg_roots` slots; the Lottie data/file/buffer and matrix entry points remain retained.
- Runtime audit: zero selected C++ exception/catch/personality, ARM unwind, C/newlib allocator, syscall, `setjmp` or `longjmp` symbols and zero selected archive members. MicroPython's own `nlr`, `m_malloc` and bytecode/native support objects are expected port runtime and were not misclassified as C/newlib dependencies.
- ABI and segments: the ELF remains VFPv4-D16 single-precision hard-float with VFP argument registers. Executable LOAD segments are `R E`, writable segments are `RW`, and no LOAD segment is RWX.
- Static input checks: both binding generators, frozen `lv_utils.py` and the `19,798`-byte HIL script parse successfully; the Lottie JSON parses successfully at `5,139` bytes. The HIL SHA-256 is `634E7BEE8B29FE22018A21F1DD9E51912EB2F083CBE3FBBCAB5F88910AE9D836`; the JSON SHA-256 remains `B8F9838D449822E651A3FBB905A0CD3163523C222EA307555642694EF3A12313`. All three Git scopes pass `git diff --check`.
- Proof boundary: Build 32 has passed clean compilation, linking and static artifact inspection only. It has not yet been committed, flashed or executed on hardware.
- Next action: review and commit the binding and parent scopes in dependency order, perform the required visible J-Link reset, visible flash and independent verify, then run the complete callback-exception, Dave2D, matrix, font and GC-before-Lottie HIL.

## 2026-09-01 15:30 +03:00 - Build 32 flash and complete HIL milestone

- Git milestone before flashing: binding `607c39780d489989c300dc6e3f36d30ca884de91`, parent port `6c9cb10eea001b13ecb60183a6b89cca9cf128bb`; LVGL remained unchanged at `dccff2ee12ea1431ed36d6fbafb4f4d7bddcc5d5`.
- The required visible J-Link reset and visible programming operation both exited successfully. Two independent `verifybin` runs followed; J-Link read exactly `1,571,712` bytes from address zero and reported `Verify successful` for Build 32 SHA-256 `3FA33DD9E5C0AAC0836A8B097D935EF735B2707EBC93AB724B0AB7C7D1E4077C`.
- Board startup: MicroPython reported version `1.28.0-preview` for `VK-RA6M3 with RA6M3` and `282,608` free heap bytes before test setup.
- HIL transport: the board-side `19,798`-byte HIL script matched host SHA-256 `634E7BEE8B29FE22018A21F1DD9E51912EB2F083CBE3FBBCAB5F88910AE9D836`; the `5,139`-byte JSON matched `B8F9838D449822E651A3FBB905A0CD3163523C222EA307555642694EF3A12313`.
- Callback-exception proof: a synchronous `screen.tree_walk()` callback deliberately raised `RuntimeError`; the exception propagated to Python and `lv._nesting.value` returned to zero. This executes the NLR branch of the new generated helper on hardware.
- Re-entry proof: the font scene passed the former `font_refresh_begin` blocking point; the MicroPython glyph callback completed `140` calls and `font_refresh_end` was reached. No nested LVGL refresh loop occurred.
- Dave2D proof: the hardware probe changed the DRW interrupt count from `1` to `24`; transformed layer rendering changed the framebuffer signature from `1732771868` to `424709893` while DRW interrupts increased. Complex linear, radial and conical gradient controls also passed.
- Matrix and Lottie proof: matrix storage, array conversion and transformed output passed. Both Lottie data and file sources rendered nonempty buffers; the file callback path read all `5,139` JSON bytes. The complete test ended with `PASS heap_free 127232` and `PASS final_drw_stats (372, 24, 16248, 16248)`.
- Continued-operation proof: using `mpremote resume` without a soft reset, `_nesting` remained zero after HIL. The DRW count advanced from `372` to `703`, then to `1084` after another three seconds, while the REPL remained responsive and free heap was `129,024` bytes.
- Diagnostic correction: one separate post-HIL command was mistakenly sent with plain `mpremote exec`, which performs a soft reset, then called `lv.refr_now(lv.display_get_default())` without reinitializing LVGL. It blocked in `anim_timer`; J-Link showed no CPU fault, `_nesting=0` and `mp_lv_roots=0`. This invalid pre-init call is excluded from the firmware result. A visible J-Link reset followed, and the complete HIL plus continued-operation check was repeated correctly with `mpremote resume`.
- Result: **PASS**. Build 32 removes the proven recursive refresh, restores callback nesting after Python exceptions, keeps Dave2D active and completes the full GC-before-Lottie hardware test.
- Proof boundary: this milestone validates the optional-feature/Dave2D HIL and continued event-loop operation on the attached VK_RA6M3. It does not by itself validate the separate dual thermal-camera application or its sensor FPS.

## 2026-09-01 21:57 +03:00 - Beam-aware single-framebuffer scheduling, build 33

- Trigger and controlled comparison: the unmodified top horizontal Flex scroll flickered, while the otherwise identical case with a `6,500 us` delay after GLCDC line detect did not flicker. The user confirmed the second case visually. This isolates the fault to scanout timing in the single DIRECT framebuffer, not Flex layout, touch direction or disabled Dave2D.
- Command: `make BOARD=VK_RA6M3 clean`, then `make BOARD=VK_RA6M3 -j16` in the MinGW64 environment with `-j16` exactly.
- Port change: `machine_lcd.c` now unions the vertical bounds of pending LVGL invalidation requests. At `LV_EVENT_RENDER_START`, it waits for GLCDC line detect as before, then delays only when the dirty region cannot be completed before scanout reaches its first line and the predicted render can instead fit behind the active scan line.
- Timing model: the measured frame period is `16,590 us`; generated GLCDC timing is `316` total and `272` active lines, hence `44` blank lines. For a representative top horizontal area `y=5..80`, the immediate window is approximately `2,572 us`, the behind-beam window is approximately `12,600 us`, and the calculated delay is approximately `6,662 us`. A representative bottom area starts immediately; a `151`-line top vertical area has no safe delayed window and follows the existing path.
- Prediction model: the first comparable render uses a conservative `9,000 us` estimate plus a `500 us` margin. Later renders reuse the previous actual Dave2D duration only when both dirty height and invalidate-request count are comparable. A `100 us` scan guard is added after the last dirty line.
- Allocation and buffering: all new state is static scalar storage. The render callback performs integer arithmetic and one bounded `mp_hal_delay_us()` only when selected. No heap allocation, second framebuffer, framebuffer copy or Python callback was added.
- Diagnostics: the original first nine `machine.LCD.render_debug()` tuple fields keep their positions. Eight appended fields report last/max beam delay, delay count, no-window count, dirty `y1/y2`, dirty height and the selected render estimate. Measured render time now starts after the intentional C delay, so it continues to describe drawing work rather than scheduling latency.
- Result: **SUCCESS** (`make` exit code 0). Linking completed and generated `firmware.elf`, `firmware.hex` and `firmware.bin`.
- Size summary: `text=1572581`, `data=0`, `bss=647704`, total `2220285` bytes (`0x21e0fd`). `firmware.bin` is `1,572,568` bytes with SHA-256 `34EA142F2E9C6F18B4CAB56824EC14BA0176650A8E9F4BB69878719DA3342918`.
- Delta from build 32: `text` and binary size increased by `856` bytes; `bss` increased by `32` bytes; `data` remains zero.
- Machine-code proof: `lcd_lv_render_start_cb` contains the `9,000 us` initial estimate and a conditional call to `mp_hal_delay_us()` before the render-start timestamp. The ELF retains `lv_draw_dave2d_init`, `d2_opendevice`, `d2_executerenderbuffer`, `d2_flushframe`, `drw_int_isr` and the VK_RA6M3 DRW ISR.
- Configuration proof: `LV_USE_DRAW_DAVE2D=1`, `LV_USE_FLOAT=1` and the board-specific ThorVG RLE pool remain unchanged. The known Dave2D float-to-integer clip warnings remain visible but non-fatal. The ELF remains VFPv4-D16 single-precision hard-float with VFP argument registers, and no LOAD segment is RWX.
- Static scope: the only source change before this journal entry was `machine_lcd.c`; its target-scoped `git diff --check` passed. Neither nested LVGL nor the binding source was changed.
- Proof boundary: Build 33 proves clean compilation, linking, static scheduling logic and Dave2D retention only. It has not yet been committed, flashed or exercised on hardware.
- Next action: review and commit exactly `machine_lcd.c` plus this journal entry, perform the required visible J-Link reset, flash and independently verify Build 33, then run the no-artificial-delay Flex diagnostic and request visual confirmation.
