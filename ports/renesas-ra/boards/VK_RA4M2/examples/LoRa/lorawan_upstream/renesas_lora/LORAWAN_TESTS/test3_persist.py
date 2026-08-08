"""Test 3 — power-cycle persistence of session state.

Stage detection via mac.rx_diag()['activation'] after lorawan_init()
(Contract A, HW6_STAGE_DETECTION_2026-05-13.md): 0 = ACTIVATION_TYPE_NONE
→ STAGE 1 (fresh join + nvm_store); non-zero (1=ABP, 2=OTAA) → STAGE 2
(session restored by NvmDataMgmtRestore inside lorawan_init, send directly).
is_joined() is unreliable here — mod_lorawan.c:823 clobbers self->joined=false
unconditionally at end of lorawan_init regardless of restore outcome.

Server-side check: f_cnt_up must be N+1 after Stage 2 (no reset to 1)."""
import lorawan, time
from machine import Pin, SPI

from _lorawan_test_helpers import bounded_ring, make_ev_cb, verdict
import credentials

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
ctxs = mac.nvm_diag()
total = sum(ctxs) if ctxs else 0
print("nvm-diag ctxs (mac,region,crypto,se,cmds,classb,cq):", ctxs)
print("  total NVM context bytes: %d" % total)

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

activation = mac.rx_diag().get('activation', 0)
if activation == 0:
    print("=== STAGE 1: fresh join (activation=0 NONE) ===")
    mac.set_adr(False)
    mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined(): break
        time.sleep_ms(20)
    if not mac.is_joined():
        verdict("test3_persist_stage1", False, reason='join_failed')
        raise SystemExit("JOIN FAILED")
    print("  joined")
    mac.send(1, b"persist1", False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    n = mac.nvm_store()
    ctxs2 = mac.nvm_diag()
    total2 = sum(ctxs2) if ctxs2 else 0
    save_count = mac.stats()['nvm']['nvm_save_count']
    print("  nvm_store -> %d bytes (ctx_total=%d, save_count=%d)" %
          (n, total2, save_count))
    print("  >>> RESET BOARD (button), then re-run import test3_persist <<<")
    verdict("test3_persist_stage1", n > 0 and total2 > 0,
            bytes_stored=n, ctx_total=total2, save_count=save_count)
else:
    print("=== STAGE 2: session restored from flash, sending uplink ===")
    print("  activation=%d (1=ABP, 2=OTAA)" % activation)
    mac.send(1, b"persist2", False)
    end = time.ticks_add(time.ticks_ms(), 8000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
    n = mac.nvm_store()
    ctxs2 = mac.nvm_diag()
    total2 = sum(ctxs2) if ctxs2 else 0
    save_count = mac.stats()['nvm']['nvm_save_count']
    print("  nvm_store -> %d bytes (ctx_total=%d, save_count=%d)" %
          (n, total2, save_count))
    print("  >>> Check ChirpStack: f_cnt_up should be >= 2 <<<")
    verdict("test3_persist_stage2", n > 0 and total2 > 0,
            bytes_stored=n, ctx_total=total2, save_count=save_count)
