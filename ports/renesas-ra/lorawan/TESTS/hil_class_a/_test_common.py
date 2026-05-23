DEV_EUI  = bytes.fromhex('70B3D57ED0077416')
JOIN_EUI = bytes.fromhex('0000000000000000')
APP_KEY  = bytes.fromhex('DC2EC645A240B46AA1DB54C16AC35ED9')

_TAG  = {0:'mcps_confirm', 1:'mcps_indication', 2:'mlme_confirm',
         3:'mlme_indication', 4:'mac_error'}
_STAT = {0:'OK', 1:'ERROR', 2:'TX_TIMEOUT', 3:'RX1_TIMEOUT', 4:'RX2_TIMEOUT',
         5:'RX1_ERROR', 6:'RX2_ERROR', 7:'JOIN_FAIL', 8:'JOIN_NONCE_FAIL',
         9:'DOWNLINK_REPEATED', 10:'TX_DR_PAYLOAD_SIZE_ERROR',
         11:'DOWNLINK_TOO_MANY_FRAMES_LOSS', 12:'ADDRESS_FAIL',
         13:'MIC_FAIL'}


def decode_event(packed):
    tag = packed & 0xff
    status = packed >> 8
    return _TAG.get(tag, '?'), _STAT.get(status, '?')


def setup_mac():
    import lorawan, time
    events = []

    def ev_cb(packed, *_):
        events.append((time.ticks_us(), packed))

    m = lorawan.Mac()
    m.set_event_callback(ev_cb)
    m.lorawan_init()
    m.init_defaults()
    m.set_keys(DEV_EUI, JOIN_EUI, APP_KEY)
    return m, events


def join_blocking(m, datarate=5):
    """m.join() is itself blocking — drains foreground pump until MLME_JOIN
    confirm or internal timeout. Returns (joined: bool, elapsed_us, status_int).
    status_int: 0 = OK, 4 = RX2_TIMEOUT, -110 = -ETIMEDOUT, etc."""
    import time
    t0 = time.ticks_us()
    status = m.join(datarate)
    elapsed = time.ticks_diff(time.ticks_us(), t0)
    return (status == 0 and m.is_joined()), elapsed, status


def pump(m, ms):
    """Drain m.process() tightly for `ms` milliseconds."""
    import time
    t0 = time.ticks_us()
    deadline = time.ticks_add(t0, ms * 1000)
    while time.ticks_diff(deadline, time.ticks_us()) > 0:
        m.process()


def print_result(name, passed, **kv):
    status = 'PASS' if passed else 'FAIL'
    extras = ' '.join('%s=%s' % (k, v) for k, v in kv.items())
    print('[%s] %s %s' % (status, name, extras))


def dump_events(events, label='events'):
    """Print decoded event log — call after a test for diagnostic context."""
    print('--- %s (%d) ---' % (label, len(events)))
    for ts, packed in events:
        tag, stat = decode_event(packed)
        print('  t=%d us  %s/%s  raw=0x%x' % (ts, tag, stat, packed))
