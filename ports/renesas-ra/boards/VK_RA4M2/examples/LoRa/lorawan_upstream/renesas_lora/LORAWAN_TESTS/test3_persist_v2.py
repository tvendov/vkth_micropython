"""Test 3 v2 — NVM session persistence over hardware reset.

Authorized rewrite of test3_persist.py to match current firmware API
(commit e6879fc27). Original kept untouched for git history.

Stage detection via mac.nvm_restore() byte count:
  bytes_restored == 0 → STAGE 1: cold, fresh OTAA join + 5 uplinks
  bytes_restored  > 0 → STAGE 2: session already restored from flash,
                       send 5 uplinks directly without rejoining.

Pass: server log shows zero JoinRequest in Stage 2 window,
      f_cnt_up continues sequentially from Stage 1 last value.
"""
import lorawan, time, binascii
from machine import Pin, SPI

# Credentials per TEST_EXECUTION_PLAN.md §0.2 (class-C-demo).
# Hardcoded because factory_reset wipes the LWCR block load_credentials reads.
DEV_EUI  = binascii.unhexlify("70B3D57ED0070003")
JOIN_EUI = binascii.unhexlify("0000000000000000")
APP_KEY  = binascii.unhexlify("202CB141A5842931F99C0C1DDFE70D68")

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.init_defaults()
mac.set_adr(False)

bytes_restored = mac.nvm_restore()
print("bytes_restored =", bytes_restored)
print("is_joined =", mac.is_joined())

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    print("  event:", ev)
mac.set_event_callback(ev_cb)

def send_5_uplinks(tag):
    for i in range(5):
        payload = tag + bytes([i])
        rc = mac.send(1, payload, False, 5)  # DR_5 = SF7 for fast smoke
        print("  uplink %d/5 rc=%d payload=%r" % (i+1, rc, payload))
        deadline = time.ticks_add(time.ticks_ms(), 6000)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            mac.process()
            time.sleep_ms(20)

if bytes_restored == 0:
    # ----- STAGE 1: cold boot, fresh join -----
    print("=== STAGE 1: cold boot, fresh OTAA join ===")
    mac.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)
    mac.join(5)
    deadline = time.ticks_add(time.ticks_ms(), 30000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    if not mac.is_joined():
        print("JOIN FAILED in Stage 1 after 30s")
        raise SystemExit(1)
    print("  joined")
    send_5_uplinks(b"s1_")
    n = mac.nvm_store()
    print("  nvm_store -> %d bytes" % n)
    print("  >>> JLink RSetType 5 to enter Stage 2 <<<")
else:
    # ----- STAGE 2: session restored from flash -----
    print("=== STAGE 2: session restored, 5 uplinks (no rejoin) ===")
    if not mac.is_joined():
        # LoRaMacInitialization already pushed MIB_NVM_CTXS internally via
        # NvmDataMgmtRestore in lorawan_init(); is_joined should be True.
        # If not, restore broke or activation state mismatch.
        raise SystemExit("ERROR: bytes_restored=%d but is_joined=False" % bytes_restored)
    send_5_uplinks(b"s2_")
    n = mac.nvm_store()
    print("  nvm_store -> %d bytes" % n)
    print("  >>> Check server: zero JoinRequest, f_cnt_up continues <<<")

print("=== T-001 stage", 2 if bytes_restored > 0 else 1, "done ===")
