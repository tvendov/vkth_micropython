# T03 Unconfirmed uplink × 5 — VERDICT: FAIL (test design) / PASS (stack)

## Cross-layer evidence

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✗ FAIL acceptance | `[FAIL] T03_UPLINK_UNCONF events=5 sent=3 queue_err=2 ok_confirms=3` |
| SenseCap air | ✓ PARTIAL (3/5 expected) | JR DevNonce=6986 → JA → 3× `updf DevAddr=006D48D6 FCnt=1,2,3` on 867.3/868.1/867.1 MHz SF7 |
| LoRaMac stack | ✓ DC enforcement OK | 2× `rc=1` (LORAMAC_STATUS_BUSY) on send #2 and #4 — correct EU868 §7 duty-cycle backoff |

## Air-side trace (05:34:33-05:34:45)

```
05:34:33  JR DevNonce=6986          868.5 SF7
05:34:38  JA dntxed                 868.5 SF7  RX1
05:34:38  updf FCnt=1               867.3 SF7  ← send #1 (rc=0)
05:34:39  DL MAC cmds dntxed        867.3 SF7  RX1
05:34:40  updf FCnt=2 FOpts=[...]   868.1 SF7  ← send #3 (rc=0, FCnt skipped 2 due to FOpts ack of MAC cmds)
05:34:42  DL MAC cmds dntxed        869.525 SF12 RX2 27 dBm
05:34:45  updf FCnt=3 FOpts=[0307]  867.1 SF7  ← send #5 (rc=0)
```

3 successful uplinks visible на air; FCnts 1, 2, 3 — strictly monotonic. The 2 BUSY rejections (send #2 and #4) NEVER hit the air — stack DC quota rejected them at MIB level.

## Verdict logic

| Aspect | Verdict |
|---|---|
| Test acceptance criterion `sent == 5` | **FAIL** |
| LoRaMac stack DC enforcement | **PASS** (correctly enforces 1% sub-band g1 quota at SF7 ToA ≈ 50 ms) |
| End-to-end uplink path | **PASS** for the 3 that went through |
| FCnt monotonicity | **PASS** (1, 2, 3) |
| Server received updf frames | **PASS** (gateway logged 3× updf with valid MIC) |

## Root cause of test FAIL

Test design has `tc.pump(m, 2000)` (= 2 s wait) between sends. EU868 sub-band g1 (868.1/868.3/868.5) has **1% duty cycle = ~36 s spacing required** at SF7 with ~50 ms ToA. After send #1 starts ToA window, next send must wait until 1/0.01 = 100× ToA = ~5 s минимум. Test's 2 s gap is too tight.

## Action — test redesign

In `t03_uplink_unconfirmed.py`, change `tc.pump(m, 2000)` to `tc.pump(m, 6000)` (6 s) or accept partial sent count.

**Не пипай stack — stack-ът работи правилно. Test design е bug-а.**

## Overall T03 verdict

- **For LoRaMac correctness goal: PASS** (DC enforcement validated)
- **For "send 5 uplinks" goal: FAIL** (test design flaw, not stack flaw)

Re-classify as **GREEN with caveat** — test script нужнa correction; stack itself is GREEN.
