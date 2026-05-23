# VK_RA4M2 LoRaWAN Class A — HIL Validation Tests

End-to-end HIL test suite for the Renesas vendor LoRaWAN stack (`MICROPY_HW_LORA_STACK=renesas`) exposed through `mod_lorawan.c`. Each script is standalone Python run via `mpremote`; no new C code is required.

## Pre-requisites

- VK_RA4M2 board flashed with the current Renesas-stack firmware (`MICROPY_HW_LORA_STACK=renesas`, `LORAWAN_BUILD_PHASE=4`).
- Wio-SX1262 wired to SCI-SPI(3) per the board's `mpconfigboard.h`; DIO1 reachable.
- SenseCAP M2 gateway @ 192.168.2.66, EU868, joined to ChirpStack 4.17 @ 192.168.2.130.
- Device provisioned on ChirpStack with credentials in `_test_common.py`:
  - DevEUI  `70B3D57ED0077416`
  - JoinEUI `0000000000000000`
  - AppKey  `DC2EC645A240B46AA1DB54C16AC35ED9`
- `class_b_params` / `class_c_params` set to SQL `NULL` (not `'{}'`) on the device row, otherwise ChirpStack rejects every JoinRequest.
- For T05 (downlink): pre-queue a frame on port 10 via psql or ChirpStack UI before running.
- For T07B (NVM resume): run after T07A and a hard reset (JLink RSetType 5 or power cycle).

## Run a single test

JLink hard reset, then mpremote:

```
JLink.exe -CommanderScript reset.jlink
mpremote connect COM34 run t01_otaa_sf7.py
```

where `reset.jlink` contains: `si SWD / speed 4000 / device R7FA4M2AD / RSetType 5 / r / g / exit`.

Each test prints exactly one `[PASS] <NAME> ...` or `[FAIL] <NAME> ...` line plus diagnostic context on failure.

## Run the batch

`pwsh _run_all.ps1 -Port COM34` cycles T01..T10 with JLink reset between each, captures stdout, parses the PASS/FAIL line, and prints a summary table. T07B and T11 are excluded from the unattended batch (T07B requires post-T07A reset and is run manually; T11 is an orchestrated multi-cycle campaign).

## Tests

| ID  | File                          | Purpose                                                  |
|-----|-------------------------------|----------------------------------------------------------|
| T01 | t01_otaa_sf7.py               | Cold-boot OTAA join @ DR5/SF7 (baseline)                 |
| T02 | t02_otaa_dr_sweep.py          | Single-DR OTAA join; DR read from `/flash/dr.txt`        |
| T03 | t03_uplink_unconfirmed.py     | 5 unconfirmed uplinks on fPort=1                         |
| T04 | t04_uplink_confirmed.py       | 1 confirmed uplink + mcps_confirm OK                     |
| T05 | t05_downlink_recv.py          | Receive pre-queued DL via `m.recv()`                     |
| T06 | t06_link_check.py             | MAC LinkCheckReq + Ans (margin, gateway count)           |
| T07 | t07_nvm_persist.py            | nvm_store after join + 2 uplinks                         |
| T07b| t07b_nvm_resume.py            | nvm_restore after cold reset, send without rejoin        |
| T08 | t08_nvm_factory_reset.py      | nvm_factory_reset wipes state; fresh join succeeds       |
| T09 | t09_adr_observe.py            | ADR=True; record DR before/after 5 uplinks               |
| T10 | t10_uplink_soak_burst.py      | 20 unconf uplinks, 30 s interval (~10 min)               |
| T11 | t11_devnonce_monotonicity.py  | Placeholder; operator runs T01 100x and checks DevNonce  |

## Output format

```
[PASS] T01_OTAA_SF7 dr=5 elapsed_us=6234521 join_status=0 events=2
```

The trailing key=value tokens are parser-friendly: split on space, then on `=`. Test runners can scrape these directly.
