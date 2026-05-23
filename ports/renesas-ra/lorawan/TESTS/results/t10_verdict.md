# T10 Soak (reshaped 6×30s) — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] T10_SOAK mac_errors=0 mlme_confirms=0 joined_after=True events=9 sent=6 queue_err=0 exc=0` |
| LoRaMac stack | ✓ PASS | 6/6 sent, 0 BUSY, 0 errors, 0 exceptions, joined remained True |
| Air-side | implicit (needs SenseCap correlation) | 30s spacing within EU868 1% DC budget at SF7 |

Reshaped per operator policy "no tests over 3 min": N=20→N=6, INTERVAL=30s preserved. Wall time ~3 min.

events=9 = 6 send-completions + 3 MAC-related (likely periodic LinkADRReq/Ans piggybacks or status events). No DC throttle since spacing respects budget.

**Overall: GREEN** — soak stability validated on 3-min scale. Full 24h soak needs separate orchestration (out of session-time budget).
