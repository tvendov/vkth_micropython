# TC05 v3 Stress (10-frame Class C burst) — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS 10/10 | `[PASS] TC05_V3 received_count=10` |
| Ports | ✓ all unique | `unique_ports=[30..39]` |
| Payloads | ✓ all matched | preview shows 01..0a in port-order (FIFO) |
| ev_cb drain | ✓ clean 2× | events=20 = 10 mcps_confirm + 10 mcps_indication |
| DC compliance | ✓ no violations | 10 SF7 UL over 150s = ~0.5% DC |
| Wall time | ✓ within budget | ~150s + boot ≈ 2:40 (< 3 min rule) |

## Stress test profile

- 10 queue items pre-armed server-side (uniform 1-byte payload, ports 30..39)
- 10 trigger uplinks @ 15s spacing
- Class C continuous RX (`set_class('C')` after NVM resume)
- ev_cb drains each `mcps_indication` via `mac.recv()` to dodge single-slot overwrite race

## Comparison vs TC03

- TC03 v4 = 3 frames, non-FIFO order, all 3 received
- TC05 v3 = 10 frames, FIFO order, all 10 received

Both confirm: at 15s UL spacing, ChirpStack's 1-DL-per-UL scheduler can drain a queue cleanly. Larger queues just need more uplinks.

## Overall: GREEN
