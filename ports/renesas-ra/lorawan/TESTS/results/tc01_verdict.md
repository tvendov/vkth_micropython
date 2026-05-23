# TC01 Class A → C → A switching — VERDICT: PASS (GREEN)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✓ PASS | `[PASS] TC01_CLASS_SWITCH rc_set_a=0 back=A a=A rc_set_c=0 c=C` |
| `set_class('C')` rc | ✓ 0 (LORAMAC_STATUS_OK) | binding accepts char |
| `get_class()` after C | ✓ returns 'C' | round-trip confirmed |
| `set_class('A')` rc | ✓ 0 | back to baseline |
| `get_class()` after A | ✓ returns 'A' | cleanup confirmed |

API round-trip clean. No air-side action required for class switching (it's a MIB write only).

**Overall: GREEN** — class switching binding works.
