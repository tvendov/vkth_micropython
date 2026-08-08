"""Test C v6 — zero-alloc ADR convergence (Phase Z4).

25 uplinks at 8 s spacing; ADR is enabled, server should issue LinkADRReq
and the board should climb DR0 → DR5 over ~16-20 uplinks. Forward-looking
against Z3 mac.stats_into.

Per-uplink hot-loop allocation audit:
  - time.ticks_*                  : SMALL_INT
  - mac.process / sleep_ms        : None
  - struct.pack_into into PAYLOAD : in-place
  - mac.send(...)                 : memcpy + SMALL_INT return
  - ring.for_each_since(...)      : Z1 zero-alloc traversal
  - mac.get_datarate()            : SMALL_INT (DR fits 0..5)
  - SNAP slot writes              : array.array index store, no alloc
  Steady-state alloc inside the `for i in range(25):` body: 0.

ADR detection: primary path is mac.get_datarate() polled per uplink and
compared against last_dr; mlme_confirm-tag events corroborate but are not
gating (firmware may emit confirm at MLME_LINK_ADR but the LoRaMac status
in the event payload is the LinkADRReq accept).

Pre-allocations (post-join, before loop):
  - PAYLOAD       : bytearray(8)
  - SNAP_LOG      : array.array('l', [0]*N_CYCLES*METRICS_PER_SNAP)
  - HIT / SAW_*   : array.array slots for closure scratch
"""
import lorawan, time, struct, array
from machine import Pin, SPI

from _lorawan_test_helpers import (
    bounded_ring, make_ev_cb, verdict,
    TAG_MCPS_CONFIRM, TAG_MLME_CONFIRM,
)
import credentials

N_CYCLES        = 25
INTERVAL_MS     = 8000
POLL_BUDGET_MS  = 6000
SNAPSHOT_EVERY  = 5
METRICS_PER_SNAP = 5  # [i, dr, ok, reject, mp_max]
N_SNAP_MAX      = (N_CYCLES + SNAPSHOT_EVERY - 1) // SNAPSHOT_EVERY

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
mac.set_adr(True)

ctxs = mac.nvm_diag()
total = sum(ctxs) if ctxs else 0
print("session: is_joined=%s nvm_ctx_total=%d  ADR=%s" %
      (mac.is_joined(), total, mac.get_adr()))
if not mac.is_joined():
    mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
    mac.join()
    deadline = time.ticks_add(time.ticks_ms(), 12000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined():
            break
        time.sleep_ms(20)
    print("joined fresh")

ring = bounded_ring(64)
mac.set_event_callback(make_ev_cb(ring))

# ---- Pre-allocations -----------------------------------------------------

PAYLOAD = bytearray(8)
struct.pack_into('>BI', PAYLOAD, 0, 0x61, 0)  # 0x61 == ord('a')  ("adr"-marker)

HIT_CONF  = array.array('l', [0])
HIT_MLME  = array.array('l', [0])
SAW_STATUS = array.array('l', [-1])

# STATS_BUF — minimal: only the leaves this test reads.
# Z3 mac.stats_into mutates the same nested dict shape as mac.stats();
# pre-populate every key the firmware emits to avoid rehash allocation.
STATS_BUF = {
    'mac': {
        'mac_process_count':    0,
        'mac_process_last_us':  0,
        'mac_process_max_us':   0,
    },
    'spi': {
        'spi_xfer_count':                  0,
        'spi_bytes_total':                 array.array('Q', [0]),
        'spi_max_len':                     0,
        'spi_one_byte_count':              0,
        'sx126x_spi_busy_reject_count':    0,
        'spi_stage_pre_busy_max_us':       0,
        'spi_stage_dtc_max_us':            0,
        'spi_stage_post_busy_max_us':      0,
        'sx126x_wake_count':               0,
    },
    'busy': {
        'busy_wait_count':       0,
        'busy_wait_last_us':     0,
        'busy_wait_max_us':      0,
        'busy_timeout_count':    0,
        'busy_timeout_opcode':   0,
        'busy_last_opcode':      0,
    },
    'isr': {
        'hard_isr_dio1_count':            0,
        'hard_isr_agt4_count':            0,
        'hard_isr_agt5_count':            0,
        'hard_isr_queue_push_count':      0,
        'hard_isr_queue_overflow_count':  0,
        'hard_isr_dtc_count':             0,
        'hard_isr_dtc_cycles_max':        0,
        'hard_isr_busy_low_count':        0,
        'hard_isr_busy_low_cycles_max':   0,
        'hard_isr_dio1_cycles_max':       0,
        'hard_isr_agt4_cycles_max':       0,
        'hard_isr_agt5_cycles_max':       0,
        'hard_isr_dio1_reentry_count':    0,
        'hard_isr_agt4_reentry_count':    0,
        'hard_isr_agt5_reentry_count':    0,
        'dwt_available':                  False,
    },
    'nvm': {
        'nvm_save_count':                 0,
        'nvm_save_last_ms':               0,
        'nvm_save_max_ms':                0,
        'nvm_save_error_count':           0,
        'nvm_save_call_us':               0,
        'nvm_save_done_us':               0,
        'nvm_save_in_rx_window_count':    0,
    },
    'heap': {
        'c_alloc_count':              0,
        'c_free_count':               0,
        'mp_alloc_count_post_init':   0,
        'mp_free_count_post_init':    0,
        'isr_alloc_count':            0,
        'init_baseline_count':        0,
    },
}

SNAP_LOG = array.array('l', [0] * (N_SNAP_MAX * METRICS_PER_SNAP))
snap_idx = 0

# Track confirm + mlme via array slots.
def _track_cb(_t, tag_id, status, _c=HIT_CONF, _m=HIT_MLME, _s=SAW_STATUS,
              _C=TAG_MCPS_CONFIRM, _M=TAG_MLME_CONFIRM):
    if tag_id == _C:
        _c[0] = 1
        _s[0] = status
    elif tag_id == _M:
        _m[0] = 1

print("starting at DR0 (SF12) and letting ADR climb...")
T0 = time.ticks_ms()
ok = 0
adr_changes = 0
mlme_events = 0
last_dr = mac.get_datarate()
print("[init] dr=%s" % last_dr)

# ============================== HOT LOOP ==================================
for i in range(N_CYCLES):
    target = time.ticks_add(T0, i * INTERVAL_MS)
    while time.ticks_diff(target, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(50)

    struct.pack_into('>I', PAYLOAD, 1, i)
    t_tx = time.ticks_ms()
    # Force DR0 only on first uplink; subsequent ones rely on ADR-driven DR.
    if i == 0:
        st = mac.send(1, PAYLOAD, False, 0)
    else:
        st = mac.send(1, PAYLOAD, False)
    if st != 0:
        last_dr = mac.get_datarate()
        continue

    HIT_CONF[0] = 0
    HIT_MLME[0] = 0
    SAW_STATUS[0] = -1
    poll_until = time.ticks_add(t_tx, POLL_BUDGET_MS)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(20)
        ring.for_each_since(t_tx, _track_cb)
        if HIT_CONF[0]:
            break
    # Drain remainder of budget so MLME confirm can land alongside mcps.
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(20)
        ring.for_each_since(t_tx, _track_cb)

    cur_dr = mac.get_datarate()
    if cur_dr != last_dr:
        adr_changes += 1
    if HIT_CONF[0] and SAW_STATUS[0] == 0:
        ok += 1
    if HIT_MLME[0]:
        mlme_events += 1
    last_dr = cur_dr

    if (i + 1) % SNAPSHOT_EVERY == 0:
        mac.stats_into(STATS_BUF)
        reject = STATS_BUF['spi']['sx126x_spi_busy_reject_count']
        mp_max = STATS_BUF['mac']['mac_process_max_us']
        base = snap_idx * METRICS_PER_SNAP
        SNAP_LOG[base + 0] = i + 1
        SNAP_LOG[base + 1] = cur_dr if isinstance(cur_dr, int) else -1
        SNAP_LOG[base + 2] = ok
        SNAP_LOG[base + 3] = reject
        SNAP_LOG[base + 4] = mp_max
        snap_idx += 1
# ============================ END HOT LOOP ================================

print("snapshots (i dr ok reject mp_max):")
for s in range(snap_idx):
    base = s * METRICS_PER_SNAP
    print("  [%2d] dr=%d ok=%d reject=%d mp_max=%d"
          % (SNAP_LOG[base + 0], SNAP_LOG[base + 1], SNAP_LOG[base + 2],
             SNAP_LOG[base + 3], SNAP_LOG[base + 4]))

print("DONE: %d/%d confirmed, %d ADR changes, %d mlme_confirm, final DR=%s"
      % (ok, N_CYCLES, adr_changes, mlme_events, last_dr))
mac.nvm_store()
verdict("testC_adr_v6", ok >= 20 and last_dr is not None and last_dr >= 4,
        confirmed=ok, adr_changes=adr_changes, mlme_events=mlme_events,
        final_dr=last_dr, cycles=N_CYCLES)
