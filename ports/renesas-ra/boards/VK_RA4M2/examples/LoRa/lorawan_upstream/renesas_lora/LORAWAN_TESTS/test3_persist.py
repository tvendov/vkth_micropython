"""Test 3 — power-cycle persistence of session state.

Stage detection via dbg_nvm_persist().init_load_ok:
  0 → cold first-ever boot, no flash data → STAGE 1: factory_reset → join
  1 → flash blob already loaded by nvm_board_init → STAGE 2: send directly,
      session keys / FCnt are already inside LoRaMac via the restore that
      ran during lorawan_init.

Server-side check: f_cnt_up must be N+1 after Stage 2 (no reset to 1)."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
p = mac.dbg_nvm_persist()
print("nvm-persist diag: init_load_ok=%d init_bytes=%d "
      "restore_calls=%d invalid=%d mib_fail=%d save=%d/%d "
      "activation_after_restore=%d (2=OTAA expected)" %
      (p[0], p[1], p[2], p[3], p[4], p[6], p[5], p[7]))

events = []
def ev_cb(ev):
    events.append((time.ticks_ms(), ev))
    print("  event:", ev)
mac.set_event_callback(ev_cb)

if p[0] == 0:
    # ----- STAGE 1: cold boot, fresh join -----
    print("=== STAGE 1: fresh join ===")
    mac.set_min_rx_symbols(24)
    mac.set_adr(False)
    deveui, joineui, appkey = mac.load_credentials()
    mac.set_keys(deveui, joineui, appkey)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    if not mac.is_joined():
        raise SystemExit("JOIN FAILED")
    print("  joined")
    mac.send(1, b"persist1", False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    n = mac.nvm_store()
    p2 = mac.dbg_nvm_persist()
    print("  nvm_store -> %d bytes (save=%d/%d)" % (n, p2[6], p2[5]))
    print("  >>> RESET BOARD (button), then re-run import test3_persist <<<")
else:
    # ----- STAGE 2: post-reset, session restored from flash -----
    print("=== STAGE 2: session restored from flash, sending uplink ===")
    print("  is_joined:", mac.is_joined())
    mac.send(1, b"persist2", False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    n = mac.nvm_store()
    p2 = mac.dbg_nvm_persist()
    print("  nvm_store -> %d bytes (save=%d/%d)" % (n, p2[6], p2[5]))
    print("  >>> Check ChirpStack: f_cnt_up should be >= 2 <<<")
