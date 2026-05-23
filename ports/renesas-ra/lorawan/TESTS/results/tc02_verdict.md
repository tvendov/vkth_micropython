# TC02 Passive DL — VERDICT: FAIL (test orchestration bug, not stack)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✗ no `deadbeef` received | reason=no_downlink mcps_indications |
| SenseCap air | ✗ no `dntxed deadbeef` | only MAC cmds DL @ 14:13:08 (LinkADRReq + DevStatusReq + PingSlotChannelReq, FRMPayload absent) |
| ChirpStack queue | ✗ deadbeef gone | `Device queue flushed count=1` @ 11:12:37 UTC ON JOIN |

## Root cause

ChirpStack 4.17 **flushes device queue on every JoinRequest** as part of LoRaWAN session reset. Test sequence:

```
Master:  INSERT device_queue_item (deadbeef@20)   @ pre-arm
Slave:   JLink reset → cold boot
Slave:   Mac() → lorawan_init → set_keys → join()
                                            ↓
Server:  JR received → "Device queue flushed count=1"  ← deadbeef DELETED
Slave:   send(port=1) prep uplink
Server:  DL = MAC commands only (queue is empty now)
Slave:   set_class('C') → 30s pump
         no DL arrives → recv() returns None → FAIL
```

## Stack-side observation

Vendor LoRaMac on device side worked correctly:
- Class C entry via `set_class('C')` succeeded
- Continuous-RX re-arm cycle would have caught a DL had server sent one
- ev_cb mcps_indication path is wired (drained in earlier TC02 design)

**Stack is NOT the issue.** The issue is server-side queue lifecycle.

## Fix path — TC02 v2 sequence

```
Slave:   JLink reset + cold boot
Slave:   join, send prep uplink, post `tc02-joined` MSG to inbox_master.md
Master:  see tc02-joined → INSERT queue item AFTER join
Master:  post `tc02-armed-post-join` MSG
Slave:   switch to Class C, pump 30s
Server:  delivers deadbeef DL (queue populated AFTER join, survives)
Slave:   ev_cb drains → recv() returns (20, b'\xDE\xAD\xBE\xEF') → PASS
```

This requires inter-process coordination master ↔ slave via inbox messages — adds 1 round-trip handshake. Or alternative: persistent session via NVM resume (skip join entirely on subsequent runs).

## Verdict

**FAIL test orchestration**, **stack working as designed**, **server LoRaWAN-spec compliant**.

Re-design TC02 + TC03 to insert queue items AFTER join confirmation. Master takes leader role to time the INSERT.
