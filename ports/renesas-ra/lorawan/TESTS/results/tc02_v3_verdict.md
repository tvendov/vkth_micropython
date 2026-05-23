# TC02 v3 Passive DL (NVM-resume strategy) — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] TC02_V3 received_count=1 preview=[(20, 'deadbeef')] events=2` |
| ChirpStack queue | ✓ delivered | row `811e99f4-...` cleared from `device_queue_item` post-run |
| Device session | ✓ stable | dev_addr=007ea08f preserved, last_seen advanced 11:25:35 → 11:51:24 (trigger uplink) |
| ev_cb drain | ✓ correct | mcps_indication fired → `mac.recv()` inside callback returned `(20, b'\xDEADBEEF')` |

## Design that worked

```
Master:  INSERT queue item (deadbeef@20) — session 007ea08f already active
Slave:   JLink reset → cold boot
Slave:   lorawan.Mac() → set_event_callback → lorawan_init()
                                                ↓
                                     NvmDataMgmtRestore lifts session
                                                ↓
Slave:   is_joined() == True   ← no JR sent, no queue flush
Slave:   set_class('C')        ← continuous RX2 armed
Slave:   send(1, 0x01)         ← trigger uplink wakes ChirpStack scheduling
Server:  delivers deadbeef on RX2 (immediate after RX1 in Class C)
Slave:   ev_cb mcps_indication → mac.recv() → received=[(20, deadbeef)]
Slave:   PASS verdict
```

## Why TC02 v1 failed and v3 passes

| Aspect | v1 (FAIL) | v3 (PASS) |
|---|---|---|
| Session activation | fresh JR every run | NVM-resume from stored session |
| Queue lifecycle | flushed on JR (ChirpStack 4.17 behavior) | preserved (no JR) |
| Master timing | must INSERT after JR but before pump end | can INSERT any time before device boots |
| Test reliability | depends on master polling race | no race — queue armed offline |

## Implication

- Vendor LoRaMac stack Class C path: **GREEN** (RX2 continuous arm, ev_cb mcps_indication wired correctly)
- TC02 v1 FAIL was orchestration design, not firmware/stack
- TC03 v3 (burst 3 DLs) can reuse same pattern: pre-arm 3 queue items → boot device → set_class('C') → drain inside ev_cb

## Overall: GREEN
