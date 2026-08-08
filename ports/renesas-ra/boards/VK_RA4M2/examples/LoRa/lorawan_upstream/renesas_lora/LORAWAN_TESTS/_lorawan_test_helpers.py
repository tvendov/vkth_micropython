"""LORAWAN_TESTS shared helpers (B-1 step 2; rev 6 — Z1 zero-alloc Ring).

Five public helpers, built on the firmware surface at
`lorawan/mod_lorawan.c` commit e6879fc27:

    bounded_ring(N)            -> Ring
    make_ev_cb(ring)           -> callable(ev) suitable for set_event_callback
    join_otaa(mac, eui, joineui, appkey, attempts=5, min_rx=0,
              join_dr=0, timeout_ms=30000) -> JoinResult
    verdict(name, passed, **metrics) -> None

VERDICT line format (regex, B-1 step 3):

    ^VERDICT:\\s+(PASS|FAIL)\\s+(\\S+)(?:\\s+(.*))?$

Z1 — Ring v2 (zero-alloc on hot path)
-------------------------------------
Per coordination/ZERO_ALLOC_REMOVAL_PLAN_2026-05-13.md §Z1, the storage is
three parallel `array.array` buffers — no per-event tuple allocation, no
deque, no `_make_record`. The event tag is encoded as a small int via
the closed LUT below (must match the qstrs `mod_lorawan.c` emits in
`mac_post_event` at line ~657; verified against e6879fc27).

Two scan APIs:
  * `ring.for_each_since(t_ms, cb)` — ZERO-ALLOC traversal (the new path).
    `cb(t, tag_id, status)` is called for each record with t > t_ms,
    newest-first, short-circuits on first non-hit. The caller's `cb` is
    the allocation surface; a counter-bump cb is alloc-free.
  * `ring.since(t_ms)` — DEPRECATED, kept for back-compat. Materialises
    a list of 3-tuples — every call allocates. New tests should not use
    this; existing callers will migrate in Z4.

Ring scan idiom (Z1+):

    HIT = [0]
    def _check_cb(_t, tag_id, _s, _hit=HIT):
        if tag_id == TAG_MCPS_CONFIRM:
            _hit[0] = 1
    t_tx = time.ticks_ms()
    mac.send(...)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process(); time.sleep_ms(20)
        HIT[0] = 0
        ring.for_each_since(t_tx, _check_cb)
        if HIT[0]:
            break
    if ring.wrapped_since(t_tx):
        print("ring wrapped — bump N or shorten poll window")
"""

import sys
import time
import array

_VALID_NAME_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"


# -- Event tag id LUT -------------------------------------------------------
# Source-of-truth: firmware `mod_lorawan.c` emits exactly five tag qstrs
# from `mac_post_event` (see mac_mcps_confirm/indication, mac_mlme_confirm/
# indication, mac_error_notify — lines 679, 708, 732, 736, 777 in
# e6879fc27). Any future tag MUST land here in the same commit; the LUT
# is closed by design (see plan §2.2 — "the firmware-side qstr set is
# closed"; a new tag is a firmware-API change).

TAG_MCPS_CONFIRM    = 0
TAG_MCPS_INDICATION = 1
TAG_MLME_CONFIRM    = 2
TAG_MLME_INDICATION = 3
TAG_MAC_ERROR       = 4
TAG_UNKNOWN         = 255

# qstr keys are interned in MicroPython — dict.get with an interned-qstr
# key does NOT allocate per call (the lookup is by hash on a cached
# qstr_t pointer, no string materialisation).
_TAG_TO_ID = {
    'mcps_confirm':    TAG_MCPS_CONFIRM,
    'mcps_indication': TAG_MCPS_INDICATION,
    'mlme_confirm':    TAG_MLME_CONFIRM,
    'mlme_indication': TAG_MLME_INDICATION,
    'mac_error':       TAG_MAC_ERROR,
}

# Reverse map, used only by the deprecated `since()` path so tests reading
# the legacy 3-tuples still see a str tag. Hot-path tests use tag_id ints
# directly via for_each_since().
_ID_TO_TAG = (
    'mcps_confirm',
    'mcps_indication',
    'mlme_confirm',
    'mlme_indication',
    'mac_error',
)


# -- Ring v2 ----------------------------------------------------------------

class Ring:
    """Bounded event ring with monotonic ms timestamps. Construct via
    bounded_ring(N), not directly. Storage is three parallel array.array
    buffers; per-event push is zero-alloc by construction.

    Scan APIs:
      * for_each_since(t_ms, cb) — zero-alloc traversal, newest-first.
        cb(t_ms, tag_id, status) is called for each record with
        t_ms > given t; short-circuits on first older record.
      * since(t_ms) — DEPRECATED. Allocates a fresh list of 3-tuples
        per call. Kept for legacy callers; new code uses for_each_since.
      * wrapped_since(t_ms) — bool, same semantics as v1.
      * latest(tag_id=None) — returns a fresh 3-tuple (alloc; test-end
        inspection only, not hot path).
    """

    __slots__ = ('_t_ms', '_tag_id', '_status',
                 '_n', '_head', '_count', '_dropped')

    def __init__(self, n):
        # Signed-long ('l') for t_ms: MicroPython time.ticks_ms() returns
        # a small int that fits 30-bit signed on 32-bit ports — array('l')
        # is 32-bit signed on this MicroPython build, sufficient for the
        # full ticks_ms range. status is also signed (errors come back as
        # small negative LoRaMacEventInfoStatus_t values). tag_id is a
        # single byte (255 sentinel).
        self._t_ms   = array.array('l', [0] * n)
        self._tag_id = array.array('B', [0] * n)
        self._status = array.array('l', [0] * n)
        self._n      = n
        self._head   = 0   # next write slot
        self._count  = 0   # entries currently held, 0..n
        self._dropped = 0  # records evicted since construction

    def push(self, t_ms, tag_id, status):
        """Zero-alloc push. Wraps when full; tracks evictions in _dropped."""
        i = self._head
        self._t_ms[i]   = t_ms
        self._tag_id[i] = tag_id
        self._status[i] = status
        i += 1
        if i >= self._n:
            i = 0
        self._head = i
        if self._count < self._n:
            self._count += 1
        else:
            self._dropped += 1

    def for_each_since(self, t_ms, cb):
        """Walk newest-first; call cb(t, tag_id, status) for each record
        with t > t_ms. Short-circuits on the first older record. Zero
        allocation in this method (no range() — uses a counted while);
        cb is the caller's alloc surface."""
        c = self._count
        if c == 0:
            return
        n = self._n
        i = self._head - 1
        if i < 0:
            i = n - 1
        ts  = self._t_ms
        tgs = self._tag_id
        sts = self._status
        remaining = c
        while remaining > 0:
            rec_t = ts[i]
            if time.ticks_diff(rec_t, t_ms) > 0:
                cb(rec_t, tgs[i], sts[i])
                i -= 1
                if i < 0:
                    i = n - 1
                remaining -= 1
            else:
                return

    def since(self, t_ms):
        """DEPRECATED — allocates a fresh list of 3-tuples each call.
        Use for_each_since(t_ms, cb) on hot paths. Returns records in
        arrival order (oldest-first among the hits)."""
        out = []
        c = self._count
        if c == 0:
            return out
        n = self._n
        i = self._head - 1
        if i < 0:
            i = n - 1
        ts  = self._t_ms
        tgs = self._tag_id
        sts = self._status
        remaining = c
        while remaining > 0:
            rec_t = ts[i]
            if time.ticks_diff(rec_t, t_ms) > 0:
                tid = tgs[i]
                tag_str = _ID_TO_TAG[tid] if tid < len(_ID_TO_TAG) else 'unknown'
                out.append((rec_t, tag_str, sts[i]))
                i -= 1
                if i < 0:
                    i = n - 1
                remaining -= 1
            else:
                break
        out.reverse()
        return out

    def wrapped_since(self, t_ms):
        """True iff the ring may have evicted records the caller asked
        about: at least one drop has happened AND the oldest still-held
        record arrived after t_ms (so anything older was lost)."""
        if self._dropped == 0:
            return False
        if self._count == 0:
            return True
        n = self._n
        oldest_i = self._head - self._count
        if oldest_i < 0:
            oldest_i += n
        return time.ticks_diff(self._t_ms[oldest_i], t_ms) > 0

    def latest(self, tag_id=None):
        """Test-end inspection only. Allocates a fresh 3-tuple on hit.
        Pass tag_id (small int) to filter; None returns the newest."""
        c = self._count
        if c == 0:
            return None
        n = self._n
        i = self._head - 1
        if i < 0:
            i = n - 1
        remaining = c
        while remaining > 0:
            tid = self._tag_id[i]
            if tag_id is None or tid == tag_id:
                return (self._t_ms[i], tid, self._status[i])
            i -= 1
            if i < 0:
                i = n - 1
            remaining -= 1
        return None

    def clear(self):
        # Reset indices and counters; the array storage is reused so no
        # GC churn. _dropped is reset since clear is a phase boundary.
        self._head = 0
        self._count = 0
        self._dropped = 0

    def __len__(self):
        return self._count


def bounded_ring(N=64):
    if not isinstance(N, int) or N <= 0:
        raise ValueError("bounded_ring N must be positive int")
    return Ring(N)


# -- Event callback factory -------------------------------------------------

def make_ev_cb(ring):
    """Return a closure suitable for `mac.set_event_callback(cb)`.

    Z2 contract (post-tuple): firmware delivers a single packed SMALL_INT
    via mp_sched_schedule (mod_lorawan.c mac_post_event). Layout:
        bits 0..7   = tag_id  (uint8 — matches TAG_* constants above)
        bits 8..31  = status  (int24, sign-extended by Python `>>`)
    The closure is zero-alloc per event: one int unpack, one ticks_ms()
    read (SMALL_INT on this port), one array.array slot write.

    Unknown tag_ids (e.g. 255 for a firmware-side qstr the helper LUT
    doesn't know about yet) are stored verbatim — readers should treat
    any tag_id >= len(_ID_TO_TAG) as TAG_UNKNOWN.
    """
    if not isinstance(ring, Ring):
        raise TypeError("make_ev_cb expects a Ring from bounded_ring()")

    # Hoist references into local closure scope so the per-event call
    # never walks module globals (small but real overhead avoidance).
    _push = ring.push
    _now  = time.ticks_ms

    def _cb(packed):
        # packed = (status << 8) | tag_id  (see firmware mac_post_event).
        # Python int >> is arithmetic, so the status sign is preserved
        # automatically — LoRaMac error codes that come back negative
        # (signed enum values) round-trip correctly.
        tag_id = packed & 0xFF
        status = packed >> 8
        _push(_now(), tag_id, status)

    return _cb


# -- JoinResult / join_otaa -------------------------------------------------

class JoinResult:
    __slots__ = ('joined', 'attempts_used', 't_join_ms', 'last_dr')

    def __init__(self, joined, attempts_used, t_join_ms, last_dr):
        self.joined = joined
        self.attempts_used = attempts_used
        self.t_join_ms = t_join_ms
        self.last_dr = last_dr

    def __repr__(self):
        return ("JoinResult(joined=%r, attempts_used=%d, t_join_ms=%d, "
                "last_dr=%r)") % (self.joined, self.attempts_used,
                                  self.t_join_ms, self.last_dr)


def join_otaa(mac, eui, joineui, appkey,
              attempts=5, min_rx=0, join_dr=0, timeout_ms=30000):
    if len(eui) != 8:
        raise ValueError("eui must be 8 bytes")
    if len(joineui) != 8:
        raise ValueError("joineui must be 8 bytes")
    if len(appkey) != 16:
        raise ValueError("appkey must be 16 bytes")
    if not isinstance(attempts, int) or attempts <= 0:
        raise ValueError("attempts must be positive int")

    mac.set_keys(eui, joineui, appkey)

    # Stage A clean-port: MinRxSymbols setter removed from default API
    # (gated under LORAWAN_DEBUG_TIMING_TUNABLES=0). The `min_rx` argument
    # to this helper is preserved for compatibility but is now a no-op;
    # the firmware uses the pristine upstream default (MinRxSymbols=6).
    if min_rx != 0:
        if min_rx < 6 or min_rx > 100:
            raise ValueError("min_rx must be 0 or in 6..100")

    started_at = time.ticks_ms()
    joined_at = -1
    attempts_used = 0
    success = False

    for attempt in range(1, attempts + 1):
        attempts_used = attempt
        mac.join(join_dr)
        deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            mac.process()
            if mac.is_joined():
                joined_at = time.ticks_ms()
                success = True
                break
            time.sleep_ms(20)
        if success:
            break
        if attempt < attempts:
            # ~2 s pause between attempts — within the "1..5 s, fixed
            # or exp, both fine" sketch contract. Constant keeps logs
            # predictable across runs.
            time.sleep_ms(2000)

    if success:
        t_join_ms = time.ticks_diff(joined_at, started_at)
    else:
        t_join_ms = -1

    try:
        last_dr = mac.get_datarate()
    except Exception:
        last_dr = None

    return JoinResult(success, attempts_used, t_join_ms, last_dr)


# -- VERDICT printer --------------------------------------------------------

def _is_valid_name(name):
    if not isinstance(name, str) or not name:
        return False
    for ch in name:
        if ch not in _VALID_NAME_CHARS:
            return False
    return True


def _serialize_metric(value):
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, int):
        return "%d" % value
    if isinstance(value, float):
        return "%g" % value
    if isinstance(value, str):
        if (' ' in value) or ('=' in value) or ('\t' in value) or ("'" in value):
            # Strip embedded quotes to keep the line parser-friendly.
            safe = value.replace("'", "")
            return "'%s'" % safe
        return value
    raise TypeError("verdict metric must be int/float/bool/str scalar, "
                    "got %s" % type(value).__name__)


def verdict(name, passed, **metrics):
    """Emit a single VERDICT: line. Allocates heavily (str format, dict
    iter) but is called ONCE at test end — not on a hot path."""
    if not _is_valid_name(name):
        raise ValueError("verdict name must match [A-Za-z0-9_-]+")
    status = "PASS" if passed else "FAIL"
    parts = ["VERDICT:", status, name]
    for k, v in metrics.items():
        if not isinstance(k, str) or not k:
            raise TypeError("verdict metric key must be non-empty str")
        if k.startswith('_'):
            # Reserved namespace; silently dropped per sketch §2.4.
            continue
        parts.append("%s=%s" % (k, _serialize_metric(v)))
    sys.stdout.write(" ".join(parts) + "\n")


# -- Smoke probe ------------------------------------------------------------
# `mpremote run _lorawan_test_helpers.py` sanity-checks the helpers
# without touching the lorawan module or any peripheral.

if __name__ == "__main__":
    # Helper that mirrors firmware's pack format for the smoke probe.
    def _pack(tag_id, status):
        return (status << 8) | (tag_id & 0xFF)

    r = bounded_ring(4)
    assert len(r) == 0
    cb = make_ev_cb(r)
    t0 = time.ticks_ms()
    cb(_pack(TAG_MCPS_CONFIRM,    0))
    cb(_pack(TAG_MCPS_INDICATION, 0))
    cb(_pack(TAG_MLME_CONFIRM,    0))
    assert len(r) == 3
    # Force a wrap: two more pushes evict the first record.
    cb(_pack(TAG_MCPS_CONFIRM, 0))
    cb(_pack(TAG_MCPS_CONFIRM, 0))
    assert len(r) == 4

    # for_each_since (new path): count records since t0 via a counter cb.
    cnt = [0]
    def _count_cb(_t, _tid, _s, _c=cnt):
        _c[0] += 1
    r.for_each_since(t0, _count_cb)
    assert cnt[0] == 4

    # since() (legacy path): list of 3-tuples with str tags.
    rs = r.since(t0)
    assert len(rs) == 4
    # First entry should be the oldest still in the ring (after wrap).
    assert rs[0][1] == 'mcps_indication'  # ev #1 was evicted, ev #2 is oldest

    # latest filter via tag_id.
    last = r.latest(TAG_MCPS_INDICATION)
    assert last is not None and last[1] == TAG_MCPS_INDICATION

    # Negative status round-trips (sign-extend via Python int >>).
    cb(_pack(TAG_MAC_ERROR, -7))
    last_err = r.latest(TAG_MAC_ERROR)
    assert last_err is not None and last_err[2] == -7

    # Unknown tag_id (firmware emits 255 for an unmapped qstr) is stored
    # verbatim; readers compare against TAG_UNKNOWN.
    cb(_pack(TAG_UNKNOWN, 9))
    last_unk = r.latest(TAG_UNKNOWN)
    assert last_unk is not None and last_unk[1] == TAG_UNKNOWN
    assert last_unk[2] == 9

    r.clear()
    assert len(r) == 0
    verdict("helpers_smoke", True, n=4, note='ok')
