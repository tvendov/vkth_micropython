# T04 Confirmed uplink + ACK — VERDICT: PASS (GREEN)

## Cross-layer evidence

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] T04_UPLINK_CONF events=2 send_rc=0` |
| SenseCap air | ✓ PASS | JR → JA on RX1; **confirmed updf mhdr=80** FCnt=1; ACK DL on RX2 |
| LoRaMac stack | ✓ PASS | send_rc=0 (LORAMAC_STATUS_OK); mcps_confirm event with OK status |

## Air-side trace (05:35:25-05:35:32)

```
05:35:25  JR DevNonce=54056           868.3 SF7    JoinRequest
05:35:30  JA dntxed                   868.3 SF7    RX1 5.0s ✓
05:35:30  updf mhdr=80 DevAddr=00DCB1CF FCnt=1 ← CONFIRMED uplink (MType=Confirmed Data Up)
05:35:32  DL dntxed                   869.525 SF12 27dBm 23 B ← ACK on RX2
```

**Confirmed uplink path verified end-to-end:** device TX-ed confirmed frame → gateway received → server processed → ACK queued → DL TX on RX2 → device demodulated → mcps_confirm event OK.

Note: ACK came on RX2 not RX1. ChirpStack scheduling preferred SF12 fallback. Не e issue — within spec.

## Verdict T04 = GREEN (3/3 layers)
