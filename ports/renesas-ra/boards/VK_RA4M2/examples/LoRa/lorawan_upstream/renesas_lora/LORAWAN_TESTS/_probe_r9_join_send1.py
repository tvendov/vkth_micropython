"""r9 validation: OTAA join + one real uplink, top-level callback.

No helper imports and no credential printing. Purpose:
  1. prove r9 init/cb/join path survives,
  2. prove real send returns rc=0 when joined,
  3. observe mcps_confirm via packed SMALL_INT callback,
  4. print .noinit breadcrumb readout after send.

Usage:
    mpremote connect COM34 run LORAWAN_TESTS/_probe_r9_join_send1.py
"""

print("0: script entered")
import time
import lorawan
import credentials
from machine import Pin, SPI
print("1: imports ok")

TAG_MCPS_CONFIRM = 0
TAG_MCPS_INDICATION = 1
TAG_MLME_CONFIRM = 2
TAG_MLME_INDICATION = 3
TAG_MAC_ERROR = 4

EVENT_COUNT = 0
LAST_TAG = -1
LAST_STATUS = 999999
MLME_OK = 0
MCPS_OK = 0
MAC_ERROR_COUNT = 0


def _ev_cb(packed):
    global EVENT_COUNT, LAST_TAG, LAST_STATUS, MLME_OK, MCPS_OK, MAC_ERROR_COUNT
    tag = packed & 0xFF
    status = packed >> 8
    EVENT_COUNT += 1
    LAST_TAG = tag
    LAST_STATUS = status
    if tag == TAG_MLME_CONFIRM and status == 0:
        MLME_OK += 1
    elif tag == TAG_MCPS_CONFIRM and status == 0:
        MCPS_OK += 1
    elif tag == TAG_MAC_ERROR:
        MAC_ERROR_COUNT += 1


spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
print("2: SPI ok")

mac = lorawan.Mac(region=lorawan.EU868)
print("3: Mac ok")
mac.lorawan_init()
print("A: init done")
# NOTE: no explicit mac.set_min_rx_symbols(...) — relying on C default.
print("A2: default rx_diag.min_rx_symbols=",
      mac.rx_diag().get('min_rx_symbols', -1))
mac.set_event_callback(_ev_cb)
print("B: top-level cb set")

try:
    act = mac.rx_diag().get('activation', -1)
except Exception as e:
    act = -2
    print("C: rx_diag failed", repr(e))
print("C: activation=", act, "joined=", mac.is_joined())

if not mac.is_joined():
    print("D: loading credentials (values not printed)")
    mac.set_keys(credentials.DEV_EUI, credentials.JOIN_EUI, credentials.APP_KEY)
    t0 = time.ticks_ms()
    rcj = mac.join(5)
    print("E: join rc=", rcj)
    deadline = time.ticks_add(t0, 30000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined():
            break
        time.sleep_ms(20)
    print("F: joined=", mac.is_joined(),
          "elapsed_ms=", time.ticks_diff(time.ticks_ms(), t0),
          "events=", EVENT_COUNT,
          "last_tag=", LAST_TAG,
          "last_status=", LAST_STATUS,
          "mlme_ok=", MLME_OK,
          "mac_errors=", MAC_ERROR_COUNT)
else:
    print("D/E/F: already joined/restored")

if not mac.is_joined():
    print("G: STOP join failed")
    print("BC:", lorawan._last_breadcrumb())
    raise SystemExit

# One real unconfirmed uplink.
t_tx = time.ticks_ms()
rcs = mac.send(1, b'r9-1', False)
print("G: send rc=", rcs)
print("BC after send:", lorawan._last_breadcrumb())

deadline = time.ticks_add(t_tx, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if MCPS_OK:
        break
    time.sleep_ms(20)

print("H: send pump done",
      "mcps_ok=", MCPS_OK,
      "events=", EVENT_COUNT,
      "last_tag=", LAST_TAG,
      "last_status=", LAST_STATUS,
      "mac_errors=", MAC_ERROR_COUNT)

try:
    heap = mac.stats().get('heap', {})
    print("I: event_drop_count=", heap.get('event_drop_count', -1),
          "isr_alloc_count=", heap.get('isr_alloc_count', -1),
          "mp_alloc_post=", heap.get('mp_alloc_count_post_init', -1))
except Exception as e:
    print("I: stats failed", repr(e))

print("J: done")
