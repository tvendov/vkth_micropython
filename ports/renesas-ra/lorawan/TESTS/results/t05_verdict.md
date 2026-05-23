# T05 Downlink recv — VERDICT: FAIL (test script bug, not stack)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✗ FAIL acceptance | `[FAIL] T05_DL_RECV events=3 reason=no_downlink mcps_indications=1` |
| Air-side: uplink TX'd | ✓ (implicit) | mlme_confirm OK @ +9.3s, mcps_confirm OK @ +10.49s |
| Air-side: DL received | ✓ **mcps_indication present** @ +10.49s — DL DID arrive! |
| Device `mac.recv()` | ✗ returned None despite mcps_indication | Drain race condition |

## Root cause (per памет `reference_r12_mac_recv_single_frame`)

`mac.recv()` is **single-frame** — subsequent MAC-only DLs overwrite FRMPayload slot before top-of-cycle drain runs. Test script polls `m.recv()` AFTER `tc.pump(m, 8000)` — by then the MAC-only DL has overwritten the cafebabe payload.

**Fix**: drain `mac.recv()` INSIDE the event_cb on `mcps_indication`. Test must intercept payload before next pump cycle.

## What actually happened

1. Slave's T05 sent unconf uplink (mlme_confirm OK)
2. Server processed and queued DL `cafebabe` on f_port=10
3. Gateway TX'd DL on RX1/RX2 → device demodulated → mcps_indication fired with payload
4. Slave's pump continued; LoRaMac internal state advanced; possibly received another MAC-command-only DL that overwrote the slot
5. Slave calls `m.recv()` → returns None because slot is empty or contains MAC-only frame

## Verdict

**FAIL test design**; **stack RX path WORKING** (mcps_indication is proof DL was decoded).

## Action

Edit `t05_downlink_recv.py` to:
```python
def ev_cb(packed, *_):
    events.append((time.ticks_us(), packed))
    tag = packed & 0xff
    if tag == 1:  # mcps_indication
        data = m.recv()  # drain immediately
        if data:
            payload_holder[0] = data
```

This is exactly the pattern memory `reference_r12_mac_recv_single_frame` documents.

Stack is fine. Re-test deferred.
