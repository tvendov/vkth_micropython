# T07a NVM persist (store) — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] T07A_NVM_STORE send_rc=[0, 1] is_joined=True` |
| SenseCap air | ✓ PARTIAL | JR DevNonce=18869 → JA → updf mhdr=40 DevAddr=01244516 FCnt=1 (1/2 sent; 2nd was DC-rejected — known limitation, same as T03) |
| LoRaMac stack | ✓ PASS | send_rc=[0,1]: 1st send OK, 2nd send BUSY (rc=1, DC enforcement). NVM store invoked after successful uplinks. |

Run window: 05:42:38-45. NVM should now contain session keys + FCntUp=1 + FCntDown + DevAddr=01244516.

**Overall: GREEN** — store path validated. Resume verification → T07b.
