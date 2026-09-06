# sdr_single.py -- the WHOLE SDR receiver app in ONE file (single-file merge of
# si5351.py + sdr.py + sdr_app.py; behavior identical, 2026-08-11).
#
# Boot use: /flash/main.py ->  from sdr_single import start; start()
#
# Display: single DIRECT workflow. start() reuses the already-created DIRECT
# display (or brings one up via pRGB.RGB()), builds the UI, renders once, and
# starts the event loop LAST. lv.init() first -- allocating before it
# hard-faults at cold boot.
#
# Persistence: ALL params -> data flash block 0, debounced SAVE_DELAY_MS after
# the last change, restored+validated at boot.
import sys, lvgl as lv, lv_utils
import array, math, gc
from machine import I2C

if '/flash' not in sys.path:
    sys.path.append('/flash')

_KEEP = {}
_VERIFY_SCROLL_STEP = 31       # one complete VERIFY row per atomic transaction


# SDRangel File Input v1.  /flash/test.sdriq remains the backwards-compatible
# default used by the transport HIL; TESTER passes an explicit allow-listed path
# for the AM/USB/LSB/CW receiver bank.  The two 8-KiB buffers are allocated in
# start(), before
# the LVGL tree fragments the MicroPython heap, then rooted on SdrApp for its full
# lifetime.  The native IQADC player owns only borrowed buffer pointers; Python
# performs file I/O from a scheduled callback, never from the ADC/DSP interrupt.
_IQ_FILE_PATH = "/flash/test.sdriq"
_IQ_FILE_HEADER_BYTES = 32
_IQ_FILE_BUFFER_BYTES = 8192
_IQ_FILE_STATUS_WORDS = 15


def _u32le(b, i):
    return (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) |
            (b[i + 3] << 24))


def _u64le(b, i):
    return _u32le(b, i) | (_u32le(b, i + 4) << 32)


def _crc32_ieee(b, n):
    """Table-free reflected CRC-32/ISO-HDLC, identical to SDRangel's header CRC."""
    crc = 0xFFFFFFFF
    for i in range(n):
        crc ^= b[i]
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


class _IqFileSource:
    """Double-buffered, allocation-free steady-state SDRangel S16LE I/Q reader.

    Provisional native contract:
      file_attach(buf0, buf1, point, refill_cb)
      file_commit(index, valid_bytes)       # zero bytes is the ordered EOF marker
      file_start()/file_stop()/file_free()
      file_status_into(array('i', 15))

    status words are:
      attached, requested, active, point, state0, state1, valid0, valid1,
      active_index, active_offset, underruns, source_blocks, samples_consumed,
      refill_pending_mask, scheduler_failures.
    """

    def __init__(self, buf0, buf1, header, status):
        self._bufs = (buf0, buf1)
        self._header = header
        self.status = status
        self._iq = None
        self._file = None
        self._attached = False
        self._feeding = False
        self._loop = True
        self._payload_bytes = 0
        self._total_samples = 0
        self._consumed_last = 0
        self._consumed_base = 0
        self._consumed_total = 0
        self.sample_rate = 0
        self.sample_bits = 0
        self.center_hz = 0
        self.timestamp_ms = 0
        self.path = _IQ_FILE_PATH
        self.error = None
        self.eof = False
        self._file_free_state = 0
        # Cache one bound-method object.  file_attach roots it on the native side;
        # keeping it here also avoids manufacturing another wrapper on each start.
        self._refill_cb = self._refill

    def _close_file(self):
        f = self._file
        self._file = None
        if f is not None:
            try:
                f.close()
            except Exception:
                pass

    def stop(self):
        """Idempotently detach from IQADC and release only the file handle."""
        self._feeding = False
        iq = self._iq
        self._iq = None
        if iq is not None and self._attached:
            try:
                iq.file_stop()
            except Exception:
                pass
            try:
                iq.file_free()
            except Exception:
                pass
        self._attached = False
        self._close_file()

    def set_loop(self, enabled):
        if self._feeding:
            return False
        self._loop = bool(enabled)
        return True

    def _read_header(self, f, expected_rate):
        h = self._header
        f.seek(0)
        got = f.readinto(h)
        if got != _IQ_FILE_HEADER_BYTES:
            raise ValueError("SDRIQ header must be exactly 32 bytes")
        rate = _u32le(h, 0)
        centre = _u64le(h, 4)
        timestamp = _u64le(h, 12)
        bits = _u32le(h, 20)
        filler = _u32le(h, 24)
        stored_crc = _u32le(h, 28)
        actual_crc = _crc32_ieee(h, 28)
        # Publish parsed metadata even when a later validation rejects the file; the
        # FILE row can then show the actual rate/word size beside the explicit error.
        self.sample_rate = rate
        self.sample_bits = bits
        self.center_hz = centre
        self.timestamp_ms = timestamp
        if stored_crc != actual_crc:
            raise ValueError("SDRIQ header CRC mismatch")
        if filler != 0:
            raise ValueError("SDRIQ reserved word must be zero")
        if bits != 16:
            raise ValueError("SDRIQ %dBIT UNSUPPORTED" % bits)
        if rate != expected_rate:
            raise ValueError("SDRIQ RATE %d NEEDS RESAMPLE TO %d" %
                             (rate, expected_rate))
        f.seek(0, 2)
        total = f.tell()
        payload = total - _IQ_FILE_HEADER_BYTES
        if payload <= 0 or payload & 3:
            raise ValueError("SDRIQ payload must be nonempty interleaved I/Q")
        f.seek(_IQ_FILE_HEADER_BYTES)
        self._payload_bytes = payload
        self._total_samples = payload // 4

    def start(self, iq, point, expected_rate, loop, path=_IQ_FILE_PATH):
        """Validate, attach, synchronously prefill both FREE buffers, then start."""
        self.stop()
        self.error = None
        self._payload_bytes = 0
        self._total_samples = 0
        self._consumed_last = 0
        self._consumed_base = 0
        self._consumed_total = 0
        self.sample_rate = 0
        self.sample_bits = 0
        self.center_hz = 0
        self.timestamp_ms = 0
        self.path = path
        if iq is None:
            self.error = "RX is off"
            raise RuntimeError(self.error)
        for name in ("file_attach", "file_commit", "file_start", "file_stop",
                     "file_free", "file_status_into"):
            if getattr(iq, name, None) is None:
                self.error = "IQ file API unavailable"
                raise RuntimeError(self.error)
        if getattr(iq, "FILE_API_VERSION", 0) != 1:
            self.error = "IQ file API version mismatch"
            raise RuntimeError(self.error)
        states = (getattr(iq, "FILE_FREE", None),
                  getattr(iq, "FILE_READY", None),
                  getattr(iq, "FILE_ACTIVE", None))
        if states != (0, 1, 2):
            self.error = "IQ file API state contract mismatch"
            raise RuntimeError(self.error)
        self._file_free_state = states[0]
        f = None
        try:
            f = open(path, "rb")
            self._read_header(f, expected_rate)
            if loop and self._payload_bytes < len(self._bufs[0]):
                raise ValueError("SDRIQ LOOP NEEDS >=%d PAYLOAD BYTES" %
                                 len(self._bufs[0]))
            self._file = f
            f = None
            self._iq = iq
            self._loop = bool(loop)
            self.error = None
            self.eof = False
            self._feeding = True
            iq.file_attach(self._bufs[0], self._bufs[1], point, self._refill_cb)
            self._attached = True
            # file_attach also schedules the initial callbacks.  Fill now so start has
            # READY data; the later callbacks re-check state and simply no-op.
            self._refill(0)
            self._refill(1)
            iq.file_start()
        except Exception as e:
            self.error = str(e)
            if f is not None:
                try:
                    f.close()
                except Exception:
                    pass
            self.stop()
            raise

    def _refill(self, index):
        """Scheduled by C for a FREE buffer; no LVGL calls and no steady allocations."""
        if not self._feeding or not self._attached or self._file is None:
            return
        try:
            index = int(index)
            if index < 0 or index > 1:
                return
            iq = self._iq
            iq.file_status_into(self.status)
            if self.status[4 + index] != self._file_free_state:
                return
            n = self._file.readinto(self._bufs[index])
            if not n and self._loop:
                self._file.seek(_IQ_FILE_HEADER_BYTES)
                n = self._file.readinto(self._bufs[index])
            if not n:
                # Ordered behind any already READY/ACTIVE payload, so ONCE mode does
                # not truncate its last partial buffer.  C stops cleanly at this marker.
                self.eof = True
                self._feeding = False
                iq.file_commit(index, 0)
                return
            if n & 3:
                raise ValueError("SDRIQ short read is not a complete I/Q sample")
            iq.file_commit(index, n)
        except Exception as e:
            self.error = "SDRIQ refill: %r" % (e,)
            self.eof = True
            self._feeding = False
            # The native trampoline catches this exception, withdraws the failing
            # callback and puts FILE into fail-closed zero-I/Q hold.  A synchronous
            # prefill reaches start(), whose cleanup explicitly stops and detaches the
            # borrowed pointers.
            raise

    def poll(self):
        if not self._attached or self._iq is None:
            return None
        try:
            self._iq.file_status_into(self.status)
            raw = self.status[12]
            if raw < 0:
                raw += 0x100000000
            if raw < self._consumed_last:
                self._consumed_base += 0x100000000
            self._consumed_last = raw
            self._consumed_total = self._consumed_base + raw
            return self.status
        except Exception as e:
            self.error = "SDRIQ status: %r" % (e,)
            return None

    def percent(self):
        total = self._total_samples
        if total <= 0:
            return 0
        done = self._consumed_total
        if self._loop:
            done %= total
        elif done > total:
            done = total
        return (done * 100) // total

# ======================================================================
# Si5351A/MS5351M I2C triple clock generator -- programming scheme of the
# Etherkit library / Elektor SDR Shield 2.0 (fixed 800 MHz PLLA from a
# 25 MHz crystal, fractional MultiSynth per output).
# CLK0 = VFO connector A, CLK2 = connector B, CLK1 = receiver LO.
# ======================================================================

ADDRS = (0x60, 0x6F)   # 0x60 = standard Si5351A; 0x6F = some MS5351M clones
ADDR = 0x60            # replaced at runtime by the detected address
XTAL = 25_000_000
PLL = 800_000_000          # PLLA fixed: 25 MHz x 32
_C = 1048575               # max fractional denominator (20 bit)
MS_OUT_MIN = 500_000       # this driver does not program the R divider
MS_OUT_MAX = 200_000_000   # Si5351 MS0..2 ceiling; >100 MHz uses PLLB planning
MS_DIV_MIN = 8.0
MS_DIV_MAX = 2048.0
MS_PLL_MIN = 600_000_000
MS_PLL_MAX = 900_000_000
# crystal calibration: measured +500 Hz at 28.160790 MHz => chip runs fast by
# +17.76 ppm; compensate by programming a proportionally lower frequency.
XTAL_PPM = 17.76


class SI5351:
    def __init__(self, i2c=None, i2c_id=1):   # VK_RA6M3: I2C(1) = P205/P206 (Arduino SDA/SCL)
        self.i2c = i2c or I2C(i2c_id)
        self.addr = None
        self.ok = False
        # A high-frequency output owns PLLB. PLLA remains fixed at 800 MHz so
        # ordinary <=100-MHz outputs keep their historical plan.
        self._pllb_clk = None
        self.probe()

    def probe(self):
        """Silent re-detect (hot-plug friendly). True if the chip is usable."""
        if not self.ok:
            try:
                devs = self.i2c.scan()
                for a in ADDRS:
                    if a in devs:
                        global ADDR
                        ADDR = a
                        self.addr = a
                        self.ok = True
                        self._init_chip()
                        break
            except Exception:
                self.ok = False
        return self.ok

    def _w(self, reg, val):
        self.i2c.writeto_mem(ADDR, reg, bytes([val & 0xFF]))

    def _burst(self, reg, data):
        self.i2c.writeto_mem(ADDR, reg, bytes(data))

    def _ms_params(self, a, b, c):
        p1 = 128 * a + (128 * b) // c - 512
        p2 = 128 * b - c * ((128 * b) // c)
        p3 = c
        return [(p3 >> 8) & 0xFF, p3 & 0xFF,
                (p1 >> 16) & 0x03, (p1 >> 8) & 0xFF, p1 & 0xFF,
                ((p3 >> 12) & 0xF0) | ((p2 >> 16) & 0x0F),
                (p2 >> 8) & 0xFF, p2 & 0xFF]

    def _init_chip(self):
        self._w(3, 0xFF)                       # all outputs off
        for r in (16, 17, 18):
            self._w(r, 0x80)                   # power down clocks
        self._w(183, 0x92)                     # xtal load 8 pF (Shield default)
        # PLLA = XTAL * (32 + 0/1) = 800 MHz
        a = PLL // XTAL
        self._burst(26, self._ms_params(a, 0, 1))
        self._w(177, 0xA0)                     # reset both PLLs

    def _pllb_params(self, pll_hz):
        """Fractional PLLB ratio corrected for the measured crystal error."""
        correction = 1.0 - XTAL_PPM * 1e-6
        if correction <= 0.0 or not MS_PLL_MIN <= pll_hz <= MS_PLL_MAX:
            return None
        ratio = (pll_hz * correction) / XTAL
        a = int(ratio)
        b = int((ratio - a) * _C + 0.5)
        if b >= _C:
            a += 1
            b = 0
        return self._ms_params(a, b, _C if b else 1)

    def set_freq(self, clk, hz):
        """Program one output from 500 kHz through the Si5351 200-MHz limit.

        PLLA stays at 800 MHz for the generic fractional <=100-MHz path. Above
        100 MHz the selected output owns PLLB: integer divide-by-6 covers up to
        150 MHz and documented DIVBY4 mode covers 150..200 MHz. Only one high
        output can own PLLB; selecting another disables the old owner.
        """
        try:
            clk = int(clk)
            hz = int(hz)
        except Exception:
            return False
        if (not self.ok or clk not in (0, 1, 2) or
                not MS_OUT_MIN <= hz <= MS_OUT_MAX):
            return False
        en = self.i2c.readfrom_mem(ADDR, 3, 1)[0]
        self._w(3, en | (1 << clk))             # output off while re-planning

        if hz <= 100_000_000:
            correction = 1.0 - XTAL_PPM * 1e-6
            if correction <= 0.0:
                return False
            div = PLL / (hz * correction)
            if not MS_DIV_MIN <= div <= MS_DIV_MAX:
                return False
            a = int(div)
            b = int((div - a) * _C)
            self._burst(42 + 8 * clk, self._ms_params(a, b, _C))
            self._w(16 + clk, 0x0F)             # fractional MS, PLLA, 8 mA
            if self._pllb_clk == clk:
                self._pllb_clk = None
        else:
            div = 6 if hz <= 150_000_000 else 4
            pll_params = self._pllb_params(hz * div)
            if pll_params is None:
                return False
            if self._pllb_clk is not None and self._pllb_clk != clk:
                old_en = self.i2c.readfrom_mem(ADDR, 3, 1)[0]
                self._w(3, old_en | (1 << self._pllb_clk))
            self._burst(34, pll_params)          # PLLB feedback MultiSynth
            ms = self._ms_params(div, 0, 1)
            if div == 4:
                ms[2] |= 0x0C                    # MSx_DIVBY4 = 11b
            self._burst(42 + 8 * clk, ms)
            self._w(16 + clk, 0x6F)             # integer MS, PLLB, 8 mA
            self._w(177, 0x80)                   # reset PLLB only
            self._pllb_clk = clk

        en = self.i2c.readfrom_mem(ADDR, 3, 1)[0]
        self._w(3, en & ~(1 << clk))           # enable output (active low)
        return True

    def disable(self, clk):
        if self.ok:
            en = self.i2c.readfrom_mem(ADDR, 3, 1)[0]
            self._w(3, en | (1 << clk))
            if self._pllb_clk == clk:
                self._pllb_clk = None


# ======================================================================
# UI -- panel-native restyle of the Figma design (frames 1012:4 + 1012:101,
# file ObSzNwlvZ4SLk6LnOLZ5NH) for the 4.3" 480x272 @ ~128 DPI TN panel.
#
# Design rules for this medium (won on hardware, 2026-08-09):
#   - type scale 12/14/16/20/36, hierarchy by SIZE, nothing below 12px
#     (below 12 glyph stems are 1px and AA splits them -> unreadable)
#   - contrast first: white/cyan on dark; gray only for secondary/inactive
#   - 2px borders, radius 6, big tap targets (>=34px)
#   - fewer elements: decorative chrome dropped; every element is live
# ======================================================================

# -- WHITE THEME palette (Figma frames 1015:2 / 1012:4 v2). NOTE: constant
# NAMES are kept from the dark theme so the app's color logic works unchanged;
# the MEANINGS are remapped: WHITE = ink text on light bg, DARK_TXT = text on
# green/active, CYAN_* = dark-cyan accent readable on white.
BG_RX      = 0xF3F4F6
BG_IN      = 0xF3F4F6
PANEL      = 0xFFFFFF
PANEL2     = 0xEDEDED
BTN_IN     = 0xFFFFFF
BTN_RX     = 0xFFFFFF
BORDER     = 0xE5E7EB
CYAN_RX    = 0x0097A7
CYAN_IN    = 0x0097A7
GRAY       = 0x6B7280
GRAY2      = 0x8C8C8C
GREEN      = 0x43A047
WHITE      = 0x111827   # ink
DARK_TXT   = 0xFFFFFF   # on-green
BIN        = 0xB6BDC6   # spectrum bins on white card
VFO_TRACK  = 0xEBEBEB
VFO_INK    = 0x262626
TEAL       = 0x33A68C

# spectrum demo bins; the app shifts this pattern when the frequency changes
SPEC_HEIGHTS = (6, 12, 8, 4, 16, 28, 24, 12, 8, 20, 38, 14, 8, 12, 24, 34,
                12, 6, 20, 10, 4, 14, 26, 16, 8, 12, 4)
NATIVE_GRAPH_HEADER_H = 18   # shared with LCD_WATERFALL_TOP_PAD in machine_lcd.c

_FONTS = {}
def font(size):
    """Nearest compiled-in montserrat, floor 12 (see design rules above)."""
    if size < 12:
        size = 12
    if size in _FONTS:
        return _FONTS[size]
    best, bestd = None, 99
    for s in (12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38):
        f = getattr(lv, "font_montserrat_%d" % s, None)
        if f is None:
            continue
        d = abs(s - size) + (0 if s <= size else 0.5)   # prefer rounding down
        if d < bestd:
            best, bestd = f, d
    _FONTS[size] = best or lv.font_default()
    return _FONTS[size]


def _base(o):
    o.remove_flag(lv.obj.FLAG.SCROLLABLE)
    o.set_style_pad_all(0, 0)
    o.set_style_border_width(0, 0)
    o.set_style_radius(0, 0)
    o.set_style_bg_opa(lv.OPA.TRANSP, 0)
    return o


def _box(parent, w, h, bg=None, border=None, radius=0, bw=2):
    o = _base(lv.obj(parent))
    o.set_size(w, h)
    if bg is not None:
        o.set_style_bg_color(lv.color_hex(bg), 0)
        o.set_style_bg_opa(lv.OPA.COVER, 0)
    if border is not None:
        o.set_style_border_color(lv.color_hex(border), 0)
        o.set_style_border_width(bw, 0)
        o.set_style_border_opa(lv.OPA.COVER, 0)
    o.set_style_radius(radius, 0)
    return o


def _lbl(parent, text, size, color):
    l = lv.label(parent)
    l.set_text(text)
    l.set_style_text_font(font(size), 0)
    l.set_style_text_color(lv.color_hex(color), 0)
    l.set_style_pad_all(0, 0)
    return l


def _btn(parent, w, h, bg, radius=6, border=None, bw=2):
    b = lv.button(parent)
    b.set_size(w, h)
    b.set_style_bg_color(lv.color_hex(bg), 0)
    b.set_style_radius(radius, 0)
    b.set_style_shadow_width(0, 0)
    if border is not None:
        b.set_style_border_color(lv.color_hex(border), 0)
        b.set_style_border_width(bw, 0)
    else:
        b.set_style_border_width(0, 0)
    b.set_flex_flow(lv.FLEX_FLOW.ROW)
    b.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
    return b


def _flex(o, flow, main=lv.FLEX_ALIGN.START, cross=lv.FLEX_ALIGN.CENTER,
          track=lv.FLEX_ALIGN.CENTER, gap=0):
    o.set_flex_flow(flow)
    o.set_flex_align(main, cross, track)
    o.set_style_pad_column(gap, 0)
    o.set_style_pad_row(gap, 0)
    return o


# ----------------------------------------------------------------------
# Shared LVGL styles for the RAM-heavy VERIFY screen. In LVGL v9 every inline
# set_style_*(..., 0) grows that object's per-object local-style array on the
# heap; 88 objects x several inline props each is thousands of tiny allocations
# (tens of KB + fragmentation). One shared lv.style_t applied with add_style()
# costs a single pointer per object instead. The style_t objects are created
# ONCE, kept alive here across screen delete/rebuild, and reused every open.
# Only the truly per-object dynamic colours (ON/OFF green, scope cyan, tap
# green) remain as inline set_style_* calls.
_SET_STYLES = {}


def _verify_styles():
    if _SET_STYLES:
        return _SET_STYLES
    s = _SET_STYLES

    # Row card wrapper (shared across the transient VERIFY rows).
    row = lv.style_t()
    row.init()
    row.set_bg_color(lv.color_hex(PANEL2))
    row.set_bg_opa(lv.OPA.COVER)
    row.set_border_width(0)
    row.set_radius(4)
    row.set_pad_all(2)
    row.set_pad_column(4)
    row.set_flex_flow(lv.FLEX_FLOW.ROW)
    # A flex flow stored in a shared style does not enable the layout engine by
    # itself (lv_obj_set_flex_flow() normally sets both properties).  Without
    # this, every child in a VERIFY row sits at the same origin and the last
    # SCOPE chip covers BLOCK / ON / TAP.
    row.set_layout(lv.LAYOUT.FLEX)
    row.set_flex_main_place(lv.FLEX_ALIGN.START)
    row.set_flex_cross_place(lv.FLEX_ALIGN.CENTER)
    row.set_flex_track_place(lv.FLEX_ALIGN.CENTER)
    s["row"] = row

    # Chip base; applied to a CLICKABLE label so each chip remains one object.
    # Applied to a CLICKABLE lv.label so a chip is ONE object, not button+label.
    chip = lv.style_t()
    chip.init()
    chip.set_bg_color(lv.color_hex(PANEL2))
    chip.set_bg_opa(lv.OPA.COVER)
    chip.set_radius(6)
    chip.set_border_width(0)
    chip.set_shadow_width(0)
    chip.set_pad_top(4)
    chip.set_pad_bottom(4)
    chip.set_pad_left(0)
    chip.set_pad_right(0)
    chip.set_text_font(font(12))
    chip.set_text_align(lv.TEXT_ALIGN.CENTER)
    s["chip"] = chip

    # Name label (font 14, ink) and dim header label (font 12, gray).
    name = lv.style_t()
    name.init()
    name.set_text_font(font(14))
    name.set_text_color(lv.color_hex(WHITE))
    s["name"] = name

    dim = lv.style_t()
    dim.init()
    dim.set_text_font(font(12))
    dim.set_text_color(lv.color_hex(GRAY))
    s["dim"] = dim

    # Transparent right-aligned inline group (was _grp: opa 0, border 0, pad 0).
    grp = lv.style_t()
    grp.init()
    grp.set_bg_opa(lv.OPA.TRANSP)
    grp.set_border_width(0)
    grp.set_pad_all(0)
    grp.set_pad_column(4)
    grp.set_flex_flow(lv.FLEX_FLOW.ROW)
    grp.set_layout(lv.LAYOUT.FLEX)
    s["grp"] = grp

    return s


class SdrUi:

    def __init__(self):
        self.w = {}          # name -> widget
        self._build_receiver()
        self._build_freq_input()

    def get(self, name):
        return self.w[name]

    # ---------------- screen 1: sdr-receiver ----------------
    def _build_receiver(self):
        scr = _base(lv.obj(None))
        scr.set_style_bg_color(lv.color_hex(BG_RX), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        scr.set_style_pad_all(8, 0)
        _flex(scr, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 8)
        self.w["scr-receiver"] = scr

        col = _base(lv.obj(scr))
        col.set_size(400, 256)
        _flex(col, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 6)
        self.w["main-column"] = col

        # --- brand line: identity (left) + SDR backend chrome (right) ---
        # 24 px instead of 16: the RX button, the RX dot and the BLK/OVR/UND/
        # CLIP counters live here because this is the only row in the column
        # with spare width, and the column has just 10 px of vertical slack.
        # Both groups are non-clickable so taps still fall through to the row
        # (which opens the VFO routing dialog); the RX button eats its own.
        top = _base(lv.obj(col))
        top.set_size(400, 24)
        _flex(top, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN, gap=6)
        self.w["brand-row"] = top     # tap -> VFO->hardware routing dialog
        bl = _flex(_base(lv.obj(top)), lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START,
                   lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 6)
        bl.set_size(156, 24)
        bl.remove_flag(lv.obj.FLAG.CLICKABLE)
        self.w["brand-dot"] = _box(bl, 8, 8, bg=TEAL, radius=4, bw=0)
        _lbl(bl, "SDR RECEIVER", 12, GRAY2)
        # VFO indicator is its own tap target: a clickable child consumes the tap
        # (opening the VFO->hardware routing + CAL popup) before it bubbles to the
        # brand row, so tapping the label routes, tapping the row still opens SETTINGS.
        vi = _lbl(bl, "VFO A", 12, CYAN_RX)
        vi.add_flag(lv.obj.FLAG.CLICKABLE)
        self.w["vfo-indicator"] = vi

        br = _flex(_base(lv.obj(top)), lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.END,
                   lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 6)
        br.set_size(238, 24)
        br.remove_flag(lv.obj.FLAG.CLICKABLE)
        # SDR status mini row: block counter + the three fault flags. The flags
        # are always present (stable layout) and only change colour.
        self.w["sdr-blk"] = _lbl(br, "BLK ----", 12, GRAY2)
        for nm, txt in (("sdr-ovr", "OVR"), ("sdr-und", "UND"), ("sdr-clip", "CLIP")):
            self.w[nm] = _lbl(br, txt, 12, BORDER)
        self.w["rx-dot"] = _box(br, 8, 8, bg=GRAY2, radius=4, bw=0)
        rxb = _btn(br, 46, 22, PANEL2, radius=6, border=BORDER)
        _lbl(rxb, "RX", 12, WHITE)
        self.w["rx-button"] = rxb

        # --- frequency display: the hero ---
        fd = _box(col, 400, 70, bg=PANEL, border=BORDER, radius=6)
        fd.set_style_pad_hor(12, 0)
        fd.set_style_pad_ver(4, 0)
        _flex(fd, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 0)
        fd.add_flag(lv.obj.FLAG.CLICKABLE)
        self.w["frequency-display"] = fd
        row1 = _base(lv.obj(fd))
        row1.set_size(374, 40)
        _flex(row1, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN,
              lv.FLEX_ALIGN.END, lv.FLEX_ALIGN.END)
        fdig = _lbl(row1, "14 205 000", 36, WHITE)   # ink digits (white theme)
        fdig.add_flag(lv.obj.FLAG.CLICKABLE)         # only BIG digits open entry
        self.w["freq-digits"] = fdig
        mhz = _lbl(row1, "MHz", 14, GRAY)
        mhz.set_style_pad_bottom(5, 0)
        row2 = _base(lv.obj(fd))
        row2.set_size(374, 18)
        _flex(row2, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN)
        band = _flex(_base(lv.obj(row2)), lv.FLEX_FLOW.ROW, gap=6)
        band.set_size(185, 18)
        _lbl(band, "BAND", 12, GRAY2)
        self.w["band-value"] = _lbl(band, "GEN", 14, WHITE)
        _lbl(band, "FILTER", 12, GRAY2)
        self.w["filter-value"] = _lbl(band, "12k", 14, CYAN_RX)
        filt = _flex(_base(lv.obj(row2)), lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.END, gap=10)
        filt.set_size(195, 18)
        for k in (0, 1):    # tap a small (parked) VFO -> swap it with the big one
            l = _lbl(filt, "-", 12, GRAY)
            l.add_flag(lv.obj.FLAG.CLICKABLE)
            self.w["vfo-alt-%d" % k] = l
        for o in (row1, row2, band, filt):
            o.remove_flag(lv.obj.FLAG.CLICKABLE)

        # --- spectrum: bins + live range labels only ---
        sp = _box(col, 400, 102, bg=PANEL, border=BORDER, radius=6)
        sp.set_style_pad_all(4, 0)
        self.w["spectrum-area"] = sp
        wf = _base(lv.obj(sp))
        wf.set_size(388, 92)
        wf.align(lv.ALIGN.BOTTOM_MID, 0, 0)
        _flex(wf, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.END,
              lv.FLEX_ALIGN.END, 4)
        self.w["spectrum-waterfall"] = wf
        self.bins = []
        for i, hh in enumerate(SPEC_HEIGHTS):
            b = _box(wf, 10, hh, bg=(CYAN_RX if i == len(SPEC_HEIGHTS) // 2 else BIN), bw=0)
            b.set_flex_grow(1)
            b.remove_flag(lv.obj.FLAG.CLICKABLE)   # taps fall through to spectrum-area
            self.bins.append(b)
            self.w["spectral-bin-%d" % i] = b
        wf.remove_flag(lv.obj.FLAG.CLICKABLE)
        for name, txt, al, ox in (("spec-lo", "14.200", lv.ALIGN.TOP_LEFT, 6),
                                  ("spec-hi", "14.210", lv.ALIGN.TOP_LEFT, 210)):
            l = _lbl(sp, txt, 12, GRAY)
            l.add_flag(lv.obj.FLAG.FLOATING)
            l.align(al, ox, 2)
            self.w[name] = l
        # S-meter dBFS readout, top-centre of the spectrum card (live backend only)
        sm = _lbl(sp, "", 12, CYAN_RX)
        sm.add_flag(lv.obj.FLAG.FLOATING)
        sm.align(lv.ALIGN.TOP_LEFT, 92, 2)
        self.w["smeter-value"] = sm
        # Native right panel mode.  It lives in the 18-px label band so the direct
        # TIME/I-Q renderer below never overwrites it.
        svm = _lbl(sp, "TIME", 10, GRAY)
        svm.add_flag(lv.obj.FLAG.FLOATING)
        svm.align(lv.ALIGN.TOP_RIGHT, -4, 3)
        self.w["scope-view"] = svm

        # Exact former tuning control, now sharing the single bottom slot with
        # the mode bar.  Only one of the two rows is visible at a time.
        tr = _base(lv.obj(col))
        tr.set_size(400, 40)
        _flex(tr, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, gap=6)
        def tbtn(name, txt, wpx, color, size=16):
            b = _btn(tr, wpx, 40, PANEL2, radius=6, border=BORDER)
            _lbl(b, txt, size, color)
            self.w[name] = b
        tbtn("btn-step-down", "<<", 52, CYAN_RX)
        sd = _box(tr, 190, 40, bg=PANEL, border=BORDER, radius=6)
        _flex(sd, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.CENTER, gap=0)
        sd.set_flex_grow(1)
        self.w["home-summary"] = _lbl(sd, "AM | 1.8 kHz | 1 kHz", 10, WHITE)
        self.w["step-display"] = sd
        tbtn("btn-step-up", ">>", 52, CYAN_RX)
        tbtn("btn-fine-down", "-", 44, WHITE, 20)
        tbtn("btn-fine-up", "+", 44, WHITE, 20)
        tr.add_flag(lv.obj.FLAG.HIDDEN)
        self.w["tuning-row"] = tr

        # --- mode bar ---
        mb = _base(lv.obj(col))
        mb.set_size(400, 34)
        _flex(mb, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, gap=5)
        self.w["mode-bar"] = mb
        for m in ("AM", "FM", "USB", "LSB", "CW"):
            b = _btn(mb, 76, 34, BTN_RX, radius=6, border=BORDER)
            b.set_flex_grow(1)
            _lbl(b, m, 14, WHITE)
            self.w["btn-" + m] = b
        # Collapsed mode-bar actions.  All objects are created once; runtime only
        # toggles HIDDEN, avoiding widget construction and heap churn on a tap.
        for name, text in (("mode-filter", "FILTER"), ("mode-step", "STEP"),
                           ("mode-view", "SPEC")):
            b = _btn(mb, 76, 34, BTN_RX, radius=6, border=BORDER)
            b.set_flex_grow(1)
            _lbl(b, text, 12, WHITE)
            b.add_flag(lv.obj.FLAG.HIDDEN)      # update_mode() reveals them at boot
            self.w["btn-" + name] = b
        # --- right panel: VOL + AGC ---
        rp = _box(scr, 56, 256, bg=PANEL, border=BORDER, radius=6)
        rp.set_style_pad_ver(8, 0)
        _flex(rp, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
              lv.FLEX_ALIGN.CENTER, 10)
        self.w["right-side-panel"] = rp
        vh = _flex(_base(lv.obj(rp)), lv.FLEX_FLOW.COLUMN, gap=0)
        vh.set_size(48, 38)
        vh.add_flag(lv.obj.FLAG.CLICKABLE)   # tap the VOL header -> gains panel toggle
        self.w["vol-header"] = vh
        vl = _lbl(vh, "VOL", 14, GRAY2)
        vv = _lbl(vh, "72%", 16, CYAN_RX)
        # Labels cover almost the whole 48x38 header.  Make them explicit hit
        # targets so a tap on the ink cannot be swallowed before the parent
        # receives CLICKED (the slider below keeps its independent drag role).
        vl.add_flag(lv.obj.FLAG.CLICKABLE)
        vv.add_flag(lv.obj.FLAG.CLICKABLE)
        self.w["vol-label"] = vl
        self.w["vol-value"] = vv
        pin = lv.checkbox(vh)
        pin.set_text("")
        pin.set_size(22, 20)
        pin.add_flag(lv.obj.FLAG.HIDDEN)
        self.w["gain-pin"] = pin
        sl = lv.slider(rp)
        sl.set_size(16, 130)
        sl.set_range(0, 100)
        sl.set_value(72, False)
        sl.set_style_bg_color(lv.color_hex(PANEL2), lv.PART.MAIN)
        sl.set_style_bg_opa(lv.OPA.COVER, lv.PART.MAIN)
        sl.set_style_border_color(lv.color_hex(BORDER), lv.PART.MAIN)
        sl.set_style_border_width(2, lv.PART.MAIN)
        sl.set_style_radius(8, lv.PART.MAIN)
        sl.set_style_bg_color(lv.color_hex(CYAN_RX), lv.PART.INDICATOR)
        # Indicator radius MUST be >= MAIN radius. If it is smaller, LVGL's lv_bar sees a
        # "radius_issue" and renders the indicator through a temporary ARGB8888 draw layer
        # (lv_bar.c). That layer grows with the value; at 100% it is ~16*130*4 ~= 8 KB, and
        # allocating that contiguous block from the fragmented MP heap fails (MemoryError),
        # which used to freeze the whole UI right at MAX. Equal radii => no layer, no alloc.
        sl.set_style_radius(8, lv.PART.INDICATOR)
        sl.set_style_bg_opa(lv.OPA.TRANSP, lv.PART.KNOB)
        sl.set_style_pad_all(0, lv.PART.KNOB)
        self.w["vol-slider"] = sl
        ag = _flex(_base(lv.obj(rp)), lv.FLEX_FLOW.COLUMN, gap=2)
        ag.set_size(48, 46)
        _lbl(ag, "AGC", 14, GRAY2)
        pill = _btn(ag, 48, 24, GREEN, radius=6)
        self.w["agc-value"] = _lbl(pill, "FAST", 12, WHITE)
        self.w["agc-pill"] = pill

    # ---------------- screen 2: freq-input ----------------
    def _build_freq_input(self):
        scr = _base(lv.obj(None))
        scr.set_style_bg_color(lv.color_hex(BG_IN), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        scr.set_style_pad_all(8, 0)
        _flex(scr, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 6)
        self.w["scr-freq-input"] = scr

        # --- title bar ---
        tb = _base(lv.obj(scr))
        tb.set_size(464, 32)
        _flex(tb, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN)
        _box(tb, 16, 16, bg=TEAL, radius=3, bw=0)
        # VFO selector: letter + frequency per segment (like the receiver line)
        vs = _box(tb, 384, 32, bg=VFO_TRACK, radius=8, bw=0)
        _flex(vs, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.CENTER, gap=2)
        vs.set_style_pad_all(3, 0)
        for tag in ("a", "b", "c"):
            b = _btn(vs, 124, 26, PANEL, radius=4)
            _lbl(b, tag.upper() + " --", 12, VFO_INK)
            self.w["vfo-" + tag] = b
        cb = _btn(tb, 40, 26, PANEL2, radius=6)
        _lbl(cb, lv.SYMBOL.CLOSE, 14, GRAY)
        self.w["close-btn"] = cb

        # --- input field ---
        ic = _box(scr, 464, 48, bg=BG_RX, border=CYAN_IN, radius=6, bw=2)
        ic.set_style_pad_hor(12, 0)
        _flex(ic, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN)
        self.w["input-container"] = ic
        left = _flex(_base(lv.obj(ic)), lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START,
                     lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 3)
        left.set_width(300)
        left.set_height(lv.SIZE_CONTENT)   # let flex center digits+cursor by Y
        self.w["input-digits"] = _lbl(left, "14.205.000", 26, CYAN_IN)
        self.w["blinking-cursor"] = _box(left, 3, 26, bg=CYAN_IN, bw=0)
        _lbl(ic, "MHz", 14, GRAY2)

        # --- band presets ---
        bp = _base(lv.obj(scr))
        bp.set_size(464, 26)
        _flex(bp, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, gap=5)
        for b_name in ("80m", "40m", "20m", "15m", "10m"):
            b = _btn(bp, 62, 26, BTN_IN, radius=4)
            b.set_flex_grow(1)
            _lbl(b, b_name, 12, WHITE)
            self.w["band-" + b_name] = b

        # --- keypad + actions ---
        grid = _base(lv.obj(scr))
        grid.set_size(464, 116)
        _flex(grid, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 6)
        kp = _base(lv.obj(grid))
        kp.set_size(374, 116)
        _flex(kp, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 4)
        for row in (("1", "2", "3"), ("4", "5", "6"), ("7", "8", "9"), (".", "0", "BS")):
            rr = _base(lv.obj(kp))
            rr.set_size(374, 26)
            _flex(rr, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, gap=4)
            for key in row:
                b = _btn(rr, 122, 26, BTN_IN, radius=4)
                b.set_flex_grow(1)
                _lbl(b, lv.SYMBOL.BACKSPACE if key == "BS" else key, 16, WHITE)
                self.w["key-" + key] = b
        ac = _base(lv.obj(grid))
        ac.set_size(84, 116)
        _flex(ac, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 4)
        cancel = _btn(ac, 84, 56, BORDER, radius=6)
        _lbl(cancel, "CANCEL", 12, WHITE)
        self.w["cancel-button"] = cancel
        ok = _btn(ac, 84, 56, GREEN, radius=6)
        _lbl(ok, "OK", 16, DARK_TXT)   # dark on bright green (contrast rule)
        self.w["ok-button"] = ok

        # --- step selector ---
        sr = _base(lv.obj(scr))
        sr.set_size(464, 24)
        _flex(sr, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, gap=5)
        _lbl(sr, "STEP", 12, GRAY2)
        for s_name in ("1 Hz", "10 Hz", "100 Hz", "1 kHz", "10 kHz"):
            b = _btn(sr, 10, 22, BTN_IN, radius=4)
            b.set_style_pad_hor(10, 0)
            b.set_width(lv.SIZE_CONTENT)
            _lbl(b, s_name, 12, WHITE)
            self.w["step-" + s_name] = b


def build():
    return SdrUi()


# ======================================================================
# App: behavior + persistence.
#
# Update discipline (measured on RA6M3, DIRECT + Dave2D: full apply_all ~39 ms
# vs targeted ~11.5 ms): hot paths call TARGETED updates only -- update_freq /
# update_mode / update_step / update_vol / update_agc. apply_all() is the one-time
# full sync used while constructing SdrApp.
# ======================================================================

MAGIC = b"SDR1"
SAVE_DELAY_MS = 60000           # 1 min after last change
# v3 record = 3 VFOs + SDR backend state; measures ~248 B, so it still lands
# in the same 4 blocks the v2 record used. save_params() erases only the blocks
# it writes, so the 512 B ceiling is a reserve, not an allocation. Growing the
# record is forward-safe (the header still lives at offset 0) but NOT
# backward-safe: a pre-v3 build rejects a >200 B payload and uses its defaults.
DF_BLOCK = 64                   # data-flash erase granularity
DF_LIMIT = 512                  # v3 ceiling (8 blocks)
DF_HEADER_BYTES = 6             # four-byte magic + uint16 payload length
DF_PAYLOAD_LIMIT = DF_LIMIT - DF_HEADER_BYTES
AGC_TARGET_MIN = 0.01
AGC_TARGET_MAX = 1.0
CAL_PPM_MIN = -200.0
CAL_PPM_MAX = 200.0

BANDS = (   # name, label, lo Hz, hi Hz, entry-base Hz
    ("80m",  "80 Meter", 3_500_000,   4_000_000,   3_500_000),
    ("40m",  "40 Meter", 7_000_000,   7_300_000,   7_000_000),
    ("20m",  "20 Meter", 14_000_000,  14_350_000,  14_000_000),
    ("15m",  "15 Meter", 21_000_000,  21_450_000,  21_000_000),
    ("10m",  "10 Meter", 28_000_000,  29_700_000,  28_000_000),
)
MODES = ("AM", "FM", "USB", "LSB", "CW")
# RA6M3 backend demod names.
MODE_DEMOD = {"AM": "am", "FM": "fm", "USB": "usb", "LSB": "lsb", "CW": "cw"}
MODE_BW = {"AM": 6000, "FM": 6000, "USB": 2400, "LSB": 2400, "CW": 500}
# selectable IF bandwidths per mode (the P1 filter menu picks from these; P0
# only stores and displays the per-mode default)
BW_CHOICES = {"AM":  (3000, 4000, 6000, 9000),
              "FM":  (4000, 5000, 6000, 9000),
              "USB": (1800, 2100, 2400, 3000),
              "LSB": (1800, 2100, 2400, 3000),
              "CW":  (250, 500, 1000)}
AGC_MODES = ("OFF", "FAST", "SLOW", "MAN")
# UI mode label -> firmware agc() mode string. Module constant so set_agc never
# allocates a dict literal per call.
_AGC_MODE_MAP = {"OFF": "off", "FAST": "fast", "SLOW": "slow",
                 "MAN": "manual", "MANUAL": "manual"}


def tester_peak_counts(ampl, depth_pct):
    """Worst positive TESTER peak, matching the C Q15 operation order."""
    a = int(ampl)
    depth_q15 = (int(depth_pct) * 32768 + 50) // 100
    env_q15 = 32768 + ((depth_q15 * 32767) >> 15)
    sample_a = (a * env_q15) >> 15
    return (sample_a * 32767) >> 15


def tester_safe_amplitude(hard_max, depth_pct):
    """Largest requested amplitude whose modeled 12-bit peak stays <= 2047."""
    lo = 0
    hi = int(hard_max)
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if tester_peak_counts(mid, depth_pct) <= 2047:
            lo = mid
        else:
            hi = mid - 1
    return lo


def fmt_bw(hz):
    """Filter label in the panel's shorthand: 500 -> '500', 2400 -> '2.4k'."""
    if hz < 1000:
        return "%d" % hz
    if hz % 1000 == 0:
        return "%dk" % (hz // 1000)
    return "%.1fk" % (hz / 1000.0)


def fmt_count(n):
    """Counter in <=6 glyphs so the status row never reflows the brand line."""
    if n is None:
        return "----"
    n = min(int(n), 99_999_999_999)     # clamp keeps the 6-glyph promise
    if n < 100_000:
        return "%d" % n
    if n < 100_000_000:
        return "%dk" % (n // 1000)
    return "%dM" % (n // 1_000_000)


STEPS = ((1, "1 Hz"), (10, "10 Hz"), (100, "100 Hz"),
         (1000, "1 kHz"), (10000, "10 kHz"))
F_MIN, F_MAX = 100_000, 40_000_000

DEFAULTS = {"f": 14_205_000, "m": "USB", "s": 1000, "v": 72, "a": "FAST"}
DEF_VFOS = [[14_205_000, "USB"], [7_100_000, "LSB"], [30_000_000, "FM"]]
# hardware targets a VFO can drive: (label, si5351 clk or None, multiplier).
# x4 = quadrature (Tayloe) LO running at 4x the tuned frequency.
# First five entries keep the indices of records saved by older builds.
TARGETS = (
    ("Si5351 CLK0",    0,    1),
    ("RX CLK1 x4",     1,    4),
    ("Si5351 CLK2",    2,    1),
    ("Si4825",         None, 1),
    ("Si4732",         None, 1),
    ("Si5351 CLK0 x4", 0,    4),
    ("Si5351 CLK1",    1,    1),
    ("Si5351 CLK2 x4", 2,    4),
)


# ---------------- data flash persistence ----------------

def _fresh_params():
    """Factory defaults, including every v4 SDR-backend field."""
    out = dict(DEFAULTS)
    out["vfos"] = [list(v) for v in DEF_VFOS]
    out["act"] = 0
    # A/B/C are station memories for one receiver.  All three therefore default
    # to the shield's real Tayloe LO input, CLK1 x4; CLK0/CLK2 remain optional AUX
    # routes selectable from ROUTE.
    out["rt"] = [1, 1, 1]
    out["cal"] = 17.76
    out["bw"] = dict(MODE_BW)     # per-mode IF bandwidth (filter_bandwidth)
    out["rxauto"] = 0             # rx_autostart: RX was on when last saved
    out["beon"] = 1               # backend_enabled: master IQADC/DAC switch
    out["again"] = 1.0            # agc_manual_gain
    out["atgt"] = 0.5             # agc_target
    out["iqe"] = 0                # saved I/Q correction enable
    out["iqa"] = 1.0              # saved Q amplitude multiplier
    out["iqp"] = 0.0              # saved I-to-Q phase coefficient (not degrees)
    return out


def load_params():
    import json, dataflash
    try:
        hdr = bytes(dataflash.read(0, 6))
        if hdr[:4] != MAGIC:
            raise ValueError("no record")
        n = hdr[4] | (hdr[5] << 8)
        if not 0 < n <= DF_PAYLOAD_LIMIT:
            raise ValueError("bad len")
        p = json.loads(bytes(dataflash.read(6, n)))
        out = _fresh_params()
        for k in DEFAULTS:
            if k in p:
                out[k] = p[k]
        out["f"] = min(max(int(out["f"]), F_MIN), F_MAX)
        if out["m"] not in MODES:
            out["m"] = DEFAULTS["m"]
        if out["s"] not in [s for s, _ in STEPS]:
            out["s"] = DEFAULTS["s"]
        out["v"] = min(max(int(out["v"]), 0), 100)
        if out["a"] not in AGC_MODES:
            out["a"] = "FAST"
        # v2: 3 VFOs [freq, mode] + active index; migrate v1 records gracefully
        vfos = p.get("V")
        act = p.get("A", 0)
        if not (isinstance(vfos, list) and len(vfos) == 3):
            vfos = [[out["f"], out["m"]]] * 3
            act = 0
        out["vfos"] = [[min(max(int(f), F_MIN), F_MAX),
                        m if m in MODES else "USB"] for f, m in vfos]
        out["act"] = act if act in (0, 1, 2) else 0
        out["f"], out["m"] = out["vfos"][out["act"]]
        rt = p.get("R")
        if not (isinstance(rt, list) and len(rt) == 3
                and all(isinstance(x, int) and 0 <= x < len(TARGETS) for x in rt)):
            rt = [1, 1, 1]          # A/B/C station memories -> receiver CLK1 x4
        out["rt"] = rt
        try:
            out["cal"] = min(max(float(p.get("C", 17.76)),
                                 CAL_PPM_MIN), CAL_PPM_MAX)
        except Exception:
            out["cal"] = 17.76
        # v3 SDR-backend fields; all optional, so v1/v2 records still boot
        bws = p.get("B")
        if isinstance(bws, list) and len(bws) == len(MODES):
            for i, m in enumerate(MODES):
                try:
                    v = int(bws[i])
                except Exception:
                    continue
                if v in BW_CHOICES[m]:
                    out["bw"][m] = v
        out["rxauto"] = 1 if p.get("X") else 0
        out["beon"] = 0 if p.get("E") == 0 else 1
        try:
            out["again"] = min(max(float(p.get("G", 1.0)), 0.0), 64.0)
        except Exception:
            out["again"] = 1.0
        try:
            out["atgt"] = min(max(float(p.get("T", 0.5)),
                                  AGC_TARGET_MIN), AGC_TARGET_MAX)
        except Exception:
            out["atgt"] = 0.5
        out["iqe"] = 1 if p.get("qe") else 0
        try:
            out["iqa"] = min(max(float(p.get("qa", 1.0)), 0.50), 1.50)
        except Exception:
            out["iqa"] = 1.0
        try:
            out["iqp"] = min(max(float(p.get("qp", 0.0)), -0.50), 0.50)
        except Exception:
            out["iqp"] = 0.0
        return out
    except Exception as e:
        print("SDR load_params:", repr(e))
        return _fresh_params()


def save_params(p):
    import json, dataflash
    vfos = [list(v) for v in p.get("vfos", DEF_VFOS)]
    act = p.get("act", 0)
    vfos[act] = [p["f"], p["m"]]          # active VFO tracks current state
    bw = p.get("bw", MODE_BW)
    rec_obj = {"V": vfos, "A": act, "R": p.get("rt", [0, 1, 2]),
               "C": p.get("cal", 17.76),
               "s": p["s"], "v": p["v"], "a": p["a"],
               "f": p["f"], "m": p["m"],  # f/m kept for v1 readers
               "B": [bw.get(m, MODE_BW[m]) for m in MODES],
               "X": 1 if p.get("rxauto") else 0,
               "E": 1 if p.get("beon", 1) else 0,
               "G": round(float(p.get("again", 1.0)), 3),
               "T": round(float(p.get("atgt", 0.5)), 3),
               "qe": 1 if p.get("iqe") else 0,
               "qa": round(float(p.get("iqa", 1.0)), 4),
               "qp": round(float(p.get("iqp", 0.0)), 4)}
    payload = json.dumps(rec_obj).encode()
    if len(payload) > DF_PAYLOAD_LIMIT:
        raise ValueError("payload %d B > %d B" %
                         (len(payload), DF_PAYLOAD_LIMIT))
    rec = MAGIC + bytes([len(payload) & 0xFF, len(payload) >> 8]) + payload
    if len(rec) % 4:
        rec += b"\xff" * (4 - len(rec) % 4)
    if len(rec) > DF_LIMIT:
        raise ValueError("record %d B > %d B" % (len(rec), DF_LIMIT))
    # erase only the blocks this record actually lands in. Erasing the whole
    # v3 reserve would destroy block 0 first and then fail on a board with
    # fewer blocks than we assumed -- losing the settings we were saving.
    for b in range((len(rec) + DF_BLOCK - 1) // DF_BLOCK):
        dataflash.erase_block(b)
    dataflash.write(0, rec)
    print("SDR saved:", payload)


# ======================================================================
# RA6M3 SDR backend shim.
#
# Python here is CONTROL/STATUS ONLY: it brings up machine.IQADC, hands the
# stream to machine.DAC and afterwards only writes settings and reads
# counters. It never pulls samples -- read_block()/read_audio() are NOT the
# realtime path. Every backend call is best-effort: firmware without the
# feature degrades to demo mode instead of raising inside an LVGL callback.
# ======================================================================

IQ_PIN_I, IQ_PIN_Q = "P000", "P004"     # coherent I/Q pair
IQ_RATE, IQ_BLOCK = 48000, 128
NCO_RECENTER_HZ = 9000                    # leave 3 kHz headroom for held tuning
AM_LOW_IF_HZ = 3000                       # keep a centred AM carrier out of the I/Q DC servo
FM_LOW_IF_HZ = 6000                       # keep +/-4 kHz NFM clear of the I/Q DC servo
DAC_PIN = "P014"                        # DAC0: mono AF or routed I
DAC_Q_PIN = "P015"                      # DAC1: optional routed Q


def _field(src, names):
    """First numeric field named in `names`, out of a dict or an object."""
    if src is None:
        return None
    for n in names:
        v = src.get(n) if isinstance(src, dict) else getattr(src, n, None)
        if isinstance(v, bool):
            v = int(v)
        if isinstance(v, (int, float)):
            return v
    return None


class Ra6m3Backend:

    def __init__(self):
        self.iq = None
        self.dac = None
        self.dac_q = None
        self.running = False
        self.err = None
        self.mode = DEFAULTS["m"]
        self.bw = MODE_BW[DEFAULTS["m"]]
        self.agc = DEFAULTS["a"]
        self.agc_gain = 1.0
        self.agc_target = 0.5
        self.vol = DEFAULTS["v"]
        self.fine_hz = 0             # digital NCO offset within the +/- fs/2 window
        self.scope_stage = 0         # cached physical DAC route (0 = normal mono AF)
        # Preallocated, alloc-free UI buffers filled by the C accessors (spectrum_bars /
        # counters). Nothing in the poll loop creates a MicroPython object -> GC never
        # runs -> the realtime ADC ISR is never stalled.
        self._bars = array.array("h", bytes(2 * len(SPEC_HEIGHTS)))   # int16 heights 0..50
        self._ctr = array.array("i", bytes(4 * 6))                    # counter snapshot
        try:
            from machine import IQADC, DAC
            self._IQADC, self._DAC = IQADC, DAC
            self.available = True
        except Exception as e:
            self._IQADC = self._DAC = None
            self.available = False
            self.err = "demo mode: %r" % (e,)

    # ---- helpers ----
    def _call(self, obj, name, *a):
        """Call an OPTIONAL backend method. True only if it existed and ran."""
        fn = getattr(obj, name, None) if obj is not None else None
        if fn is None:
            return False
        try:
            fn(*a)
            return True
        except Exception as e:
            self.err = "%s: %r" % (name, e)
            return False

    @staticmethod
    def vol_gain(percent):
        """UI percent -> linear final DAC gain (15% means 15% amplitude)."""
        p = min(max(int(percent), 0), 100) / 100.0
        return round(p, 4)

    # ---- lifecycle ----
    def start_rx(self):
        if not self.available or self.running:
            return self.running
        if self.iq is not None:
            # A previous checked teardown failed and deliberately retained the
            # singleton handle/source roots.  Never let a later HOME press make the
            # constructor tear it down behind persistent TST state and start ADC.
            self.err = "IQADC teardown incomplete; reset required"
            return False
        try:
            # Fixed unity direct-input path on P000/P004.  The receiver exposes no
            # selectable analogue gain state.
            self.iq = self._IQADC(IQ_PIN_I, IQ_PIN_Q,
                                  rate=IQ_RATE, block=IQ_BLOCK)
            self.dac = self._DAC(DAC_PIN)
            self.iq.start()
            # Float 4-pole channel filter has the real wide-band coefficients needed
            # by 6/9-kHz verification and cursor rejection; the integer kernel clamps
            # or bypasses above roughly 3.8 kHz.  Optional on older firmware.
            self._call(self.iq, "chf_kernel", True)
            self.apply_settings()          # demod / agc / bandwidth / volume
            self.dac.stream_from(self.iq)  # autonomous from here on
            self.running = True
            if not self.set_scope(self.scope_stage):
                raise RuntimeError(self.err or "scope route failed")
            self.fine_hz = 0               # firmware NCO starts centred
            self.err = None
        except Exception as e:
            self.err = repr(e)
            self._teardown()
        return self.running

    def stop_rx(self):
        self._call(self.iq, "demod", "off")
        return self._teardown()

    def _teardown(self):
        # Idempotent and safe after a partially completed start. Stop DAC1 first so
        # firmware leaves paired-I/Q mode before DAC0 falls back to mono silence.
        if self.dac_q is not None:
            try:
                # Leave the unused physical Q pin at a defined mid-scale, not at
                # the last arbitrary quadrature code held when DMAC stopped.
                self.dac_q.write(2048)
            except Exception:
                try:
                    self.dac_q.stop()
                except Exception:
                    pass
        for obj in (self.dac,):
            try:
                if obj is not None:
                    obj.stop()
            except Exception:
                pass
        # stop() only halts conversions; it deliberately keeps ADC0/ADC1, DTC,
        # ELC, AGT and ADC configured so the same IQADC object can be restarted.
        # The application discards that object here, therefore retaining those
        # owners is both unnecessary and unsafe: an intervening machine.ADC user
        # could rewrite shared ADC registers, and the next STOP/START would inherit
        # stale peripheral state.  deinit() performs the checked full teardown so
        # every application START reconstructs the complete acquisition chain.
        iq_released = True
        if self.iq is not None:
            self._call(self.iq, "stop")
            iq_released = self._call(self.iq, "deinit")
        # A failed checked deinit means ADC/DTC/ELC/AGT may still be owned.  Keep
        # the handle and report failure instead of presenting a false clean STOP.
        if iq_released:
            self.iq = None
        self.dac = None
        self.dac_q = None
        self.running = False
        self.fine_hz = 0
        return iq_released

    # ---- settings: cached always, pushed only while RX is up ----
    def apply_settings(self):
        self.set_mode(self.mode)
        self.set_agc(self.agc)
        self.set_bandwidth(self.bw)
        self.set_volume(self.vol)

    def set_mode(self, mode):
        self.mode = mode
        # Unknown future modes fail closed to demod off.
        return self._call(self.iq, "demod", MODE_DEMOD.get(mode, "off"))

    def set_scope(self, stage):
        """Route a DSP block to mono DAC0 or to the optional DAC0/DAC1 I/Q pair."""
        try:
            stage = int(stage)
        except Exception:
            stage = 0
        if stage < 0 or stage > 11:
            stage = 0

        if not self.running or self.iq is None:
            self.scope_stage = stage
            return True

        def route_iq(block):
            fn = getattr(self.iq, "scope", None)
            if fn is None:
                # Scope routing is optional on older firmware; normal mono stage 0
                # must still start and play even when the extension is absent.
                if block == 0:
                    return True
                self.err = "scope route unavailable"
                return False
            try:
                fn(block)
                return True
            except Exception as e:
                self.err = "scope: %r" % (e,)
                return False

        want_iq = 1 <= stage <= 5
        if want_iq:
            q_created = False
            if self.dac_q is not None:
                try:
                    q_alive = self.dac_q.playing()
                except Exception as e:
                    self.err = "DAC1/Q health: %r" % (e,)
                    q_alive = False
                if not q_alive:
                    try:
                        self.dac_q.write(2048)
                    except Exception:
                        pass
                    self.dac_q = None
            if self.dac_q is None:
                q = None
                try:
                    q = self._DAC(DAC_Q_PIN)
                    q.stream_from(self.iq)
                    self.dac_q = q
                    q_created = True
                except Exception as e:
                    try:
                        if q is not None:
                            q.stop()
                    except Exception:
                        pass
                    self.err = "DAC1/Q: %r" % (e,)
                    return False
            if not route_iq(stage):
                # If this switch reused an already-live Q stream, preserve it: the
                # firmware is still on the previous complex route and still needs Q.
                if q_created:
                    if self._call(self.dac_q, "write", 2048):
                        self.dac_q = None
                return False
        else:
            # Change the producer first; only then withdraw the Q consumer.
            if not route_iq(stage):
                return False
            if self.dac_q is not None:
                if self._call(self.dac_q, "write", 2048):
                    self.dac_q = None
                # If cleanup failed, the native route is nevertheless already mono.
                # Keep ownership for teardown/retry, but commit the truthful route/UI.

        self.scope_stage = stage
        return True

    def set_bandwidth(self, hz):
        self.bw = int(hz)
        return self._call(self.iq, "bandwidth", self.bw)

    def set_volume(self, percent):
        self.vol = int(percent)
        return self._call(self.iq, "volume", self.vol_gain(percent))

    def agc_gain_now(self):
        """Live AGC gain as a factor (auto-AGC moves it, manual holds it); None if down."""
        if not self.running or self.iq is None:
            return None
        try:
            return self.iq.agc_status().get("gain")
        except Exception:
            return None

    def set_agc(self, mode, gain=None, target=None):
        # Transactional cache: a failed live firmware call must not make Python
        # claim a mode/gain/target which the running DSP never accepted.
        new_mode = mode
        new_gain = self.agc_gain if gain is None else gain
        new_target = self.agc_target if target is None else target
        # Real firmware API is a SINGLE call: agc(mode, gain=, rms_target=).
        # mode strings are off/fast/slow/manual; gain (float, 1.0 = unity) only
        # matters in manual; rms_target is a 0..1 fraction of full scale. There is
        # NO agc_gain()/agc_target() -- those were silent no-ops. The map is a module
        # constant (not a per-call dict literal) so a repeated AGC control does not
        # allocate a dict every event.
        m = _AGC_MODE_MAP.get(new_mode, "fast")
        fn = getattr(self.iq, "agc", None) if self.iq is not None else None
        if fn is None:
            # Stopped receiver: this is a legitimate configuration change which
            # start_rx/apply_settings will push after constructing the next IQADC.
            if self.iq is None:
                self.agc = new_mode
                self.agc_gain = new_gain
                self.agc_target = new_target
                return True
            return False
        try:
            fn(m, gain=new_gain, rms_target=new_target)
            self.agc = new_mode
            self.agc_gain = new_gain
            self.agc_target = new_target
            return True
        except Exception as e:
            self.err = "agc: %r" % (e,)
            return False

    # ---- status ----
    def poll_status(self):
        """Counters for the mini status row, or None when RX is not running.
        Field names are probed, not assumed -- the backend status() shape is
        still moving."""
        if not self.running or self.iq is None:
            return None
        st = {"blk": None, "ovr": None, "und": None, "clip": None, "play": None}
        try:
            # status() has blocks/overruns/unit1_stalls/last_error (NOT underruns).
            s = self.iq.status()
            st["blk"] = _field(s, ("blocks", "blk", "nblocks", "count"))
            st["ovr"] = _field(s, ("overruns", "overrun", "ovr"))
        except Exception as e:
            self.err = "status: %r" % (e,)
        try:
            # underruns live in audio_status() as audio_underruns.
            a = self.iq.audio_status()
            st["und"] = _field(a, ("audio_underruns", "underruns", "underrun", "und"))
        except Exception:
            pass
        try:
            # clips live in agc_status() as agc_clips.
            g = self.iq.agc_status()
            st["clip"] = _field(g, ("agc_clips", "clips", "clip", "clipped"))
        except Exception:
            pass
        try:
            st["play"] = 1 if self.dac.playing() else 0
        except Exception:
            pass
        if 1 <= self.scope_stage <= 5:
            try:
                q_alive = self.dac_q is not None and self.dac_q.playing()
            except Exception:
                q_alive = False
            if not q_alive:
                # Status inspection is deliberately read-only. Reconstructing DAC1
                # here used to turn a failed Q route into an unbounded hardware retry.
                st["play"] = 0
        return st

    def agc_status(self):
        try:
            return self.iq.agc_status()
        except Exception:
            return None

    # ---- alloc-free live readouts (fill preallocated buffers via the C accessors) ----
    def read_bars(self):
        """Fill self._bars (int16 heights 0..50, DC centred) via the C FFT+reduce.
        Returns the array when a fresh snapshot arrived, else None. ZERO alloc."""
        if not self.running or self.iq is None:
            return None
        try:
            if self.iq.spectrum_bars(self._bars):
                return self._bars
        except Exception as e:
            self.err = "spectrum_bars: %r" % (e,)
        return None

    def read_counters(self):
        """Fill self._ctr = [blocks, overruns, unit1_stalls, audio_underruns,
        ring_overruns, agc_clips] via the C accessor. Returns the array. ZERO alloc."""
        if not self.running or self.iq is None:
            return None
        try:
            self.iq.counters(self._ctr)
            return self._ctr
        except Exception as e:
            self.err = "counters: %r" % (e,)
        return None

    def fine_tune(self, delta_hz):
        """Nudge the digital NCO offset by delta_hz; returns the clamped offset."""
        if not self.running or self.iq is None:
            return None
        try:
            self.fine_hz = int(self.iq.tune(self.fine_hz + int(delta_hz)))
            return self.fine_hz
        except Exception as e:
            self.err = "tune: %r" % (e,)
            return None

    def set_fine(self, hz):
        """Set the absolute NCO offset (0 = centre)."""
        if not self.running or self.iq is None:
            self.fine_hz = 0
            return None
        try:
            self.fine_hz = int(self.iq.tune(int(hz)))
            return self.fine_hz
        except Exception as e:
            self.err = "tune: %r" % (e,)
            return None


# ---------------- app ----------------

class SdrApp:

    def __init__(self, ui, iq_file_mem=None):
        self.ui = ui
        self.p = load_params()
        # Four different frequencies must never be conflated:
        #   p["f"]        last selected frequency published by the UI;
        #   _requested_hz latest operator request (may still be in flight);
        #   _lo_hz        last Si5351 frequency confirmed by set_freq();
        #   _axis_hz      physical LO belonging to the FFT pixels now on screen.
        # A live NCO move changes only the selected frequency/marker.  A physical
        # LO move publishes _axis_hz only after C accepts a complete FFT carrying
        # the matching generation token.
        self._lo_hz = self.p["f"]
        self._requested_hz = self.p["f"]
        self._axis_hz = self.p["f"]
        self.entry = ""              # keypad buffer (MHz string, e.g. "14.205")
        self.save_timer = None
        self._band = None            # cached band name to skip no-op label writes
        self._st = None              # last painted backend status tuple
        self.spec = list(SPEC_HEIGHTS)   # live-shifted spectrum pattern
        # Zero-alloc loop state: preallocated diff buffers + precompiled colours so the
        # consumer never creates a MicroPython object (no lv.color_hex()/tuple/str/float
        # per tick). NB: on this port GC does NOT stall the realtime ISR (proven: 7/7
        # blocks, 0 underruns across 18 ms collections) -- a GC is only a UI hitch. Keeping
        # the loop alloc-free is about UI smoothness (rarer GC), not audio integrity.
        self._last_bars = array.array("h", bytes(2 * len(SPEC_HEIGHTS)))  # prev heights
        self._demo_bars = array.array("h", SPEC_HEIGHTS)  # fixed native-render targets
        self._pc = array.array("i", b"\xff\xff\xff\xff" * 6)              # prev counters (=-1)
        self._demo_shift_acc = 0      # residual delta_hz * bar_count; preserves sub-bar steps
        self._C_RED = lv.color_hex(0xE53935)
        self._C_BORDER = lv.color_hex(BORDER)
        self._C_CYAN = lv.color_hex(CYAN_RX)
        self._C_GRAY2 = lv.color_hex(GRAY2)
        # cache the display handle: lv.display_get_default() allocates a fresh wrapper
        # each call, so it must NOT be called in the loop.
        self._dd = lv.display_get_default()
        self._gated = hasattr(self._dd, "enable_invalidation")
        self._spec_marked = False    # centre tuning marker painted on the live spectrum?
        # Preferred renderer: one C-drawn LVGL surface.  It deletes the 27 legacy bar
        # children, stores targets in fixed C memory, and commits five interleaved
        # groups at 20 ms so all columns do not jump in the same physical frame. Old firmware
        # keeps the widget fallback below.
        self._lcd = None
        self._spec_lcd = None
        self._spectrum_center_fn = None
        self._spectrum_publish_fn = None
        self._spectrum_generation_api = False
        self._spec_native = False
        self._spectrum_view = 0       # 0=SPEC, 1=WF, 2=OFF on the left native panel
        self._scope_view = 0          # 0=TIME, 1=I-Q, 2=OFF on the right native panel
        try:
            from machine import LCD
            lcd = LCD()
            self._lcd = lcd
            if hasattr(lcd, "spectrum_attach") and hasattr(lcd, "spectrum_update"):
                self._spec_native = bool(lcd.spectrum_attach(
                    self.ui.get("spectrum-waterfall"), PANEL, BIN, CYAN_RX))
                if self._spec_native:
                    self._spec_lcd = lcd
                    self._spectrum_center_fn = getattr(lcd, "spectrum_center", None)
                    self._spectrum_publish_fn = getattr(lcd, "spectrum_publish", None)
                    if (self._spectrum_center_fn is not None and
                            self._spectrum_publish_fn is not None):
                        try:
                            # New API: no argument is a signed generation getter.
                            # Old panorama-panning firmware requires one argument.
                            int(self._spectrum_center_fn())
                            self._spectrum_generation_api = True
                        except Exception:
                            self._spectrum_generation_api = False
                    self.ui.bins = ()       # release deleted LVGL wrapper objects
        except Exception:
            self._spec_lcd = None
            self._spectrum_center_fn = None
            self._spectrum_publish_fn = None
            self._spec_native = False
        # Deferred-work flags: interaction callbacks only SET these (microseconds);
        # the 100 ms GUI worker applies the latest values outside the touch callback.
        # Spectrum/status still run at their own divisors and are never starved by flags.
        self._hw_pending = True      # worker owns every Si5351 transaction
        self._hw_config_pending = False  # route/CAL refresh, independent of frequency
        # A pending LO target never changes _lo_hz until set_freq() succeeds.  A
        # pending station target is used only for a discontinuous live jump (VFO or
        # keypad), so p["f"] also stays at the last frequency the hardware achieved.
        self._lo_pending_hz = self._lo_hz
        self._station_pending_hz = None
        self._station_pending_vfo = None
        self._station_pending_mode = None
        self._axis_pending_token = 0
        self._axis_pending_hz = None
        self._axis_pending_station_hz = None
        self._axis_pending_vfo = None
        self._axis_pending_mode = None
        self._vol_pending = False    # firmware volume needs the latest slider value
        self._rx_pending = 0         # 0 none, 1 start, 2 stop (heavy IQADC/DAC bring-up)
        self._poll_div = 0           # 100 ms GUI tick; status every fifth tick
        self._modal = False          # non-HOME screen/overlay -> pause native framebuffer writes
        self._spectrum_transition_hold = False  # pause only around the physical I2C edge
        # Settings modal (firmware DSP verification controls).  The test-source preset,
        # injection point, waveform and LIVE flag survive VERIFY screen rebuilds in this
        # App instance, but are never written to flash.  The source itself runs in C;
        # there is no Python sample timer.
        self._inj_ampl = 100
        self._inj_overload = False    # fail-closed; tap the S/O amplitude chip to arm OVR
        self._inj_on = False
        self._inj_fm_nco = False      # TESTER FM temporarily owns the pre-NCO offset
        self._inj_point = 0          # 0=raw IN, 1=movable 24-kS/s MID, 2=post-filter OUT
        self._inj_mid = 1            # _INJ_MIDS index; default NCO preserves legacy MID
        self._inj_mode = 0           # index into _INJ_PRESETS (AM/FM/USB/LSB/CW/IQ)
        self._inj_wave = 0           # SIN/SQR/TRI/PULSE; PULSE also uses the 2-Hz gate
        self._inj_live = False       # deterministic C phase jitter for a live scope trace
        # TESTER never rewrites SCOPE: no route means normal mono demod audio;
        # a selected complex block gives DA0=I/DA1=Q; a selected audio block is mono.
        self._inj_prev_scope = None  # retained only for fail-safe legacy restoration
        self._inj_source = 0         # 0=native generator, 1=/flash/test.sdriq player
        self._iq_file_preset = 0     # independent FILE bank selection
        # Expose each FILE profile independently only when both point-rate assets are
        # present.  A missing optional R:FM pair must not hide otherwise usable R:AM,
        # R:USB, R:LSB or R:CW recordings (the former all-or-nothing probe did that).
        available_profiles = []
        for _profile in self._IQ_FILE_PRESETS + self._IQ_FILE_REAL_PRESETS:
            profile_ready = True
            for _path in (_profile[2], _profile[3]):
                try:
                    _probe = open(_path, "rb")
                    _probe.close()
                except OSError:
                    profile_ready = False
                    break
            if profile_ready:
                available_profiles.append(_profile)
        # Keep one deterministic error-reporting choice when a development flash has
        # no bank at all.  start() will then report the missing AM path explicitly.
        self._iq_file_profiles = (tuple(available_profiles) if available_profiles else
                                  self._IQ_FILE_PRESETS[:1])
        self._iq_file_loop = True
        self._iq_file_ui_pct = -1
        if iq_file_mem is None:
            # Fallback for direct test construction.  start() takes the preferred path
            # and allocates these BEFORE build(), while the heap is still contiguous.
            iq_file_mem = (bytearray(_IQ_FILE_BUFFER_BYTES),
                           bytearray(_IQ_FILE_BUFFER_BYTES),
                           bytearray(_IQ_FILE_HEADER_BYTES),
                           array.array("i", bytes(4 * _IQ_FILE_STATUS_WORDS)))
        self._iq_file_mem = iq_file_mem       # explicit GC root for both native buffers
        self._iq_file = _IqFileSource(iq_file_mem[0], iq_file_mem[1],
                                      iq_file_mem[2], iq_file_mem[3])
        self._tap_stage = 0
        self._passthru_on = False    # verify-only demod("thru") override (not persisted)
        self._squelch = 0            # verify-only squelch threshold (not persisted)
        self._af_preset = 0          # audio_filter index into _AF_PRESETS
        self._iqc_on = bool(self.p["iqe"])  # persisted I/Q imbalance correction
        self._iqc_amp = self.p["iqa"]       # Q amplitude multiplier
        self._iqc_phase = self.p["iqp"]     # I leakage added to Q (not degrees)
        self._iqc_dirty = False
        self._tester_arm_pending = False
        self._kernels = {"dec_kernel": 0, "hil_kernel": 0,
                         "chf_kernel": 0, "mag_kernel": 0}
        # Per-block DSP verification table. IDs 2..11 retain the firmware ABI; raw
        # input ID 1 remains an internal scope tap and is not a processing block.
        # _scope_id is the ONE block whose output is routed to the DAC(s) via
        # iq.scope() (0 = normal receiver output).
        # _tap_stage (0..3) stays the one-of UART tap; only blocks 2/4/5 map to stages
        # 1/2/3, every other block's tap control is rendered disabled (firmware has no tap).
        self._blk_on = {i: True for i in range(2, 12)}
        self._scope_id = 0
        self._tap_id = 0             # block id currently holding the one-of UART tap (0 = none)
        self._set_lbls = {}
        # Inline gains panel. One shared checkbox occupies the normal VOL-value
        # position and pins the most recently touched inline slider into the
        # permanent far-right slot when the panel closes.
        self._active_gain = "AF"
        self._gain_candidate = "AF"
        self._gain_vlbls = {}
        self._gain_vlbls_all = {}    # persistent value-label handles (screen lifetime)
        # Per-open refresh handles: key -> (slider, value_label) for the gains screen,
        # row-name -> widget(s) for the settings screen. Both screens are built lazily
        # once and re-shown on later opens, so their displayed values are re-read from
        # the current state on EACH open via _refresh_gains / _refresh_settings.
        self._gain_widgets = {}
        self._set_widgets = {}
        # Last actually painted VERIFY header values.  The DSP getters return
        # fresh dicts, but unchanged formatted values must not invalidate LVGL.
        # Entries: AGC x10, S rms, S dBFS, timing valid/generation/AVG/peak.
        # The timing values are a completed 500-ms window, not a since-start maximum.
        self._set_live_cache = [None, None, None, None, None, None, None,
                                None, None]
        self._set_scroll_gate = False
        self._set_scroll_adjust = False
        self._set_scroll_origin = 0
        self._set_scroll_idle = 0
        self._set_scroll_quiet = 0       # 100-ms ticks; keep status off the commit frame
        # bottom bar: 0 HOME tuning, 1 modes, 2 filters, 4 steps, 5 three-way chooser
        self._mode_expanded = 0
        self._ovr_red = False        # status-indicator "is red" states (change-only paint)
        self._und_red = False
        self._clip_red = False
        self._blk_lit = False        # BLK label coloured live (set once when counting)
        # Fixed NUL-terminated text buffer for the BLK counter: "BLK " + 10 digit slots.
        # Mutated in place (no str/format/concat) and shown via set_text_static -> the
        # binding passes the pointer without copying, so a live counter allocates nothing.
        self._blk_buf = bytearray(b"BLK 0000000000\x00")
        self.be = Ra6m3Backend()
        self._wire()
        self.apply_all()
        # rx_autostart: come back up in the state the radio was left in
        if self.backend_on() and self.p["rxauto"]:
            self.start_rx()

    def _set_modal(self, value):
        """Keep Python and native C spectrum rendering in the same HOME state."""
        self._modal = bool(value)
        self._sync_spectrum_pause()

    def _sync_spectrum_pause(self):
        """Apply the union of navigation and the short physical-retune hold."""
        if self._spec_native and hasattr(self._spec_lcd, "spectrum_pause"):
            paused = self._modal or self._spectrum_transition_hold
            try:
                try:
                    # New firmware latches a dirty-rebuild request only for a real
                    # modal.  A short Si5351 hold must preserve the old waterfall
                    # until the matching new-generation frame is ready.
                    self._spec_lcd.spectrum_pause(paused, self._modal)
                except TypeError:
                    # Older firmware has the one-argument API.  Keep it usable;
                    # its resume semantics may rebuild, but tuning must not fail.
                    self._spec_lcd.spectrum_pause(paused)
            except Exception as e:
                self.be.err = "spectrum pause: %r" % (e,)

    def _set_spectrum_transition(self, value):
        """Freeze capture only while Si5351 is physically being changed.

        The hold is released immediately after the C generation request.  Keeping
        it raised while waiting for the acknowledgement would deadlock the publish:
        a paused native surface cannot draw and commit the matching frame.
        """
        self._spectrum_transition_hold = bool(value)
        self._sync_spectrum_pause()

    def _begin_verify_scroll_gate(self, rows):
        """Freeze repaint before LVGL moves the VERIFY children."""
        if (self._set_scroll_adjust or self._set_scroll_gate or
                not self._gated):
            return
        view = self._set_widgets.get("scroll_view")
        if view is None:
            return
        rows.get_coords(view)
        self._set_scroll_origin = rows.get_scroll_y()
        self._set_scroll_idle = 0
        self._dd.enable_invalidation(False)
        self._set_scroll_gate = True

    def _end_verify_scroll_gate(self, commit=True):
        """Commit one row with a VSYNC framebuffer blit plus strip redraw."""
        if self._set_scroll_adjust or not self._set_scroll_gate:
            return
        rows = self._set_widgets.get("rows")
        view = self._set_widgets.get("scroll_view")
        dirty = self._set_widgets.get("scroll_dirty")
        native = False
        moved = False
        dy = 0

        try:
            if commit and rows is not None and view is not None and dirty is not None:
                origin = self._set_scroll_origin
                actual = rows.get_scroll_y()
                max_scroll = actual + rows.get_scroll_bottom()
                if max_scroll < 0:
                    max_scroll = 0

                # Cap one gesture to a complete 31-px row.  The viewport is an exact
                # multiple of that pitch, so the exposed strip intersects one row tree
                # rather than two. A shorter tail reaches the final boundary.
                delta = actual - origin
                target = origin
                if delta >= 8 and origin < max_scroll:
                    target = origin + _VERIFY_SCROLL_STEP
                    if target > max_scroll:
                        target = max_scroll
                elif delta <= -8 and origin > 0:
                    target = origin - _VERIFY_SCROLL_STEP
                    if target < 0:
                        target = 0

                if target != actual:
                    self._set_scroll_adjust = True
                    try:
                        rows.scroll_to_y(target, False)
                    finally:
                        self._set_scroll_adjust = False
                # Scrolling a flex container marks the screen layout dirty.  Resolve
                # all 21 child positions while invalidation is still gated; deferring
                # this until the render would invalidate every old/new row rectangle
                # and overflow LVGL's 32-area buffer into a full viewport redraw.
                rows.update_layout()
                target = rows.get_scroll_y()       # use LVGL's bounded result
                dy = origin - target               # framebuffer pixel direction
                moved = dy != 0

                lcd = self._lcd
                if moved:
                    if dy < 0:                     # pixels move up; reveal bottom
                        dirty.set(view.x1, view.y2 + dy + 1, view.x2, view.y2)
                    else:                          # pixels move down; reveal top
                        dirty.set(view.x1, view.y1, view.x2, view.y1 + dy - 1)
                    view_h = view.y2 - view.y1 + 1
                    if (-view_h < dy < view_h and lcd is not None and
                            hasattr(lcd, "scroll_rect")):
                        native = bool(lcd.scroll_rect(
                            view.x1, view.y1, view.x2 - view.x1 + 1,
                            view.y2 - view.y1 + 1, dy))
        except Exception as e:
            moved = rows is not None
            self.be.err = "verify scroll: %r" % (e,)
        finally:
            self._set_scroll_adjust = False
            self._set_scroll_gate = False
            self._set_scroll_idle = 0
            if self._gated:
                self._dd.enable_invalidation(True)

        if rows is not None and moved:
            # A live AGC/S/DSP header update generates about 12 old/new label
            # invalidations. If its 2-Hz tick lands on this same transaction the
            # combined render can cross VSYNC even though the 31-px strip alone has
            # safe margin. Keep two 100-ms ticks quiet so the strip commits alone.
            self._set_scroll_quiet = 2
            if native:
                try:
                    rows.invalidate_area(dirty)
                except Exception:
                    # The blit is already pending.  A full invalidation keeps the
                    # next render correct even if a future binding rejects area_t.
                    rows.invalidate()
            else:
                # Older firmware, invalid geometry, or an already pending blit.
                # Preserve correctness even though this path costs one full repaint.
                rows.invalidate()

    # ---- formatting ----
    @staticmethod
    def fmt_freq(hz):
        # fixed-width radio style: MHz zero-padded to 2 digits ("00 212 200")
        return "%02d %03d %03d" % (hz // 1_000_000, (hz // 1000) % 1000, hz % 1000)

    @staticmethod
    def fmt_khz(hz):
        return "%d.%03d" % (hz // 1_000_000, (hz // 1000) % 1000)

    def band_of(self, hz):
        for name, label, lo, hi, _ in BANDS:
            if lo <= hz <= hi:
                return name, label
        return None, "GEN"

    # ---- hardware: drive the routed synth chip ----
    def hw_tune(self, target_hz=None, vfo=None):
        """Program one synthesizer target without committing any App state.

        This function is called only by the 100-ms worker.  In particular, callers
        must not assign _lo_hz before this returns True: _lo_hz means the last LO
        frequency which set_freq() actually reported as programmed.
        """
        # NEVER raises, NEVER prints: hardware status is shown by the brand
        # dot only -- teal = synth driven, red = chip missing/unreachable.
        global XTAL_PPM
        ok = False
        target_hz = self._lo_hz if target_hz is None else int(target_hz)
        vfo = self.p["act"] if vfo is None else int(vfo)
        _lab, clk, mult = TARGETS[self.p["rt"][vfo]]
        if clk is not None:
            try:
                if getattr(self, "_synth", None) is None:
                    self._synth = SI5351()
                XTAL_PPM = self.p.get("cal", 17.76)
                if self._synth.probe():
                    ok = self._synth.set_freq(clk, target_hz * mult)
            except Exception:
                ok = False
        self.ui.get("brand-dot").set_style_bg_color(
            lv.color_hex(TEAL if ok else 0xE53935), 0)
        return ok

    def _clear_station_pending(self):
        self._station_pending_hz = None
        self._station_pending_vfo = None
        self._station_pending_mode = None

    def _clear_axis_pending(self):
        self._axis_pending_token = 0
        self._axis_pending_hz = None
        self._axis_pending_station_hz = None
        self._axis_pending_vfo = None
        self._axis_pending_mode = None

    def _cancel_frequency_pending(self):
        """Cancel only frequency work; preserve an unrelated route/CAL refresh."""
        self._lo_pending_hz = None
        self._clear_station_pending()
        self._hw_pending = self._hw_config_pending

    def _queue_hw_config(self):
        """Queue route/CAL programming without manufacturing an LO transition."""
        self._hw_config_pending = True
        self._hw_pending = True

    @staticmethod
    def _preferred_lo_hz(station_hz, mode):
        """Physical LO for one selected RF station.

        A zero-IF carrier is indistinguishable from ADC DC to the mandatory
        pre-demod I/Q servo. Keep AM at a deliberate 3-kHz low IF. NFM uses
        6 kHz so +/-4-kHz deviation with a 1-kHz tone occupies about -11..-1
        kHz before the NCO, clear of both DC and the 12-kHz Nyquist edge.
        """
        station_hz = min(max(int(station_hz), F_MIN), F_MAX)
        offset = AM_LOW_IF_HZ if mode == "AM" else (FM_LOW_IF_HZ if mode == "FM" else 0)
        # F_MAX limits the selected station, not the physical low-IF LO.  At the
        # 40-MHz station ceiling the largest request is 40.006 MHz (CLK1 x4 =
        # 160.024 MHz), still inside the Si5351 200-MHz output limit.
        return station_hz + offset

    def _normal_lo_hz(self, station_hz=None, mode=None):
        if station_hz is None:
            station_hz = self.p["f"]
        if mode is None:
            mode = self.p["m"]
        return self._preferred_lo_hz(station_hz, mode)

    def _nco_recenter_due(self, station_hz, mode=None):
        """True when the real live NCO offset reaches the safe recenter edge.

        AM normally starts at -3 kHz because its physical LO is station+3 kHz.
        Comparing the old LO with the *next preferred LO* therefore made the two
        edges asymmetric: +6 kHz in one direction and the -11,999 Hz clamp in the
        other.  The edge belongs to the NCO itself, so compare station-LO directly;
        ``mode`` remains in the signature for the existing VFO-switch callers.
        """
        limit = 7000 if mode == "FM" else NCO_RECENTER_HZ
        return abs(int(station_hz) - int(self._lo_hz)) >= limit

    def _queue_station_recenter(self, hz, vfo=None, mode=None):
        """Queue an absolute live jump; do not change truthful UI state yet."""
        hz = min(max(int(hz), F_MIN), F_MAX)
        # A held tuning control can supersede either a worker-owned request which
        # has not reached Si5351 yet, or an applied axis awaiting its first tagged
        # frame.  In both windows p[] is deliberately stale.  Preserve the newer
        # queued identity first, then the applied in-flight identity, before deriving
        # the next low-IF LO target.
        if self._station_pending_hz is not None:
            if vfo is None:
                vfo = self._station_pending_vfo
            if mode is None:
                mode = self._station_pending_mode
        if self._axis_pending_token:
            if vfo is None:
                vfo = self._axis_pending_vfo
            if mode is None:
                mode = self._axis_pending_mode
        selected_mode = self.p["m"] if mode is None else mode
        self._requested_hz = hz
        self._lo_pending_hz = self._normal_lo_hz(hz, selected_mode)
        self._station_pending_hz = hz
        self._station_pending_vfo = vfo
        self._station_pending_mode = mode
        self._hw_pending = True

    def _queue_nco_recenter(self):
        """Move the physical LO under an already-achieved NCO selection."""
        if self._station_pending_hz is not None:
            return
        # Use the common path so an in-flight VFO switch contributes its pending
        # mode and routing identity to the new low-IF target.
        self._queue_station_recenter(self._requested_hz)

    def _commit_current_frequency(self, hz):
        """Commit a frequency which the current LO+NCO has actually achieved."""
        hz = min(max(int(hz), F_MIN), F_MAX)
        self.p["f"] = hz
        self._requested_hz = hz
        self.p["vfos"][self.p["act"]][0] = hz
        self.update_freq()
        self.update_vfo_ui()
        self.touch_params()

    def _commit_vfo_switch(self, i, hz, mode):
        """Commit VFO identity only after its requested RF frequency is real."""
        p = self.p
        old_act = p["act"]
        old_f = p["f"]
        p["vfos"][old_act] = [old_f, p["m"]]
        alt = getattr(self, "_alt", None)
        if alt and i in alt:
            alt[alt.index(i)] = old_act
        p["act"] = i
        p["f"] = min(max(int(hz), F_MIN), F_MAX)
        self._requested_hz = p["f"]
        p["m"] = mode
        p["vfos"][i] = [p["f"], mode]
        self.update_freq()
        self.update_mode()
        self.be.set_mode(mode)
        self.be.set_bandwidth(self.cur_bw())
        self.update_vfo_ui()
        self.update_entry_digits()
        self.update_entry_bands()
        self.touch_params()

    def _apply_hw_pending(self):
        """Apply one physical LO request, then arm its matching FFT generation.

        The display producer is paused only across the Si5351 write and generation
        request.  It is resumed immediately afterwards so C can build the new frame.
        Python does not publish the new axis or selected station until the matching
        positive generation token is observed by _poll_axis_generation().
        """
        target = self._lo_pending_hz
        station = self._station_pending_hz
        pending_vfo = self._station_pending_vfo
        pending_mode = self._station_pending_mode
        # A route/CAL refresh or a second rollover can arrive while the previous
        # physical generation is awaiting its first frame.  Carry its unpublished
        # selection/VFO identity forward so superseding the token cannot lose the
        # operator request.
        if self._axis_pending_token:
            if station is None:
                station = self._axis_pending_station_hz
            if pending_vfo is None:
                pending_vfo = self._axis_pending_vfo
            if pending_mode is None:
                pending_mode = self._axis_pending_mode
        self._hw_pending = False
        self._hw_config_pending = False
        self._lo_pending_hz = None
        self._clear_station_pending()

        # Routing/calibration-only updates reprogram the already-known LO and still
        # need a fresh tagged frame: the selected output clock may have changed.
        if target is None:
            target = self._lo_hz

        route_vfo = self.p["act"] if pending_vfo is None else pending_vfo
        self._set_spectrum_transition(True)
        try:
            if not self.hw_tune(target, route_vfo):
                # A newer physical request must not make the still-published old
                # selection look achieved.  If an earlier generation is already in
                # flight, keep its requested selection; otherwise return to p["f"].
                self._requested_hz = (self._axis_pending_station_hz
                                      if self._axis_pending_token and
                                      self._axis_pending_station_hz is not None
                                      else self.p["f"])
                return False

            # set_freq() succeeded: _lo_hz is now physical truth.  p["f"] and
            # _axis_hz deliberately remain the last published display truth.
            self._lo_hz = int(target)
            # Any older generation belongs to a physical window that no longer
            # exists.  It must not be allowed to publish after this successful edge.
            self._clear_axis_pending()
            desired = self._requested_hz if station is None else int(station)
            achieved = desired
            if self.be.running and station is not None:
                actual = self.be.set_fine(desired - self._lo_hz)
                if actual is None:
                    # The backend cache is the last confirmed NCO after an error.
                    actual = self.be.fine_hz
                achieved = min(max(self._lo_hz + actual, F_MIN), F_MAX)
                self._requested_hz = achieved

            if self.be.running and self._spectrum_generation_api:
                token = self._request_spectrum_generation(self._lo_hz)
                if token > 0:
                    # A later physical request replaces this record/token.  C also
                    # coalesces or supersedes its pending generation accordingly.
                    self._axis_pending_token = token
                    self._axis_pending_hz = self._lo_hz
                    self._axis_pending_station_hz = (
                        achieved if station is not None else None)
                    self._axis_pending_vfo = pending_vfo
                    self._axis_pending_mode = pending_mode
                    return achieved == desired

            # RX stopped, Python fallback, or old firmware without generation
            # readback: there is no observable matching-frame barrier.  Keep that
            # compatibility path usable, but never call the old history-panning API.
            self._finish_axis_publish(
                self._lo_hz, achieved if station is not None else None,
                pending_vfo, pending_mode)
            if self.be.running and self._spec_native:
                self.be.err = "spectrum generation API unavailable"
            return achieved == desired
        finally:
            # Do not hold this until ACK: pause disables the producer needed to ACK.
            self._set_spectrum_transition(False)

    # ---- RA6M3 backend: RX on/off + status ----
    def backend_on(self):
        return self.be.available and bool(self.p["beon"])

    def cur_bw(self):
        return self.p["bw"].get(self.p["m"], MODE_BW[self.p["m"]])

    def _apply_iq_correction(self, enable_path=True):
        """Apply the RAM profile and make block 3 effective when correction is ON."""
        iq = self.be.iq
        fn = getattr(iq, "iq_correction", None) if iq is not None else None
        if fn is None:
            self.be.err = "iq_correction unavailable"
            return False
        try:
            if self._iqc_on and enable_path:
                block_fn = getattr(iq, "block", None)
                if block_fn is not None:
                    block_fn(3, 1)
                    self._blk_on[3] = True
            fn(enable=self._iqc_on, amp=self._iqc_amp, phase=self._iqc_phase)
            self.be.err = None
            return True
        except Exception as e:
            self.be.err = "iq_correction: %r" % (e,)
            return False

    def _save_iq_profile(self):
        """Commit the currently applied manual I/Q profile to data flash."""
        self.p["iqe"] = 1 if self._iqc_on else 0
        self.p["iqa"] = self._iqc_amp
        self.p["iqp"] = self._iqc_phase
        try:
            save_params(self.p)
            self._iqc_dirty = False
            return True
        except Exception as e:
            self.be.err = "IQ save: %r" % (e,)
            return False

    def start_rx(self):
        if self.backend_on() and not self.be.running:
            # Keep an old/demo framebuffer stable while IQADC and the physical LO
            # are reconstructed.  _apply_hw_pending releases this short hold as
            # soon as the new generation has been requested from C.
            self._set_spectrum_transition(True)
            self.be.mode = self.p["m"]
            self.be.bw = self.cur_bw()
            self.be.agc = self.p["a"]
            self.be.agc_gain = self.p["again"]
            self.be.agc_target = self.p["atgt"]
            self.be.vol = self.p["v"]
            self.be.start_rx()
            if self.be.running:
                # RX construction proves ADC/DSP/DAC ownership.  Re-arm the
                # active Si5351 route as well, even when the cached LO already equals
                # the selected station: a prior I2C fault or external synth reset can
                # otherwise leave CLKx disabled while STOP/START appears successful.
                # The 100-ms worker performs the actual I2C transaction.
                self._queue_hw_config()
                self._apply_iq_correction(True)
                # Squelch belongs to the App's inline/VERIFY state rather than the
                # persisted receiver parameters; reapply it to each fresh IQADC.
                self._apply_gain("SQL", self._squelch)
                # IQADC starts with NCO=0.  Preload the selected station against the
                # last confirmed physical LO while capture is held.  The worker then
                # establishes the final LO+NCO pair and its tagged FFT generation.
                requested = self.p["f"]
                self._requested_hz = requested
                actual = self.be.set_fine(requested - self._lo_hz)
                if actual is None:
                    actual = self.be.fine_hz
                # Always perform one confirmed physical write and generation request
                # after a fresh IQADC.  This also establishes AM/FM low IF and avoids
                # accepting an untagged first/partial FFT from the reconstructed path.
                self._queue_station_recenter(requested)
                # ra_iq_adc_init() intentionally clears all optional native display
                # producer gates and partial frames.  Reconcile the already-selected
                # SPEC/WF and TIME/I-Q views only after the fresh IQADC is running.
                # The C setter is deliberately idempotent, so this preserves both
                # view selection and retained waterfall history.
                self._sync_spectrum_pause()
                # The data path is alloc-free (C accessors -> preallocated arrays); the
                # only churn is LVGL's own render binding (~22 B/repaint). That is far
                # too little to disable GC over -- disabling it just fills the heap and
                # the app stops. Keep GC ENABLED: with so little garbage it runs rarely
                # and briefly, and the DTC/DMAC ping-pong absorbs the sub-ms pause.
                gc.collect()
            else:
                self._set_spectrum_transition(False)
            self.p["rxauto"] = 1 if self.be.running else 0
            self.touch_params()
        self.update_rx()

    def stop_rx(self):
        self._tester_arm_pending = False
        # Stop acquisition before FILE is detached.  Reversing this order would let
        # one last ISR fall through to real ADC while HOME still truthfully says TST.
        # IQADC.deinit() first invalidates the native borrowed pointers; the idempotent
        # Python stop below then closes its file and tolerates the inactive singleton.
        released = self.be.stop_rx()
        if not released:
            # Checked teardown retained the native IQADC owner.  Its stop state is
            # uncertain, so FILE buffers must remain rooted/attached.  Keep TST
            # truthful when it was active and require explicit hardware recovery
            # instead of falling through to ADC or pretending STOP succeeded.
            self._hw_config_pending = False
            self._cancel_frequency_pending()
            self._clear_axis_pending()
            self._requested_hz = self.p["f"]
            self._set_spectrum_transition(False)
            self.p["rxauto"] = 0
            self.touch_params()
            self._paint_tester()
            self._paint_output_status()
            self.update_rx()
            return False
        self._iq_file.stop()
        # A queued live VFO/keypad jump or an old plain recenter is no longer
        # meaningful after the backend has stopped.  Preserve only an unrelated
        # route/CAL refresh, then park the LO for the station which is actually
        # committed in HOME.
        self._cancel_frequency_pending()
        self._clear_axis_pending()
        self._requested_hz = self.p["f"]
        self._set_spectrum_transition(False)
        # Ask the worker to park the physical LO on the selected station's normal
        # physical centre (AM keeps its deliberate 3-kHz low IF).  Do not
        # assign _lo_hz here: stop_rx() is a UI callback and no Si5351 write has yet
        # succeeded.  The backend NCO has returned to zero.
        normal_lo = self._normal_lo_hz()
        if self._lo_hz != normal_lo:
            self._lo_pending_hz = normal_lo
            self._hw_pending = True
        # A user STOP always clears TESTER and restores the normal receiver route.
        self._inj_fm_nco = False
        self._inj_on = False
        self._inj_overload = False
        self._update_tuning_role()
        self._paint_tester()
        if self._inj_prev_scope is not None:
            self._scope_id = self._inj_prev_scope
            self.be.set_scope(self._scope_id)   # RX is down: update the cached route only
            self._inj_prev_scope = None
        self._paint_listen_markers()
        self._paint_output_status()
        gc.collect()                 # reclaim the run's churn (GC stays enabled)
        self.p["rxauto"] = 0
        for i in range(len(self._last_bars)):    # force a clean live repaint next time
            self._last_bars[i] = 0
        for i in range(6):
            self._pc[i] = -1
        self._blk_lit = False
        self._ovr_red = self._und_red = self._clip_red = False
        self.spec = list(SPEC_HEIGHTS)       # back to the demo spectrum pattern
        self._demo_shift_acc = 0
        self.paint_spectrum()
        self.ui.get("smeter-value").set_text("")
        self.update_freq()                   # drop the fine offset + widen labels off
        self.update_rx()             # clears the counters via paint_status(None)
        self.touch_params()
        return released

    def toggle_rx(self):
        # Direct: RX on/off is a single deliberate press, so a brief one-time blip
        # during bring-up is fine (unlike a slider drag). Deferring it just made the
        # button feel dead.
        if self.be.running:
            self.stop_rx()
        else:
            self.start_rx()

    def update_rx(self):
        ui = self.ui
        run = self.be.running
        if run:
            dot, bg, txt = GREEN, GREEN, DARK_TXT
        elif self.backend_on() and self.be.err:
            dot, bg, txt = 0xE53935, PANEL2, WHITE   # tried and failed
        else:
            dot, bg, txt = GRAY2, PANEL2, WHITE      # off / demo mode
        ui.get("rx-dot").set_style_bg_color(lv.color_hex(dot), 0)
        b = ui.get("rx-button")
        b.set_style_bg_color(lv.color_hex(bg), 0)
        b.set_style_border_color(lv.color_hex(GREEN if run else BORDER), 0)
        label = b.get_child(0)
        # HOME must never present a hidden synthetic/file source as ordinary RX.
        # The same hardware button still stops the complete receiver, but its label
        # identifies the source which currently owns the DSP input.
        label.set_text("TST" if self._inj_on else "RX")
        label.set_style_text_color(lv.color_hex(txt), 0)
        if not run:
            self.paint_status(None)

    def paint_status(self, st):
        """BLK/OVR/UND/CLIP mini row. Repaints only on a real change so the
        500 ms poll costs nothing when the radio is quiet."""
        ui = self.ui
        if st is None:
            if self._st is None:
                return
            self._st = None
            ui.get("sdr-blk").set_text("BLK ----")
            ui.get("sdr-blk").set_style_text_color(lv.color_hex(GRAY2), 0)
            for n in ("sdr-ovr", "sdr-und", "sdr-clip"):
                ui.get(n).set_style_text_color(lv.color_hex(BORDER), 0)
            return
        key = (st["blk"], st["ovr"], st["und"], st["clip"], st["play"])
        if key == self._st:
            return
        self._st = key
        blk = ui.get("sdr-blk")
        blk.set_text("BLK " + fmt_count(st["blk"]))
        blk.set_style_text_color(lv.color_hex(CYAN_RX if st["play"] else GRAY2), 0)
        for name, v in (("sdr-ovr", st["ovr"]), ("sdr-und", st["und"]),
                        ("sdr-clip", st["clip"])):
            ui.get(name).set_style_text_color(
                lv.color_hex(0xE53935 if v else BORDER), 0)

    # ---- targeted updates (hot paths) ----
    def update_freq(self):
        ui, f = self.ui, self.p["f"]
        # The large digits are the listened selection.  The two scale labels belong
        # to the immutable captured RF window, whose centre is _axis_hz.  Therefore
        # an NCO-only move changes the DSP-supplied marker, not these labels and not
        # the spectrum/waterfall samples.  TESTER has a temporary listened selection
        # without changing the persisted VFO.
        selected = (self._lo_hz + self.be.fine_hz
                    if self.be.running and self._inj_on else f)
        axis = self._axis_hz if self.be.running else selected
        half = 12000 if self.be.running else 5000
        ui.get("freq-digits").set_text(self.fmt_freq(selected))
        ui.get("spec-lo").set_text(self.fmt_khz(axis - half))
        ui.get("spec-hi").set_text(self.fmt_khz(axis + half))
        band, label = self.band_of(selected)
        if band != self._band:
            self._band = band
            ui.get("band-value").set_text(label)

    def update_mode(self):
        ui, m = self.ui, self.p["m"]
        dead = None
        for name in MODES:
            b = ui.get("btn-" + name)
            on = name == m
            off = name == dead
            bg = BORDER if (on and off) else (GREEN if on else BTN_RX)
            b.set_style_bg_color(lv.color_hex(bg), 0)
            b.set_style_border_color(lv.color_hex(GREEN if on and not off else BORDER), 0)
            # dark text on the bright green, white on the dark inactive bg
            b.get_child(0).set_style_text_color(
                lv.color_hex(GRAY2 if off and not on else
                             (DARK_TXT if on else WHITE)), 0)
        bw_text = fmt_bw(self.cur_bw())
        ui.get("filter-value").set_text(bw_text)
        ui.get("btn-mode-filter").get_child(0).set_text(bw_text)
        self._update_home_summary()
        self._set_mode_bar(False)

    def _set_mode_bar(self, expanded):
        """HOME is the tuning row; expanded=True shows modulation choices."""
        if not expanded:
            self._mode_expanded = 0
            self.ui.get("mode-bar").add_flag(lv.obj.FLAG.HIDDEN)
            self.ui.get("tuning-row").remove_flag(lv.obj.FLAG.HIDDEN)
            self._update_home_summary()
            return

        self._mode_expanded = 1
        self.ui.get("tuning-row").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("mode-bar").remove_flag(lv.obj.FLAG.HIDDEN)
        for name in MODES:
            b = self.ui.get("btn-" + name)
            b.get_child(0).set_text(name)  # restore labels after FILTER choices
            visible = True
            hidden = b.has_flag(lv.obj.FLAG.HIDDEN)
            if visible and hidden:
                b.remove_flag(lv.obj.FLAG.HIDDEN)
            elif not visible and not hidden:
                b.add_flag(lv.obj.FLAG.HIDDEN)
        for name in ("mode-step", "mode-filter", "mode-view"):
            b = self.ui.get("btn-" + name)
            hidden = b.has_flag(lv.obj.FLAG.HIDDEN)
            if not hidden:
                b.add_flag(lv.obj.FLAG.HIDDEN)

    def _update_home_summary(self):
        """Current MODE | FILTER | STEP inside the permanent tuning row."""
        step = "?"
        for value, name in STEPS:
            if value == self.p["s"]:
                step = name
                break
        self.ui.get("home-summary").set_text("%s | %s | %s" % (
            self.p["m"], fmt_bw(self.cur_bw()), step))

    def open_home_choices(self):
        """Replace HOME tuning controls with MODE | FILTER | STEP buttons."""
        self._mode_expanded = 5
        self.ui.get("tuning-row").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("mode-bar").remove_flag(lv.obj.FLAG.HIDDEN)
        selected = self.p["m"]
        for name in MODES:
            b = self.ui.get("btn-" + name)
            b.get_child(0).set_text(name)
            if name == selected:
                b.remove_flag(lv.obj.FLAG.HIDDEN)
            else:
                b.add_flag(lv.obj.FLAG.HIDDEN)
        for name in ("mode-filter", "mode-step"):
            self.ui.get("btn-" + name).remove_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("btn-mode-view").add_flag(lv.obj.FLAG.HIDDEN)

    def set_mode(self, m):
        p = self.p
        p["m"] = m
        p["vfos"][p["act"]][1] = m   # active VFO state stays coherent before flash save
        self.update_mode()
        self.be.set_mode(m)
        self.be.set_bandwidth(self.cur_bw())
        # While TESTER owns the samples, HOME mode selection is deliberately a
        # demodulator comparison only.  Retuning the physical Si5351/normal low-IF
        # policy here would move the NCO out from under the unchanged test source.
        if (self.be.running and not self._inj_on and
                self._lo_hz != self._normal_lo_hz(self._requested_hz, m)):
            # Entering/leaving AM changes the physical low-IF policy.  Queue the
            # Si5351 write; _apply_hw_pending() atomically supplies the matching NCO.
            self._queue_station_recenter(self._requested_hz, mode=m)
        self._paint_output_status()
        self.touch_params()

    def update_step(self):
        ui, s = self.ui, self.p["s"]
        # The same physical controls have two deliberate meanings.  With an armed
        # TESTER source they move the baseband signal through the digital NCO; in
        # normal reception they tune RF/Si5351.  Keep that distinction visible.
        self._update_tuning_role()
        for sv, name in STEPS:
            if sv == s:
                # The collapsed bottom action shows the selected step itself;
                # tapping it opens the step choices directly.
                ui.get("btn-mode-step").get_child(0).set_text(name)
            chip = ui.get("step-" + name)
            on = sv == s
            chip.set_style_bg_color(lv.color_hex(CYAN_IN if on else BTN_IN), 0)
            chip.get_child(0).set_style_text_color(
                lv.color_hex(DARK_TXT if on else WHITE), 0)
        self._update_home_summary()

    def update_vol(self):
        # The far-right slot may currently represent RF/AGC/SQL.  A volume change
        # must not overwrite that selected control's value label.
        if self._active_gain == "AF":
            self.ui.get("vol-value").set_text("%d%%" % self.p["v"])

    def set_volume(self, percent):
        """AF master volume: store, push to the backend, refresh the label + save.
        The gains panel and the collapsed slider both route AF gain through here."""
        self.p["v"] = min(max(int(percent), 0), 100)
        self.be.set_volume(self.p["v"])
        self.update_vol()
        self.touch_params()

    AGC_BG = {"OFF": BORDER, "FAST": GREEN, "SLOW": GREEN, "MAN": CYAN_RX}

    def update_agc(self):
        ui, a = self.ui, self.p["a"]
        old_active = self._active_gain
        lbl = ui.get("agc-value")
        lbl.set_text(a)
        lbl.set_style_text_color(
            lv.color_hex(WHITE if a == "OFF" else DARK_TXT), 0)
        ui.get("agc-pill").set_style_bg_color(
            lv.color_hex(self.AGC_BG.get(a, BORDER)), 0)
        self._refresh_gains()
        if self._active_gain != old_active:
            self._bind_active_slider()

    def open_agc_menu(self):
        def pick(v):
            if self.be.set_agc(v, self.p["again"], self.p["atgt"]):
                self.p["a"] = v
                self.update_agc()
                self.touch_params()
        self.open_pick_menu("AGC", tuple((v, v) for v in AGC_MODES),
                            self.p["a"], pick)

    # ---- gains panel (AF / AGC / SQL plus the reserved ATT slot) ----
    def _gain_spec(self, key):
        """(lo, hi, cur_int, fmt) for a gain slider. fmt(v_int) -> label string.
        The slider works in integers; AGC packs gain*10 so 0.1 steps stay on-grid."""
        if key == "AF":
            return 0, 100, int(self.p["v"]), lambda v: "%d%%" % v
        if key == "SQL":
            return 0, 2000, int(self._squelch), lambda v: "%d" % v
        # AGC: slider integer = gain * 10 (float ~0.1..8.0 -> 1..80)
        return 1, 80, int(self.p["again"] * 10), lambda v: "x%.1f" % (v / 10.0)

    def _gain_available(self, key):
        """Whether a named slider has a truthful writable backend right now."""
        if key in ("AF", "SQL"):
            return True
        if key == "AGC":
            return self.p["a"] == "MAN"
        # ATT has no backend API.
        return False

    def _paint_gain_pin(self):
        """Paint the one shared pin checkbox in the normal VOL-value position."""
        pin = self.ui.get("gain-pin")
        if self._gain_candidate == self._active_gain:
            pin.add_state(lv.STATE.CHECKED)
        else:
            pin.remove_state(lv.STATE.CHECKED)
        self.ui.get("vol-label").set_text(
            "VOL" if self._gain_candidate == "AF" else self._gain_candidate)

    def _apply_gain(self, key, v_int):
        """Push a slider value to the backend + persist. AF routes through the
        existing set_volume so the vol-value label + save timer still fire."""
        if key == "AF":
            self.set_volume(v_int)
        elif key == "AGC":
            candidate = v_int / 10.0
            if self.be.set_agc(self.be.agc, gain=candidate):
                self.p["again"] = candidate
                self.touch_params()
        elif key == "SQL":
            self._squelch = int(v_int)
            iq = self.be.iq
            fn = getattr(iq, "squelch", None) if iq is not None else None
            if fn is not None:
                try:
                    fn(self._squelch)
                    self.be.err = None
                except Exception as e:
                    self.be.err = "squelch: %r" % (e,)

    def toggle_gains_panel(self):
        if "gains_panel" in _KEEP:
            self.close_gains_panel()
        else:
            self.open_gains_panel()

    def open_gains_panel(self):
        if "gains_panel" in _KEEP:
            return
        # This is an inline HOME replacement, not a second LVGL screen. Pause the
        # direct framebuffer writers before hiding their surface, then lazily build
        # the persistent slider strip inside the existing main column.
        self._set_mode_bar(False)
        self._set_modal(True)
        try:
            if self.ui.w.get("gains-inline") is None:
                self._build_gains()
        except Exception:
            partial = self.ui.w.pop("gains-inline", None)
            if partial is not None:
                try:
                    partial.delete()
                except Exception:
                    pass
            self._gain_widgets = {}
            self._gain_vlbls_all = {}
            self._gain_vlbls = {}
            self._gains_cbs = []
            self._set_modal(False)
            gc.collect()
            raise
        for name in ("frequency-display", "spectrum-area", "tuning-row", "mode-bar"):
            self.ui.get(name).add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("gains-inline").remove_flag(lv.obj.FLAG.HIDDEN)
        # The persistent slot remains visible but is read-only while its full-size
        # counterpart is open; this prevents two visible sliders from diverging.
        self.ui.get("vol-slider").add_state(lv.STATE.DISABLED)
        self._gain_candidate = self._active_gain
        self.ui.get("vol-value").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("gain-pin").remove_flag(lv.obj.FLAG.HIDDEN)
        self._paint_gain_pin()
        # Re-arm the live value labels (close clears the active mapping so the
        # status poll stays idle while normal HOME is visible).
        self._gain_vlbls = self._gain_vlbls_all
        _KEEP["gains_panel"] = True  # open-flag: _consume_status refreshes AGC live gain
        self._refresh_gains()

    def close_gains_panel(self):
        # Restore the HOME children in place. No screen_load and no second screen tree.
        _KEEP.pop("gains_panel", None)
        panel = self.ui.w.get("gains-inline")
        if panel is not None:
            panel.add_flag(lv.obj.FLAG.HIDDEN)
        for name in ("frequency-display", "spectrum-area"):
            self.ui.get(name).remove_flag(lv.obj.FLAG.HIDDEN)
        self._set_mode_bar(False)
        self.ui.get("vol-slider").remove_state(lv.STATE.DISABLED)
        self.ui.get("gain-pin").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("vol-value").remove_flag(lv.obj.FLAG.HIDDEN)
        self._gain_vlbls = {}        # poll loop only touches these while the view is up
        self._set_modal(("settings" in _KEEP) or ("pick_menu" in _KEEP))
        self._bind_active_slider()

    def _build_gains(self):
        # Replace only the 400x226 HOME body below SDR RECEIVER.  Four columns are
        # packed against its RIGHT edge, immediately beside the existing 56-px
        # right-side slot, which stays in place. AF and SQL are writable; AGC gain
        # is writable only in MAN mode. ATT remains N/A because there is no
        # attenuator backend API.
        panel = _base(lv.obj(self.ui.get("main-column")))
        panel.set_size(400, 226)
        panel.set_style_pad_all(0, 0)
        _flex(panel, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.END,
              lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 8)
        panel.add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.w["gains-inline"] = panel

        cbs = []
        self._gains_cbs = cbs        # keep every event cb alive for the panel lifetime
        self._gain_vlbls_all = {}

        for key in ("AF", "AGC", "SQL", "ATT"):
            supported = key in ("AF", "AGC", "SQL")
            live = self._gain_available(key)
            col = _flex(_base(lv.obj(panel)), lv.FLEX_FLOW.COLUMN,
                        lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER,
                        lv.FLEX_ALIGN.CENTER, 6)
            col.set_size(72, 218)
            _lbl(col, key, 14, CYAN_RX if supported else GRAY2)
            sl = lv.slider(col)
            # Same physical height as the permanent far-right slider.  The remaining
            # column space is reserved for value + one-of checkbox.
            sl.set_size(16, 130)
            sl.set_style_bg_color(lv.color_hex(PANEL2), lv.PART.MAIN)
            sl.set_style_bg_opa(lv.OPA.COVER, lv.PART.MAIN)
            sl.set_style_border_color(lv.color_hex(BORDER), lv.PART.MAIN)
            sl.set_style_border_width(2, lv.PART.MAIN)
            sl.set_style_radius(8, lv.PART.MAIN)
            sl.set_style_bg_color(lv.color_hex(CYAN_RX), lv.PART.INDICATOR)
            # Indicator radius MUST be >= MAIN radius, else lv_bar renders the indicator
            # through a temporary ARGB8888 layer that fails to allocate at 100% (see the
            # long note on the collapsed vol-slider). Keep both radii equal at 8.
            sl.set_style_radius(8, lv.PART.INDICATOR)
            sl.set_style_bg_opa(lv.OPA.TRANSP, lv.PART.KNOB)
            sl.set_style_pad_all(0, lv.PART.KNOB)
            if supported:
                lo, hi, cur, fmt = self._gain_spec(key)
                sl.set_range(lo, hi)
                sl.set_value(cur, False)
                vlbl = _lbl(col, fmt(cur), 12, WHITE)
                self._gain_vlbls_all[key] = vlbl
                self._gain_widgets[key] = (sl, vlbl)

                def gain_cb(e, kk=key, s=sl, vl=vlbl):
                    v = s.get_value()
                    self._apply_gain(kk, v)
                    _lo, _hi, applied, f = self._gain_spec(kk)
                    if applied != v:
                        s.set_value(applied, False)
                    vl.set_text(f(applied))
                    self._gain_candidate = kk
                    self._paint_gain_pin()
                # The knob moves natively during drag; apply once at release. This
                # avoids the former Python callback/repaint burst on every touch pixel.
                sl.add_event_cb(gain_cb, lv.EVENT.RELEASED, None)
                sl.add_event_cb(gain_cb, lv.EVENT.PRESS_LOST, None)
                cbs.append(gain_cb)
                if not live:
                    sl.add_state(lv.STATE.DISABLED)
            else:
                sl.add_state(lv.STATE.DISABLED)
                _lbl(col, "N/A", 12, GRAY2)

    def _refresh_gains(self):
        """Re-read AF/AGC/SQL into the inline sliders + value labels.
        Called on every open so a re-shown panel never displays the stale
        values captured at build time."""
        if not self._gain_available(self._active_gain):
            self._active_gain = "AF"
        if not self._gain_available(self._gain_candidate):
            self._gain_candidate = self._active_gain
        for key, (sl, vlbl) in self._gain_widgets.items():
            _lo, _hi, cur, fmt = self._gain_spec(key)
            sl.set_value(cur, False)
            vlbl.set_text(fmt(cur))
            if self._gain_available(key):
                sl.remove_state(lv.STATE.DISABLED)
            else:
                sl.add_state(lv.STATE.DISABLED)
        if "gains_panel" in _KEEP:
            self._paint_gain_pin()

    def _bind_active_slider(self):
        """Show the checkbox-selected gain in the permanent far-right slot."""
        lo, hi, cur, fmt = self._gain_spec(self._active_gain)
        s = self.ui.get("vol-slider")
        s.set_range(lo, hi)
        s.set_value(cur, False)
        self.ui.get("vol-label").set_text(
            "VOL" if self._active_gain == "AF" else self._active_gain)
        self.ui.get("vol-value").set_text(fmt(cur))

    def update_entry_digits(self):
        # show the buffer verbatim -- empty means empty (cursor only), no
        # confusing fallback to the current frequency
        self.ui.get("input-digits").set_text(self._group(self.entry))

    def update_entry_bands(self):
        ui = self.ui
        band, _ = self.band_of(self._entry_hz() if self.entry else self.p["f"])
        for name, _l, _lo, _hi, _b in BANDS:
            b = ui.get("band-" + name)
            on = name == band
            b.set_style_bg_color(lv.color_hex(CYAN_IN if on else BTN_IN), 0)
            b.get_child(0).set_style_text_color(
                lv.color_hex(DARK_TXT if on else WHITE), 0)

    # ---- full sync (init / screen switches only) ----
    def apply_all(self):
        self._band = None            # force band label refresh
        self.paint_spectrum()
        self.update_freq()
        self.update_mode()
        self.update_step()
        self.ui.get("vol-slider").set_value(self.p["v"], False)
        self.update_vol()
        self.update_agc()
        self.update_rx()
        self.update_vfo_ui()
        self.update_entry_digits()
        self.update_entry_bands()

    # ---- persistence (debounced) ----
    def touch_params(self):
        if self.save_timer:
            self.save_timer.set_period(SAVE_DELAY_MS)
            self.save_timer.reset()
        else:
            def _cb(t):
                # repeat_count(1) => LVGL auto-deletes the timer after this cb
                self.save_timer = None
                try:
                    save_params(self.p)
                except Exception as e:
                    print("SDR save_params:", repr(e))
            self.save_timer = lv.timer_create(_cb, SAVE_DELAY_MS, None)
            self.save_timer.set_repeat_count(1)

    def paint_spectrum(self):
        if self._spec_native:
            out = self._demo_bars
            for i in range(len(out)):
                out[i] = self.spec[i]
            self._spec_lcd.spectrum_update(out)
            return
        # Batch-update 27 bins as a SINGLE invalidated area: 27 height changes
        # add ~54 invalid areas, overflow LVGL's inv buffer (32) and trigger a
        # FULL-SCREEN refresh -> visible blink. Suppress per-widget invalidation
        # and invalidate just the waterfall container once instead.
        n = len(self.spec)
        c = n // 2
        dd = lv.display_get_default()
        gated = hasattr(dd, "enable_invalidation")
        if gated:
            dd.enable_invalidation(False)
        try:
            for i, b in enumerate(self.ui.bins):
                if i == c:
                    # center marker: always lit, never shorter than 24px
                    b.set_height(max(self.spec[i], 24))
                    b.set_style_bg_color(lv.color_hex(CYAN_RX), 0)
                else:
                    b.set_height(self.spec[i])
                    b.set_style_bg_color(lv.color_hex(BORDER), 0)
        finally:
            if gated:
                dd.enable_invalidation(True)
        self.ui.get("spectrum-waterfall").invalidate()

    def _paint_bars(self, b):
        """ZERO-ALLOC live waterfall repaint from the int16 height array b (0..50 from
        the C reducer). Touches only bars that changed >= THRESH px; invalidates the
        waterfall once only if something changed. Reads array('h')[i] -> tagged small
        int (no heap), set_height -> C. No Python object is created.

        The firmware reducer keeps the 512-bin pre-NCO panorama fixed on its physical
        RF axis. Python paints those reduced heights directly; only native firmware can
        add the independent high-resolution NCO marker."""
        if self._spec_native:
            self._spec_lcd.spectrum_update(b)
            return

        THRESH = 3
        bins = self.ui.bins
        n = len(bins)
        prev = self._last_bars
        dd = self._dd                        # cached: no wrapper alloc in the loop
        gated = self._gated
        if not self._spec_marked:            # centre bar = tuned freq (paint once, persists)
            self._spec_marked = True
            bins[n // 2].set_style_bg_color(self._C_CYAN, 0)
        changed = False
        if gated:
            dd.enable_invalidation(False)
        try:
            for i in range(n):
                h = b[i]
                d = h - prev[i]
                if d >= THRESH or d <= -THRESH:
                    bins[i].set_height(h if h > 2 else 2)
                    prev[i] = h
                    changed = True
        finally:
            if gated:
                dd.enable_invalidation(True)
        if changed:
            self.ui.get("spectrum-waterfall").invalidate()

    def _consume_status(self):
        """2 Hz status consumer; HOME is alloc-free and VERIFY is change-only."""
        if not self.be.running:
            return
        try:
            c = self.be.read_counters()
            if c is not None:
                pc = self._pc
                # Live block counter (proves packets are flowing). Written into a fixed
                # bytearray + set_text_static -> zero Python string per update.
                if c[0] != pc[0]:
                    pc[0] = c[0]
                    b = self._blk_buf                 # write digits in place, no str
                    v = c[0]
                    i = 13
                    if v <= 0:
                        b[i] = 0x30
                        i -= 1
                    else:
                        while v > 0 and i >= 4:
                            b[i] = 0x30 + (v % 10)
                            v //= 10
                            i -= 1
                    while i >= 4:
                        b[i] = 0x20                   # left-pad with spaces
                        i -= 1
                    blk = self.ui.get("sdr-blk")
                    blk.set_text_static(b)            # pointer, no copy -> 0 alloc
                    if not self._blk_lit:
                        self._blk_lit = True
                        blk.set_style_text_color(self._C_CYAN, 0)
                # Live warnings: red only while a counter is actively GROWING (a real
                # event this poll), not merely nonzero -- a startup underrun/clip
                # transient would otherwise pin the indicator red forever. OVR uses the
                # REAL ring_overruns (c[4]); c[1] acquire-overruns grow by design (the
                # app uses the autonomous DAC stream, not acquire()) and are ignored.
                g = c[4] > pc[4]
                if g != self._ovr_red:
                    self._ovr_red = g
                    self.ui.get("sdr-ovr").set_style_text_color(
                        self._C_RED if g else self._C_BORDER, 0)
                g = c[3] > pc[3]
                if g != self._und_red:
                    self._und_red = g
                    self.ui.get("sdr-und").set_style_text_color(
                        self._C_RED if g else self._C_BORDER, 0)
                g = c[5] > pc[5]
                if g != self._clip_red:
                    self._clip_red = g
                    self.ui.get("sdr-clip").set_style_text_color(
                        self._C_RED if g else self._C_BORDER, 0)
                pc[3] = c[3]
                pc[4] = c[4]
                pc[5] = c[5]
            self._poll_iq_file()
            # Live AGC readout, only while the gains panel is open and showing AGC. Auto-AGC
            # moves the gain, so the label tracks the real value; guarded so the normal
            # (panel-closed) loop stays alloc-free.
            if "gains_panel" in _KEEP and "AGC" in self._gain_vlbls:
                g = self.be.agc_gain_now()
                if g is not None:
                    self._gain_vlbls["AGC"].set_text("x%.1f" % g)
            # VERIFY live read-outs are outside the scroll subtree.  Paint them
            # change-only and suspend them during an active drag so a status tick
            # cannot compete with the touch/scroll frame.  The SQUELCH row itself is
            # deliberately NOT updated here: its live envelope changes every poll,
            # forcing a flex/layout pass inside the list every 500 ms.  The row shows
            # the configured threshold and is updated by its control callback/open.
            if "settings" in _KEEP and self.be.iq is not None:
                rows = self._set_widgets.get("rows")
                if (self._set_scroll_gate or self._set_scroll_quiet or
                        (rows is not None and rows.is_scrolling())):
                    return
                live = self._set_live_cache
                lb = self._set_lbls.get("agc")
                if lb is not None:
                    try:
                        g = self.be.iq.agc_status().get("gain")
                        if g is not None:
                            gv = int(float(g) * 10.0 + 0.5)
                            if gv != live[0]:
                                live[0] = gv
                                lb.set_text("x%.1f" % (gv / 10.0))
                    except Exception:
                        pass
                lb = self._set_lbls.get("smeter")
                if lb is not None:
                    try:
                        sm = self.be.iq.smeter()
                        rms = int(sm.get("rms", 0))
                        dbfs = int(sm.get("dbfs", 0))
                        if rms != live[1] or dbfs != live[2]:
                            live[1] = rms
                            live[2] = dbfs
                            lb.set_text("%d / %ddBFS" % (rms, dbfs))
                    except Exception:
                        pass
                lb = self._set_lbls.get("timing")
                if lb is not None:
                    try:
                        tm = self.be.iq.timing()
                        window_valid = tm.get("window_valid", None)
                        if window_valid is None:
                            # Compatibility with an older firmware image: its only
                            # percentage is the legacy since-start high-water mark.
                            valid = 1
                            generation = -1
                            avg_pct = int(float(tm.get("max_pct", 0.0)) + 0.5)
                            peak_pct = avg_pct
                        else:
                            valid = 1 if window_valid else 0
                            generation = int(tm.get("window_generation", 0))
                            if valid:
                                avg_pct = int(tm.get("avg_pct", 0))
                                peak_pct = int(tm.get("peak_pct", 0))
                            else:
                                avg_pct = -1
                                peak_pct = -1
                        if (valid != live[3] or generation != live[4] or
                                avg_pct != live[5] or peak_pct != live[6]):
                            live[3] = valid
                            live[4] = generation
                            live[5] = avg_pct
                            live[6] = peak_pct
                            lb.set_text("%d/%d%%" % (avg_pct, peak_pct)
                                        if valid else "--/--")
                    except Exception:
                        pass
                lb = self._set_lbls.get("iqdc")
                if lb is not None:
                    try:
                        ds = self.be.iq.dsp_status()
                        di = int(ds.get("i_mean", 2048)) - 2048
                        dq = int(ds.get("q_mean", 2048)) - 2048
                        if di != live[7] or dq != live[8]:
                            live[7] = di
                            live[8] = dq
                            lb.set_text("I%+d  Q%+d" % (di, dq))
                    except Exception:
                        pass
        except Exception as e:
            self.be.err = "status: %r" % (e,)

    def _consume_spectrum(self):
        """Fallback Python spectrum consumer for firmware without the native surface.

        The native surface consumes the existing DSP FFT buffer itself: 10 Hz for
        phased bars or 30 Hz for direct-framebuffer waterfall.  Do not race that C
        consumer for the single ready snapshot here."""
        if self._spec_native:
            return
        if not self.be.running or self._modal:
            return                   # don't repaint the spectrum under an overlay
        try:
            b = self.be.read_bars()
            if b is not None:
                self._paint_bars(b)
        except Exception as e:
            self.be.err = "spectrum: %r" % (e,)

    def shift_spectrum(self, delta_hz):
        """Scroll the (demo) spectrum pattern so the display tracks tuning:
        positive delta -> content moves left, tuned signal stays centered. Sub-bar
        frequency steps accumulate instead of being rounded to zero on every tap."""
        if self.be.running:
            return            # live FFT already reflects tuning; demo scroll would only
            #                   allocate a new list and fight the real _paint_bars
        n = len(self.spec)
        acc = self._demo_shift_acc + int(delta_hz) * n
        if acc >= 5000:
            shift = (acc + 5000) // 10000
        elif acc <= -5000:
            shift = -((-acc + 5000) // 10000)
        else:
            shift = 0
        self._demo_shift_acc = acc - shift * 10000
        if shift:
            k = shift % n
            self.spec = self.spec[k:] + self.spec[:k]
            self.paint_spectrum()

    def _request_spectrum_generation(self, hz):
        """Ask C to tag future FFT frames with a new physical-LO generation."""
        fn = self._spectrum_center_fn
        if fn is None or not self._spectrum_generation_api:
            return 0
        try:
            token = int(fn(int(hz)))
            if token > 0:
                return token
            raise ValueError("invalid generation token %d" % token)
        except Exception as e:
            self._spectrum_generation_api = False
            if getattr(self, "be", None) is not None:
                self.be.err = "spectrum_center: %r" % (e,)
            return 0

    def _request_tester_generation(self):
        """Supersede any pre-source FFT request with one source-derived frame."""
        if not self.be.running or not self._spectrum_generation_api:
            return 0
        # A station/VFO transaction may already be awaiting a publishable frame.
        # Carry that identity to the replacement token; only the data boundary is
        # restarted after GEN/FILE and its final NCO have been established.
        station = self._axis_pending_station_hz
        vfo = self._axis_pending_vfo
        mode = self._axis_pending_mode
        token = self._request_spectrum_generation(self._lo_hz)
        if token > 0:
            self._axis_pending_token = token
            self._axis_pending_hz = self._lo_hz
            self._axis_pending_station_hz = station
            self._axis_pending_vfo = vfo
            self._axis_pending_mode = mode
        return token

    def _finish_axis_publish(self, axis_hz, station_hz, vfo, mode):
        """Put the matching axis/selection into LVGL object state before C draws."""
        self._axis_hz = int(axis_hz)
        if vfo is not None:
            self._commit_vfo_switch(vfo, station_hz, mode)
        elif station_hz is not None:
            self._commit_current_frequency(station_hz)
        else:
            self.update_freq()

    def _axis_publish_snapshot(self):
        """Capture the small Python/backend state touched by an axis publication."""
        p = self.p
        return (self._axis_hz, self._requested_hz, self._band,
                p["f"], p["m"], p["act"],
                tuple((row[0], row[1]) for row in p["vfos"]),
                tuple(self._alt) if getattr(self, "_alt", None) is not None else None,
                self.be.mode, self.be.bw)

    def _restore_axis_publish(self, snapshot):
        """Restore an interrupted LVGL label transaction or fail explicitly.

        Core values are restored before any fallible backend/UI work.  Every backend
        rejection and repaint exception is retained; callers must not mistake a
        partial rollback for the old axis being visible and safe to retry.
        """
        (axis_hz, requested_hz, _band, freq, mode, active,
         vfos, alt, be_mode, be_bw) = snapshot
        p = self.p
        self._axis_hz = axis_hz
        self._requested_hz = requested_hz
        self._band = _band
        p["f"] = freq
        p["m"] = mode
        p["act"] = active
        for i in range(len(vfos)):
            p["vfos"][i][0] = vfos[i][0]
            p["vfos"][i][1] = vfos[i][1]
        if alt is None:
            self._alt = None
        elif self._alt is None:
            self._alt = [alt[0], alt[1]]
        else:
            self._alt[0] = alt[0]
            self._alt[1] = alt[1]

        # Bit 0 is collection, bits 1/2 are backend mode/bandwidth and bits 3..7
        # are the five UI rebuilds below.  Keep the first concrete exception or
        # rejection as evidence while still attempting every independent repair.
        failed = 0
        first_failure = None
        try:
            gc.collect()
        except Exception as rollback_error:
            failed |= 1
            first_failure = rollback_error
        try:
            if not self.be.set_mode(be_mode):
                failed |= 2
                if first_failure is None:
                    first_failure = "backend mode rejected"
        except Exception as rollback_error:
            failed |= 2
            if first_failure is None:
                first_failure = rollback_error
        try:
            if not self.be.set_bandwidth(be_bw):
                failed |= 4
                if first_failure is None:
                    first_failure = "backend bandwidth rejected"
        except Exception as rollback_error:
            failed |= 4
            if first_failure is None:
                first_failure = rollback_error

        # Force the band label too: it may already have been changed before the
        # exception, so restoring the cached old value would suppress its repaint.
        self._band = None
        try:
            self.update_freq()
        except Exception as rollback_error:
            failed |= 8
            if first_failure is None:
                first_failure = rollback_error
        try:
            self.update_mode()
        except Exception as rollback_error:
            failed |= 16
            if first_failure is None:
                first_failure = rollback_error
        try:
            self.update_vfo_ui()
        except Exception as rollback_error:
            failed |= 32
            if first_failure is None:
                first_failure = rollback_error
        try:
            self.update_entry_digits()
        except Exception as rollback_error:
            failed |= 64
            if first_failure is None:
                first_failure = rollback_error
        try:
            self.update_entry_bands()
        except Exception as rollback_error:
            failed |= 128
            if first_failure is None:
                first_failure = rollback_error
        # The cache is transaction state too.  Restore it even if update_freq()
        # changed it before another label setter failed.
        self._band = _band
        if failed:
            raise RuntimeError("axis rollback incomplete mask=0x%02x first=%r" %
                               (failed, first_failure))

    def _poll_axis_generation(self):
        """Publish one matching axis/data generation in a single LVGL transaction."""
        token = self._axis_pending_token
        fn = self._spectrum_center_fn
        publish = self._spectrum_publish_fn
        if (token <= 0 or fn is None or publish is None or
                not self._spectrum_generation_api):
            return False
        try:
            state = int(fn())
        except Exception as e:
            self._spectrum_generation_api = False
            self.be.err = "spectrum generation read: %r" % (e,)
            return False

        if state == 0:
            # IQADC reconstruction can clear C's pending request.  Re-arm the same
            # physical axis rather than publishing an untagged/partial frame.
            new_token = self._request_spectrum_generation(self._axis_pending_hz)
            if new_token > 0:
                self._axis_pending_token = new_token
            return False
        if state != token:
            # -token is the expected in-flight state.  A different positive token
            # is stale/foreign and must never publish this Python request.
            return False

        axis_hz = self._axis_pending_hz
        station_hz = self._axis_pending_station_hz
        vfo = self._axis_pending_vfo
        mode = self._axis_pending_mode
        # C has staged, but not exposed, the first complete matching FFT.  Arming
        # only queues a one-pixel invalidation; LVGL cannot render it until this
        # Python callback yields or calls refr_now().  Therefore it is safe to arm,
        # update every label object, and then run the one transaction whose
        # RENDER_READY callback commits the graph.
        armed = False
        snapshot = None
        try:
            # Snapshot allocation happens before C is armed; failure here cannot
            # expose either a new graph or a partially committed Python selection.
            snapshot = self._axis_publish_snapshot()
            if not publish(token):
                self.be.err = "spectrum publish rejected %d" % token
                return False
            armed = True
            self._finish_axis_publish(axis_hz, station_hz, vfo, mode)
            lv.refr_now(self._dd)
            if int(publish()) != token:
                raise RuntimeError("spectrum render did not commit %d" % token)
            # Forget the transaction only after C confirms that RENDER_READY drew
            # the staged graph under the matching Python label state.
            self._clear_axis_pending()
        except Exception as e:
            try:
                committed = int(publish()) == token
            except Exception:
                committed = False
            if committed:
                # refr_now() may have completed the transaction before surfacing an
                # unrelated binding exception.  C is authoritative in that case.
                self._clear_axis_pending()
                self.be.err = "spectrum publish completed with: %r" % (e,)
                return True
            abort_error = None
            if armed:
                try:
                    if not publish(-token):
                        abort_error = "C abort rejected"
                except Exception as rollback_error:
                    abort_error = rollback_error
            restore_error = None
            if snapshot is not None:
                try:
                    self._restore_axis_publish(snapshot)
                except Exception as rollback_error:
                    restore_error = rollback_error
            if abort_error is not None or restore_error is not None:
                raise RuntimeError(
                    "spectrum publish failed=%r rollback abort=%r restore=%r" %
                    (e, abort_error, restore_error))
            self.be.err = "spectrum publish: %r" % (e,)
            return False
        return True

    # ---- tuning ----
    def _tester_before_nco(self):
        return self._inj_point == 0 or (
            self._inj_point == 1 and self._inj_mid <= 1)

    def _update_tuning_role(self):
        enabled = self._blk_on.get(4, True) and (
            (not self._inj_on) or self._tester_before_nco())
        color = self._C_CYAN if enabled else self._C_GRAY2
        for name in ("btn-step-down", "btn-step-up", "btn-fine-down", "btn-fine-up"):
            self.ui.get(name).get_child(0).set_style_text_color(color, 0)

    def _tester_nco_set(self, wanted):
        """Set TESTER's temporary NCO, with point and BYP validation."""
        if not self._inj_on:
            return False

        # IN and the movable IQC/NCO boundaries enter before the NCO.  CHF and OUT
        # enter after it, so changing iq.tune() cannot move those sources.
        if not self._tester_before_nco():
            self._update_tuning_role()
            return True

        # Read the real native bypass state on every operator command.  The Python
        # cache is only a fallback for older firmware without block(id) readback.
        nco_on = self._blk_on.get(4, True)
        iq = self.be.iq
        block_fn = getattr(iq, "block", None) if iq is not None else None
        if block_fn is not None:
            try:
                nco_on = bool(block_fn(4))
                self._blk_on[4] = nco_on
            except Exception:
                pass
        if not nco_on:
            self._update_tuning_role()
            return True

        value = self.be.set_fine(wanted)
        if value is None:
            return True
        self._update_tuning_role()
        self.update_freq()
        # VERIFY's NCO readout can coexist only while that transient screen is open.
        nco_label = self._set_widgets.get("nco")
        if nco_label is not None:
            nco_label.set_text("%d" % value)
        return True

    def _tester_nco_tune(self, delta):
        """Move an armed TESTER source relative to its current listened centre.

        Returns True when TESTER owned the command, including a truthful rejected
        command.  False means normal receiver tuning should handle the button.
        """
        if not self._inj_on:
            return False
        return self._tester_nco_set(self.be.fine_hz + int(delta))

    def _nco_enabled(self):
        """Read the real block-4 bypass state; cached True supports old firmware."""
        enabled = self._blk_on.get(4, True)
        iq = self.be.iq
        block_fn = getattr(iq, "block", None) if iq is not None else None
        if block_fn is not None:
            try:
                enabled = bool(block_fn(4))
                self._blk_on[4] = enabled
            except Exception:
                pass
        self._update_tuning_role()
        return enabled

    def _set_live_nco_absolute(self, wanted, exact=True):
        """Try one absolute RF selection using only the current physical LO.

        The helper does not mutate p["f"].  With exact=True a native clamp is rolled
        back; exact=False accepts the clamped edge for spectrum tap-to-tune.
        """
        wanted = min(max(int(wanted), F_MIN), F_MAX)
        old_fine = self.be.fine_hz
        actual = self.be.set_fine(wanted - self._lo_hz)
        if actual is None:
            return None
        selected = min(max(self._lo_hz + actual, F_MIN), F_MAX)
        if selected == wanted or not exact:
            return selected

        restored = self.be.set_fine(old_fine)
        if restored is None:
            # The failed rollback leaves `actual` as the last confirmed NCO result.
            self.be.fine_hz = actual
            self._commit_current_frequency(selected)
            live_mode = self.p["m"]
            if (self._axis_pending_token and
                    self._axis_pending_mode is not None):
                live_mode = self._axis_pending_mode
            if self._nco_recenter_due(selected, live_mode):
                self._queue_nco_recenter()
        return None

    def _accept_live_nco_selection(self, selected, vfo=None, mode=None):
        """Publish or stage one NCO-only selection without moving the RF axis."""
        selected = min(max(int(selected), F_MIN), F_MAX)
        self._cancel_frequency_pending()
        self._requested_hz = selected
        if self._axis_pending_token:
            # Physical LO is already real but its tagged frame is not visible yet.
            # Keep the latest marker/selection attached to that pending generation.
            self._axis_pending_station_hz = selected
            if vfo is not None:
                self._axis_pending_vfo = vfo
                self._axis_pending_mode = mode
            return
        if vfo is not None:
            self._commit_vfo_switch(vfo, selected, mode)
        else:
            self._commit_current_frequency(selected)

    def _select_live_nco_only(self, wanted):
        """Select inside the current capture window; never queue a VFO retune."""
        if not self._nco_enabled():
            return False
        selected = self._set_live_nco_absolute(wanted, exact=False)
        if selected is None:
            return False
        self._accept_live_nco_selection(selected)
        return True

    def _move_live_frequency(self, delta):
        """Move the persisted station through the current 24-kHz panorama.

        The touch callback performs only the native NCO write.  Near the edge it
        queues an LO re-centre; the 100-ms worker performs that I2C transaction.
        """
        if not self._nco_enabled():
            return False
        old = self._requested_hz
        wanted = min(max(old + int(delta), F_MIN), F_MAX)
        if wanted == old:
            return False

        # A required physical policy/routing change may already be queued but not
        # applied.  Supersede its station target instead of cancelling it with a
        # direct write to the still-old NCO/LO pair.
        if self._station_pending_hz is not None:
            self._queue_station_recenter(wanted)
            return True

        # p["m"] is intentionally stale until a matching generation is visible.
        # During that short window the already-applied axis mode owns the safe NCO
        # edge (FM 7 kHz versus the normal 9 kHz limit).
        live_mode = self.p["m"]
        if self._axis_pending_token and self._axis_pending_mode is not None:
            live_mode = self._axis_pending_mode

        # Do not briefly place a wide FM channel across the 12-kHz Nyquist edge.
        # Coarse steps and the final FM edge step go straight to the deferred
        # physical-LO recenter instead of first programming an unsafe NCO value.
        if self._nco_recenter_due(wanted, live_mode):
            self._queue_station_recenter(wanted)
            return True

        actual = self.be.set_fine(wanted - self._lo_hz)
        if actual is None:
            return False
        selected = min(max(self._lo_hz + actual, F_MIN), F_MAX)
        if selected == old:
            # Recover even if an older run reached the native +/-11,999 Hz clamp
            # before it managed to queue the physical-LO recenter.
            if self._nco_recenter_due(selected, live_mode):
                self._queue_nco_recenter()
                return True
            return False

        # Every achieved direct move supersedes any older frequency request.  This
        # includes a plain recenter: reversing below 9 kHz before sdr_poll must cancel
        # its stale target.  An independent route/CAL refresh remains queued.
        self._accept_live_nco_selection(selected)
        if self._nco_recenter_due(selected, live_mode):
            self._queue_nco_recenter()
        return True

    def tune(self, delta):
        if self._tester_nco_tune(delta):
            return
        # While RX is live both arrow pairs navigate the SAME captured panorama:
        # << / >> use the selected coarse step and - / + use one tenth of it.  An
        # in-window NCO step moves only the marker/filter; the FFT, waterfall and RF
        # scale remain fixed.  The worker recentres the physical LO only at the edge.
        if self.be.running:
            self._move_live_frequency(delta)
            return

        # With RX stopped there is no NCO producer, so preserve the normal coarse
        # LO selection for the next start.
        old = self.p["f"]
        selected = min(max(old + int(delta), F_MIN), F_MAX)
        if selected == old:
            return
        self.shift_spectrum(selected - old)
        self._commit_current_frequency(selected)
        # RX is down, so the selected setting can commit immediately; _lo_hz still
        # remains the last physical LO until the worker reports set_freq success.
        self._cancel_frequency_pending()
        self._lo_pending_hz = self._normal_lo_hz(selected)
        self._station_pending_hz = selected
        self._hw_pending = True

    def fine(self, delta):
        if self._tester_nco_tune(delta):
            return
        # fine: digital NCO within the capture window while the backend is live
        # (no I2C, no LO move); otherwise a small coarse step so the UI still tunes.
        if self.be.running:
            self._move_live_frequency(delta)
        else:
            self.tune(delta)

    # ---- keypad entry ----
    # entry buffer = DIGITS ONLY, positional fixed grid "MM MMM MMH" filled
    # left-to-right and zero-padded on the right (radio style):
    # "00212200" -> 00 212 200 -> 212 200 Hz; 9 digits -> "144 300 000".
    @staticmethod
    def _group(d):
        a = 2 if len(d) <= 8 else 3
        return " ".join(x for x in (d[:a], d[a:a + 3], d[a + 3:a + 6]) if x)

    def _entry_hz(self):
        d = self.entry
        if not d:
            return self.p["f"]
        a = 2 if len(d) <= 8 else 3
        d = (d + "0" * (a + 6))[:a + 6]
        return int(d[:a]) * 1_000_000 + int(d[a:a + 3]) * 1000 + int(d[a + 3:])

    def key(self, k):
        if k == "BS":
            self.entry = self.entry[:-1]
        elif k != ".":                     # separators are automatic now
            if len(self.entry) < 9:
                self.entry += k
        self.update_entry_digits()
        self.update_entry_bands()

    # ---- 3x VFO ----
    def update_vfo_ui(self):
        ui = self.ui
        ui.get("vfo-indicator").set_text("VFO " + "ABC"[self.p["act"]])
        if getattr(self, "_alt", None) is None:
            self._alt = [i for i in (0, 1, 2) if i != self.p["act"]]
        for k in (0, 1):
            i = self._alt[k]
            ui.get("vfo-alt-%d" % k).set_text(
                "%s %s" % ("ABC"[i], self.fmt_freq(self.p["vfos"][i][0])))
        for i, tag in enumerate(("a", "b", "c")):
            b = ui.get("vfo-" + tag)
            on = i == self.p["act"]
            b.set_style_bg_color(lv.color_hex(PANEL if on else VFO_TRACK), 0)
            lbl = b.get_child(0)
            lbl.set_style_text_color(lv.color_hex(VFO_INK if on else GRAY2), 0)
            f = self.p["f"] if on else self.p["vfos"][i][0]
            lbl.set_text("%s %s" % ("ABC"[i], self.fmt_freq(f)))

    def switch_vfo(self, i):
        p = self.p
        # Once a physical retune has succeeded, its VFO/mode are the effective
        # routing policy even though p[] remains deliberately unpublished until
        # the matching FFT generation is visible.  A second VFO press in that
        # interval must compare against this in-flight physical identity.  This
        # also lets the old published VFO supersede a still-pending new selection.
        current_vfo = p["act"]
        current_mode = p["m"]
        if self.be.running and self._axis_pending_token:
            if self._axis_pending_vfo is not None:
                current_vfo = self._axis_pending_vfo
            if self._axis_pending_mode is not None:
                current_mode = self._axis_pending_mode
        if i == current_vfo:
            return

        target, mode = p["vfos"][i]
        old_f = p["f"]

        if self.be.running:
            # Compare both policies at one common RF frequency.  This isolates
            # the required physical 0/+3k/+6k low-IF offset from station delta.
            same_low_if = (self._normal_lo_hz(target, current_mode) ==
                           self._normal_lo_hz(target, mode))
            # A nearby VFO is achievable immediately with the native NCO.  A distant
            # VFO is queued and the current VFO remains visibly active until the
            # worker confirms the Si5351 transaction.  A mode-policy transition
            # (zero IF <-> AM +3 kHz <-> FM +6 kHz) must also take that physical
            # path even when both selected station frequencies are nearby.
            if (same_low_if and self._station_pending_hz is None and
                    p["rt"][i] == p["rt"][current_vfo] and self._nco_enabled() and
                    abs(int(target) - self._lo_hz) < (IQ_RATE // 4)):
                selected = self._set_live_nco_absolute(target)
                if selected is not None:
                    self._accept_live_nco_selection(selected, i, mode)
                    if self._nco_recenter_due(selected, mode):
                        self._queue_nco_recenter()
                    return
            self._queue_station_recenter(target, i, mode)
            return

        # With RX stopped, selection is a configuration change rather than a claim
        # about a live NCO.  Commit the VFO now but let the worker establish _lo_hz.
        self.shift_spectrum(int(target) - old_f)
        self._commit_vfo_switch(i, target, mode)
        self._cancel_frequency_pending()
        self._lo_pending_hz = self._normal_lo_hz(target, mode)
        self._hw_pending = True

    # ---- spectrum tap-to-tune ----
    def spec_jump(self, frac):
        """Select the tapped absolute point in the visible physical RF window.

        Tap-to-tune is deliberately NCO-only.  It never asks Si5351 to move and
        therefore never shifts/relabels the FFT or retained waterfall history.
        """
        frac = min(max(frac, 0.0), 1.0)
        if not self.be.running:
            # With RX stopped there is neither a live panorama nor an NCO marker to
            # move.  In particular, never turn this gesture into a deferred VFO tune.
            return
        s = self.p["s"]
        # While a physical generation is changing, the visible old axis cannot be
        # mapped truthfully to the already-new hardware LO.  Ignore the tap until
        # the exact matching frame/axis pair is published.
        if self._axis_pending_token or self._station_pending_hz is not None:
            return
        # Complex 48-kS/s input is decimated to the displayed 24-kHz span.
        offset = int((frac - 0.5) * (IQ_RATE // 2))
        offset = int(round(offset / s) * s)
        wanted = self._axis_hz + offset
        if self._inj_on:
            self._tester_nco_set(wanted - self._lo_hz)
        else:
            self._select_live_nco_only(wanted)

    # ---- SETTINGS view (tap "SDR RECEIVER") ----
    # A dedicated full-screen (480x272) view of firmware DSP verification controls
    # (demod / inject / tap / gain / agc / squelch / bandwidth / audio-filter /
    # kernels + live S-meter), with the VFO routing, CAL and BACKEND rows folded in
    # (they are settings too). Laid out in TWO columns so every row fits without
    # depending on drag-scroll; a BACK button returns to the receiver via screen_load.
    # Every backend call is guarded (RX may be down -> self.be.iq is None) and wrapped
    # so a control never faults the LVGL callback that fired it. inject/tap/demod("thru")
    # are verification-only: they are NOT written to self.p and do not persist.
    # machine.IQADC.audio_filter() consumes these exact string names (not enum ints).
    _AF_PRESETS = (("OFF", "off"), ("AM", "am"),
                   ("VOICE", "voice"), ("CW", "cw"))
    _DSP_BW_PRESETS = (0, 250, 500, 1000, 1800, 2100, 2400,
                       3000, 4000, 6000, 9000)
    # label, receiver mode (None keeps the current demod), firmware constant name,
    # carrier Hz, modulation Hz, depth percent, FM deviation Hz.  Resolve constants from the live IQADC
    # object rather than duplicating enum values in Python.  The established RF
    # convention is positive complex rotation for USB and negative for LSB.
    _INJ_PRESETS = (("AM", "AM", "INJECT_AM", 3000, 1000, 50, 0),
                    ("FM", "FM", "INJECT_FM", 6000, 1000, 0, 4000),
                    ("USB", "USB", "INJECT_USB", 1500, 0, 0, 0),
                    ("LSB", "LSB", "INJECT_LSB", 1500, 0, 0, 0),
                    ("CW", "CW", "INJECT_CW", 10, 10, 0, 0),
                    ("IQ", None, "INJECT_IQ", 1000, 0, 0, 0))
    # label, receiver mode, 48-kS/s IN asset, 24-kS/s MID/OUT asset.  The first
    # five entries are deterministic project-owned vectors.  R:* entries are
    # optional local conversions of real ZS-1 recordings and are never embedded
    # in firmware or committed as third-party sample data.
    _IQ_FILE_PRESETS = (
        ("AM", "AM", "/flash/iqbank/am48.sdriq", "/flash/iqbank/am24.sdriq"),
        ("FM", "FM", "/flash/iqbank/fm48.sdriq", "/flash/iqbank/fm24.sdriq"),
        ("USB", "USB", "/flash/iqbank/usb48.sdriq", "/flash/iqbank/usb24.sdriq"),
        ("LSB", "LSB", "/flash/iqbank/lsb48.sdriq", "/flash/iqbank/lsb24.sdriq"),
        ("CW", "CW", "/flash/iqbank/cw48.sdriq", "/flash/iqbank/cw24.sdriq"))
    _IQ_FILE_REAL_PRESETS = (
        ("R:AM", "AM", "/flash/iqbank/zam48.sdriq", "/flash/iqbank/zam24.sdriq"),
        ("R:FM", "FM", "/flash/iqbank/zfm48.sdriq", "/flash/iqbank/zfm24.sdriq"),
        ("R:USB", "USB", "/flash/iqbank/zusb48.sdriq", "/flash/iqbank/zusb24.sdriq"),
        ("R:LSB", "LSB", "/flash/iqbank/zlsb48.sdriq", "/flash/iqbank/zlsb24.sdriq"),
        ("R:CW", "CW", "/flash/iqbank/zcw48.sdriq", "/flash/iqbank/zcw24.sdriq"))
    # PULSE is the firmware's sinusoidal analytic source under the existing 2-Hz,
    # 50-percent outer gate.  SQUARE/TRIANGLE are band-limited analytic waveforms.
    _INJ_WAVES = (("SIN", "INJECT_WAVE_SINE", 0),
                  ("SQR", "INJECT_WAVE_SQUARE", 0),
                  ("TRI", "INJECT_WAVE_TRIANGLE", 0),
                  ("PULSE", "INJECT_WAVE_PULSE", 2))
    _INJ_POINTS = (("IN", "INJECT_POINT_IN"),
                   ("MID", "INJECT_POINT_MID"),
                   ("OUT", "INJECT_POINT_OUT"))
    # Movable MID is still the one 24-kS/s high-level point.  inject_mid() selects
    # which complex block receives it: immediately before IQ correction, NCO or CHF.
    # Index 1/NCO is the legacy boundary and therefore remains safe on old firmware.
    _INJ_MIDS = (("IQC", "INJECT_MID_IQCORR"),
                 ("NCO", "INJECT_MID_NCO"),
                 ("CHF", "INJECT_MID_CHFILT"))
    _INJ_MID_DEFAULT = 1
    _DEMOD_MODES = ("AM", "FM", "USB", "LSB", "CW", "THRU")
    _TAP_STAGES = ("OFF", "decim", "nco", "chfilt")
    # Ten visible DSP stages keep their fixed firmware IDs 2..11.  Complex stages
    # 2..5 route I->DAC0 / Q->DAC1; post-demod 6..11 route mono audio to DAC0.
    # Raw input remains the internal scope ABI at ID 1, but is not a processing row.
    _BLOCKS = ((2, "Decimation", True), (3, "IQ correction", True),
               (4, "NCO / tune", True), (5, "Channel filter", True),
               (6, "Demod", False), (7, "AF filter", False), (8, "Squelch", False),
               (9, "AGC", False), (10, "Volume", False), (11, "Limiter", False))
    _OUTPUT_STAGE = ("FINAL", "INPUT", "DECIM", "IQC", "NCO", "CHF",
                     "DEMOD", "AF", "SQL", "AGC", "VOL", "LIMIT")
    # block id -> UART tap stage passed to iq.tap(); absent => no tap (control greyed).
    _BLK_TAP = {2: 1, 4: 2, 5: 3}

    def toggle_settings(self):
        if "settings" in _KEEP:
            self.close_settings()
        else:
            self.open_settings()

    def _drop_settings_screen(self):
        """Destroy the RAM-heavy VERIFY tree after leaving it.

        HOME is the stable owner screen.  Keeping VERIFY plus ROUTE alive at the
        same time exhausts the MicroPython heap, so secondary screens are rebuilt
        on demand and never coexist.
        """
        self._end_verify_scroll_gate(False)
        scr = self.ui.w.get("scr-settings")
        if scr is not None:
            lv.screen_load(self.ui.get("scr-receiver"))
            scr.delete()
            self.ui.w["scr-settings"] = None
        self._settings_cbs = []
        self._set_widgets = {}
        self._set_lbls = {}
        self._set_scroll_quiet = 0
        gc.collect()

    def close_settings(self):
        # Return HOME and release the 50+ KB VERIFY widget tree.  Re-opening rebuilds
        # it after collecting ROUTE, which keeps the two secondary screens exclusive.
        _KEEP.pop("settings", None)
        self._drop_settings_screen()
        self._set_modal(("pick_menu" in _KEEP) or ("gains_panel" in _KEEP))

    def _tester_tune_pending(self):
        """True only while the physical LO write itself is still outstanding.

        An applied-LO axis token must not block TESTER: VERIFY pauses the native
        renderer which would otherwise publish that token.  First arm replaces it
        with a TESTER generation and carries the pending station/VFO identity.
        """
        return bool(self._hw_pending or self._station_pending_hz is not None)

    def _commit_tester_receiver_mode(self, mode):
        """Make HOME describe the demodulator selected by an explicit TEST profile."""
        if mode is None:
            return
        if self._axis_pending_token and self._axis_pending_vfo is not None:
            # The physical LO for a VFO switch is already real, but its first tagged
            # frame has not yet made that VFO the active HOME identity.  Bind the
            # explicit TEST profile to the pending target.  Changing p["m"] or the
            # current VFO here would make _commit_vfo_switch() save TEST mode into the
            # old VFO when the replacement TESTER generation is finally published.
            self._axis_pending_mode = mode
            return
        self.p["m"] = mode
        self.p["vfos"][self.p["act"]][1] = mode
        self.update_mode()
        self.update_vfo_ui()
        self.touch_params()

    def _arm_tester_source(self):
        """Perform the explicit first arm after all older tuning work is settled."""
        if self._tester_tune_pending():
            self._tester_arm_pending = True
            self.be.err = "TESTER waiting for tuning"
            self._paint_tester()
            return False
        self._tester_arm_pending = False
        self._inj_on = True
        if not self._apply_tester_source(True):
            error = self.be.err
            self._inj_on = False
            self._apply_tester_source(False)
            self._inj_overload = False
            self.be.err = error
            self._paint_tester()
            return False
        self._request_tester_generation()
        self._paint_tester()
        return True

    def _apply_tester_source(self, receiver_setup=True):
        """Apply GEN/FILE from persistent App state, independent of VERIFY widgets."""
        iq = self.be.iq
        fn = getattr(iq, "inject", None) if iq is not None else None
        if not self._inj_on:
            ok = True
            self._iq_file.stop()
            if fn is not None:
                try:
                    fn(False)
                except Exception as e:
                    self.be.err = "inject: %r" % (e,)
                    ok = False
            if not self._restore_tester_receiver():
                ok = False
            self._inj_fm_nco = False
            return ok
        if iq is None:
            self.be.err = "test source unavailable (RX off)"
            return False
        # OUT is the version probe for the common three-point contract used by
        # both GEN and FILE.  Never silently reinterpret a point on old firmware.
        if not hasattr(iq, "INJECT_POINT_OUT"):
            self.be.err = "multi-point IQ test source unavailable"
            return False
        _point_name, point_attr = self._INJ_POINTS[self._inj_point]
        try:
            point = getattr(iq, point_attr)
        except Exception as e:
            self.be.err = "test point: %r" % (e,)
            return False

        # Publish the movable internal boundary before either native source starts.
        if not self._configure_inject_mid(iq):
            return False

        if self._inj_source:
            # Native GEN and FILE are mutually exclusive.  FILE owns borrowed Python
            # buffers, while all control remains in App state rather than the
            # transient VERIFY tree.
            expected_rate = IQ_RATE if self._inj_point == 0 else IQ_RATE // 2
            profile = self._iq_file_profiles[self._iq_file_preset]
            _name, receiver_mode, path48, path24 = profile
            try:
                if receiver_setup:
                    if not self.be.set_mode(receiver_mode):
                        return False
                    if not self.be.set_bandwidth(
                            self.p["bw"].get(receiver_mode,
                                             MODE_BW[receiver_mode])):
                        return False

                # An IN FM file emulates the real receiver's RF+6-kHz LO: its complex
                # carrier is therefore at -6 kHz and needs NCO=-6 kHz.  The 24-kS/s
                # MID file is already centred.  Late M:CHF/OUT injection bypasses NCO.
                fm_file = receiver_mode == "FM"
                if fm_file and self._tester_before_nco():
                    if not self._nco_enabled():
                        self.be.err = "FM FILE needs NCO block ON"
                        return False
                    wanted = -FM_LOW_IF_HZ if self._inj_point == 0 else 0
                    if receiver_setup or not self._inj_fm_nco:
                        self._tester_nco_set(wanted)
                        actual = self.be.fine_hz
                        if actual is None or int(actual) != wanted:
                            self.be.err = "FM FILE NCO %r != %d" % (actual, wanted)
                            return False
                    self._inj_fm_nco = True
                else:
                    self._inj_fm_nco = False

                path = path48 if expected_rate == IQ_RATE else path24
                self._iq_file.start(iq, point, expected_rate,
                                    self._iq_file_loop, path)
                if receiver_setup:
                    self._commit_tester_receiver_mode(receiver_mode)
                self._iq_file_ui_pct = -1
                self.be.err = None
                return True
            except Exception as e:
                self.be.err = "file: %r" % (e,)
                return False

        if fn is None:
            self.be.err = "generator unavailable"
            return False
        if not hasattr(iq, "INJECT_WAVE_TRIANGLE"):
            self.be.err = "multi-wave IQ generator unavailable"
            return False
        name, receiver_mode, kind_attr, carrier, mod_hz, depth, deviation = \
            self._INJ_PRESETS[self._inj_mode]
        fm_test = name == "FM"
        if self._inj_fm_nco and not fm_test:
            if not self._restore_tester_receiver():
                return False
            self._inj_fm_nco = False
        hard_max, safe_max, _peak, _gain = self._tester_levels()
        if self._inj_ampl > hard_max:
            self._inj_ampl = hard_max
        if self._inj_ampl > safe_max and not self._inj_overload:
            self.be.err = "TESTER SAFE <=%d; tap S for OVR" % safe_max
            return False
        _wave_name, wave_attr, gate_hz = self._INJ_WAVES[self._inj_wave]
        noise = 2 if self._inj_live else 0
        try:
            if fm_test:
                if (getattr(iq, "INJECT_API_VERSION", 0) < 2 or
                        not hasattr(iq, "INJECT_FM")):
                    self.be.err = "FM generator needs INJECT_API_VERSION>=2"
                    return False
                # FM is phase-only.  Before NCO generate +6 kHz and translate it to
                # zero; after NCO inject directly at zero.
                wave_attr = "INJECT_WAVE_SINE"
                gate_hz = 0
                carrier = FM_LOW_IF_HZ if self._tester_before_nco() else 0
            kind = getattr(iq, kind_attr)
            wave = getattr(iq, wave_attr)
            if receiver_mode is not None and receiver_setup:
                if not self.be.set_mode(receiver_mode):
                    return False
                if not self.be.set_bandwidth(
                        self.p["bw"].get(receiver_mode,
                                         MODE_BW[receiver_mode])):
                    return False
            if fm_test and self._tester_before_nco():
                if not self._nco_enabled():
                    self.be.err = "FM TEST needs NCO block ON"
                    return False
                if receiver_setup or not self._inj_fm_nco:
                    self._tester_nco_set(carrier)
                    actual = self.be.fine_hz
                    if actual is None or int(actual) != carrier:
                        self.be.err = "FM TEST NCO %r != %d" % (actual, carrier)
                        return False
                self._inj_fm_nco = True
            elif fm_test:
                self._inj_fm_nco = False
            self._iq_file.stop()
            if fm_test:
                fn(True, carrier, self._inj_ampl, kind, mod_hz, depth,
                   gate_hz, noise, point, wave, deviation)
            else:
                fn(True, carrier, self._inj_ampl, kind, mod_hz, depth,
                   gate_hz, noise, point, wave)
            if receiver_setup:
                self._commit_tester_receiver_mode(receiver_mode)
            self.be.err = None
            return True
        except Exception as e:
            self.be.err = "inject: %r" % (e,)
            return False

    def open_settings(self):
        if "settings" in _KEEP:
            if self.ui.w.get("scr-settings") is not None:
                return
            _KEEP.pop("settings", None)  # stale flag from an interrupted build
        # ROUTE and VERIFY cannot coexist in this heap.  Loading HOME happens inside
        # the drop helper but is not rendered before VERIFY is loaded below.
        _KEEP.pop("route_menu", None)
        self._drop_route_screen()
        # VERIFY is transient (build-on-open, delete-on-close) so it holds no RAM while
        # the operator is on the receiver.  Build into a LOCAL; only after every row
        # succeeds do we publish scr-settings, set the open-flag, and load it.  If the
        # build raises (low RAM), delete the partial tree, keep scr-settings None and
        # the open-flag clear, and return -- so the button is a no-op instead of
        # stranding a half-built screen that makes BACKEND look dead on a clean boot.
        if self.ui.w.get("scr-settings") is None:
            gc.collect()
            try:
                scr = self._build_settings()
            except Exception as e:
                part = getattr(self, "_set_scr_partial", None)
                if part is not None:
                    try:
                        part.delete()                 # free the partial LVGL tree
                    except Exception:
                        pass
                self._set_scr_partial = None
                self.ui.w["scr-settings"] = None
                self._settings_cbs = []
                self._set_widgets = {}
                self._set_lbls = {}
                gc.collect()
                self.be.err = "verify build: %r" % (e,)
                return
            self.ui.w["scr-settings"] = scr           # publish only on full success
        _KEEP["settings"] = True     # open-flag: _consume_status refreshes live labels
        self._set_modal(True)
        # Populate controls before the screen becomes visible: no second full paint
        # after loading and no apparent jump back to the top of the list.
        self._refresh_settings()
        for i in range(len(self._set_live_cache)):
            self._set_live_cache[i] = None
        self._consume_status()
        lv.screen_load(self.ui.get("scr-settings"))

    def _build_settings(self):
        # Full-screen backend/DSP VERIFICATION view: a per-block table of the ten
        # DSP chain. A compact header (INJECT toggle + amplitude, live AGC-gain and
        # S-meter read-outs) sits above a VERTICALLY SCROLLABLE column of block rows.
        # Each block row exposes: name, an ON/OFF bypass toggle (iq.block(id,1|0)), a
        # one-of UART TAP (iq.tap(stage); only ids 2/4/5 are tappable, the rest greyed),
        # and a one-of SCOPE ->DAC route (iq.scope(id)). Squelch / audio-filter /
        # kernels follow as extra rows below the stages. The table cannot fit
        # 272 px without scroll, so the rows container is intentionally scrollable.
        # Callbacks live on self._settings_cbs for the screen lifetime.
        # Shared styles are created once and outlive the transient screen tree; the
        # widgets below are rebuilt every open (build-on-open, delete-on-close) so
        # VERIFY holds no RAM while the operator is on the receiver.  The explicitly
        # saved I/Q profile is persistent; the remaining bench controls are runtime-only.
        st = _verify_styles()

        scr = _base(lv.obj(None))
        # Root captured immediately so open_settings can scr.delete() the partial LVGL
        # tree if any row below raises (the Python wrapper going out of scope does NOT
        # free the underlying C object). Cleared to None only after full success.
        self._set_scr_partial = scr
        scr.set_style_bg_color(lv.color_hex(BG_RX), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        scr.set_style_pad_all(8, 0)
        _flex(scr, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 4)
        # NOTE: scr-settings is published to self.ui.w only at the END, after every
        # row succeeds, so a mid-build MemoryError cannot strand a half-built screen.

        # --- title bar: "VERIFY" (left) + live AGC/S-meter read-outs + BACK (right) ---
        tb = _base(lv.obj(scr))
        tb.set_size(464, 28)
        _flex(tb, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN)
        _lbl(tb, "VERIFY", 16, GRAY)
        rd = _base(lv.obj(tb))            # compact read-outs: AGC, S-meter, DSP budget
        rd.set_size(286, 24)
        _flex(rd, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.END, lv.FLEX_ALIGN.CENTER,
              lv.FLEX_ALIGN.CENTER, 6)
        _lbl(rd, "AGC", 12, GRAY)
        again = _lbl(rd, "--", 14, CYAN_RX)
        self._set_lbls["agc"] = again
        _lbl(rd, "S", 12, GRAY)
        smval = _lbl(rd, "--", 14, CYAN_RX)
        self._set_lbls["smeter"] = smval
        _lbl(rd, "DSP", 12, GRAY)
        timing = _lbl(rd, "--", 14, CYAN_RX)
        self._set_lbls["timing"] = timing
        back = _btn(tb, 70, 26, PANEL2, radius=6, border=BORDER)
        _lbl(back, "BACK", 14, WHITE)

        cbs = []
        self._settings_cbs = cbs     # keep every event cb alive for the screen lifetime

        def back_cb(e):
            self.close_settings()
        back.add_event_cb(back_cb, lv.EVENT.CLICKED, None)
        cbs.append(back_cb)

        # -- compact chip builder: ONE clickable lv.label styled by the shared "chip"
        # style, replacing the old button+child-label pair (2 objects -> 1). The two
        # returned handles are the SAME object so existing callers that set text on one
        # and bg on the other keep working unchanged. Only the dynamic text colour is
        # an inline per-object call; the rest comes from the shared style.
        def _sbtn(row, txt, w, size=16, color=WHITE):
            c = lv.label(row)
            c.add_flag(lv.obj.FLAG.CLICKABLE)
            c.add_style(st["chip"], 0)
            c.set_size(w, 24)
            c.set_text(txt)
            if size != 12:
                c.set_style_text_font(font(size), 0)
            c.set_style_text_color(lv.color_hex(color), 0)
            return c, c

        def _grp(row, w):
            """A right-aligned inline flex group so several chips+label pack tight."""
            g = lv.obj(row)
            g.remove_flag(lv.obj.FLAG.SCROLLABLE)
            g.add_style(st["grp"], 0)
            g.set_size(w, 24)
            g.set_flex_align(lv.FLEX_ALIGN.END, lv.FLEX_ALIGN.CENTER,
                             lv.FLEX_ALIGN.CENTER)
            return g

        def iq_call(name, *a):
            """Guarded optional IQADC call -> (success, result).

            Some setters (notably tap()) legitimately return None on success, so a
            separate success flag is required before committing the UI cache/paint.
            """
            iq = self.be.iq
            if iq is None:
                return False, None
            fn = getattr(iq, name, None)
            if fn is None:
                return False, None
            try:
                return True, fn(*a)
            except Exception as e:
                self.be.err = "%s: %r" % (name, e)
                return False, None

        # -- TESTER source | point | generator/file controls | ON --
        # The three injection classes stay in the complex path: IN replaces raw ADC I/Q,
        # movable MID enters before IQCORR/NCO/CHF, and OUT is after the channel filter.
        # C publishes point+waveform+generator parameters as one block-atomic tuple.
        # Tapping the left chip selects GEN or FILE without spending another row.
        irow = _base(lv.obj(scr))
        irow.set_size(464, 26)
        _flex(irow, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN,
              lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 6)
        isb, isl = _sbtn(irow, "TESTER GEN", 82, 12, CYAN_RX)
        ig = _grp(irow, 370)
        point_name = self._tester_point_label()
        mode_name = (self._iq_file_profiles[self._iq_file_preset][0]
                     if self._inj_source else
                     self._INJ_PRESETS[self._inj_mode][0])
        wave_name = self._INJ_WAVES[self._inj_wave][0]
        ipb, ipl = _sbtn(ig, point_name, 46, 12, DARK_TXT)
        ipb.set_style_bg_color(lv.color_hex(CYAN_RX), 0)
        imb, iml = _sbtn(ig, mode_name, 44, 12, CYAN_RX)
        iwb, iwl = _sbtn(ig, wave_name, 54, 12, CYAN_RX)
        ilb, ill = _sbtn(ig, "LIVE" if self._inj_live else "CLEAN", 50, 12,
                         DARK_TXT if self._inj_live else CYAN_RX)
        if self._inj_live:
            ilb.set_style_bg_color(lv.color_hex(GREEN), 0)
        iamp = lv.label(ig)
        iamp.set_text("S%d" % self._inj_ampl)
        iamp.set_size(48, 20)
        iamp.add_style(st["dim"], 0)
        iamp.set_style_text_align(lv.TEXT_ALIGN.CENTER, 0)
        iamp.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        iamp.add_flag(lv.obj.FLAG.CLICKABLE)
        idn, idnl = _sbtn(ig, "-", 24, 20)
        iup, iupl = _sbtn(ig, "+", 24, 20)
        tester_label = "ON" if self._inj_on else (
            "WAIT" if self._tester_arm_pending else "OFF")
        itg, itgl = _sbtn(ig, tester_label, 46, 12,
                          DARK_TXT if self._inj_on else WHITE)
        if self._inj_on:
            itg.set_style_bg_color(lv.color_hex(GREEN), 0)
        self._set_widgets["inject"] = (isb, isl, ipb, ipl, imb, iml,
                                        iwb, iwl, ilb, ill, iamp,
                                        idn, idnl, iup, iupl, itg, itgl)

        def inj_apply(receiver_setup=True):
            return self._apply_tester_source(receiver_setup)
        def inj_paint():
            self._paint_tester()

        def inj_reapply(receiver_setup=False):
            """Apply a live edit; on failure disarm and restore the displaced route."""
            if self._inj_on and not inj_apply(receiver_setup):
                error = self.be.err
                self._inj_on = False
                inj_apply()
                self._inj_overload = False
                self.be.err = error

        def inj_mode_cb(e):
            if self._inj_source:
                if self._inj_on:
                    return
                self._iq_file_preset = ((self._iq_file_preset + 1) %
                                        len(self._iq_file_profiles))
                self._iq_file.error = None
                self._iq_file.sample_rate = 0
                self._iq_file.sample_bits = 0
                self._iq_file_ui_pct = -1
                inj_paint()
                return
            self._inj_mode = (self._inj_mode + 1) % len(self._INJ_PRESETS)
            self._inj_overload = False
            if self._INJ_PRESETS[self._inj_mode][0] == "FM":
                self._inj_wave = 0           # FM carrier is phase-only, always SIN
            if self._INJ_PRESETS[self._inj_mode][0] == "AM" and self._inj_ampl > 1000:
                self._inj_ampl = 1000
            inj_reapply(True)
            inj_paint()

        def inj_point_cb(e):
            if self._inj_on:                 # file/source rate contract is fixed on start
                return
            self._inj_point = (self._inj_point + 1) % len(self._INJ_POINTS)
            self._inj_overload = False
            inj_paint()

        def inj_wave_cb(e):
            if self._inj_source:
                return
            if self._INJ_PRESETS[self._inj_mode][0] == "FM":
                return
            self._inj_wave = (self._inj_wave + 1) % len(self._INJ_WAVES)
            inj_reapply(False)
            inj_paint()

        def inj_live_cb(e):
            if self._inj_source:
                if self._inj_on:
                    return                 # ordered EOF/LOOP contract is immutable ON
                self._iq_file_loop = not self._iq_file_loop
                self._iq_file.set_loop(self._iq_file_loop)
                inj_paint()
                return
            self._inj_live = not self._inj_live
            inj_reapply(False)
            inj_paint()

        def inj_amp_cb(e, d=0, lbl=iamp):
            if self._inj_source:
                # In FILE mode the left small chip is rewind/restart; the right one is
                # hidden.  Rewind is available while OFF: the two native buffers are
                # borrowed by the active source, so replacing them live would expose
                # an unlabelled ADC gap during file open/prefill.
                if d < 0 and not self._inj_on:
                    self._iq_file_ui_pct = -1
                    inj_paint()
                return
            hard_max, safe_max, _peak, _gain = self._tester_levels()
            max_ampl = hard_max if self._inj_overload else safe_max
            self._inj_ampl = min(max(self._inj_ampl + d, 0), max_ampl)
            inj_reapply(False)
            inj_paint()

        def inj_level_cb(e):
            if self._inj_source:
                return
            self._inj_overload = not self._inj_overload
            if not self._inj_overload:
                _hard, safe_max, _peak, _gain = self._tester_levels()
                if self._inj_ampl > safe_max:
                    self._inj_ampl = safe_max
            inj_reapply(False)
            inj_paint()

        def inj_source_cb(e):
            if self._inj_on:                 # borrowed pointers/point stay immutable
                return
            self._inj_source = 1 - self._inj_source
            self._inj_overload = False
            self._iq_file.error = None
            self._iq_file.sample_rate = 0
            self._iq_file.sample_bits = 0
            self._iq_file_ui_pct = -1
            inj_paint()

        def inj_tg_cb(e):
            if self._tester_arm_pending:
                self._tester_arm_pending = False
                self.be.err = None
                inj_paint()
                return
            if not self._inj_on:
                if self._tester_tune_pending():
                    # Keep RX truthful until the older Si5351 + tagged-axis request
                    # is visible.  sdr_poll arms this exact requested profile once.
                    self._tester_arm_pending = True
                    self.be.err = "TESTER waiting for tuning"
                    inj_paint()
                    return
                self._arm_tester_source()
                return
            self._inj_on = False
            if not inj_apply():
                error = self.be.err
                self.be.err = error
            else:
                self._inj_overload = False
            inj_paint()
        for b, cb in ((isb, inj_source_cb), (ipb, inj_point_cb),
                      (imb, inj_mode_cb), (iwb, inj_wave_cb),
                      (ilb, inj_live_cb), (iamp, inj_level_cb),
                      (itg, inj_tg_cb)):
            b.add_event_cb(cb, lv.EVENT.CLICKED, None)
            cbs.append(cb)
        for b, d in ((idn, -10), (iup, +10)):
            def _iac(e, dd=d):
                inj_amp_cb(e, dd)
            b.add_event_cb(_iac, lv.EVENT.CLICKED, None)
            cbs.append(_iac)

        # Column header above the scrollable block table. PROC is deliberately not
        # called ON/OFF: BYP means a direct short to the next block, never signal stop.
        hdr = _base(lv.obj(scr))
        hdr.set_size(464, 16)
        _flex(hdr, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
              lv.FLEX_ALIGN.CENTER, 0)
        for txt, wpx in (("MID / BLOCK", 176), ("PROC", 62),
                         ("TAP", 92), ("OUTPUT", 92)):
            hl = lv.label(hdr)
            hl.set_text(txt)
            hl.set_size(wpx, 14)
            hl.add_style(st["dim"], 0)
            if txt == "OUTPUT":
                # Reuse this existing header label as a live routing explanation;
                # no extra LVGL object or heap cost.
                self._set_widgets["output_header"] = hl

        # -- scrollable rows container: 11 block rows + extra bench knobs below --
        rows = _base(lv.obj(scr))
        # Five exact 31-px row pitches keep each atomic scroll repaint to one row
        # tree.  The remaining 19 px stay as a clean bottom margin; a 174-px
        # viewport exposed fragments of two adjacent rows and took > one frame.
        rows.set_size(464, 155)
        rows.add_flag(lv.obj.FLAG.SCROLLABLE)
        # DIRECT mode has one framebuffer: repainting all 21 rows on every FT5x06
        # MOVE packet is visible as 5-6 flashes.  No momentum/elastic tail, and
        # batch the drag into one invalidation on SCROLL_END instead.
        rows.remove_flag(lv.obj.FLAG.SCROLL_MOMENTUM)
        rows.remove_flag(lv.obj.FLAG.SCROLL_ELASTIC)
        rows.set_scroll_dir(lv.DIR.VER)
        # The scrollbar thumb moves at a different ratio than the row pixels and
        # therefore cannot participate in the framebuffer blit transaction.
        rows.set_scrollbar_mode(lv.SCROLLBAR_MODE.OFF)
        rows.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        rows.set_flex_align(lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
                            lv.FLEX_ALIGN.CENTER)
        rows.set_style_pad_row(3, 0)
        rows.set_style_pad_all(0, 0)
        self._set_widgets["rows"] = rows
        self._set_widgets["scroll_view"] = lv.area_t()
        self._set_widgets["scroll_dirty"] = lv.area_t()

        def scroll_begin_cb(e):
            self._begin_verify_scroll_gate(rows)

        def scroll_end_cb(e):
            self._end_verify_scroll_gate()

        # PREPROCESS runs before LVGL's base handler adds SCROLLED state and
        # invalidates the complete viewport.
        rows.add_event_cb(scroll_begin_cb,
                          lv.EVENT.SCROLL_BEGIN | lv.EVENT.PREPROCESS, None)
        rows.add_event_cb(scroll_end_cb, lv.EVENT.SCROLL_END, None)
        cbs.append(scroll_begin_cb)
        cbs.append(scroll_end_cb)

        def _brow():
            r = lv.obj(rows)
            r.remove_flag(lv.obj.FLAG.SCROLLABLE)
            r.add_style(st["row"], 0)
            r.set_size(448, 28)
            return r

        # blk_w[id] = (row, name_lbl, name, proc_btn, proc_lbl,
        #              output_btn, output_lbl, tap_btn, tap_lbl)
        # so _refresh_settings can repaint every ON/BYP, the one-of scope and one-of tap.
        blk_w = {}
        self._set_widgets["blocks"] = blk_w
        mid_labels = {}
        self._set_widgets["mid_labels"] = mid_labels

        def _scope_paint(bid, on):
            r, _nl, _name, _ob, _ol, sb, sl, _tb, _tl = blk_w[bid]
            sb.set_style_bg_color(lv.color_hex(CYAN_RX if on else PANEL2), 0)
            sl.set_style_text_color(lv.color_hex(DARK_TXT if on else CYAN_RX), 0)
            r.set_style_bg_color(lv.color_hex(BORDER if on else PANEL2), 0)

        def _tap_paint(bid, on):
            _r, _nl, _name, _ob, _ol, _sb, _sl, tb2, tl = blk_w[bid]
            tb2.set_style_bg_color(lv.color_hex(GREEN if on else PANEL2), 0)
            tl.set_style_text_color(lv.color_hex(DARK_TXT if on else CYAN_RX), 0)

        for bid, name, cplx in self._BLOCKS:
            r = _brow()
            nl = lv.label(r)
            mid_idx = bid - 3 if 3 <= bid <= 5 else -1
            nl.set_text((("[x] " if mid_idx == self._inj_mid else "[ ] ") + name)
                        if mid_idx >= 0 else name)
            nl.set_size(168, 20)
            nl.add_style(st["name"], 0)
            if mid_idx >= 0:
                # Reuse the existing name label as the one-of radio: zero new LVGL
                # widgets and no extra row. The mark means "inject BEFORE this block".
                mid_labels[bid] = (nl, name, mid_idx)
                if not self._inj_on:
                    nl.add_flag(lv.obj.FLAG.CLICKABLE)

                def mid_cb(e, idx=mid_idx):
                    if self._inj_on:
                        return
                    self._inj_mid = idx
                    self._inj_point = 1
                    self._paint_tester()
                nl.add_event_cb(mid_cb, lv.EVENT.CLICKED, None)
                cbs.append(mid_cb)

            safety_limiter = bid == 11
            on0 = True if safety_limiter else self._blk_on.get(bid, True)
            proc_text = "SAFE" if safety_limiter else ("ON" if on0 else "BYP")
            ob, ol = _sbtn(r, proc_text, 58, 12,
                           GRAY2 if safety_limiter else
                           (DARK_TXT if on0 else WHITE))
            ob.set_style_bg_color(
                lv.color_hex(PANEL2 if safety_limiter else
                             (GREEN if on0 else PANEL2)), 0)
            if safety_limiter:
                # The DAC-range clamp is intentionally unconditional in C.  Keep its
                # SCOPE route, but never claim that this safety boundary can be bypassed.
                ob.remove_flag(lv.obj.FLAG.CLICKABLE)

            tap_stage = self._BLK_TAP.get(bid, 0)   # 0 => this block has no firmware tap
            tb2, tl = _sbtn(r, "TAP", 88, 12, CYAN_RX if tap_stage else GRAY2)
            if not tap_stage:
                tb2.remove_flag(lv.obj.FLAG.CLICKABLE)   # honest: no tap in firmware

            # Say what leaves the board, not the implementation word "scope".
            # Complex stages use both converters; audio stages are heard on DAC0.
            scope_lbl = "I/Q D0/1" if cplx else "HEAR D0"
            sb, sl = _sbtn(r, scope_lbl, 88, 12, CYAN_RX)

            blk_w[bid] = (r, nl, name, ob, ol, sb, sl, tb2, tl)

            if not safety_limiter:
                def on_cb(e, i=bid, b=ob, bl=ol):
                    v = 0 if self._blk_on.get(i, True) else 1
                    ok, current = iq_call("block", i, v)
                    if not ok:
                        return
                    actual = bool(v if current is None else current)
                    self._blk_on[i] = actual
                    if i == 4:
                        self._update_tuning_role()
                    elif i == 3 and "iqc_enable" in self._set_widgets:
                        eb, el = self._set_widgets["iqc_enable"]
                        effective = self._iqc_on and actual
                        el.set_text("ON" if effective else
                                    ("PATH" if self._iqc_on else "BYP"))
                        el.set_style_text_color(
                            lv.color_hex(DARK_TXT if effective else WHITE), 0)
                        eb.set_style_bg_color(
                            lv.color_hex(GREEN if effective else PANEL2), 0)
                    elif i == 6:
                        self._paint_output_status()
                    bl.set_text("ON" if actual else "BYP")
                    bl.set_style_text_color(
                        lv.color_hex(DARK_TXT if actual else WHITE), 0)
                    b.set_style_bg_color(
                        lv.color_hex(GREEN if actual else PANEL2), 0)
                ob.add_event_cb(on_cb, lv.EVENT.CLICKED, None)
                cbs.append(on_cb)

            if tap_stage:
                def tap_cb(e, i=bid, stg=tap_stage):
                    new_stage = 0 if self._tap_stage == stg else stg
                    ok, _ = iq_call("tap", new_stage)
                    if not ok:
                        return
                    prev = self._tap_id
                    if prev and prev in blk_w and prev != i:
                        _tap_paint(prev, False)       # clear the previous one-of tap
                    self._tap_stage = new_stage
                    self._tap_id = i if self._tap_stage else 0
                    _tap_paint(i, self._tap_stage == stg)
                tb2.add_event_cb(tap_cb, lv.EVENT.CLICKED, None)
                cbs.append(tap_cb)

            def scope_cb(e, i=bid):
                if self._scope_id == i:
                    if not self.be.set_scope(0):
                        return
                    self._scope_id = 0               # second press routes nothing
                    _scope_paint(i, False)
                else:
                    prev = self._scope_id
                    if not self.be.set_scope(i):
                        return
                    if prev and prev in blk_w:
                        _scope_paint(prev, False)     # clear the previous one-of route
                    self._scope_id = i
                    _scope_paint(i, True)
                self._paint_listen_markers()
                self._paint_output_status()
            sb.add_event_cb(scope_cb, lv.EVENT.CLICKED, None)
            cbs.append(scope_cb)

        # -- extra bench knobs below the DSP-stage table --
        def _krow(label):
            r = _brow()
            l = lv.label(r)
            l.set_text(label)
            l.set_size(168, 20)
            l.add_style(st["name"], 0)
            g = _grp(r, 270)
            return r, g

        # SQUELCH -/+ threshold (verify-only).  Firmware envelope units span roughly
        # 0..2048; the former 0..100 range could never tune a practical gate.
        _r, sg = _krow("SQUELCH")
        sdn, _ = _sbtn(sg, "-", 34, 20)
        sval = lv.label(sg)
        sval.set_text("%d" % self._squelch)
        sval.add_style(st["name"], 0)
        sval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        sup, _ = _sbtn(sg, "+", 34, 20)
        self._set_widgets["squelch"] = sval

        def sq_cb(e, d=0, lbl=sval):
            value = min(max(self._squelch + d, 0), 2000)
            ok, _ = iq_call("squelch", value)
            if not ok:
                return
            self._squelch = value
            lbl.set_text("%d" % value)
        for b, d in ((sdn, -100), (sup, +100)):
            def _sqc(e, dd=d):
                sq_cb(e, dd)
            b.add_event_cb(_sqc, lv.EVENT.CLICKED, None)
            cbs.append(_sqc)

        # AUDIO FILTER cycle OFF/AM/VOICE/CW (verify-only).
        _r, afg = _krow("AUDIO FILT")
        afb, afbl = _sbtn(afg, self._AF_PRESETS[self._af_preset][0], 120, 14, CYAN_RX)
        self._set_widgets["af"] = afbl

        def af_cb(e, bl=afbl):
            new_index = (self._af_preset + 1) % len(self._AF_PRESETS)
            name, val = self._AF_PRESETS[new_index]
            ok, _ = iq_call("audio_filter", val)
            if not ok:
                return
            self._af_preset = new_index
            bl.set_text(name)
        afb.add_event_cb(af_cb, lv.EVENT.CLICKED, None)
        cbs.append(af_cb)

        # CHANNEL bandwidth: live pre-demod I/Q low-pass, including 0 = bypass.
        _r, bwg = _krow("CHANNEL BW")
        bwdn, _ = _sbtn(bwg, "-", 34, 20)
        bwval = lv.label(bwg)
        bwval.set_text("BYP" if self.be.bw == 0 else fmt_bw(self.be.bw))
        bwval.add_style(st["name"], 0)
        bwval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        bwup, _ = _sbtn(bwg, "+", 34, 20)
        self._set_widgets["bw"] = bwval

        def bw_cb(e, d=0, lbl=bwval):
            presets = self._DSP_BW_PRESETS
            best = 0
            distance = abs(presets[0] - self.be.bw)
            for j in range(1, len(presets)):
                nd = abs(presets[j] - self.be.bw)
                if nd < distance:
                    best, distance = j, nd
            hz = presets[(best + d) % len(presets)]
            if self.be.set_bandwidth(hz):
                lbl.set_text("BYP" if hz == 0 else fmt_bw(hz))
        for b, d in ((bwdn, -1), (bwup, +1)):
            def _bwc(e, dd=d):
                bw_cb(e, dd)
            b.add_event_cb(_bwc, lv.EVENT.CLICKED, None)
            cbs.append(_bwc)

        # Tuning NCO: live complex frequency shift before the channel filter.
        _r, ng = _krow("NCO Hz")
        ndn, _ = _sbtn(ng, "-", 34, 20)
        nval = lv.label(ng)
        nval.set_text("%d" % self.be.fine_hz)
        nval.add_style(st["name"], 0)
        nval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        nup, _ = _sbtn(ng, "+", 34, 20)
        nz, _ = _sbtn(ng, "0", 34, 14)
        self._set_widgets["nco"] = nval

        def nco_cb(e, d=0, zero=False, lbl=nval):
            delta = -self.be.fine_hz if zero else int(d)
            if self._inj_on:
                self._tester_nco_tune(delta)
            else:
                self._move_live_frequency(delta)
            lbl.set_text("%d" % self.be.fine_hz)
        for b, d, zero in ((ndn, -100, False), (nup, 100, False), (nz, 0, True)):
            def _ncoc(e, dd=d, zz=zero):
                nco_cb(e, dd, zz)
            b.add_event_cb(_ncoc, lv.EVENT.CLICKED, None)
            cbs.append(_ncoc)

        # AGC target is already a real/persisted backend parameter, but previously had
        # no control.  Keep AGC mode/gain in the existing gains panel; VERIFY adjusts
        # only the detector target in 5-percent full-scale steps.
        _r, atg = _krow("AGC TARGET")
        atdn, _ = _sbtn(atg, "-", 34, 20)
        atval = lv.label(atg)
        atval.set_text("%d%%" % int(self.p["atgt"] * 100 + 0.5))
        atval.add_style(st["name"], 0)
        atval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        atup, _ = _sbtn(atg, "+", 34, 20)
        self._set_widgets["agc_target"] = atval

        def at_cb(e, d=0, lbl=atval):
            candidate = min(max(self.p["atgt"] + d,
                                AGC_TARGET_MIN), AGC_TARGET_MAX)
            if self.be.set_agc(self.be.agc, target=candidate):
                self.p["atgt"] = candidate
                lbl.set_text("%d%%" % int(candidate * 100 + 0.5))
                self.touch_params()
        for b, d in ((atdn, -0.05), (atup, +0.05)):
            def _atc(e, dd=d):
                at_cb(e, dd)
            b.add_event_cb(_atc, lv.EVENT.CLICKED, None)
            cbs.append(_atc)

        # Manual I/Q correction: enable, Q amplitude balance, and I->Q phase leakage.
        def iqc_apply():
            return self._apply_iq_correction()

        _r, iqeg = _krow("IQ CORR")
        # This is processing enable, not signal enable: BYP passes I/Q unchanged.
        iqeb, iqel = _sbtn(iqeg, "ON" if self._iqc_on else "BYP", 64, 12,
                            DARK_TXT if self._iqc_on else WHITE)
        iqeb.set_style_bg_color(lv.color_hex(GREEN if self._iqc_on else PANEL2), 0)
        iqrst, _ = _sbtn(iqeg, "RESET", 72, 12, CYAN_RX)
        self._set_widgets["iqc_enable"] = (iqeb, iqel)

        _r, iag = _krow("IQ AMP")
        iadn, _ = _sbtn(iag, "-", 34, 20)
        iaval = lv.label(iag)
        iaval.set_text("%.2f" % self._iqc_amp)
        iaval.add_style(st["name"], 0)
        iaval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        iaup, _ = _sbtn(iag, "+", 34, 20)
        self._set_widgets["iqc_amp"] = iaval

        _r, ipg = _krow("IQ PHASE")
        ipdn, _ = _sbtn(ipg, "-", 34, 20)
        ipval = lv.label(ipg)
        ipval.set_text("%+.2f" % self._iqc_phase)
        ipval.add_style(st["name"], 0)
        ipval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        ipup, _ = _sbtn(ipg, "+", 34, 20)
        self._set_widgets["iqc_phase"] = ipval

        # Read-only learned ADC means help diagnose DC settling.  They are never
        # stored as calibration values; the C estimator continuously updates them.
        _r, idcg = _krow("IQ DC INPUT")
        idcval = lv.label(idcg)
        idcval.set_text("I --  Q --")
        idcval.add_style(st["name"], 0)
        idcval.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        self._set_widgets["iqc_dc"] = idcval
        self._set_lbls["iqdc"] = idcval

        # Manual edits are live RAM changes.  SAVE is explicit because a test
        # generator can verify the algorithm but must not overwrite a real-input
        # calibration accidentally.
        _r, isg = _krow("IQ PROFILE")
        isval = lv.label(isg)
        isval.set_text("SAVED")
        isval.add_style(st["name"], 0)
        isval.set_style_text_color(lv.color_hex(GRAY2), 0)
        isave, _ = _sbtn(isg, "SAVE", 72, 12, CYAN_RX)
        self._set_widgets["iqc_profile"] = isval

        def iqc_paint():
            effective = self._iqc_on and self._blk_on.get(3, True)
            iqel.set_text("ON" if effective else
                          ("PATH" if self._iqc_on else "BYP"))
            iqel.set_style_text_color(
                lv.color_hex(DARK_TXT if effective else WHITE), 0)
            iqeb.set_style_bg_color(
                lv.color_hex(GREEN if effective else PANEL2), 0)
            iaval.set_text("%.2f" % self._iqc_amp)
            ipval.set_text("%+.2f" % self._iqc_phase)
            isval.set_text("EDIT" if self._iqc_dirty else "SAVED")
            isval.set_style_text_color(
                lv.color_hex(CYAN_RX if self._iqc_dirty else GRAY2), 0)

        def iqc_enable_cb(e):
            self._iqc_on = not self._iqc_on
            if not iqc_apply():
                self._iqc_on = False
            self._iqc_dirty = True
            iqc_paint()

        def iqc_reset_cb(e):
            self._iqc_on = False
            self._iqc_amp = 1.0
            self._iqc_phase = 0.0
            iqc_apply()
            self._iqc_dirty = True
            iqc_paint()

        def iqc_amp_cb(e, d=0.0):
            self._iqc_amp = min(max(self._iqc_amp + d, 0.50), 1.50)
            self._iqc_on = True
            if not iqc_apply():
                self._iqc_on = False
            self._iqc_dirty = True
            iqc_paint()

        def iqc_phase_cb(e, d=0.0):
            self._iqc_phase = min(max(self._iqc_phase + d, -0.50), 0.50)
            self._iqc_on = True
            if not iqc_apply():
                self._iqc_on = False
            self._iqc_dirty = True
            iqc_paint()

        def iqc_save_cb(e):
            self._save_iq_profile()
            iqc_paint()

        for b, cb in ((iqeb, iqc_enable_cb), (iqrst, iqc_reset_cb)):
            b.add_event_cb(cb, lv.EVENT.CLICKED, None)
            cbs.append(cb)
        for b, d in ((iadn, -0.01), (iaup, +0.01)):
            def _iac2(e, dd=d):
                iqc_amp_cb(e, dd)
            b.add_event_cb(_iac2, lv.EVENT.CLICKED, None)
            cbs.append(_iac2)
        for b, d in ((ipdn, -0.01), (ipup, +0.01)):
            def _ipc(e, dd=d):
                iqc_phase_cb(e, dd)
            b.add_event_cb(_ipc, lv.EVENT.CLICKED, None)
            cbs.append(_ipc)
        isave.add_event_cb(iqc_save_cb, lv.EVENT.CLICKED, None)
        cbs.append(iqc_save_cb)

        # KERNELS: four tiny A/B toggles dec / hil / chf / mag (verify-only).
        _r, kg = _krow("KERNELS")
        kern_w = []
        self._set_widgets["kernels"] = kern_w
        for tag, meth in (("dec", "dec_kernel"), ("hil", "hil_kernel"),
                          ("chf", "chf_kernel"), ("mag", "mag_kernel")):
            kb, kbl = _sbtn(kg, tag, 34, 12)
            kon = self._kernels.get(meth, 0)
            kbl.set_style_text_color(lv.color_hex(DARK_TXT if kon else WHITE), 0)
            kb.set_style_bg_color(lv.color_hex(GREEN if kon else PANEL2), 0)
            kern_w.append((meth, kb, kbl))

            def kern_cb(e, m=meth, b=kb, bl=kbl):
                v = 0 if self._kernels.get(m, 0) else 1
                ok, _ = iq_call(m, v)
                if not ok:
                    return
                self._kernels[m] = v
                bl.set_style_text_color(lv.color_hex(DARK_TXT if v else WHITE), 0)
                b.set_style_bg_color(lv.color_hex(GREEN if v else PANEL2), 0)
            kb.add_event_cb(kern_cb, lv.EVENT.CLICKED, None)
            cbs.append(kern_cb)

        # One graphical "where we listen" marker for the complete DSP chain.
        # IGNORE_LAYOUT keeps it out of the flex column while still letting it scroll
        # with the rows.  FLOATING must not be used here: it would stay screen-fixed
        # and fight the native one-row framebuffer scroll transaction above.
        listen = _box(rows, 6, 20, bg=GREEN, radius=3, bw=0)
        listen.add_flag(lv.obj.FLAG.IGNORE_LAYOUT)
        listen.remove_flag(lv.obj.FLAG.CLICKABLE)
        self._set_widgets["listen_marker"] = listen

        # Every row built without raising: only now is the screen a valid, complete
        # tree. The caller publishes it to self.ui.w and loads it.
        self._set_scr_partial = None
        return scr

    def _tester_levels(self):
        """Return hard max, safe max and current unity-input peak."""
        name, _receiver_mode, _kind, _carrier, _mod_hz, depth, _deviation = \
            self._INJ_PRESETS[self._inj_mode]
        hard_max = 1000 if name == "AM" else 2000
        depth = depth if name == "AM" else 0
        safe_max = tester_safe_amplitude(hard_max, depth)
        peak = tester_peak_counts(self._inj_ampl, depth)
        return hard_max, safe_max, peak, 1000

    def _tester_point_label(self):
        if self._inj_point == 1:
            return "M:" + self._INJ_MIDS[self._inj_mid][0]
        return self._INJ_POINTS[self._inj_point][0]

    def _tester_first_visible_block(self):
        """First SCOPE block that contains TESTER rather than the real ADC."""
        if self._inj_point == 0:
            return 2
        if self._inj_point == 1:
            return 3 + self._inj_mid       # IQC=3, NCO=4, CHF=5
        return 5                           # OUT is before block-5 tap/scope capture

    def _output_status_text(self):
        """Source@stage description of the physical DAC output."""
        if 1 <= self._scope_id <= 5:
            if self._inj_on:
                source = ("TEST" if self._scope_id >= self._tester_first_visible_block()
                          else "ADC")
            else:
                source = "RX"
            return source + "@" + self._OUTPUT_STAGE[self._scope_id]
        if 6 <= self._scope_id <= 11:
            if not self._blk_on.get(6, True):
                source = "RAW"
            else:
                source = "TEST" if self._inj_on else "RX"
            return source + "@" + self._OUTPUT_STAGE[self._scope_id]
        if not self._blk_on.get(6, True):
            return "RAW@FINAL"
        if self._inj_on:
            if self._inj_source:
                mode = self._iq_file_profiles[self._iq_file_preset][0]
                if mode.startswith("R:"):
                    return "RECORD@FINAL"
            else:
                mode = self._INJ_PRESETS[self._inj_mode][0]
            if mode == "AM":
                return "AM1k@FINAL"
            if mode == "FM":
                return "FM1k/D4k@FINAL"
            if mode == "USB" or mode == "LSB":
                return "SSB1k5@FINAL"
            if mode == "CW":
                return "CW700@FINAL"
            return "MODE@FINAL"
        return "HEAR@FINAL"

    def _paint_output_status(self):
        label = self._set_widgets.get("output_header")
        if label is not None:
            label.set_text(self._output_status_text())

    def _configure_inject_mid(self, iq):
        """Publish the one movable MID boundary, preserving legacy pre-NCO MID."""
        if self._inj_point != 1:
            return True
        label, attr = self._INJ_MIDS[self._inj_mid]
        fn = getattr(iq, "inject_mid", None)
        api = getattr(iq, "INJECT_MID_API_VERSION", 0)
        attrs = tuple(getattr(iq, item[1], None) for item in self._INJ_MIDS)
        complete = fn is not None and api >= 1 and attrs == (3, 4, 5)
        legacy = fn is None and api == 0 and attrs == (None, None, None)
        if not complete:
            if legacy and self._inj_mid == self._INJ_MID_DEFAULT:
                return True
            self.be.err = "MID %s unavailable (need INJECT_MID_API_VERSION>=1 + stages)" % label
            return False
        stage = attrs[self._inj_mid]
        try:
            current = fn(stage)
            if current is not None and int(current) != stage:
                self.be.err = "inject_mid readback %r != %d" % (current, stage)
                return False
            return True
        except Exception as e:
            self.be.err = "inject_mid: %r" % (e,)
            return False

    def _paint_mid_labels(self):
        """Repaint/lock the three zero-widget ASCII radios in block-name labels."""
        entries = self._set_widgets.get("mid_labels")
        if not entries:
            return
        for _bid, item in entries.items():
            label, name, idx = item
            selected = idx == self._inj_mid
            label.set_text(("[x] " if selected else "[ ] ") + name)
            label.set_style_text_color(
                lv.color_hex(CYAN_RX if selected and self._inj_point == 1 else WHITE), 0)
            if self._inj_on:
                label.remove_flag(lv.obj.FLAG.CLICKABLE)
            else:
                label.add_flag(lv.obj.FLAG.CLICKABLE)

    def _paint_listen_markers(self):
        """Move the single graphical marker to the block feeding the physical DAC(s)."""
        marker = self._set_widgets.get("listen_marker")
        if marker is None:
            return
        scope = self._scope_id
        # SCOPE 0 is the normal receiver route: DA0 comes from the final Limiter.
        stage = scope if 2 <= scope <= 11 else 11
        marker.set_pos(1, (stage - 2) * _VERIFY_SCROLL_STEP + 4)
        # Cyan = complex I/Q on DA0/DA1; green = mono audio heard on DA0.
        marker.set_style_bg_color(
            lv.color_hex(CYAN_RX if 1 <= scope <= 5 else GREEN), 0)

    def _restore_tester_scope(self):
        """Restore the route displaced by TESTER, including VERIFY row paint."""
        if self._inj_prev_scope is None:
            return True
        restore = self._inj_prev_scope
        previous = self._scope_id
        if not self.be.set_scope(restore):
            return False
        self._scope_id = restore
        self._inj_prev_scope = None
        self._paint_output_status()
        blocks = self._set_widgets.get("blocks")
        if blocks:
            for bid, on in ((previous, False), (restore, True)):
                if not bid or bid not in blocks:
                    continue
                r, _nl, _name, _ob, _ol, sb, sl, _tb, _tl = blocks[bid]
                sb.set_style_bg_color(lv.color_hex(CYAN_RX if on else PANEL2), 0)
                sl.set_style_text_color(
                    lv.color_hex(DARK_TXT if on else CYAN_RX), 0)
                r.set_style_bg_color(lv.color_hex(BORDER if on else PANEL2), 0)
        self._paint_listen_markers()
        return True

    def _restore_tester_receiver(self):
        """Restore demod/filter and the normal station's exact LO-relative NCO."""
        if self.be.iq is None:
            return True
        mode = self.p["m"]
        ok = self.be.set_mode(mode)
        if not self.be.set_bandwidth(self.p["bw"].get(mode, MODE_BW[mode])):
            ok = False
        nco_on = self._nco_enabled()
        requested = self.p["f"]
        old_centre = self._lo_hz + self.be.fine_hz

        # With NCO active, restore the saved station's LO-relative offset.  With
        # NCO bypassed, preload zero instead: the currently heard frequency is the
        # physical LO, and re-enabling block 4 before the queued LO move must not
        # expose either the stale TESTER offset or a hidden saved-station offset.
        wanted = (requested - self._lo_hz) if nco_on else 0
        actual = self.be.set_fine(wanted)
        if actual is None:
            return False
        nco_label = self._set_widgets.get("nco")
        if nco_label is not None:
            nco_label.set_text("%d" % actual)

        if not nco_on:
            # The preloaded value is real state but does not currently affect the
            # signal.  With NCO bypassed the heard centre is the physical LO.  Keep
            # HOME/panorama truthful now, and queue an LO move back to the requested
            # normal station without doing I2C in this callback.
            heard = self._lo_hz
            if requested != heard:
                self._commit_current_frequency(heard)
                self._queue_station_recenter(requested)
            else:
                self.update_freq()
            if actual != 0:
                self.be.err = "normal NCO clear returned %d" % actual
                ok = False
            return ok

        achieved = min(max(self._lo_hz + actual, F_MIN), F_MAX)
        if achieved != requested:
            self.be.err = "normal NCO restore clamped %d -> %d" % (wanted, actual)
            self._commit_current_frequency(achieved)
            ok = False
        elif achieved != old_centre:
            self.update_freq()
        # HOME may deliberately change demodulation while TESTER remains armed.
        # Keep the test source/NCO fixed during that comparison, then restore the
        # newly selected mode's physical zero/+3k/+6k low-IF policy only on TEST OFF.
        if self._lo_hz != self._normal_lo_hz(requested, mode):
            self._queue_station_recenter(requested, mode=mode)
        return ok

    def _paint_tester(self):
        # HOME remains alive behind VERIFY.  Keep its shared tuning control truthful
        # even if VERIFY is closed before a FILE reaches EOF.
        self.update_rx()
        self._update_tuning_role()
        self._paint_output_status()
        w = self._set_widgets.get("inject")
        if not w:
            return
        (isb, isl, ipb, ipl, imb, iml, iwb, iwl, ilb, ill, iamp,
         idn, idnl, iup, iupl, itg, itgl) = w
        file_mode = bool(self._inj_source)
        if file_mode:
            isl.set_text("TESTER FILE")
            isl.set_style_text_color(self._C_CYAN, 0)
        else:
            _hard, safe_max, peak, _gain = self._tester_levels()
            unsafe = self._inj_ampl > safe_max
            clipped = peak > 2047
            if self._inj_overload:
                isl.set_text("GEN CLIP" if clipped else "GEN OVR")
            else:
                isl.set_text("GEN<=%d" % safe_max)
            isl.set_style_text_color(self._C_RED if (unsafe or clipped)
                                     else self._C_CYAN, 0)
        ipl.set_text(self._tester_point_label())

        # Source and injection point own native buffer/rate semantics and are locked
        # for the complete ON interval.  Other GEN controls remain live-editable.
        if self._inj_on:
            isb.remove_flag(lv.obj.FLAG.CLICKABLE)
            ipb.remove_flag(lv.obj.FLAG.CLICKABLE)
        else:
            isb.add_flag(lv.obj.FLAG.CLICKABLE)
            ipb.add_flag(lv.obj.FLAG.CLICKABLE)

        if file_mode:
            ferr = self._iq_file.error or ""
            if "BIT UNSUPPORTED" in ferr:
                iml.set_text("%dBIT" % self._iq_file.sample_bits)
            elif "NEEDS RESAMPLE" in ferr:
                iml.set_text("RATE!")
            elif "CRC" in ferr:
                iml.set_text("CRC!")
            elif "LOOP NEEDS" in ferr:
                iml.set_text("SHORT!")
            elif "API" in ferr:
                iml.set_text("API!")
            elif " UND " in ferr or "SCHED" in ferr:
                iml.set_text("UND!")
            elif ferr:
                iml.set_text("FILE!")
            else:
                iml.set_text(self._iq_file_profiles[self._iq_file_preset][0])
            rate = self._iq_file.sample_rate
            bits = self._iq_file.sample_bits or 16
            if rate <= 0:
                rate = IQ_RATE if self._inj_point == 0 else IQ_RATE // 2
            iwl.set_text("%dK/%d" % (rate // 1000, bits))
            ill.set_text("LOOP" if self._iq_file_loop else "ONCE")
            ill.set_style_text_color(
                lv.color_hex(DARK_TXT if self._iq_file_loop else CYAN_RX), 0)
            ilb.set_style_bg_color(
                lv.color_hex(GREEN if self._iq_file_loop else PANEL2), 0)
            if self._inj_on:
                ilb.remove_flag(lv.obj.FLAG.CLICKABLE)
            else:
                ilb.add_flag(lv.obj.FLAG.CLICKABLE)
            pct = self._iq_file_ui_pct
            iamp.set_text("ERR" if ferr else
                          ("--" if pct < 0 else "%d%%" % pct))
            iamp.set_style_text_color(self._C_RED if ferr else self._C_CYAN, 0)
            iamp.remove_flag(lv.obj.FLAG.CLICKABLE)
            idnl.set_text("|<")
            if self._inj_on:
                idn.remove_flag(lv.obj.FLAG.CLICKABLE)
            else:
                idn.add_flag(lv.obj.FLAG.CLICKABLE)
            iupl.set_text("")
            iup.add_flag(lv.obj.FLAG.HIDDEN)
            if self._inj_on:
                imb.remove_flag(lv.obj.FLAG.CLICKABLE)
            else:
                imb.add_flag(lv.obj.FLAG.CLICKABLE)
            iwb.remove_flag(lv.obj.FLAG.CLICKABLE)
        else:
            ilb.add_flag(lv.obj.FLAG.CLICKABLE)
            mode_name = self._INJ_PRESETS[self._inj_mode][0]
            iml.set_text(mode_name)
            iwl.set_text("SIN" if mode_name == "FM" else
                         self._INJ_WAVES[self._inj_wave][0])
            ill.set_text("LIVE" if self._inj_live else "CLEAN")
            ill.set_style_text_color(
                lv.color_hex(DARK_TXT if self._inj_live else CYAN_RX), 0)
            ilb.set_style_bg_color(
                lv.color_hex(GREEN if self._inj_live else PANEL2), 0)
            _hard, safe_max, peak, _gain = self._tester_levels()
            unsafe = self._inj_ampl > safe_max
            clipped = peak > 2047
            iamp.set_text(("O" if self._inj_overload else "S") +
                          ("!" if (unsafe or clipped) else "") +
                          "%d" % self._inj_ampl)
            iamp.set_style_text_color(self._C_RED if (unsafe or clipped)
                                       else self._C_CYAN, 0)
            iamp.add_flag(lv.obj.FLAG.CLICKABLE)
            idnl.set_text("-")
            iupl.set_text("+")
            iup.remove_flag(lv.obj.FLAG.HIDDEN)
            imb.add_flag(lv.obj.FLAG.CLICKABLE)
            if mode_name == "FM":
                iwb.remove_flag(lv.obj.FLAG.CLICKABLE)
            else:
                iwb.add_flag(lv.obj.FLAG.CLICKABLE)

        itgl.set_text("ON" if self._inj_on else
                      ("WAIT" if self._tester_arm_pending else "OFF"))
        itgl.set_style_text_color(
            lv.color_hex(DARK_TXT if self._inj_on else WHITE), 0)
        itg.set_style_bg_color(
            lv.color_hex(GREEN if self._inj_on else
                         (CYAN_IN if self._tester_arm_pending else PANEL2)), 0)
        self._paint_mid_labels()

    def _finish_iq_file(self, error=None):
        """Fail closed, then restore the receiver settings used before FILE."""
        self._iq_file.stop()
        self._inj_on = False
        self._inj_fm_nco = False    # TESTER FM temporarily owns the pre-NCO offset
        receiver_restored = self._restore_tester_receiver()
        restored = self._restore_tester_scope()
        if not receiver_restored:
            restore_error = self.be.err or "receiver restore failed"
            error = ((error + "; ") if error else "") + restore_error
        if not restored:
            restore_error = self.be.err or "scope restore failed"
            error = ((error + "; ") if error else "") + restore_error
        if error:
            self.be.err = error
            self._iq_file.error = error
        # Also restores HOME's TST -> RX label when EOF/error happens while VERIFY
        # is closed.  _paint_tester() safely returns after updating shared HOME state.
        self._paint_tester()
        return restored

    def _poll_iq_file(self):
        """2-Hz FILE status paint; the scheduled refill path never touches LVGL."""
        if not self._inj_on:
            # A failed native scope setter leaves _inj_prev_scope intact.  Retry on
            # the slow status cadence instead of painting a false restored state.
            if self._inj_prev_scope is not None and self.be.running:
                self._restore_tester_scope()
            return
        if not self._inj_source:
            return
        st = self._iq_file.poll()
        if self._iq_file.error is not None:
            self.be.err = self._iq_file.error
        if st is None:
            self._finish_iq_file(self._iq_file.error or "SDRIQ status failed")
            return
        # UND/SCHED are cumulative transport diagnostics, not terminal states.
        # Native FILE zero-fills a missed block, preserves the pending FREE mask,
        # and retries the refill from a later ADC notification.  Stopping here on
        # the first recoverable UI/render delay truncated otherwise valid files.
        # A real read/refill exception sets self._iq_file.error + EOF and remains
        # fail-closed until the EOF path below performs the explicit teardown.
        pct = self._iq_file.percent()
        if pct != self._iq_file_ui_pct:
            self._iq_file_ui_pct = pct
            w = self._set_widgets.get("inject")
            if "settings" in _KEEP and w:
                w[10].set_text("%d%%" % pct)
        # A zero-byte commit is consumed only after every older data commit.  At this
        # point C has stopped, so closing the file and restoring DA0/DA1 cannot truncate
        # the tail of an ONCE capture.
        # Native EOF/error is fail-closed: requested becomes false but FILE remains
        # the active owner and supplies zero I/Q until this control-plane stop.  That
        # keeps HOME's TST label truthful instead of exposing ADC for up to one 2-Hz
        # status interval.  Do not wait for active to fall by itself.
        if self._iq_file.eof and not st[1]:
            self._finish_iq_file()

    def _refresh_settings(self):
        """Re-read the current state into every settings-screen control on each open,
        so a re-shown screen never displays the values captured at build time. Live
        read-outs (AGC gain, S-meter) keep updating via _consume_status while open."""
        w = self._set_widgets

        # Read back the real DSP block mask while IQADC is live. Getter-only
        # block(id) is control-context safe.
        iq = self.be.iq
        block_fn = getattr(iq, "block", None) if iq is not None else None
        if block_fn is not None:
            for bid in range(2, 11):
                try:
                    self._blk_on[bid] = bool(block_fn(bid))
                except Exception:
                    pass

        # Per-block table: repaint every ON/BYP, and re-apply the ONE-OF scope + tap
        # highlight so a re-shown screen matches the current routing exactly. _tap_id /
        # _scope_id are the single armed rows (0 = none); every other row is cleared.
        blk_w = w["blocks"]
        for bid, tpl in blk_w.items():
            r, _nl, _name, ob, ol, sb, sl, tb2, tl = tpl
            if bid == 11:
                ol.set_text("SAFE")
                ol.set_style_text_color(lv.color_hex(GRAY2), 0)
                ob.set_style_bg_color(lv.color_hex(PANEL2), 0)
            else:
                on0 = self._blk_on.get(bid, True)
                ol.set_text("ON" if on0 else "BYP")
                ol.set_style_text_color(lv.color_hex(DARK_TXT if on0 else WHITE), 0)
                ob.set_style_bg_color(lv.color_hex(GREEN if on0 else PANEL2), 0)

            scoped = (bid == self._scope_id)
            sb.set_style_bg_color(lv.color_hex(CYAN_RX if scoped else PANEL2), 0)
            sl.set_style_text_color(lv.color_hex(DARK_TXT if scoped else CYAN_RX), 0)
            r.set_style_bg_color(lv.color_hex(BORDER if scoped else PANEL2), 0)

            if bid in self._BLK_TAP:
                tapped = (bid == self._tap_id) and (self._tap_stage != 0)
                tb2.set_style_bg_color(lv.color_hex(GREEN if tapped else PANEL2), 0)
                tl.set_style_text_color(
                    lv.color_hex(DARK_TXT if tapped else CYAN_RX), 0)

        self._paint_output_status()
        self._paint_listen_markers()

        # SQUELCH threshold.
        w["squelch"].set_text("%d" % self._squelch)

        # AUDIO FILTER preset.
        w["af"].set_text(self._AF_PRESETS[self._af_preset][0])

        # Live DSP parameters exposed by VERIFY.
        w["bw"].set_text("BYP" if self.be.bw == 0 else fmt_bw(self.be.bw))
        w["nco"].set_text("%d" % self.be.fine_hz)
        w["agc_target"].set_text("%d%%" % int(self.p["atgt"] * 100 + 0.5))

        iq = self.be.iq
        if iq is not None:
            try:
                st_iqc = iq.iq_correction_status()
                self._iqc_on = bool(st_iqc.get("correcting", 0))
                self._iqc_amp = float(st_iqc.get("amp", 1.0))
                self._iqc_phase = float(st_iqc.get("phase", 0.0))
            except Exception:
                pass
            # Kernel defaults are not all zero (Hilbert is CMSIS by default).  Query
            # the hardware instead of repainting the stale Python construction cache.
            for meth in self._kernels:
                try:
                    self._kernels[meth] = 1 if getattr(iq, meth)() else 0
                except Exception:
                    pass

        iqeb, iqel = w["iqc_enable"]
        effective = self._iqc_on and self._blk_on.get(3, True)
        iqel.set_text("ON" if effective else
                      ("PATH" if self._iqc_on else "BYP"))
        iqel.set_style_text_color(
            lv.color_hex(DARK_TXT if effective else WHITE), 0)
        iqeb.set_style_bg_color(lv.color_hex(GREEN if effective else PANEL2), 0)
        w["iqc_amp"].set_text("%.2f" % self._iqc_amp)
        w["iqc_phase"].set_text("%+.2f" % self._iqc_phase)
        self._iqc_dirty = (bool(self.p.get("iqe")) != self._iqc_on or
                           abs(float(self.p.get("iqa", 1.0)) - self._iqc_amp) > 0.00005 or
                           abs(float(self.p.get("iqp", 0.0)) - self._iqc_phase) > 0.00005)
        w["iqc_profile"].set_text("EDIT" if self._iqc_dirty else "SAVED")
        w["iqc_profile"].set_style_text_color(
            lv.color_hex(CYAN_RX if self._iqc_dirty else GRAY2), 0)

        # TESTER source/point and source-specific compact row.
        self._paint_tester()

        # KERNELS: four A/B toggles.
        for meth, kb, kbl in w["kernels"]:
            st = self._kernels.get(meth, 0)
            kbl.set_style_text_color(lv.color_hex(DARK_TXT if st else WHITE), 0)
            kb.set_style_bg_color(lv.color_hex(GREEN if st else PANEL2), 0)

    # ---- generic modal list picker (step / AGC) ----
    def open_pick_menu(self, title, items, current, on_pick):
        """items = ((value, label), ...); on_pick(value) runs on selection."""
        if "pick_menu" in _KEEP:
            return
        self._set_modal(True)
        scr = self.ui.get("scr-receiver")
        scrim = lv.obj(scr)
        scrim.add_flag(lv.obj.FLAG.FLOATING)     # ignore the screen's flex layout
        scrim.remove_flag(lv.obj.FLAG.SCROLLABLE)
        scrim.set_pos(-8, -8)                    # compensate screen padding
        scrim.set_size(480, 272)
        scrim.set_style_bg_color(lv.color_hex(0x000000), 0)
        scrim.set_style_bg_opa(150, 0)
        scrim.set_style_border_width(0, 0)
        scrim.set_style_radius(0, 0)
        scrim.set_style_pad_all(0, 0)

        panel = lv.obj(scrim)
        panel.remove_flag(lv.obj.FLAG.SCROLLABLE)
        panel.set_size(220, min(252, 50 + 36 * len(items)))
        panel.center()
        panel.set_style_bg_color(lv.color_hex(PANEL), 0)
        panel.set_style_bg_opa(lv.OPA.COVER, 0)
        panel.set_style_border_color(lv.color_hex(BORDER), 0)
        panel.set_style_border_width(2, 0)
        panel.set_style_radius(8, 0)
        panel.set_style_pad_all(10, 0)
        panel.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        panel.set_flex_align(lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
                             lv.FLEX_ALIGN.CENTER)
        panel.set_style_pad_row(6, 0)

        t = lv.label(panel)
        t.set_text(title)
        t.set_style_text_font(font(14), 0)
        t.set_style_text_color(lv.color_hex(GRAY), 0)

        cbs = []
        for val, name in items:
            b = lv.button(panel)
            b.set_size(196, 30)
            on = val == current
            b.set_style_bg_color(lv.color_hex(CYAN_IN if on else BTN_RX), 0)
            b.set_style_radius(6, 0)
            b.set_style_border_width(0, 0)
            b.set_style_shadow_width(0, 0)
            b.set_flex_flow(lv.FLEX_FLOW.ROW)
            b.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER,
                             lv.FLEX_ALIGN.CENTER)
            l = lv.label(b)
            l.set_text(name)
            l.set_style_text_font(font(14), 0)
            l.set_style_text_color(lv.color_hex(DARK_TXT if on else WHITE), 0)

            def pick_cb(e, vv=val):
                self.close_pick_menu()
                on_pick(vv)
            b.add_event_cb(pick_cb, lv.EVENT.CLICKED, None)
            cbs.append(pick_cb)

        def scrim_cb(e):
            self.close_pick_menu()
        scrim.add_event_cb(scrim_cb, lv.EVENT.CLICKED, None)
        cbs.append(scrim_cb)
        _KEEP["pick_menu"] = (scrim, cbs)

    def close_pick_menu(self):
        m = _KEEP.pop("pick_menu", None)
        if m:
            m[0].delete()
        self._set_modal(("settings" in _KEEP) or ("gains_panel" in _KEEP))

    # ---- VFO -> hardware routing + CAL: full-screen ROUTE view (tap the VFO ----
    # ---- indicator / brand row). Level 2 of the 3-level nav: receiver -> route ----
    # ---- -> backend. Built lazily once as scr-route (a full-screen _base(lv.obj( ----
    # ---- None)), mirroring scr-settings), re-shown on later opens and re-read via ----
    # ---- _refresh_route so it always shows the CURRENT rt/cal. Callbacks are kept ----
    # ---- alive on self._route_cbs; _KEEP["route_menu"] is just an open-flag now. ----
    def _build_route(self):
        # Full-screen VFO ROUTING view: the three A/B/C hardware-target rows, the
        # crystal-ppm CAL trim, and a full-width BACKEND button that drills into the
        # DSP verification page. Same callbacks/behaviour as the old popup; the layout
        # copies scr-settings (dark app bg, title bar + BACK, then stacked rows).
        scr = _base(lv.obj(None))
        scr.set_style_bg_color(lv.color_hex(BG_RX), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        scr.set_style_pad_all(8, 0)
        _flex(scr, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 6)
        self.ui.w["scr-route"] = scr

        # --- title bar: "VFO ROUTING" (left) + BACK button (right) ---
        tb = _base(lv.obj(scr))
        tb.set_size(464, 30)
        _flex(tb, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN)
        _lbl(tb, "VFO ROUTING", 16, GRAY)
        back = _btn(tb, 80, 30, PANEL2, radius=6, border=BORDER)
        _lbl(back, "BACK", 14, WHITE)

        # Single column of rows on the 480x272 panel; each row is a label + control.
        col = _base(lv.obj(scr))
        col.set_size(464, 226)
        _flex(col, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
              lv.FLEX_ALIGN.CENTER, 6)

        cbs = []
        self._route_cbs = cbs        # keep every event cb alive for the screen lifetime
        self._route_widgets = {}     # label handles re-read by _refresh_route on each open

        def back_cb(e):
            self.close_route_menu()
        back.add_event_cb(back_cb, lv.EVENT.CLICKED, None)
        cbs.append(back_cb)

        def _rrow(label):
            row = lv.obj(col)
            row.remove_flag(lv.obj.FLAG.SCROLLABLE)
            row.set_size(456, 34)
            row.set_style_bg_opa(0, 0)
            row.set_style_border_width(0, 0)
            row.set_style_pad_all(0, 0)
            row.set_flex_flow(lv.FLEX_FLOW.ROW)
            row.set_flex_align(lv.FLEX_ALIGN.SPACE_BETWEEN, lv.FLEX_ALIGN.CENTER,
                               lv.FLEX_ALIGN.CENTER)
            row.set_style_pad_column(6, 0)
            l = lv.label(row)
            l.set_text(label)
            l.set_style_text_font(font(14), 0)
            l.set_style_text_color(lv.color_hex(WHITE), 0)
            return row

        def _rbtn(row, txt, w, size=14, color=WHITE):
            b = lv.button(row)
            b.set_size(w, 30)
            b.set_style_bg_color(lv.color_hex(PANEL2), 0)
            b.set_style_radius(6, 0)
            b.set_style_border_width(0, 0)
            b.set_style_shadow_width(0, 0)
            b.set_flex_flow(lv.FLEX_FLOW.ROW)
            b.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER,
                             lv.FLEX_ALIGN.CENTER)
            bl = lv.label(b)
            bl.set_text(txt)
            bl.set_style_text_font(font(size), 0)
            bl.set_style_text_color(lv.color_hex(color), 0)
            return b, bl

        # VFO A/B/C -> hardware target cycle (behaviour verbatim from the popup)
        for i in range(3):
            row = _rrow("VFO " + "ABC"[i])
            b, bl = _rbtn(row, TARGETS[self.p["rt"][i]][0], 300, 14, CYAN_RX)
            self._route_widgets["rt%d" % i] = bl

            def cyc(e, ii=i, lbl=bl):
                self.p["rt"][ii] = (self.p["rt"][ii] + 1) % len(TARGETS)
                lbl.set_text(TARGETS[self.p["rt"][ii]][0])
                if ii == self.p["act"]:
                    self._queue_hw_config()   # active VFO moved to a different output
                self.touch_params()
            b.add_event_cb(cyc, lv.EVENT.CLICKED, None)
            cbs.append(cyc)

        # CAL: live crystal-ppm trim (behaviour verbatim from the popup)
        crow = _rrow("CAL")
        cg = lv.obj(crow)
        cg.remove_flag(lv.obj.FLAG.SCROLLABLE)
        cg.set_size(300, 30)
        cg.set_style_bg_opa(0, 0)
        cg.set_style_border_width(0, 0)
        cg.set_style_pad_all(0, 0)
        cg.set_flex_flow(lv.FLEX_FLOW.ROW)
        cg.set_flex_align(lv.FLEX_ALIGN.END, lv.FLEX_ALIGN.CENTER,
                          lv.FLEX_ALIGN.CENTER)
        cg.set_style_pad_column(6, 0)
        cm, _ = _rbtn(cg, "-", 44, 20)
        cvl = lv.label(cg)
        cvl.set_text("%.2f ppm" % self.p["cal"])
        cvl.set_style_text_font(font(14), 0)
        cvl.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        cp, _ = _rbtn(cg, "+", 44, 20)
        self._route_widgets["cal"] = cvl

        def cal_adj(e, d=0, lbl=cvl):
            self.p["cal"] = round(min(max(self.p["cal"] + d,
                                            CAL_PPM_MIN), CAL_PPM_MAX), 2)
            lbl.set_text("%.2f ppm" % self.p["cal"])
            self._queue_hw_config()    # re-program synth via the worker (no I2C here)
            self.touch_params()
        for b, d in ((cm, -0.1), (cp, +0.1)):
            def cb(e, dd=d):
                cal_adj(e, dd)
            # SHORT_CLICKED is suppressed after a long press; CLICKED is not and
            # used to add one unwanted terminal 0.1-ppm step on release.
            b.add_event_cb(cb, lv.EVENT.SHORT_CLICKED, None)
            b.add_event_cb(cb, lv.EVENT.LONG_PRESSED_REPEAT, None)
            cbs.append(cb)

        # BACKEND button: drill one level deeper into the DSP verification page.
        bkb = _btn(col, 456, 36, PANEL2, radius=6, border=CYAN_RX, bw=1)
        _lbl(bkb, "BACKEND  >", 14, CYAN_RX)

        def backend_cb(e):
            # ROUTE and VERIFY are mutually exclusive owners of the spare GUI heap.
            # HOME is loaded only as a safe deletion target and is not refreshed in
            # between, so the operator still sees a direct ROUTE -> VERIFY transition.
            _KEEP.pop("route_menu", None)
            self._drop_route_screen()
            self.open_settings()        # level 3: DSP verification page
        bkb.add_event_cb(backend_cb, lv.EVENT.CLICKED, None)
        cbs.append(backend_cb)

    def open_route_menu(self):
        if "route_menu" in _KEEP:
            if self.ui.w.get("scr-route") is not None:
                return
            _KEEP.pop("route_menu", None)  # stale flag from an interrupted build
        if self.ui.w.get("scr-route") is None:
            gc.collect()
            self._build_route()
        _KEEP["route_menu"] = True       # open-flag
        self._set_modal(True)
        lv.screen_load(self.ui.get("scr-route"))
        self._refresh_route()            # re-read live state so a re-shown screen is current

    def _refresh_route(self):
        """Re-read the current rt/cal into the route-screen labels on each open, so a
        re-shown screen never displays the values captured at build time."""
        w = self._route_widgets
        for i in range(3):
            w["rt%d" % i].set_text(TARGETS[self.p["rt"][i]][0])
        w["cal"].set_text("%.2f ppm" % self.p["cal"])

    def _drop_route_screen(self):
        scr = self.ui.w.get("scr-route")
        if scr is not None:
            lv.screen_load(self.ui.get("scr-receiver"))
            scr.delete()
            self.ui.w["scr-route"] = None
        self._route_cbs = []
        self._route_widgets = {}
        gc.collect()

    def close_route_menu(self):
        _KEEP.pop("route_menu", None)
        self._drop_route_screen()
        self._set_modal(("settings" in _KEEP) or ("gains_panel" in _KEEP) \
            or ("pick_menu" in _KEEP))

    def _open_bottom_choices(self, state, choices, current):
        self._mode_expanded = state
        self.ui.get("tuning-row").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("mode-bar").remove_flag(lv.obj.FLAG.HIDDEN)
        pairs = state == 4
        for i, name in enumerate(MODES):
            b = self.ui.get("btn-" + name)
            if i < len(choices):
                item = choices[i]
                value, text = item if pairs else (item, fmt_bw(item))
                on = value == current
                b.get_child(0).set_text(text)
                b.set_style_bg_color(lv.color_hex(GREEN if on else BTN_RX), 0)
                b.set_style_border_color(lv.color_hex(GREEN if on else BORDER), 0)
                b.get_child(0).set_style_text_color(
                    lv.color_hex(DARK_TXT if on else WHITE), 0)
                b.remove_flag(lv.obj.FLAG.HIDDEN)
            else:
                b.add_flag(lv.obj.FLAG.HIDDEN)
        for name in ("mode-step", "mode-filter", "mode-view"):
            self.ui.get("btn-" + name).add_flag(lv.obj.FLAG.HIDDEN)

    def open_step_menu(self):
        self._open_bottom_choices(4, STEPS, self.p["s"])

    def open_step_controls(self):
        """Close the step selector and return to the permanent HOME controls."""
        self._set_mode_bar(False)

    def open_filter_menu(self):
        self._open_bottom_choices(2, BW_CHOICES[self.p["m"]], self.cur_bw())

    def toggle_spectrum_view(self):
        """Cycle the left native panel SPEC -> WF -> OFF -> SPEC."""
        if not self._spec_native or not hasattr(self._spec_lcd, "spectrum"):
            return
        new_view = (self._spectrum_view + 1) % 3
        try:
            if self._spec_lcd.spectrum(new_view):
                self._spectrum_view = new_view
                self.ui.get("btn-mode-view").get_child(0).set_text(
                    ("SPEC", "WF", "OFF")[new_view])
        except Exception as e:
            self.be.err = "spectrum view: %r" % (e,)

    def toggle_scope_view(self):
        """Cycle the right native panel TIME -> I-Q -> OFF -> TIME."""
        if not self._spec_native or not hasattr(self._spec_lcd, "scope_view"):
            return
        new_view = (self._scope_view + 1) % 3
        try:
            if self._spec_lcd.scope_view(new_view):
                self._scope_view = new_view
                self.ui.get("scope-view").set_text(("TIME", "I-Q", "OFF")[new_view])
        except Exception as e:
            self.be.err = "scope view: %r" % (e,)

    # ---- navigation ----
    def open_entry(self):
        # pre-fill with the current frequency so the user can backspace just a
        # few digits and retype them (partial edit) instead of starting over
        self.entry = "%02d%03d%03d" % (self.p["f"] // 1_000_000,
                                       (self.p["f"] // 1000) % 1000,
                                       self.p["f"] % 1000)
        self.update_entry_digits()
        self.update_entry_bands()
        self._set_modal(True)
        lv.screen_load(self.ui.get("scr-freq-input"))

    def close_entry(self, accept):
        if accept and self.entry:
            hz = self._entry_hz()
            if F_MIN <= hz <= F_MAX:
                old = self.p["f"]
                if self.be.running:
                    # Nearby keypad entries can be selected immediately by NCO.  A
                    # discontinuity stays pending and the HOME display remains on the
                    # last achieved frequency until sdr_poll confirms the new LO.
                    moved = False
                    if (self._nco_enabled() and
                            abs(int(hz) - self._lo_hz) < (IQ_RATE // 4)):
                        moved = self._move_live_frequency(
                            int(hz) - self._requested_hz)
                    if not moved and int(hz) != self.p["f"]:
                        self._queue_station_recenter(hz)
                elif int(hz) != old:
                    self.shift_spectrum(int(hz) - old)
                    self._commit_current_frequency(hz)
                    self._cancel_frequency_pending()
                    self._lo_pending_hz = self._normal_lo_hz(hz)
                    self._hw_pending = True
        self.entry = ""
        self.update_freq()
        lv.screen_load(self.ui.get("scr-receiver"))
        self._set_modal(False)

    # ---- wiring ----
    def _wire(self):
        ui = self.ui
        add = lambda name, fn: ui.get(name).add_event_cb(fn, lv.EVENT.CLICKED, None)
        cbs = []

        def mk(fn):
            cbs.append(fn)
            return fn

        def repeat_button(name, op):
            """One step on tap; native LVGL auto-repeat while held.

            Do not combine CLICKED with LONG_PRESSED_REPEAT: LVGL also emits
            CLICKED after a long press, which would add one unwanted terminal
            step.  This one callback and its two booleans are allocated once at
            wiring time; holding creates neither a Python timer nor callbacks.
            """
            state = [False, False]     # active press, at least one repeat emitted

            def repeat_cb(e):
                code = e.get_code()
                if code == lv.EVENT.PRESSED:
                    state[0] = True
                    state[1] = False
                elif code == lv.EVENT.LONG_PRESSED:
                    if state[0]:
                        op()
                        state[1] = True
                elif code == lv.EVENT.LONG_PRESSED_REPEAT:
                    if state[0]:
                        op()
                        state[1] = True
                elif code == lv.EVENT.RELEASED:
                    if state[0] and not state[1]:
                        op()
                    state[0] = False
                    state[1] = False
                elif code == lv.EVENT.PRESS_LOST:
                    state[0] = False
                    state[1] = False

            cb = mk(repeat_cb)
            b = ui.get(name)
            b.add_event_cb(cb, lv.EVENT.PRESSED, None)
            b.add_event_cb(cb, lv.EVENT.LONG_PRESSED, None)
            b.add_event_cb(cb, lv.EVENT.LONG_PRESSED_REPEAT, None)
            b.add_event_cb(cb, lv.EVENT.RELEASED, None)
            b.add_event_cb(cb, lv.EVENT.PRESS_LOST, None)

        repeat_button("btn-step-down", lambda: self.tune(-self.p["s"]))
        repeat_button("btn-step-up", lambda: self.tune(self.p["s"]))
        repeat_button("btn-fine-down",
                      lambda: self.fine(-max(self.p["s"] // 10, 1)))
        repeat_button("btn-fine-up",
                      lambda: self.fine(max(self.p["s"] // 10, 1)))
        # The HOME summary opens three large choices in the same bottom slot.
        add("step-display",  mk(lambda e: self.open_home_choices()))
        add("freq-digits", mk(lambda e: self.open_entry()))
        for k in (0, 1):
            def alt_cb(e, kk=k):
                self.switch_vfo(self._alt[kk])
            add("vfo-alt-%d" % k, mk(alt_cb))
        def brand_cb(e):
            if "gains_panel" in _KEEP:
                self.close_gains_panel()         # SDR RECEIVER exits inline gains
            elif self._mode_expanded:
                self._set_mode_bar(False)       # SDR RECEIVER exits any bottom control
            else:
                self.open_route_menu()          # level 2: VFO routing + CAL + BACKEND button
        _brand_cb = mk(brand_cb)
        add("brand-row", _brand_cb)
        add("vfo-indicator", _brand_cb)         # clickable child otherwise consumes the tap

        def spec_cb(e):
            indev = lv.indev_active()
            if indev is None:
                return
            pt = lv.point_t()
            indev.get_point(pt)
            a = lv.area_t()
            ui.get("spectrum-waterfall").get_coords(a)
            local_x = pt.x - a.x1
            local_y = pt.y - a.y1
            # The native surface is physically 256 px spectrum + 4 px gap +
            # 128 px right panel.  A left tap tunes; a right tap is the zero-widget
            # TIME/I-Q control.  This also fixes the former frequency mapping, which
            # incorrectly stretched the 256 spectrum pixels across all 388 pixels.
            if self._spec_native:
                if 0 <= local_x < 256 and 0 <= local_y < 92:
                    # Reserve only the existing 18-px label band for view cycling.
                    # Every measured pixel below it, including the complete live
                    # histogram, selects the independent NCO marker and never asks
                    # Si5351/VFO to move.  The header remains usable while OFF.
                    if local_y < NATIVE_GRAPH_HEADER_H:
                        self.toggle_spectrum_view()
                    else:
                        self.spec_jump(local_x / 255.0)
                elif 260 <= local_x < 388 and 0 <= local_y < 92:
                    self.toggle_scope_view()
            elif 0 <= local_x < 388:
                # The Python fallback bars span the complete object and have no
                # native right-hand panel.
                self.spec_jump(local_x / 387.0)
        add("spectrum-area", mk(spec_cb))

        add("rx-button", mk(lambda e: self.toggle_rx()))

        for i, m in enumerate(MODES):
            def mode_cb(e, mm=m, ii=i):
                if self._mode_expanded == 2:
                    choices = BW_CHOICES[self.p["m"]]
                    if ii < len(choices):
                        hz = choices[ii]
                        mode = self.p["m"]
                        self.p["bw"][mode] = hz
                        self.be.set_bandwidth(hz)
                        self.touch_params()
                        self.update_mode()
                elif self._mode_expanded == 4:
                    if ii < len(STEPS):
                        self.p["s"] = STEPS[ii][0]
                        self.update_step()
                        self.touch_params()
                        self.open_step_controls()
                elif self._mode_expanded == 1:
                    self.set_mode(mm)
                    # FM can be rejected while the backend is enabled; the row still
                    # closes and keeps the previous valid selection visible.
                    self._set_mode_bar(False)
                else:
                    self._set_mode_bar(True)
            add("btn-" + m, mk(mode_cb))

        add("btn-mode-step", mk(lambda e: self.open_step_menu()))
        add("btn-mode-filter", mk(lambda e: self.open_filter_menu()))
        add("btn-mode-view", mk(lambda e: self.toggle_spectrum_view()))

        def vol_cb(e):
            # Fires on RELEASED / PRESS_LOST -- once, when the finger lifts -- NOT on every
            # drag movement. LVGL still moves the knob live; only the firmware apply + label
            # update happen here. Stays on RELEASED/PRESS_LOST on purpose: VALUE_CHANGED ran a
            # Python event callback on every drag pixel (wrapper alloc + a repaint each), an
            # interactive garbage burst that storms GC.
            # The permanent far-right slider controls the one item explicitly chosen
            # by the inline panel's one-of checkbox group.
            v = ui.get("vol-slider").get_value()
            self._apply_gain(self._active_gain, v)
            _lo, _hi, _cur, fmt = self._gain_spec(self._active_gain)
            ui.get("vol-value").set_text(fmt(v))
        _volcb = mk(vol_cb)
        _vsl = ui.get("vol-slider")
        _vsl.add_event_cb(_volcb, lv.EVENT.RELEASED, None)
        _vsl.add_event_cb(_volcb, lv.EVENT.PRESS_LOST, None)
        # VOL and its numeric value are child labels which occupy practically the
        # complete header.  Bind the same callback to all three possible event
        # targets; no EVENT_BUBBLE means one physical tap can toggle only once.
        _gain_toggle_cb = mk(lambda e: self.toggle_gains_panel())
        for _name in ("vol-header", "vol-label", "vol-value"):
            add(_name, _gain_toggle_cb)

        def gain_pin_cb(e):
            # One shared checkbox, in the normal value position.  Moving a large
            # slider chooses the candidate; checking here pins that candidate as
            # the one permanent far-right control after the panel is closed.
            if "gains_panel" not in _KEEP:
                return
            if self._gain_available(self._gain_candidate):
                self._active_gain = self._gain_candidate
            self._paint_gain_pin()
        add("gain-pin", mk(gain_pin_cb))

        add("agc-pill", mk(lambda e: self.open_agc_menu()))

        # entry screen
        for i, tag in enumerate(("a", "b", "c")):
            def vfo_cb(e, ii=i):
                self.switch_vfo(ii)
            add("vfo-" + tag, mk(vfo_cb))
        add("close-btn",     mk(lambda e: self.close_entry(False)))
        add("cancel-button", mk(lambda e: self.close_entry(False)))
        add("ok-button",     mk(lambda e: self.close_entry(True)))
        for kname in ("1", "2", "3", "4", "5", "6", "7", "8", "9", "0", ".", "BS"):
            def key_cb(e, kk=kname):
                self.key(kk)
            add("key-" + kname, mk(key_cb))
        for name, _l, _lo, _hi, base in BANDS:
            def band_cb(e, hz=base):
                self.entry = "%02d%03d%03d" % (hz // 1_000_000,
                                               (hz // 1000) % 1000, hz % 1000)
                self.update_entry_digits()
                self.update_entry_bands()
            add("band-" + name, mk(band_cb))
        for s, name in STEPS:
            def step_cb(e, ss=s):
                self.p["s"] = ss
                self.update_step()
                self.touch_params()
            add("step-" + name, mk(step_cb))

        # One 100 ms GUI tick. Control writes are coalesced and run outside touch event
        # callbacks, but they never gate the spectrum: spectrum runs every tick (10 Hz),
        # while counters run every fifth tick (2 Hz). There is deliberately NO periodic
        # Si5351 "probe": it used to reprogram the unchanged LO every 2 s, block this same
        # Python/LVGL thread, and steal a spectrum frame. Real LO/routing/calibration
        # changes set _hw_pending explicitly.
        def sdr_poll(t):
            try:
                # Defensive release: a lost touch packet must never leave global
                # LVGL invalidation disabled after a VERIFY drag.
                if self._set_scroll_gate:
                    rows = self._set_widgets.get("rows")
                    if rows is None:
                        self._end_verify_scroll_gate()
                    elif rows.is_scrolling():
                        self._set_scroll_idle = 0
                    else:
                        self._set_scroll_idle += 1
                        if self._set_scroll_idle >= 10:  # 1-s lost-release fail-safe
                            self._end_verify_scroll_gate()
                if self._hw_pending:
                    self._apply_hw_pending()  # the ONLY Si5351 I2C path
                if self._axis_pending_token:
                    self._poll_axis_generation()
                if self._tester_arm_pending and not self._tester_tune_pending():
                    self._arm_tester_source()
                if self._vol_pending:
                    self._vol_pending = False
                    self.be.set_volume(self.p["v"])   # coalesced from the drag
                    self.update_vol()
                    self.touch_params()
                self._poll_div += 1
                if self._poll_div >= 5:
                    self._poll_div = 0
                    self._consume_status()
                self._consume_spectrum()
                if self._set_scroll_quiet:
                    self._set_scroll_quiet -= 1
            except Exception as e:
                self.be.err = "sdr_poll: %r" % (e,)
        self.sdr_timer = lv.timer_create(sdr_poll, 100, None)
        _KEEP["sdr_poll"] = sdr_poll

        # blinking cursor
        cur = ui.get("blinking-cursor")
        def blink_cb(t):
            if cur.has_flag(lv.obj.FLAG.HIDDEN):
                cur.remove_flag(lv.obj.FLAG.HIDDEN)
            else:
                cur.add_flag(lv.obj.FLAG.HIDDEN)
        self.blink_timer = lv.timer_create(blink_cb, 500, None)
        _KEEP["cbs"] = cbs
        _KEEP["blink"] = blink_cb


# ---------------- single DIRECT-display workflow ----------------

def start():
    # LVGL first: collecting/allocating before lv.init() hard-faults at cold boot
    if not lv.is_initialized():
        lv.init()

    # start() is also used from mpremote/Thonny while this module is already live.
    # In that case sys.modules keeps _KEEP, including modal navigation flags.  A
    # stale "settings" flag made open_settings() return before loading VERIFY, so
    # the ROUTE -> BACKEND button appeared dead.  Rebuilding here would be worse:
    # the old LVGL timers/callbacks would keep running beside a second UI.  Make a
    # repeated start idempotent instead -- dismiss transient overlays, return HOME,
    # and reuse the one existing app/event loop.
    old_app = _KEEP.get("app")
    old_ui = _KEEP.get("ui")
    if old_app is not None and old_ui is not None:
        pick = _KEEP.pop("pick_menu", None)
        if pick:
            try:
                pick[0].delete()
            except Exception:
                pass
        had_gains = "gains_panel" in _KEEP
        if had_gains:
            old_app.close_gains_panel()
        for key in ("settings", "route_menu", "gains_panel"):
            _KEEP.pop(key, None)
        old_app._drop_settings_screen()
        old_app._drop_route_screen()
        if old_app._mode_expanded:
            old_app._set_mode_bar(False)
        old_app._set_modal(False)
        if old_app.backend_on() and old_app.p.get("rxauto") and not old_app.be.running:
            old_app.start_rx()
        lv.screen_load(old_ui.get("scr-receiver"))
        dd = lv.display_get_default()
        if dd is not None:
            lv.refr_now(dd)
        return old_app

    # Fresh-build path. A real MicroPython soft reset re-imports this module and
    # recreates _KEEP. Clearing transient flags here instead protects a partial or
    # manually repeated build in the same VM, where widget construction may have
    # failed after publishing an open-flag.
    # Do NOT touch the hardware/loop handles (lcd, loop, dd, ui, app, vs_lcd, vs_cb,
    # sdr_poll, cbs, blink).
    for _k in ("settings", "route_menu", "pick_menu", "gains_panel"):
        _KEEP.pop(_k, None)

    gc.collect()  # release source/compiler temporaries before building the widget tree

    # Reserve the streaming memory while the heap is still contiguous.  The tuple is
    # temporarily rooted in _KEEP across display/UI construction, then also rooted by
    # SdrApp.  No second sample ring is hidden in C.
    iq_file_mem = _KEEP.get("iq_file_mem")
    if iq_file_mem is None:
        iq_file_mem = (bytearray(_IQ_FILE_BUFFER_BYTES),
                       bytearray(_IQ_FILE_BUFFER_BYTES),
                       bytearray(_IQ_FILE_HEADER_BYTES),
                       array.array("i", bytes(4 * _IQ_FILE_STATUS_WORDS)))
        _KEEP["iq_file_mem"] = iq_file_mem

    dd = lv.display_get_default()
    if dd is None:
        # bring the DIRECT display up via the board driver (pRGB)
        from pRGB import RGB
        _KEEP["lcd"] = RGB()
        dd = lv.display_get_default()
    if dd is None:
        raise RuntimeError("no display after pRGB.RGB()")

    # VSYNC-gate every render: wait for the GLCDC frame pulse before LVGL
    # starts drawing into the scanned-out framebuffer (kills DIRECT-mode
    # flicker on frequent updates, e.g. VOL slider drags). Needs firmware
    # with machine.LCD.vsync(); degrades gracefully without it.
    try:
        from machine import LCD
        _lcd = LCD()                          # handle only -- no init()
        # If the firmware provides the C LVGL bridge (lvgl_setup), VSYNC gating is done
        # in the C RENDER_START callback -- registering a Python _vs_cb too would wait
        # for VSYNC twice. Only add the Python gate on firmware without the C bridge.
        if hasattr(_lcd, "vsync") and not hasattr(_lcd, "lvgl_setup"):
            ev = getattr(lv.EVENT, "RENDER_START", None) or lv.EVENT.REFR_START

            def _vs_cb(e):
                _lcd.vsync(25)

            dd.add_event_cb(_vs_cb, ev, None)
            _KEEP.update(vs_lcd=_lcd, vs_cb=_vs_cb)
    except Exception as e:
        print("vsync gate unavailable:", repr(e))

    ui = build()
    app = SdrApp(ui, iq_file_mem)
    lv.screen_load(ui.get("scr-receiver"))
    lv.refr_now(dd)
    # event loop LAST
    if not lv_utils.event_loop.is_running():
        # Native waterfall targets 30 rows/s.  The board default is 25 Hz, which
        # hard-limits every LVGL C timer even though the direct framebuffer write
        # itself takes only ~2 ms.  Run the control/task pump at the measured 50 Hz
        # panel cadence; pixels still bypass LVGL and are VSYNC-gated in C.
        _KEEP["loop"] = lv_utils.event_loop(freq=50)
    _KEEP.update(dd=dd, ui=ui, app=app)
    return app


if __name__ == "__main__":      # Thonny F5 / paste-run; import leaves it to main.py
    start()
