# LoRaWAN Class A — HIL test orchestration

## Directory layout

```
TESTS/
├── README.md            ← this file
├── hil_class_a/         ← test scripts (Python, executable via mpremote)
│   ├── _test_common.py  ← helpers (credentials, decode_event, setup_mac, ...)
│   ├── _run_all.ps1     ← batch runner with JLink reset between tests
│   ├── _README.md       ← per-suite operator guide
│   ├── t01_otaa_sf7.py
│   ├── t02_otaa_dr_sweep.py
│   ├── t03_uplink_unconfirmed.py
│   ├── t04_uplink_confirmed.py
│   ├── t05_downlink_recv.py
│   ├── t06_link_check.py
│   ├── t07_nvm_persist.py
│   ├── t07b_nvm_resume.py
│   ├── t08_nvm_factory_reset.py
│   ├── t09_adr_observe.py
│   ├── t10_uplink_soak_burst.py
│   └── t11_devnonce_monotonicity.py
└── results/             ← slave REPL captures + master verdicts
    ├── tNN_repl.log     ← slave: captured mpremote stdout
    └── tNN_verdict.md   ← master: pass/fail with air-side cross-check
```

## Workflow

1. **Master** dispatches test via `inbox_slave.md`: subject `tNN-go`
2. **Slave** receives:
   - JLink hard reset (`RSetType 5` if available; else SYSRESETREQ default)
   - `mpremote connect COM34 run lorawan/TESTS/hil_class_a/tNN_*.py 2>&1 | Tee-Object lorawan/TESTS/results/tNN_repl.log`
   - Post 1-line confirmation in `inbox_master.md`: `## MSG ... subject=tNN-done log=results/tNN_repl.log`
3. **Master** processes:
   - Reads `TESTS/results/tNN_repl.log`
   - Pulls SenseCap log; finds JR/JA pair matching run timestamp
   - Queries ChirpStack journalctl + psql for join_accept / updf event
   - Writes `TESTS/results/tNN_verdict.md`: PASS/FAIL with evidence

## Verdict template

```markdown
# T## verdict

| Layer | Status | Evidence |
|---|---|---|
| Device REPL (slave) | PASS / FAIL | exit_code, elapsed, status_int |
| SenseCap air-side | OBSERVED / MISSING | JR @ HH:MM:SS DevNonce=N, JA dntxed +N.Ns |
| ChirpStack server | OBSERVED / MISSING | journalctl line / device.last_seen advance |

**Overall: PASS / FAIL**

Notes:
- ...
```

## Credentials (per disclosure policy)

| Field | Value |
|---|---|
| DevEUI | `70B3D57ED0077416` |
| JoinEUI | `0000000000000000` |
| AppKey | `DC2EC645A240B46AA1DB54C16AC35ED9` |
| Gateway IP | `192.168.2.66` (LuCI admin/pEphQxyC) |
| ChirpStack IP | `192.168.2.130` (SSH vkrz/vkrzg2lc) |
| ChirpStack SSH host key | `SHA256:BMG0U+ohV23QgKDjh6WCIn9gbUvNyLeic44prvT9w2U` |

## Status board

Master maintains `TESTS/STATUS.md` — running pass/fail matrix across all tests.
