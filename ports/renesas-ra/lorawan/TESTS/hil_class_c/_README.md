# hil_class_c — VK_RA4M2 LoRaWAN Class C HIL Tests

Parallel companion to `hil_class_a/`. Tests the vendor-Renesas Class C path
(single hardware RX + ISR-driven re-arm; see `LoRaMac.c:3623`
`OpenContinuousRxCWindow` -> `Radio.Rx(0)` -> `radio.c` `SX126xSetRx(0x0)` ->
ISR-driven re-arm in `RadioIrqProcess` while `RxContinuous == true`).

NO C code edits. Tests exercise only the Python API exposed by
`ports/renesas-ra/lorawan/mod_lorawan.c`.

## Prerequisites

- Class A `t01_otaa_sf7.py` GREEN baseline on the same firmware build.
- Bench:
  - SenseCAP M2 gateway @ `192.168.2.66`
  - ChirpStack 4.17 @ `192.168.2.130`
- Device DevEUI / AppKey provisioned in ChirpStack (see `_test_common.py`).

### Master pre-arm (psql / API) before slave runs the test

TC02 (`tc02_dl_passive.py`): queue 1 downlink before slave starts.
  - `f_port = 20`, payload `DEADBEEF`, fCnt auto.

TC03 (`tc03_dl_burst.py`): queue 3 downlinks back-to-back.
  - `f_port = 21`, payload `AA`
  - `f_port = 22`, payload `BB`
  - `f_port = 23`, payload `CC`

TC05 (`tc05_extended_passive.py`): optional — queue 1-3 DLs at random times
during the 3-min window to stress re-arm cycle. Verdict still passes if no
DLs are queued, since the test verifies "no crash + still joined".

TC01 / TC04 need no server pre-arm.

## Run pattern

Same as Class A: JLink hardware reset then `mpremote run`, tee output to
`results/`. Each script is a cold-boot standalone — no inter-test state.

```
JLinkExe -CommandFile reset.jlink
mpremote connect COM34 run tc01_class_switch.py | tee results/tc01.log
```

## Files

- `_test_common_c.py` — `setup_mac_class_c()` with drain-inside-ev_cb pattern;
  `class_c_pump()` for tight `m.process()` waiting. Imports `_test_common` from
  `hil_class_a/` for credentials/decoder/join helper.
- `tc01_class_switch.py` — `set_class('C')` round-trip + verify.
- `tc02_dl_passive.py` — 30-s passive listen, expect pre-armed DL on port 20.
- `tc03_dl_burst.py` — 45-s passive listen, expect 3 pre-armed DLs.
- `tc04_uplink_under_c.py` — uplink still works while class C is active.
- `tc05_extended_passive.py` — 3-min reliability (no crash, still joined).

## Verdict format

`[PASS] TC0N_NAME k1=v1 k2=v2 ...`
`[FAIL] TC0N_NAME k1=v1 k2=v2 ...`

## Constraints honoured

- No C edits; no vendor pristine touch.
- All waits use `class_c_pump(m, ms)` — never `time.sleep_ms`.
- `mac.recv()` drained inside `ev_cb` on `tag == 1` per
  `reference_r12_mac_recv_single_frame`.
- All scripts assume cold-boot, independent run.
