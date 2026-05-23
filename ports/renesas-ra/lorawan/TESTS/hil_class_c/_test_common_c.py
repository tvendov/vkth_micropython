import sys
sys.path.insert(0, '../hil_class_a')
from _test_common import (
    DEV_EUI, JOIN_EUI, APP_KEY,
    decode_event, join_blocking, pump, print_result, dump_events,
)

import lorawan, time


def setup_mac_class_c():
    """Setup Mac with an ev_cb that drains mac.recv() inside mcps_indication.

    Per `reference_r12_mac_recv_single_frame`, the rx_pending slot is single-
    frame; for Class C we must consume it inside the callback or the next DL
    overwrites it before the foreground pump notices. Returns
    (m, events, received) where received is a list of (port, bytes) tuples."""
    events = []
    received = []
    mac_ref = [None]

    def ev_cb(packed, *_):
        events.append((time.ticks_us(), packed))
        tag = packed & 0xff
        if tag == 1 and mac_ref[0] is not None:
            try:
                pkt = mac_ref[0].recv()
                if pkt is not None:
                    received.append(pkt)
            except Exception:
                pass

    m = lorawan.Mac()
    mac_ref[0] = m
    m.set_event_callback(ev_cb)
    m.lorawan_init()
    m.init_defaults()
    m.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)
    return m, events, received


def class_c_pump(m, ms):
    """Tight m.process() loop for `ms` ms.

    Class C reliability depends on this: the vendor LoRaMac re-arms the SX1262
    RX from inside RadioIrqProcess after each RxDone, which is reached only
    via mac.process(). Any time.sleep_ms() here would leave the chip in
    STANDBY between DLs and miss frames."""
    t0 = time.ticks_us()
    deadline = time.ticks_add(t0, ms * 1000)
    while time.ticks_diff(deadline, time.ticks_us()) > 0:
        m.process()
