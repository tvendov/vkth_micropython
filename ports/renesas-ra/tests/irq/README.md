# RA-IRQ-001: Pin IRQ dispatch

Source baseline: `1c6357b5d11d7a3d2d3697202027655a67d7f33f` plus the attached patch.
The patch is local; no Git commit, tag, push or firmware flash is implied.

## Contract

- `hard=False` (default): enqueue the current handler and Pin using the core
  scheduler. Python runs when the VM handles pending work, not in the GPIO ISR.
- Queue full: the newest event is dropped. There is no immediate Python fallback.
  The RA queue depth is 8, shared with other scheduled callbacks.
- Already queued callbacks are not cancelled by `handler=None` or re-registration.
  They retain the original handler and Pin. Stop the external source and drain
  pending work before changing application resources used by an old callback.
- Soft exceptions use the scheduler's protected callback execution. They do not
  disable the GPIO source. Handle recurring errors in the application.
- `hard=True`, fast ASM precedence/IRQ-number argument, and legacy ExtInt remain
  direct. RTC slot 16 keeps direct dispatch, independently of the GPIO flag.
- GPIO teardown disables sources and clears callback/fast references before
  `gc_sweep_all()`. The next `mp_init()` resets the scheduler queue.

## Host Regression

```powershell
# Run from the existing repository root; the compiler is a host GCC, not ARM GCC.
python ports/renesas-ra/tests/irq/run_host_tests.py --cc C:/msys_64/mingw64/bin/gcc.exe
python ports/renesas-ra/tests/irq/test_dac_build_guard.py --cc C:/msys_64/mingw64/bin/gcc.exe
```

The runner compiles extracted, unmodified production C function bodies and the
real core scheduler enqueue/lock/drain bodies at `-O0` and the port's `-Os`.
Hardware routing, Python objects, NLR/GC and protected calls use host stubs.
Consequently this proves dispatch/queue/lifecycle logic, not real heap allocation,
NVIC timing, interrupt priorities or physical soft reset. The old dispatcher fails
the first assertion: soft callbacks run synchronously.

`test_dac_build_guard.py` covers the independent RA-BUILD-001 build prerequisite
with IQ ADC both disabled and enabled, including failed stop and a stop callback
that changes the live IQ flag. It does not test DAC hardware.

## ARM Builds

Working directory: `ports/renesas-ra` inside the existing MSYS2 source tree.

```bash
export PATH=/mingw64/bin:/ucrt64/bin:/usr/bin:$PATH
make BOARD=VK_RA4M2 BUILD=build-VK_RA4M2-irq-20260922 PYTHON=/mingw64/bin/python3.exe -j8
make BOARD=VK_RA4M2 BUILD=build-VK_RA4M2-irq-no-subclk-20260922 PYTHON=/mingw64/bin/python3.exe \
  CFLAGS_EXTRA="-DMICROPY_HW_RTC_SOURCE=1 -DMICROPY_HW_SUBCLK_POPULATED=0 -DBSP_CLOCK_CFG_SUBCLOCK_POPULATED=0" -j8
```

The no-subclock flags come from `build_vk_ra4m2_hexes.sh` and the guarded board
headers. They define this new candidate, not the provenance of an older HEX.
Keep these outputs separate from the existing published firmware. Use the exact
board/oscillator variant; a common build string does not distinguish the variants.

## Hardware Acceptance Still Required

1. Identify BOARD, oscillator variant, connected probe/COM port, source baseline,
   candidate and approved test pins. Save existing firmware/files as applicable.
2. Run `hil_pin_irq.run(rx_name, tx_name)` with the documented physical loopback.
   Record the real IPSR, allocation result and callback arguments for soft/hard/fast.
3. Saturate the shared scheduler and verify recovery without an ISR Python call.
   Test soft exceptions, hard exceptions and an already queued old handler.
4. Drive edges during repeated soft resets. Verify no old callback/ASM entry
   accesses the freed heap and no callback survives into the new VM.
5. Measure latency separately if required; soft dispatch has no fixed realtime
   latency guarantee. Repeat on other affected boards before claiming their HIL.

Do not use `pyb.ExtInt.swint()` as proof of real ISR context: RA implements it as
a synchronous C callback. Do not exercise the existing RTC exception/OOB defect
as part of this GPIO patch. Track that independently.
