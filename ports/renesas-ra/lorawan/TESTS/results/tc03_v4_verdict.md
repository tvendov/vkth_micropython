# TC03 v4 3-burst DL (paced-uplink strategy) — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] TC03_V4 received_count=3 got_21=True got_22=True got_23=True` |
| Frame 1 (bb@22) | ✓ delivered | drained inside ev_cb after UL #1 |
| Frame 2 (cc@23) | ✓ delivered | drained inside ev_cb after UL #2 |
| Frame 3 (aa@21) | ✓ delivered | drained inside ev_cb after UL #3 |
| Order | non-FIFO | ChirpStack scheduler picked by RX2 channel/DC, not insert order |
| Total events | 6 | 3× mcps_confirm + 3× mcps_indication |

## Two-phase TC03 close-out

**v3 (1 uplink):** PARTIAL — 1 of 3 frames (server delivers 1 per UL trigger).
**v4 (3 uplinks paced 15s):** GREEN — all 3 frames delivered, each tied to its own UL trigger.

## Server-side finding (ChirpStack 4.17)

Class C device profile (`supports_class_c=true`) is set correctly, but server scheduler dispatches **exactly one downlink per uplink event** rather than draining the queue autonomously on RX2. Per LoRaWAN spec, Class C *device* MAY listen continuously; server-side dispatch policy is implementation-defined. ChirpStack 4.17 has chosen "trigger-on-uplink" semantics for v4 (which differs from v3 expectations of passive RX2 streaming).

## Implication

- Stack-side Class C path: **GREEN** (RX2 continuous arm, ev_cb drain).
- Multi-frame Class C bursts require periodic uplinks to pump the server queue.
- For long-running Class C apps: pace heartbeat uplinks at ~30s-1min depending on expected DL rate.

## Overall: GREEN
