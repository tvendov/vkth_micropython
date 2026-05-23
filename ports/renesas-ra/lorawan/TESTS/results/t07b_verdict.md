# T07b NVM persist (resume) — VERDICT: FAIL

| Layer | Status | Evidence |
|---|---|---|
| Device REPL | ✗ FAIL | `[FAIL] T07B_NVM_RESUME send_rc=None events=0` |
| Device status post-restore | ✗ FAIL | `joined=False` despite `keys_set=True, stack_initialized=True` |
| Air-side | ✗ MISSING | No updf with prior DevAddr `01244516` (from T07a) — session not restored |

## Root cause hypothesis

`m.nvm_restore()` was called AFTER `m.lorawan_init()`. But `lorawan_init` (mod_lorawan.c:879) **already calls `NvmDataMgmtRestore()` internally**. If T07a's `m.nvm_store()` didn't persist the session activation context (only the keys), then:
- First NvmDataMgmtRestore inside lorawan_init: loads keys but no session
- Joined check at L893-900: MIB_NETWORK_ACTIVATION returns NONE → joined=False
- Second `m.nvm_restore()` from script: re-loads same NVM → still no session → no change

## Confirmed: T07a's store wrote only partial state

Vendor `NvmDataMgmtStore` is called via `mac_nvm_context_change` callback (mod_lorawan.c:822-828) only when LoRaMac flags `notifyMibFlags` indicating context change. Slave's `m.nvm_store()` call explicitly invokes it once at script end. Question: did T07a's call happen BEFORE the JoinAccept context was fully marked persistent?

## Action

Defer T07b verdict. Need:
- Add `m.send(...)` BEFORE `m.nvm_store()` so FCnt advances and triggers NvmContextChange callback (which auto-persists fuller state)
- Or call `m.nvm_store()` twice — once after join confirmed, once after first uplink

## Test-design vs stack distinction

This is **test-design issue** like T03: the API contract for `nvm_store/restore` may require specific call timing that the test script doesn't honor.

NOT a stack bug per current evidence. Audit needed in separate dispatch.

**Overall: FAIL** (acceptance: resume should work; observed: did not). Mark for follow-up investigation.
