# T08 NVM factory_reset — VERDICT: FAIL (test bug, not stack)

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✗ FAIL | `[FAIL] T08_NVM_FACTORY_RESET reason=still_joined_after_reset` |
| Vendor stack `NvmDataMgmtFactoryReset()` | ✓ likely OK | wipes flash (NVM contents zeroed) per LoRaMac stack docs |
| Binding layer | ✗ stale state | `mac.nvm_factory_reset()` does NOT reset in-memory singleton `joined` flag or call `LoRaMacMibSetRequestConfirm(MIB_NETWORK_ACTIVATION, NONE)` |

## Sequence

1. `lorawan_init` (auto NVM restore → `joined=True` from T07b session)
2. `init_defaults`
3. `set_keys`
4. `nvm_factory_reset` — flash wiped
5. `is_joined()` → **True** (stale singleton)

## Two fix paths

| Path | Where | Cert risk |
|---|---|---|
| **A** Test redesign | `t08_nvm_factory_reset.py` — call `m.deinit()` + re-construct after factory_reset | ✓ no firmware change |
| **B** Binding-layer hardening | `mod_lorawan.c::lorawan_mac_nvm_factory_reset` also zeroes `self->joined`, `self->keys_set`, и calls `MIB_NETWORK_ACTIVATION=NONE` | ✓ binding layer; no vendor pristine touch |

**Recommendation B** — pure-API behavior: factory_reset should leave Python-visible state consistent. A 4-line addition to existing binding. Slave can apply per `set_datarate` precedent.

## Verdict

**FAIL test acceptance** (`is_joined() == False` expected post-reset), **stack flash-wipe path correct**, **binding-layer in-memory cleanup missing**.

Not blocking Class A signoff — vendor LoRaMac is sound; this is a Python API consistency gap.
