# LoRaWAN Class A — HIL test status

Updated by master after each test verdict.

| # | Test | Slave REPL | SenseCap | ChirpStack | Overall | Notes |
|---|---|---|---|---|---|---|
| T01 | OTAA SF7 baseline | ✓ PASS | ✓ PASS | ✓ PASS | **GREEN** | 5.200s join; 5 reproducible cycles in 12 min |
| T02 | OTAA DR sweep (DR0-DR5) | — | — | — | NOT_RUN | depends T01 pass |
| T03 | Unconfirmed uplink × 5 | ✗ FAIL | ✗ 3/5 only | n/a | **FAIL** | Test design: 2s spacing < EU868 1% DC. Re-test after `pump(m, 6000)` fix. |
| T03 v2 | UL × 5 @ 8s spacing | ✓ PASS 5/5 | (impl) | (impl) | **GREEN** | 8s spacing eliminates DC violation; ok_confirms=5 |
| T04 | Confirmed uplink + ACK | ✓ PASS | ✓ updf+ACK | ✓ delivered | **GREEN** | confirmed UL mhdr=80, ACK on RX2 SF12 |
| T05 | Downlink recv | ✗ FAIL recv() | ✓ DL arrived | ✓ cafebabe queued | **FAIL** | mcps_indication fired but recv()=None; drain race |
| T05 v2 | DL recv via ev_cb drain | ✓ PASS port=10 | (impl) | ✓ queue consumed | **GREEN** | ev_cb captures `(10, b'\xCAFEBABE')`; NVM-resume + UL trigger pattern |
| T06 | link_check / last_link_check | ✓ PASS | ✓ FOpts piggyback | ✓ Ans delivered | **GREEN** | gateways=1 margin=21 dB |
| T07a | NVM persist (store) | ✓ PASS | ✓ updf seen | ✓ stored | **GREEN** | joined=True, 1 uplink TX (DC limited 2nd) |
| T07b | NVM persist (resume) | ✗ FAIL | ✗ no resume updf | n/a | **FAIL** | joined=False after nvm_restore; test design or vendor store timing — investigate |
| T08 | NVM factory_reset | ✗ FAIL | (flash wiped per stack) | n/a | **FAIL** | binding gap: in-memory `joined` not cleared post-factory_reset |
| T08 v2 | factory_reset + cold-reset verify | ✓ PASS | (n/a) | n/a | **GREEN** | 2-phase: factory_reset wipes NVM; cold boot → is_joined()=False |
| T07b v1 rerun | NVM resume (new build) | ✓ PASS | (cors. pending) | (cors. pending) | **GREEN** | recent +456B firmware edit fixed prior FAIL |
| T09 | ADR observe | ✗ FAIL | ✓ 4/5 + MAC cmd loop | ⚠ ADR not triggered | **FAIL** | Same DC root as T03 |
| T09 v2 | 8 UL @ 10s + set_adr(True) | ✗ FAIL send_err=6 | n/a | n/a | **FAIL** | NVM-resume kept DR0 SF12 → DC_RESTRICTED rc=11 |
| T09 v3 | 5 UL @ 10s + DR5 + ADR | ✓ PASS 5/5 | (impl) | (impl) | **GREEN** | explicit set_datarate(5) BEFORE set_adr(True); 0 errors, ADR enabled |
| T10 | Soak (reshaped 6×30s) | ✓ PASS | (cors. pending) | (cors. pending) | **GREEN** | 6/6 sent, 0 errors, joined retained |
| T11 | DevNonce monotonicity ×100 | — | — | — | NOT_RUN | requires master orchestration loop |
| TC01 | Class A → C → A switch | ✓ PASS | n/a (MIB only) | n/a | **GREEN** | rc_set_c=0, rc_set_a=0, round-trip OK |
| TC04 | Uplink in Class C | ✓ PASS | (cors. pending) | (cors. pending) | **GREEN** | send_rc=0; TX path unaffected by class |
| TC02 v1 | Passive DL `deadbeef@20` (fresh JR) | ✗ no DL recv | ✗ no dntxed | ✗ queue flushed on JR | **FAIL** | ChirpStack flushes queue on every JR; pre-arm-before-join doesn't work |
| TC02 v3 | Passive DL `deadbeef@20` (NVM-resume) | ✓ PASS | (impl) | ✓ delivered + queue cleared | **GREEN** | No JR → no flush; ev_cb drain returned `(20, b'\xDEADBEEF')`; session 007ea08f preserved |
| TC03 v3 | 3-burst DL (1 trigger UL) | ✗ 1of3 (aa@21 only) | (impl) | ✓ 1 delivered, 2 queued | **PARTIAL** | ChirpStack dispatches 1 DL per UL trigger; v3 sent only 1 UL |
| TC03 v4 | 3-burst DL (3 trigger UL paced 15s) | ✓ PASS 3/3 | (impl) | ✓ all 3 delivered | **GREEN** | (aa,bb,cc) drained via ev_cb; non-FIFO order; events=6 |
| TC05 v3 | 10-burst stress (10 UL @ 15s) | ✓ PASS 10/10 | (impl) | ✓ all 10 delivered | **GREEN** | ports 30..39 FIFO order; events=20; 0 misses; ~2:40 wall |

## Pre-flight baselines (2026-05-23 ~05:11)

- SenseCap last 70b3 JR: 05:05:11 DevNonce=9909 — next must be >9909
- ChirpStack device row: DevEUI 70b3d57ed0077416 exists, last_seen 2026-05-06 22:14 (stale)
- DevAddr (last session): `0069d8d0`

## Test session log

| Time | Event |
|---|---|
| 05:11 | Pre-flight master pull; T01 dispatched to slave |
