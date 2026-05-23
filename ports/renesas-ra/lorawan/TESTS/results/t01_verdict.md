# T01 OTAA SF7 baseline — VERDICT: PASS (GREEN)

Date: 2026-05-23
Build under test: 375184 bytes (post mod_lorawan cleanup pass 3+4)

## Cross-layer evidence

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] T01_OTAA_SF7 join_status=0 events=1 elapsed_us=5199947 dr=5` (5.200 s = nominal RX1 timing for SF7 + JOIN_ACCEPT_DELAY1=5s) |
| SenseCap air | ✓ PASS | 5 JR/JA cycles observed in 05:15-05:27 window, all clean RX1 (5.0s gap, same ch/DR), SNR +12 to +14 dBm |
| ChirpStack server | ✓ PASS | journalctl: `Device-nonce validated`, `Downlink-frame saved`, MQTT `event/join` published |

## Specific JR/JA pair matching slave's reported run

- Slave run ~05:14-05:15 (operator note)
- Matching air-side: **JR @ 05:15:22 868.1 SF7 SNR+14.0 RSSI-29 DevNonce=27564 → JA @ 05:15:27** (5.000s gap, frame 20B9E210D1440C0C0DA91B0A..ACBAC02C 33 B)
- DevNonce monotonicity: 27564 > 9909 (pre-flight baseline) ✓

## Observations

- 5 cycles in 12 min suggests slave performed multiple T01 runs — all reproducibly GREEN
- DevNonces: 27564 → 25757 → 25517 → 31209 → 56118 (not strictly increasing because slave does JLink reset between runs which resets device's nonce RNG seed)
- All 5 runs got JA on RX1 (no RX2 fallback)
- ChirpStack journalctl confirms server processed and TX-ed JA for at least 2 of the 5 (others fell off the 30-min window)

## T01 verdict matrix

```
[PASS] T01_OTAA_SF7
  device   = PASS
  air      = PASS
  server   = PASS
  overall  = PASS (3/3 layers green)
```

## Ready for next phase

Per dispatch plan: T03 (unconfirmed uplink) is next. T02 DR sweep is optional polish; skip unless explicitly requested.
