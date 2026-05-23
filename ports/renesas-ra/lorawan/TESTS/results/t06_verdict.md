# T06 link_check — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] T06_LINK_CHECK gateways=1 margin=21` |
| SenseCap air | ✓ PASS | JR DevNonce=12983 → JA → **confirmed updf mhdr=80** FCtrl=81 FCnt=1 **FOpts=[02]** (LinkCheckReq piggyback) → DL on RX1 867.1 SF7 26B with LinkCheckAns |
| ChirpStack server | ✓ PASS | LinkCheckAns delivered (margin=21 dB, gateways=1 confirmed via `mac.last_link_check()`) |

Run window: 05:41:46-52. margin=21 dB is healthy at -29 dBm RSSI / SNR +11.8. gateways=1 confirms single-gateway bench.

**Overall: GREEN**
