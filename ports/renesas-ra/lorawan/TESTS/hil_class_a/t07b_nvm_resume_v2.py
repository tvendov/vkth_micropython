# t07b_v2 — restore-then-resume WITHOUT init_defaults clobbering session
import _test_common as tc
import lorawan

m = lorawan.Mac()
# lorawan_init internally calls NvmDataMgmtRestore() — restores session keys + FCnt
# DO NOT call init_defaults() — it would overwrite restored MIBs
m.lorawan_init()
m.set_keys(tc.DEV_EUI, tc.JOIN_EUI, tc.APP_KEY)  # keys idempotent set

status_before = m.status()
print('status before nvm_restore explicit:', status_before)

# Explicit redundant restore (lorawan_init already did it, but harmless)
m.nvm_restore()

status_after = m.status()
print('status after nvm_restore explicit:', status_after)

already_joined = m.is_joined()
print('already_joined:', already_joined)

if already_joined:
    # Try a confirmed uplink to prove session is real (FCntUp continues)
    rc = m.send(1, b'\xCC', False)
    print('send rc:', rc)
    tc.pump(m, 4000)
    print('final status:', m.status())
    tc.print_result('T07B_V2_NVM_RESUME', True, joined=True)
else:
    tc.print_result('T07B_V2_NVM_RESUME', False, joined=False)
