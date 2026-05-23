# T09 ADR observe — VERDICT: FAIL (test design, same DC root cause as T03)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✗ FAIL | `[FAIL] T09_ADR dr_after=5 adr_before=True adr=True send_errors=1 dr_before=5` |
| LoRaMac stack | ✓ DC enforcement OK | send_rc=1 BUSY rejected 1 of 5 uplinks (DC throttle at SF7 SF7-50ms ToA, sub-band g1 1%) |
| SenseCap air | ✓ PARTIAL (4/5 on air) | JR DevNonce=9162 → JA → 4× updf DevAddr=01675996 FCnt=1,2,3,4 — FCnt=2 has big FOpts=[030706000A1103] = server MAC cmds (LinkADRReq + others); FCnt=3 ACKs server cmds via FOpts=[0307] |
| ADR negotiation | ✗ NOT OBSERVED | dr_before=5 dr_after=5 — server-side LinkADRReq present but device's response didn't change DR (may need more uplinks or non-min-DR start) |

Run window: 05:43:24-42. 5 sends, 4 made it to air, 1 BUSY.

## Root cause analysis

Same as T03: test's `tc.pump(m, 3000)` (3s between sends) violates EU868 DC at SF7. Stack correctly rejects.

ADR observation also limited: 5 uplinks not enough to trigger server-driven DR change. ADR typically needs ADR_ACK_DELAY (32) or ADR_ACK_LIMIT (64) uplinks before forcing DR change.

## Verdict

**FAIL** test acceptance criterion (send_errors > 0); **stack DC enforcement PASS**; **ADR negotiation INCONCLUSIVE** (test too short to observe). Re-design needed:
- Increase pump interval to 36+ s
- Run 50+ uplinks to give server time to issue LinkADRReq
- Or pre-arm ADR via psql device_session direct DR override

Same root pattern as T03 — test design issue, not stack issue.
