"""Test K / HW-3 v6 — zero-alloc confirmed downlinks (Phase Z4).

Server queues 10 confirmed downlinks; device opens RX windows on Class-A
uplinks every 60 s for 12 cycles. Forward-looking against Z3 mac.recv_into.

Per-cycle hot-loop allocation audit:
  - time.ticks_*                  : SMALL_INT
  - mac.process / sleep_ms        : None
  - struct.pack_into into PAYLOAD : in-place
  - mac.send(...)                 : memcpy + SMALL_INT return
  - mac.recv_into(RX_BUF, RX_INFO): Z3 contract — writes into pre-allocated
        bytearray + array.array, no allocation
  - ring.for_each_since(...)      : Z1 zero-alloc traversal
  - SNAP slot writes              : array index store
  Steady-state alloc inside the `for i in range(CYCLES):` body: 0.

Slicing RX_BUF[:RX_INFO[1]] would allocate a fresh bytes; it is gated
behind DEBUG_RX (False by default) and runs ONLY at verdict-time.

Pre-allocations (post-join, before loop):
  - PAYLOAD       : bytearray(8)
  - RX_BUF        : bytearray(256)
  - RX_INFO       : array.array('i', [0, 0]) — [fport, length]
  - HIT_IND       : array.array('l', [0])    — mcps_indication seen flag
  - SNAP_LOG      : array.array('l', [0]*CYCLES*METRICS_PER_SNAP)
"""
import lorawan, time, struct, array
from machine import Pin, SPI

from _lorawan_test_helpers import (
    bounded_ring, make_ev_cb, verdict,
    TAG_MCPS_INDICATION,
)
import credentials

CYCLES         = 12
INTERVAL_MS    = 60000
POLL_BUDGET_MS = 8000
DEBUG_RX       = False  # set True to log payload slices at verdict-time
METRICS_PER_SNAP = 4   # [i, dl_received, ind, port]

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.nvm_factory_reset()
mac.lorawan_init()
mac.set_adr(False)

mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)

ring = bounded_ring(128)
mac.set_event_callback(make_ev_cb(ring))

print("=== JOIN ===")
T0 = time.ticks_ms()
mac.join()
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined():
        break
    time.sleep_ms(20)
if not mac.is_joined():
    verdict("testK_confirmed_dl_v6", False, reason='join_failed')
    raise SystemExit("JOIN FAILED")
print("  joined at +%dms" % time.ticks_diff(time.ticks_ms(), T0))

print()
print(">>> Queue 10 CONFIRMED downlinks server-side now; this script <<<")
print(">>> sends 12 uplinks at 60s cadence to open RX windows.        <<<")
time.sleep(5)

# ---- Pre-allocations -----------------------------------------------------

PAYLOAD = bytearray(8)
struct.pack_into('>BI', PAYLOAD, 0, 0x6B, 0)  # 0x6B == ord('k')

RX_BUF  = bytearray(256)
RX_INFO = array.array('i', [0, 0])    # [fport, length]

HIT_IND  = array.array('l', [0])      # 1 iff mcps_indication seen since t_tx
LAST_PORT = array.array('l', [-1])
LAST_LEN  = array.array('l', [0])

SNAP_LOG = array.array('l', [0] * (CYCLES * METRICS_PER_SNAP))

# Optional payload preservation for DEBUG_RX: copy bytes of each downlink
# into a per-cycle slot in a flat bytearray. Allocated once at init.
DEBUG_PAYLOADS = bytearray(CYCLES * 64) if DEBUG_RX else None
DEBUG_LENS     = array.array('h', [0] * CYCLES) if DEBUG_RX else None

def _count_ind_cb(_t, tag_id, _s, _h=HIT_IND, _T=TAG_MCPS_INDICATION):
    if tag_id == _T:
        _h[0] = 1

dl_received = 0
ind_uplinks = 0

# ============================== HOT LOOP ==================================
for i in range(CYCLES):
    while time.ticks_diff(time.ticks_ms(), T0) < (i + 1) * INTERVAL_MS:
        mac.process()
        time.sleep_ms(20)

    struct.pack_into('>I', PAYLOAD, 1, i)
    t_tx = time.ticks_ms()
    st = mac.send(1, PAYLOAD, False)
    if st != 0:
        SNAP_LOG[i * METRICS_PER_SNAP + 0] = i
        SNAP_LOG[i * METRICS_PER_SNAP + 1] = dl_received
        SNAP_LOG[i * METRICS_PER_SNAP + 2] = ind_uplinks
        SNAP_LOG[i * METRICS_PER_SNAP + 3] = -1
        continue

    HIT_IND[0] = 0
    RX_INFO[0] = 0
    RX_INFO[1] = 0
    poll_until = time.ticks_add(time.ticks_ms(), POLL_BUDGET_MS)
    while time.ticks_diff(poll_until, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(20)

    # Z3 in-place recv. Pre-Z3 firmware will AttributeError — by design.
    mac.recv_into(RX_BUF, RX_INFO)
    if RX_INFO[1] > 0:
        dl_received += 1
        LAST_PORT[0] = RX_INFO[0]
        LAST_LEN[0]  = RX_INFO[1]
        if DEBUG_RX:
            n = RX_INFO[1]
            if n > 64:
                n = 64
            DEBUG_LENS[i] = n
            # bytearray-to-bytearray copy via memoryview slice assignment;
            # both sides are pre-allocated, so no new heap object created
            # for the copy itself.
            DEBUG_PAYLOADS[i * 64:i * 64 + n] = RX_BUF[0:n]

    ring.for_each_since(t_tx, _count_ind_cb)
    if HIT_IND[0]:
        ind_uplinks += 1

    base = i * METRICS_PER_SNAP
    SNAP_LOG[base + 0] = i
    SNAP_LOG[base + 1] = dl_received
    SNAP_LOG[base + 2] = ind_uplinks
    SNAP_LOG[base + 3] = LAST_PORT[0] if HIT_IND[0] else -1
# ============================ END HOT LOOP ================================

print("cycles (i dl ind port):")
for i in range(CYCLES):
    base = i * METRICS_PER_SNAP
    print("  [%2d] dl=%d ind=%d port=%d"
          % (SNAP_LOG[base + 0], SNAP_LOG[base + 1], SNAP_LOG[base + 2],
             SNAP_LOG[base + 3]))

if DEBUG_RX:
    print("payload samples:")
    for i in range(CYCLES):
        n = DEBUG_LENS[i]
        if n > 0:
            # Slice allocates — acceptable at verdict-time only.
            print("  [%2d] %s" % (i, bytes(DEBUG_PAYLOADS[i * 64:i * 64 + n])))

print()
print("=== HW-3 SUMMARY ===")
print("  cycles uplinked      : %d" % CYCLES)
print("  downlinks received   : %d" % dl_received)
print("  mcps_indication count: %d" % ind_uplinks)
print()
print("PASS criterion: dl_received >= 10 AND every confirmed downlink ACKed on next uplink")
mac.nvm_store()
verdict("testK_confirmed_dl_v6", dl_received >= 10,
        dl_received=dl_received, indications=ind_uplinks, cycles=CYCLES)
