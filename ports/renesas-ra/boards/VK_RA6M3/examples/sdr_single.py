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
# crystal calibration: measured +500 Hz at 28.160790 MHz => chip runs fast by
# +17.76 ppm; compensate by programming a proportionally lower frequency.
XTAL_PPM = 17.76


class SI5351:
    def __init__(self, i2c=None, i2c_id=1):   # VK_RA6M3: I2C(1) = P205/P206 (Arduino SDA/SCL)
        self.i2c = i2c or I2C(i2c_id)
        self.addr = None
        self.ok = False
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

    def set_freq(self, clk, hz):
        """clk 0..2, hz 2.5 kHz .. 200 MHz (fractional MultiSynth path)."""
        if not self.ok or not 2500 <= hz <= 200_000_000:
            return False
        div = PLL / (hz * (1.0 - XTAL_PPM * 1e-6))
        a = int(div)
        b = int((div - a) * _C)
        self._burst(42 + 8 * clk, self._ms_params(a, b, _C))
        # CLKx: powered up, MSx fractional, PLLA, 8 mA drive
        self._w(16 + clk, 0x0F)
        en = self.i2c.readfrom_mem(ADDR, 3, 1)[0]
        self._w(3, en & ~(1 << clk))           # enable output (active low)
        return True

    def disable(self, clk):
        if self.ok:
            en = self.i2c.readfrom_mem(ADDR, 3, 1)[0]
            self._w(3, en | (1 << clk))


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

    # Row card wrapper (was _brow: bg PANEL2, opa COVER, border 0, radius 4, pad 2).
    row = lv.style_t()
    row.init()
    row.set_bg_color(lv.color_hex(PANEL2))
    row.set_bg_opa(lv.OPA.COVER)
    row.set_border_width(0)
    row.set_radius(4)
    row.set_pad_all(2)
    row.set_pad_column(4)
    row.set_flex_flow(lv.FLEX_FLOW.ROW)
    row.set_flex_main_place(lv.FLEX_ALIGN.START)
    row.set_flex_cross_place(lv.FLEX_ALIGN.CENTER)
    row.set_flex_track_place(lv.FLEX_ALIGN.CENTER)
    s["row"] = row

    # Chip base (was _sbtn button: bg PANEL2, radius 6, border 0, shadow 0, centered).
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
        _flex(sd, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.CENTER, gap=0)
        sd.set_flex_grow(1)
        _lbl(sd, "STEP", 12, GRAY2)
        self.w["step-value"] = _lbl(sd, "1.0 kHz", 16, WHITE)
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
            _lbl(b, m, 14, WHITE)
            self.w["btn-" + m] = b
        # Collapsed mode-bar actions.  All objects are created once; runtime only
        # toggles HIDDEN, avoiding widget construction and heap churn on a tap.
        for name, text in (("mode-step", "STEP"), ("mode-filter", "FILTER"),
                           ("mode-view", "SPEC")):
            b = _btn(mb, 76, 34, BTN_RX, radius=6, border=BORDER)
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
        self.w["vol-label"] = _lbl(vh, "VOL", 14, GRAY2)
        self.w["vol-value"] = _lbl(vh, "72%", 16, CYAN_RX)
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
        for b_name in ("80m", "40m", "20m", "15m", "10m", "2m", "70cm"):
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
# update_mode / update_step / update_vol / update_agc. apply_all() is reserved
# for one-time full syncs (init, returning from the entry screen).
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

BANDS = (   # name, label, lo Hz, hi Hz, entry-base Hz
    ("80m",  "80 Meter", 3_500_000,   4_000_000,   3_500_000),
    ("40m",  "40 Meter", 7_000_000,   7_300_000,   7_000_000),
    ("20m",  "20 Meter", 14_000_000,  14_350_000,  14_000_000),
    ("15m",  "15 Meter", 21_000_000,  21_450_000,  21_000_000),
    ("10m",  "10 Meter", 28_000_000,  29_700_000,  28_000_000),
    ("2m",   "2 Meter",  144_000_000, 148_000_000, 144_000_000),
    ("70cm", "70 cm",    430_000_000, 440_000_000, 430_000_000),
)
MODES = ("AM", "FM", "USB", "LSB", "CW")
# RA6M3 backend demod names. FM is absent on purpose: the backend has no FM
# demod yet, so FM stays a UI-only mode (button greyed while the backend runs).
MODE_DEMOD = {"AM": "am", "USB": "usb", "LSB": "lsb", "CW": "cw"}
MODE_BW = {"AM": 6000, "FM": 12000, "USB": 2400, "LSB": 2400, "CW": 500}
# selectable IF bandwidths per mode (the P1 filter menu picks from these; P0
# only stores and displays the per-mode default)
BW_CHOICES = {"AM":  (3000, 4000, 6000, 9000),
              "FM":  (12000,),
              "USB": (1800, 2100, 2400, 3000),
              "LSB": (1800, 2100, 2400, 3000),
              "CW":  (250, 500, 1000)}
# RF front-end PGA factor per gain code 0..14 (matches be.set_rf_gain codes). The
# gains panel shows "x<factor>" instead of the raw code so the label reads like the
# other multiplier readouts.
PGA_FACT = (2.0, 2.5, 2.667, 2.857, 3.077, 3.333, 3.636, 4.0,
            4.444, 5.0, 5.714, 6.667, 8.0, 10.0, 13.333)
AGC_MODES = ("OFF", "FAST", "SLOW", "MAN")
# UI mode label -> firmware agc() mode string. Module constant so set_agc never
# allocates a dict literal per call.
_AGC_MODE_MAP = {"OFF": "off", "FAST": "fast", "SLOW": "slow",
                 "MAN": "manual", "MANUAL": "manual"}


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
F_MIN, F_MAX = 100_000, 470_000_000

DEFAULTS = {"f": 14_205_000, "m": "USB", "s": 1000, "v": 72, "a": "FAST"}
DEF_VFOS = [[14_205_000, "USB"], [7_100_000, "LSB"], [144_300_000, "FM"]]
# hardware targets a VFO can drive: (label, si5351 clk or None, multiplier).
# x4 = quadrature (Tayloe) LO running at 4x the tuned frequency.
# First five entries keep the indices of records saved by older builds.
TARGETS = (
    ("Si5351 CLK0",    0,    1),
    ("Si5351 CLK1 x4", 1,    4),
    ("Si5351 CLK2",    2,    1),
    ("Si4825",         None, 1),
    ("Si4732",         None, 1),
    ("Si5351 CLK0 x4", 0,    4),
    ("Si5351 CLK1",    1,    1),
    ("Si5351 CLK2 x4", 2,    4),
)


# ---------------- data flash persistence ----------------

def _fresh_params():
    """Factory defaults, including every v3 SDR-backend field."""
    out = dict(DEFAULTS)
    out["vfos"] = [list(v) for v in DEF_VFOS]
    out["act"] = 0
    out["rt"] = [0, 1, 2]
    out["cal"] = 17.76
    out["bw"] = dict(MODE_BW)     # per-mode IF bandwidth (filter_bandwidth)
    out["rxauto"] = 0             # rx_autostart: RX was on when last saved
    out["beon"] = 1               # backend_enabled: master IQADC/DAC switch
    out["again"] = 1.0            # agc_manual_gain
    out["atgt"] = 0.5             # agc_target
    out["rf"] = 0                 # RF PGA gain code 0..14
    return out


def load_params():
    import json, dataflash
    try:
        hdr = bytes(dataflash.read(0, 6))
        if hdr[:4] != MAGIC:
            raise ValueError("no record")
        n = hdr[4] | (hdr[5] << 8)
        if not 0 < n <= 500:
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
            rt = [0, 1, 2]          # A/B/C -> the three Si5351 channels
        out["rt"] = rt
        try:
            out["cal"] = float(p.get("C", 17.76))
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
            out["atgt"] = min(max(float(p.get("T", 0.5)), 0.01), 1.0)
        except Exception:
            out["atgt"] = 0.5
        try:
            out["rf"] = min(max(int(p.get("rf", 0)), 0), 14)   # RF PGA gain code 0..14
        except Exception:
            out["rf"] = 0
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
               "rf": int(p.get("rf", 0))}
    payload = json.dumps(rec_obj).encode()
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
        self.rf_code = 0             # RF PGA gain code 0..14 (only effective off BYPASS)
        self.fine_hz = 0             # digital NCO offset within the +/- fs/2 window
        self.scope_stage = 0         # cached physical DAC route (0 = normal mono AF)
        # Preallocated, alloc-free UI buffers filled by the C accessors (spectrum_bars /
        # counters). Nothing in the poll loop creates a MicroPython object -> GC never
        # runs -> the realtime ADC ISR is never stalled.
        self._bars = array.array("h", bytes(2 * len(SPEC_HEIGHTS)))   # int16 heights 0..50
        self._ctr = array.array("i", bytes(4 * 6))                    # counter snapshot
        try:
            from machine import IQADC, ADC, DAC
            self._IQADC, self._ADC, self._DAC = IQADC, ADC, DAC
            self.available = True
        except Exception as e:
            self._IQADC = self._ADC = self._DAC = None
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
        """UI percent -> backend AF gain. Square law: usable low-end travel."""
        p = min(max(int(percent), 0), 100) / 100.0
        return round(p * p, 4)

    # ---- lifecycle ----
    def start_rx(self):
        if not self.available or self.running:
            return self.running
        try:
            kw = {"rate": IQ_RATE, "block": IQ_BLOCK}
            pga = getattr(self._ADC, "PGA_BYPASS", None)
            if pga is not None:
                kw["pga"] = pga
            self.iq = self._IQADC(IQ_PIN_I, IQ_PIN_Q, **kw)
            self.dac = self._DAC(DAC_PIN)
            self.iq.start()
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
        self._teardown()
        return True

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
        try:
            if self.iq is not None:
                self.iq.stop()
        except Exception:
            pass
        self.iq = None
        self.dac = None
        self.dac_q = None
        self.running = False
        self.fine_hz = 0

    # ---- settings: cached always, pushed only while RX is up ----
    def apply_settings(self):
        self.set_mode(self.mode)
        self.set_agc(self.agc)
        self.set_bandwidth(self.bw)
        self.set_volume(self.vol)
        self.set_rf_gain(self.rf_code)

    def set_mode(self, mode):
        self.mode = mode
        # unknown mode (FM today) -> demod off rather than a wrong demodulator
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

    def set_rf_gain(self, code):
        # RF front-end PGA gain, code 0..14 (x2.0..x13.3). iq.gain() is a no-op while the
        # unit runs in PGA_BYPASS -- the slider still tracks; the AFE mode is a separate call.
        self.rf_code = max(0, min(int(code), 14))
        return self._call(self.iq, "gain", self.rf_code)

    def rf_gain(self):
        try:
            return self.iq.gain()
        except Exception:
            return self.rf_code

    def agc_gain_now(self):
        """Live AGC gain as a factor (auto-AGC moves it, manual holds it); None if down."""
        if not self.running or self.iq is None:
            return None
        try:
            return self.iq.agc_status().get("gain")
        except Exception:
            return None

    def set_agc(self, mode, gain=None, target=None):
        self.agc = mode
        if gain is not None:
            self.agc_gain = gain
        if target is not None:
            self.agc_target = target
        # Real firmware API is a SINGLE call: agc(mode, gain=, rms_target=).
        # mode strings are off/fast/slow/manual; gain (float, 1.0 = unity) only
        # matters in manual; rms_target is a 0..1 fraction of full scale. There is
        # NO agc_gain()/agc_target() -- those were silent no-ops. The map is a module
        # constant (not a per-call dict literal) so a repeated AGC control does not
        # allocate a dict every event.
        m = _AGC_MODE_MAP.get(mode, "fast")
        fn = getattr(self.iq, "agc", None) if self.iq is not None else None
        if fn is None:
            return False
        try:
            fn(m, gain=self.agc_gain, rms_target=self.agc_target)
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
                # A runtime DMAC cleanup withdraws the native Q consumer. Recreate it
                # here in control context; never let the UI silently claim dual I/Q.
                q_alive = self.set_scope(self.scope_stage)
            if not q_alive:
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
        except Exception:
            return None


# ---------------- app ----------------

class SdrApp:

    def __init__(self, ui):
        self.ui = ui
        self.p = load_params()
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
        self._spec_lcd = None
        self._spec_native = False
        self._spectrum_view = 0       # 0=SPEC, 1=WF on the left native panel
        self._scope_view = 0          # 0=TIME, 1=I-Q constellation on the right
        try:
            from machine import LCD
            lcd = LCD()
            if hasattr(lcd, "spectrum_attach") and hasattr(lcd, "spectrum_update"):
                self._spec_native = bool(lcd.spectrum_attach(
                    self.ui.get("spectrum-waterfall"), PANEL, BIN, CYAN_RX))
                if self._spec_native:
                    self._spec_lcd = lcd
                    self.ui.bins = ()       # release deleted LVGL wrapper objects
        except Exception:
            self._spec_lcd = None
            self._spec_native = False
        # Deferred-work flags: interaction callbacks only SET these (microseconds);
        # the 100 ms GUI worker applies the latest values outside the touch callback.
        # Spectrum/status still run at their own divisors and are never starved by flags.
        self._hw_pending = True      # initial Si5351 program; later set only on real LO changes
        self._vol_pending = False    # firmware volume needs the latest slider value
        self._rx_pending = 0         # 0 none, 1 start, 2 stop (heavy IQADC/DAC bring-up)
        self._poll_div = 0           # 100 ms GUI tick; status every fifth tick
        self._modal = False          # non-HOME screen/overlay -> pause native framebuffer writes
        # Settings modal (firmware DSP verification controls).  The test-source preset,
        # waveform and LIVE flag survive VERIFY screen rebuilds in this App instance, but
        # are never written to flash.  The source itself runs in C; no Python sample timer.
        self._inj_ampl = 500
        self._inj_on = False
        self._inj_mode = 0           # index into _INJ_PRESETS (AM/USB/LSB/CW)
        self._inj_shape = 0          # 0=SINE continuous, 1=PULSE 2-Hz/50-percent gate
        self._inj_live = False       # deterministic C phase jitter for a live scope trace
        self._tap_stage = 0
        self._passthru_on = False    # verify-only demod("thru") override (not persisted)
        self._squelch = 0            # verify-only squelch threshold (not persisted)
        self._af_preset = 0          # audio_filter index into _AF_PRESETS
        self._iqc_on = False         # verify-only I/Q imbalance correction
        self._iqc_amp = 1.0          # Q amplitude multiplier
        self._iqc_phase = 0.0        # I leakage added to Q
        self._kernels = {"dec_kernel": 0, "hil_kernel": 0,
                         "chf_kernel": 0, "mag_kernel": 0}
        # Per-block DSP verification table (bench-only, never persisted). _blk_on maps
        # block id 1..11 -> bool (all True = every block active by default). _scope_id is
        # the ONE block whose output is routed to the DAC(s) via iq.scope() (0 = none).
        # _tap_stage (0..3) stays the one-of UART tap; only blocks 2/4/5 map to stages
        # 1/2/3, every other block's tap control is rendered disabled (firmware has no tap).
        self._blk_on = {i: True for i in range(1, 12)}
        self._scope_id = 0
        self._tap_id = 0             # block id currently holding the one-of UART tap (0 = none)
        self._set_lbls = {}
        # Gains panel: which of RF/AF/AGC the collapsed VOL slot currently controls,
        # and the live value-labels inside the open panel (cleared on close so the poll
        # loop only touches them while the panel is up).
        self._active_gain = "AF"
        self._gain_vlbls = {}
        self._gain_vlbls_all = {}    # persistent value-label handles (screen lifetime)
        # Per-open refresh handles: key -> (slider, value_label) for the gains screen,
        # row-name -> widget(s) for the settings screen. Both screens are built lazily
        # once and re-shown on later opens, so their displayed values are re-read from
        # the current state on EACH open via _refresh_gains / _refresh_settings.
        self._gain_widgets = {}
        self._set_widgets = {}
        # bottom bar: 0 normal, 1 modes, 2 filters, 3 tuning controls, 4 steps
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
        if self._spec_native and hasattr(self._spec_lcd, "spectrum_pause"):
            try:
                self._spec_lcd.spectrum_pause(self._modal)
            except Exception as e:
                self.be.err = "spectrum pause: %r" % (e,)

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
    def hw_tune(self):
        # NEVER raises, NEVER prints: hardware status is shown by the brand
        # dot only -- teal = synth driven, red = chip missing/unreachable.
        global XTAL_PPM
        ok = False
        _lab, clk, mult = TARGETS[self.p["rt"][self.p["act"]]]
        if clk is not None:
            try:
                if getattr(self, "_synth", None) is None:
                    self._synth = SI5351()
                XTAL_PPM = self.p.get("cal", 17.76)
                if self._synth.probe():
                    ok = self._synth.set_freq(clk, self.p["f"] * mult)
            except Exception:
                ok = False
        self.ui.get("brand-dot").set_style_bg_color(
            lv.color_hex(TEAL if ok else 0xE53935), 0)

    # ---- RA6M3 backend: RX on/off + status ----
    def backend_on(self):
        return self.be.available and bool(self.p["beon"])

    def cur_bw(self):
        return self.p["bw"].get(self.p["m"], MODE_BW[self.p["m"]])

    def start_rx(self):
        if self.backend_on() and not self.be.running:
            self.be.mode = self.p["m"]
            self.be.bw = self.cur_bw()
            self.be.agc = self.p["a"]
            self.be.agc_gain = self.p["again"]
            self.be.agc_target = self.p["atgt"]
            self.be.vol = self.p["v"]
            self.be.start_rx()
            if self.be.running:
                # The data path is alloc-free (C accessors -> preallocated arrays); the
                # only churn is LVGL's own render binding (~22 B/repaint). That is far
                # too little to disable GC over -- disabling it just fills the heap and
                # the app stops. Keep GC ENABLED: with so little garbage it runs rarely
                # and briefly, and the DTC/DMAC ping-pong absorbs the sub-ms pause.
                gc.collect()
            self.p["rxauto"] = 1 if self.be.running else 0
            self.touch_params()
        self.update_rx()

    def stop_rx(self):
        self.be.stop_rx()
        # The native IQADC object (and therefore its synthetic source) no longer
        # exists.  Keep the VERIFY toggle truthful if RX is started again later.
        self._inj_on = False
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
        b.get_child(0).set_style_text_color(lv.color_hex(txt), 0)
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
        # The displayed spectrum is centred on the selected RF frequency. The C reducer
        # shifts the pre-NCO 256-bin capture by this same fine offset, so the panorama
        # scrolls under the fixed centre marker at FFT-bin (~94 Hz) resolution.
        fine = self.be.fine_hz if self.be.running else 0
        centre = f + fine
        half = 12000 if self.be.running else 5000
        ui.get("freq-digits").set_text(self.fmt_freq(centre))
        ui.get("spec-lo").set_text(self.fmt_khz(centre - half))
        ui.get("spec-hi").set_text(self.fmt_khz(centre + half))
        band, label = self.band_of(centre)
        if band != self._band:
            self._band = band
            ui.get("band-value").set_text(label)

    def update_mode(self):
        ui, m = self.ui, self.p["m"]
        # FM has no RA6M3 demod yet: grey it out whenever the backend is live.
        # A stored FM VFO still shows as selected -- just muted, not rewritten.
        dead = "FM" if self.backend_on() else None
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
        self._set_mode_bar(False)

    def _set_mode_bar(self, expanded):
        """Switch the fixed bottom row without creating/deleting LVGL objects."""
        self._mode_expanded = 1 if expanded else 0
        self.ui.get("tuning-row").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("mode-bar").remove_flag(lv.obj.FLAG.HIDDEN)
        selected = self.p["m"]
        for name in MODES:
            b = self.ui.get("btn-" + name)
            b.get_child(0).set_text(name)  # restore labels after FILTER choices
            visible = self._mode_expanded or name == selected
            hidden = b.has_flag(lv.obj.FLAG.HIDDEN)
            if visible and hidden:
                b.remove_flag(lv.obj.FLAG.HIDDEN)
            elif not visible and not hidden:
                b.add_flag(lv.obj.FLAG.HIDDEN)
        for name in ("mode-step", "mode-filter", "mode-view"):
            b = self.ui.get("btn-" + name)
            hidden = b.has_flag(lv.obj.FLAG.HIDDEN)
            if self._mode_expanded and not hidden:
                b.add_flag(lv.obj.FLAG.HIDDEN)
            elif not self._mode_expanded and hidden:
                b.remove_flag(lv.obj.FLAG.HIDDEN)

    def set_mode(self, m):
        if m == "FM" and self.backend_on():
            return                      # no backend FM demod yet
        p = self.p
        p["m"] = m
        p["vfos"][p["act"]][1] = m   # active VFO state stays coherent before flash save
        self.update_mode()
        self.be.set_mode(m)
        self.be.set_bandwidth(self.cur_bw())
        self.touch_params()

    def update_step(self):
        ui, s = self.ui, self.p["s"]
        for sv, name in STEPS:
            if sv == s:
                txt = name.replace(" ", "") if sv < 1000 else "%.1f kHz" % (sv / 1000)
                ui.get("step-value").set_text(txt)
                # The collapsed bottom action shows the selected step itself;
                # tapping it still opens the horizontal STEP choices.
                ui.get("btn-mode-step").get_child(0).set_text(name)
            chip = ui.get("step-" + name)
            on = sv == s
            chip.set_style_bg_color(lv.color_hex(CYAN_IN if on else BTN_IN), 0)
            chip.get_child(0).set_style_text_color(
                lv.color_hex(DARK_TXT if on else WHITE), 0)

    def update_vol(self):
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
        lbl = ui.get("agc-value")
        lbl.set_text(a)
        lbl.set_style_text_color(
            lv.color_hex(WHITE if a == "OFF" else DARK_TXT), 0)
        ui.get("agc-pill").set_style_bg_color(
            lv.color_hex(self.AGC_BG.get(a, BORDER)), 0)

    def open_agc_menu(self):
        def pick(v):
            self.p["a"] = v
            self.update_agc()
            self.be.set_agc(v, self.p["again"], self.p["atgt"])
            self.touch_params()
        self.open_pick_menu("AGC", tuple((v, v) for v in AGC_MODES),
                            self.p["a"], pick)

    # ---- gains panel (RF / AF / AGC on 5 vertical sliders) ----
    def _gain_spec(self, key):
        """(lo, hi, cur_int, fmt) for a gain slider. fmt(v_int) -> label string.
        The slider works in integers; AGC packs gain*10 so 0.1 steps stay on-grid."""
        if key == "AF":
            return 0, 100, int(self.p["v"]), lambda v: "%d%%" % v
        if key == "RF":
            return 0, 14, int(self.p["rf"]), lambda v: "x%.1f" % PGA_FACT[v]
        # AGC: slider integer = gain * 10 (float ~0.1..8.0 -> 1..80)
        return 1, 80, int(self.p["again"] * 10), lambda v: "x%.1f" % (v / 10.0)

    def _apply_gain(self, key, v_int):
        """Push a slider value to the backend + persist. AF routes through the
        existing set_volume so the vol-value label + save timer still fire."""
        if key == "AF":
            self.set_volume(v_int)
        elif key == "RF":
            self.p["rf"] = v_int
            self.be.set_rf_gain(v_int)
            self.touch_params()
        elif key == "AGC":
            self.p["again"] = v_int / 10.0
            self.be.set_agc(self.be.agc, gain=self.p["again"])
            self.touch_params()

    def toggle_gains_panel(self):
        if "gains_panel" in _KEEP:
            self.close_gains_panel()
        else:
            self.open_gains_panel()

    def open_gains_panel(self):
        if "gains_panel" in _KEEP:
            return
        if self.ui.w.get("scr-gains") is None:
            self._build_gains()      # lazy one-time build; swapped in on later opens
        # re-arm the live value labels (close_gains_panel cleared _gain_vlbls so the
        # poll loop stays idle while the screen is down; the widgets themselves persist).
        self._gain_vlbls = dict(self._gain_vlbls_all)
        _KEEP["gains_panel"] = True  # open-flag: _consume_status refreshes AGC live gain
        self._set_modal(True)
        lv.screen_load(self.ui.get("scr-gains"))
        self._refresh_gains()        # re-read live state so a re-shown screen is current

    def close_gains_panel(self):
        # Return to the receiver screen. The gains screen object + its callbacks
        # persist (kept alive on self._gains_cbs) so re-opening is a plain swap.
        _KEEP.pop("gains_panel", None)
        lv.screen_load(self.ui.get("scr-receiver"))
        self._gain_vlbls = {}        # poll loop only touches these while the view is up
        self._set_modal(("settings" in _KEEP) or ("pick_menu" in _KEEP))
        self._bind_active_slider()

    def _build_gains(self):
        # Full-screen (480x272) view of the RF / AF / AGC gains + two reserved
        # columns, mirroring the SETTINGS screen. Five vertical sliders lie in a
        # ROW with a name label above and a live value label below each. RF/AF/AGC
        # are live; the two "--" columns are disabled placeholders. Callbacks live
        # on self._gains_cbs for the screen lifetime.
        scr = _base(lv.obj(None))
        scr.set_style_bg_color(lv.color_hex(BG_RX), 0)
        scr.set_style_bg_opa(lv.OPA.COVER, 0)
        scr.set_style_pad_all(8, 0)
        _flex(scr, lv.FLEX_FLOW.COLUMN, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.START,
              lv.FLEX_ALIGN.START, 6)
        self.ui.w["scr-gains"] = scr

        # --- title bar: "GAINS" (left) + BACK button (right) ---
        tb = _base(lv.obj(scr))
        tb.set_size(464, 30)
        _flex(tb, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN)
        _lbl(tb, "GAINS", 16, GRAY)
        back = _btn(tb, 80, 30, PANEL2, radius=6, border=BORDER)
        _lbl(back, "BACK", 14, WHITE)

        cbs = []
        self._gains_cbs = cbs        # keep every event cb alive for the screen lifetime
        self._gain_vlbls_all = {}    # persistent handles; copied into _gain_vlbls per open

        def back_cb(e):
            self.close_gains_panel()
        back.add_event_cb(back_cb, lv.EVENT.CLICKED, None)
        cbs.append(back_cb)

        # --- slider row: 5 columns spread across the full width ---
        row = _base(lv.obj(scr))
        row.set_size(464, 220)
        _flex(row, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_EVENLY,
              lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 4)

        for key in ("RF", "AF", "AGC", "--", "--"):
            live = key in ("RF", "AF", "AGC")
            col = _flex(_base(lv.obj(row)), lv.FLEX_FLOW.COLUMN,
                        lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER,
                        lv.FLEX_ALIGN.CENTER, 6)
            col.set_size(80, 216)
            _lbl(col, key, 14, CYAN_RX if live else GRAY2)
            sl = lv.slider(col)
            sl.set_size(16, 170)
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
            if live:
                lo, hi, cur, fmt = self._gain_spec(key)
                sl.set_range(lo, hi)
                sl.set_value(cur, False)
                vlbl = _lbl(col, fmt(cur), 12, WHITE)
                self._gain_vlbls_all[key] = vlbl
                self._gain_widgets[key] = (sl, vlbl)

                def gain_cb(e, kk=key, s=sl, vl=vlbl):
                    v = s.get_value()
                    self._apply_gain(kk, v)
                    _lo, _hi, _cur, f = self._gain_spec(kk)
                    vl.set_text(f(v))
                    self._active_gain = kk
                sl.add_event_cb(gain_cb, lv.EVENT.VALUE_CHANGED, None)
                cbs.append(gain_cb)
            else:
                sl.add_state(lv.STATE.DISABLED)
                _lbl(col, "--", 12, GRAY2)

    def _refresh_gains(self):
        """Re-read the current RF/AF/AGC state into the gains-screen sliders + value
        labels. Called on every open so a re-shown screen never displays the stale
        values captured at build time."""
        for key, (sl, vlbl) in self._gain_widgets.items():
            _lo, _hi, cur, fmt = self._gain_spec(key)
            sl.set_value(cur, False)
            vlbl.set_text(fmt(cur))

    def _bind_active_slider(self):
        """Rebind the collapsed VOL slot (label + slider) to the last-touched gain."""
        lo, hi, cur, fmt = self._gain_spec(self._active_gain)
        s = self.ui.get("vol-slider")
        s.set_range(lo, hi)
        s.set_value(cur, False)
        self.ui.get("vol-label").set_text(self._active_gain)
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

        The firmware reducer has already shifted the full 256-bin pre-NCO panorama by
        fine_hz before reducing it to these 27 bars. Python therefore paints the buffer
        directly: no second shift, float, round, or modulo allocation in the hot loop."""
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
        """ZERO-ALLOC 2 Hz counter/status consumer. Never raises."""
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
            # Live AGC readout, only while the gains panel is open and showing AGC. Auto-AGC
            # moves the gain, so the label tracks the real value; guarded so the normal
            # (panel-closed) loop stays alloc-free.
            if "gains_panel" in _KEEP and "AGC" in self._gain_vlbls:
                g = self.be.agc_gain_now()
                if g is not None:
                    self._gain_vlbls["AGC"].set_text("x%.1f" % g)
            # SETTINGS view live read-outs: AGC gain + S-meter. Only while the settings
            # screen is showing (_KEEP["settings"] open-flag; labels in self._set_lbls,
            # cleared on BACK) and RX is up.
            if "settings" in _KEEP and self.be.iq is not None:
                lb = self._set_lbls.get("agc")
                if lb is not None:
                    try:
                        g = self.be.iq.agc_status().get("gain")
                        if g is not None:
                            lb.set_text("x%.1f" % g)
                    except Exception:
                        pass
                lb = self._set_lbls.get("smeter")
                if lb is not None:
                    try:
                        sm = self.be.iq.smeter()
                        lb.set_text("%d / %ddBFS" % (int(sm.get("rms", 0)),
                                                     int(sm.get("dbfs", 0))))
                    except Exception:
                        pass
                lb = self._set_lbls.get("timing")
                if lb is not None:
                    try:
                        tm = self.be.iq.timing()
                        lb.set_text("%.0f%%" % float(tm.get("max_pct", 0.0)))
                    except Exception:
                        pass
                lb = self._set_widgets.get("squelch")
                if lb is not None:
                    try:
                        sq = self.be.iq.squelch()
                        lb.set_text("%d/%d %s" % (int(sq.get("thresh", 0)),
                                                   int(sq.get("env", 0)),
                                                   "O" if sq.get("open", True) else "C"))
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

    # ---- tuning ----
    def tune(self, delta):
        # coarse: moves the analog LO (Si5351). Re-centre the digital NCO so the
        # new band centre is 0 Hz offset again.
        old = self.p["f"]
        self.p["f"] = min(max(old + delta, F_MIN), F_MAX)
        if self.be.running:
            self.be.set_fine(0)
        self.shift_spectrum(self.p["f"] - old)
        self._hw_pending = True       # the analog LO centre really changed
        self.update_freq()
        self.touch_params()

    def fine(self, delta):
        # fine: digital NCO within the capture window while the backend is live
        # (no I2C, no LO move); otherwise a small coarse step so the UI still tunes.
        if self.be.running:
            self.be.fine_tune(delta)
            self.update_freq()
            self.touch_params()
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
        if i == p["act"]:
            return
        p["vfos"][p["act"]] = [p["f"], p["m"]]   # park current state
        old_f = p["f"]
        # in-place swap: the tapped slot now shows the previously active VFO,
        # the other slot stays where it was (no reshuffling)
        alt = getattr(self, "_alt", None)
        if alt and i in alt:
            alt[alt.index(i)] = p["act"]
        p["act"] = i
        p["f"], p["m"] = p["vfos"][i]
        self.shift_spectrum(p["f"] - old_f)
        self._hw_pending = True       # route the new VFO frequency to its hardware target
        self.update_freq()
        self.update_mode()
        self.be.set_mode(p["m"])          # a VFO carries its own mode
        self.be.set_bandwidth(self.cur_bw())
        self.update_vfo_ui()
        self.update_entry_digits()
        self.update_entry_bands()
        self.touch_params()

    # ---- spectrum tap-to-tune ----
    def spec_jump(self, frac):
        """Tap-to-tune at horizontal fraction frac of the spectrum. While RX is live
        the span is the real +/- 12 kHz capture window and the tap is applied as an
        ABSOLUTE digital fine-tune offset (iq.tune, no LO move) -- so a tap lands
        exactly where the edge labels (f +/- 12 kHz) say. Off RX it keeps the old
        demo +/- 5 kHz coarse (Si5351) jump. Both snap to the tuning step."""
        frac = min(max(frac, 0.0), 1.0)
        s = self.p["s"]
        if self.be.running:
            half = 12000                          # matches update_freq's RX span
            off = int((frac - 0.5) * 2 * half)    # -half..+half around the LO centre
            off = int(round(off / s) * s)         # snap to the tuning step
            self.be.set_fine(off)                 # absolute NCO offset (clamped in C)
            self.update_freq()
            self.touch_params()
        else:
            old = self.p["f"]
            new = int(old - 5000 + frac * 10000)
            new = int(round(new / s) * s)
            self.p["f"] = min(max(new, F_MIN), F_MAX)
            self.shift_spectrum(self.p["f"] - old)
            self._hw_pending = True
            self.update_freq()
            self.touch_params()

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
    # label, demodulator, C injector kind, carrier Hz, AM modulation Hz, depth percent.
    # Current Hilbert signs require negative complex rotation for USB and positive for LSB.
    _INJ_PRESETS = (("AM", "am", 1, 3000, 1000, 50),
                    ("USB", "usb", 2, 1500, 0, 0),
                    ("LSB", "lsb", 3, 1500, 0, 0),
                    ("CW", "cw", 4, 10, 0, 0))
    _INJ_SHAPES = (("SINE", 0), ("PULSE", 2))
    _DEMOD_MODES = ("AM", "USB", "LSB", "CW", "THRU")
    _TAP_STAGES = ("OFF", "decim", "nco", "chfilt")
    # The 11-block DSP chain: (id, name, complex?). Pre-demod blocks 1..5 are complex
    # (SCOPE routes I->DAC0 / Q->DAC1, label "->I/Q"); post-demod 6..11 are mono
    # (SCOPE routes mono DAC, label "->DAC"). Only ids 2/4/5 have a firmware UART tap.
    _BLOCKS = ((1, "PGA", True), (2, "Decimation", True), (3, "IQ correction", True),
               (4, "NCO / tune", True), (5, "Channel filter", True),
               (6, "Demod", False), (7, "AF filter", False), (8, "Squelch", False),
               (9, "AGC", False), (10, "Volume", False), (11, "Limiter", False))
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
        scr = self.ui.w.get("scr-settings")
        if scr is not None:
            lv.screen_load(self.ui.get("scr-receiver"))
            scr.delete()
            self.ui.w["scr-settings"] = None
        self._settings_cbs = []
        self._set_widgets = {}
        self._set_lbls = {}
        gc.collect()

    def close_settings(self):
        # Return HOME and release the 50+ KB VERIFY widget tree.  Re-opening rebuilds
        # it after collecting ROUTE, which keeps the two secondary screens exclusive.
        _KEEP.pop("settings", None)
        self._drop_settings_screen()
        self._set_modal(("pick_menu" in _KEEP) or ("gains_panel" in _KEEP))

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
        lv.screen_load(self.ui.get("scr-settings"))
        self._refresh_settings()     # re-read live state so a re-shown screen is current

    def _build_settings(self):
        # Full-screen backend/DSP VERIFICATION view: a per-block table of the 11-block
        # DSP chain. A compact header (INJECT toggle + amplitude, live AGC-gain and
        # S-meter read-outs) sits above a VERTICALLY SCROLLABLE column of block rows.
        # Each block row exposes: name, an ON/OFF bypass toggle (iq.block(id,1|0)), a
        # one-of UART TAP (iq.tap(stage); only ids 2/4/5 are tappable, the rest greyed),
        # and a one-of SCOPE ->DAC route (iq.scope(id)). RF gain / squelch / audio-filter
        # / kernels follow as extra rows below the 11 blocks. The 11-row table cannot fit
        # 272 px without scroll, so the rows container is intentionally scrollable.
        # Callbacks live on self._settings_cbs for the screen lifetime. Every verify
        # control is bench-only: NONE is written to self.p and none persists.
        # Shared styles are created once and outlive the transient screen tree; the
        # widgets below are rebuilt every open (build-on-open, delete-on-close) so
        # VERIFY holds no RAM while the operator is on the receiver.
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

        # -- TEST source: AM/USB/LSB/CW | SINE/PULSE | CLEAN/LIVE | amplitude | ON --
        # One compact row adds only one LVGL object over the former fixed 1-kHz INJECT
        # control.  All waveform generation, gating and phase jitter run in the existing
        # C IQADC injector; callbacks merely publish a new block-atomic configuration.
        irow = _base(lv.obj(scr))
        irow.set_size(464, 26)
        _flex(irow, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.SPACE_BETWEEN,
              lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, 6)
        _lbl(irow, "TEST", 14, WHITE)
        ig = _grp(irow, 370)
        mode_name = self._INJ_PRESETS[self._inj_mode][0]
        shape_name = self._INJ_SHAPES[self._inj_shape][0]
        imb, iml = _sbtn(ig, mode_name, 48, 12, CYAN_RX)
        iwb, iwl = _sbtn(ig, shape_name, 58, 12, CYAN_RX)
        ilb, ill = _sbtn(ig, "LIVE" if self._inj_live else "CLEAN", 54, 12,
                         DARK_TXT if self._inj_live else CYAN_RX)
        if self._inj_live:
            ilb.set_style_bg_color(lv.color_hex(GREEN), 0)
        iamp = lv.label(ig)
        iamp.set_text("%d" % self._inj_ampl)
        iamp.add_style(st["dim"], 0)
        iamp.set_style_text_color(lv.color_hex(CYAN_RX), 0)
        idn, _ = _sbtn(ig, "-", 26, 20)
        iup, _ = _sbtn(ig, "+", 26, 20)
        itg, itgl = _sbtn(ig, "ON" if self._inj_on else "OFF", 48, 12,
                          DARK_TXT if self._inj_on else WHITE)
        if self._inj_on:
            itg.set_style_bg_color(lv.color_hex(GREEN), 0)
        self._set_widgets["inject"] = (iml, iwl, ilb, ill, iamp, itg, itgl)

        def inj_apply():
            iq = self.be.iq
            fn = getattr(iq, "inject", None) if iq is not None else None
            if not self._inj_on:
                if fn is not None:
                    try:
                        fn(False)
                    except Exception as e:
                        self.be.err = "inject: %r" % (e,)
                        return False
                if iq is not None:
                    mode = self.p["m"]
                    self.be.set_mode(mode)           # restore receiver DSP settings
                    self.be.set_bandwidth(self.p["bw"].get(mode, MODE_BW[mode]))
                return True
            if fn is None:
                self.be.err = "test source unavailable (RX off)"
                return False
            # Presence of the new constants is an honest capability probe.  An older
            # firmware only supports the legacy positive complex tone and cannot fake
            # true AM, negative-rotation USB, pulse gating or phase noise.
            if not hasattr(iq, "INJECT_AM"):
                self.be.err = "extended IQ test source unavailable"
                return False
            name, demod, kind, carrier, mod_hz, depth = \
                self._INJ_PRESETS[self._inj_mode]
            if name == "AM" and self._inj_ampl > 1000:
                # AM uses A*(1+depth*cos); at the 50-percent preset A=1000 peaks at
                # 1500 counts and stays comfortably inside the 12-bit ADC midpoint.
                self._inj_ampl = 1000
                iamp.set_text("1000")
            gate_hz = self._INJ_SHAPES[self._inj_shape][1]
            noise = 2 if self._inj_live else 0
            try:
                if not self.be.set_mode(name):
                    return False
                if not self.be.set_bandwidth(
                        self.p["bw"].get(name, MODE_BW[name])):
                    return False
                previous = self._scope_id
                if not self.be.set_scope(6):          # inspect the detected mono output
                    return False
                self._scope_id = 6
                fn(True, carrier, self._inj_ampl, kind, mod_hz, depth,
                   gate_hz, noise)
                # The table is already complete by the time a user can click this row.
                blocks = self._set_widgets.get("blocks")
                if blocks:
                    if previous and previous != 6 and previous in blocks:
                        _scope_paint(previous, False)
                    if 6 in blocks:
                        _scope_paint(6, True)
                self.be.err = None
                return True
            except Exception as e:
                self.be.err = "inject: %r" % (e,)
                return False

        def inj_paint():
            iml.set_text(self._INJ_PRESETS[self._inj_mode][0])
            iwl.set_text(self._INJ_SHAPES[self._inj_shape][0])
            ill.set_text("LIVE" if self._inj_live else "CLEAN")
            ill.set_style_text_color(
                lv.color_hex(DARK_TXT if self._inj_live else CYAN_RX), 0)
            ilb.set_style_bg_color(
                lv.color_hex(GREEN if self._inj_live else PANEL2), 0)
            itgl.set_text("ON" if self._inj_on else "OFF")
            itgl.set_style_text_color(
                lv.color_hex(DARK_TXT if self._inj_on else WHITE), 0)
            itg.set_style_bg_color(
                lv.color_hex(GREEN if self._inj_on else PANEL2), 0)

        def inj_mode_cb(e):
            self._inj_mode = (self._inj_mode + 1) % len(self._INJ_PRESETS)
            if self._INJ_PRESETS[self._inj_mode][0] == "AM" and self._inj_ampl > 1000:
                self._inj_ampl = 1000
                iamp.set_text("1000")
            if self._inj_on and not inj_apply():
                self._inj_on = False
                inj_apply()
            inj_paint()

        def inj_shape_cb(e):
            self._inj_shape = (self._inj_shape + 1) % len(self._INJ_SHAPES)
            if self._inj_on and not inj_apply():
                self._inj_on = False
                inj_apply()
            inj_paint()

        def inj_live_cb(e):
            self._inj_live = not self._inj_live
            if self._inj_on and not inj_apply():
                self._inj_on = False
                inj_apply()
            inj_paint()

        def inj_amp_cb(e, d=0, lbl=iamp):
            max_ampl = 1000 if self._INJ_PRESETS[self._inj_mode][0] == "AM" else 2000
            self._inj_ampl = min(max(self._inj_ampl + d, 0), max_ampl)
            lbl.set_text("%d" % self._inj_ampl)
            if self._inj_on and not inj_apply():
                self._inj_on = False
                inj_apply()
                inj_paint()

        def inj_tg_cb(e):
            want_on = not self._inj_on
            self._inj_on = want_on
            if not inj_apply():
                self._inj_on = False
                inj_apply()
            inj_paint()
        for b, cb in ((imb, inj_mode_cb), (iwb, inj_shape_cb),
                      (ilb, inj_live_cb), (itg, inj_tg_cb)):
            b.add_event_cb(cb, lv.EVENT.CLICKED, None)
            cbs.append(cb)
        for b, d in ((idn, -100), (iup, +100)):
            def _iac(e, dd=d):
                inj_amp_cb(e, dd)
            b.add_event_cb(_iac, lv.EVENT.CLICKED, None)
            cbs.append(_iac)

        # column header above the scrollable block table (name | ON | TAP | ->DAC)
        hdr = _base(lv.obj(scr))
        hdr.set_size(464, 16)
        _flex(hdr, lv.FLEX_FLOW.ROW, lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
              lv.FLEX_ALIGN.CENTER, 0)
        for txt, wpx in (("BLOCK", 176), ("ON", 62), ("TAP", 92), ("SCOPE", 92)):
            hl = lv.label(hdr)
            hl.set_text(txt)
            hl.set_size(wpx, 14)
            hl.add_style(st["dim"], 0)

        # -- scrollable rows container: 11 block rows + extra bench knobs below --
        rows = _base(lv.obj(scr))
        rows.set_size(464, 176)
        rows.add_flag(lv.obj.FLAG.SCROLLABLE)
        rows.set_scroll_dir(lv.DIR.VER)
        rows.set_scrollbar_mode(lv.SCROLLBAR_MODE.AUTO)
        rows.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        rows.set_flex_align(lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER,
                            lv.FLEX_ALIGN.CENTER)
        rows.set_style_pad_row(3, 0)
        rows.set_style_pad_all(0, 0)

        def _brow():
            r = lv.obj(rows)
            r.remove_flag(lv.obj.FLAG.SCROLLABLE)
            r.add_style(st["row"], 0)
            r.set_size(448, 28)
            return r

        # blk_w[id] = (row, on_btn, on_lbl, scope_btn, scope_lbl, tap_btn, tap_lbl)
        # so _refresh_settings can repaint every ON/OFF, the one-of scope and one-of tap.
        blk_w = {}
        self._set_widgets["blocks"] = blk_w

        def _scope_paint(bid, on):
            r, _ob, _ol, sb, sl, _tb, _tl = blk_w[bid]
            sb.set_style_bg_color(lv.color_hex(CYAN_RX if on else PANEL2), 0)
            sl.set_style_text_color(lv.color_hex(DARK_TXT if on else CYAN_RX), 0)
            r.set_style_bg_color(lv.color_hex(BORDER if on else PANEL2), 0)

        def _tap_paint(bid, on):
            _r, _ob, _ol, _sb, _sl, tb2, tl = blk_w[bid]
            tb2.set_style_bg_color(lv.color_hex(GREEN if on else PANEL2), 0)
            tl.set_style_text_color(lv.color_hex(DARK_TXT if on else CYAN_RX), 0)

        for bid, name, cplx in self._BLOCKS:
            r = _brow()
            nl = lv.label(r)
            nl.set_text(name)
            nl.set_size(168, 20)
            nl.add_style(st["name"], 0)

            safety_limiter = bid == 11
            on0 = True if safety_limiter else self._blk_on.get(bid, True)
            ob, ol = _sbtn(r, "SAFE" if safety_limiter else ("ON" if on0 else "OFF"),
                           58, 12, GRAY2 if safety_limiter else
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

            scope_lbl = "→I/Q" if cplx else "→DAC"
            sb, sl = _sbtn(r, scope_lbl, 88, 12, CYAN_RX)

            blk_w[bid] = (r, ob, ol, sb, sl, tb2, tl)

            if not safety_limiter:
                def on_cb(e, i=bid, b=ob, bl=ol):
                    v = 0 if self._blk_on.get(i, True) else 1
                    ok, _ = iq_call("block", i, v)
                    if not ok:
                        return
                    self._blk_on[i] = bool(v)
                    bl.set_text("ON" if v else "OFF")
                    bl.set_style_text_color(lv.color_hex(DARK_TXT if v else WHITE), 0)
                    b.set_style_bg_color(lv.color_hex(GREEN if v else PANEL2), 0)
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
            sb.add_event_cb(scope_cb, lv.EVENT.CLICKED, None)
            cbs.append(scope_cb)

        # -- extra bench knobs (still useful) as rows below the 11-block table --
        def _krow(label):
            r = _brow()
            l = lv.label(r)
            l.set_text(label)
            l.set_size(168, 20)
            l.add_style(st["name"], 0)
            g = _grp(r, 270)
            return r, g

        # IQADC is deliberately constructed in PGA_BYPASS.  Do not present a live
        # gain control that the firmware will truthfully ignore in this mode.
        _r, gg = _krow("RF GAIN (BYP)")
        gdn, _ = _sbtn(gg, "-", 34, 20, GRAY2)
        gval = lv.label(gg)
        gval.set_text("FIXED")
        gval.add_style(st["name"], 0)
        gval.set_style_text_color(lv.color_hex(GRAY2), 0)
        gup, _ = _sbtn(gg, "+", 34, 20, GRAY2)
        gdn.remove_flag(lv.obj.FLAG.CLICKABLE)
        gup.remove_flag(lv.obj.FLAG.CLICKABLE)
        self._set_widgets["rf"] = gval

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
            value = self.be.set_fine(0) if zero else self.be.fine_tune(d)
            if value is not None:
                lbl.set_text("%d" % value)
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
            self.p["atgt"] = min(max(self.p["atgt"] + d, 0.05), 1.0)
            if self.be.set_agc(self.be.agc, target=self.p["atgt"]):
                lbl.set_text("%d%%" % int(self.p["atgt"] * 100 + 0.5))
                self.touch_params()
        for b, d in ((atdn, -0.05), (atup, +0.05)):
            def _atc(e, dd=d):
                at_cb(e, dd)
            b.add_event_cb(_atc, lv.EVENT.CLICKED, None)
            cbs.append(_atc)

        # Manual I/Q correction: enable, Q amplitude balance, and I->Q phase leakage.
        def iqc_apply():
            iq = self.be.iq
            fn = getattr(iq, "iq_correction", None) if iq is not None else None
            if fn is None:
                self.be.err = "iq_correction unavailable"
                return False
            try:
                fn(enable=self._iqc_on, amp=self._iqc_amp, phase=self._iqc_phase)
                self.be.err = None
                return True
            except Exception as e:
                self.be.err = "iq_correction: %r" % (e,)
                return False

        _r, iqeg = _krow("IQ CORR")
        iqeb, iqel = _sbtn(iqeg, "ON" if self._iqc_on else "OFF", 64, 12,
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

        def iqc_paint():
            iqel.set_text("ON" if self._iqc_on else "OFF")
            iqel.set_style_text_color(
                lv.color_hex(DARK_TXT if self._iqc_on else WHITE), 0)
            iqeb.set_style_bg_color(
                lv.color_hex(GREEN if self._iqc_on else PANEL2), 0)
            iaval.set_text("%.2f" % self._iqc_amp)
            ipval.set_text("%+.2f" % self._iqc_phase)

        def iqc_enable_cb(e):
            self._iqc_on = not self._iqc_on
            if not iqc_apply():
                self._iqc_on = False
            iqc_paint()

        def iqc_reset_cb(e):
            self._iqc_on = False
            self._iqc_amp = 1.0
            self._iqc_phase = 0.0
            iqc_apply()
            iqc_paint()

        def iqc_amp_cb(e, d=0.0):
            self._iqc_amp = min(max(self._iqc_amp + d, 0.50), 1.50)
            self._iqc_on = True
            if not iqc_apply():
                self._iqc_on = False
            iqc_paint()

        def iqc_phase_cb(e, d=0.0):
            self._iqc_phase = min(max(self._iqc_phase + d, -0.50), 0.50)
            self._iqc_on = True
            if not iqc_apply():
                self._iqc_on = False
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

        # Every row built without raising: only now is the screen a valid, complete
        # tree. The caller publishes it to self.ui.w and loads it.
        self._set_scr_partial = None
        return scr

    def _refresh_settings(self):
        """Re-read the current state into every settings-screen control on each open,
        so a re-shown screen never displays the values captured at build time. Live
        read-outs (AGC gain, S-meter) keep updating via _consume_status while open."""
        w = self._set_widgets

        # Per-block table: repaint every ON/OFF, and re-apply the ONE-OF scope + tap
        # highlight so a re-shown screen matches the current routing exactly. _tap_id /
        # _scope_id are the single armed rows (0 = none); every other row is cleared.
        blk_w = w["blocks"]
        for bid, tpl in blk_w.items():
            r, ob, ol, sb, sl, tb2, tl = tpl
            if bid == 11:
                ol.set_text("SAFE")
                ol.set_style_text_color(lv.color_hex(GRAY2), 0)
                ob.set_style_bg_color(lv.color_hex(PANEL2), 0)
            else:
                on0 = self._blk_on.get(bid, True)
                ol.set_text("ON" if on0 else "OFF")
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

        # The current IQADC instance is fixed in PGA_BYPASS.
        w["rf"].set_text("FIXED")

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
        iqel.set_text("ON" if self._iqc_on else "OFF")
        iqel.set_style_text_color(
            lv.color_hex(DARK_TXT if self._iqc_on else WHITE), 0)
        iqeb.set_style_bg_color(lv.color_hex(GREEN if self._iqc_on else PANEL2), 0)
        w["iqc_amp"].set_text("%.2f" % self._iqc_amp)
        w["iqc_phase"].set_text("%+.2f" % self._iqc_phase)

        # TEST source preset, waveform, LIVE phase jitter, amplitude and ON/OFF.
        iml, iwl, ilb, ill, iamp, itg, itgl = w["inject"]
        iml.set_text(self._INJ_PRESETS[self._inj_mode][0])
        iwl.set_text(self._INJ_SHAPES[self._inj_shape][0])
        ill.set_text("LIVE" if self._inj_live else "CLEAN")
        ill.set_style_text_color(
            lv.color_hex(DARK_TXT if self._inj_live else CYAN_RX), 0)
        ilb.set_style_bg_color(
            lv.color_hex(GREEN if self._inj_live else PANEL2), 0)
        iamp.set_text("%d" % self._inj_ampl)
        itgl.set_text("ON" if self._inj_on else "OFF")
        itgl.set_style_text_color(
            lv.color_hex(DARK_TXT if self._inj_on else WHITE), 0)
        itg.set_style_bg_color(
            lv.color_hex(GREEN if self._inj_on else PANEL2), 0)

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
        self._set_modal("settings" in _KEEP)

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
                    self._hw_pending = True   # active VFO moved to a different output
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
            self.p["cal"] = round(self.p["cal"] + d, 2)
            lbl.set_text("%.2f ppm" % self.p["cal"])
            self._hw_pending = True    # re-program synth via the worker (no I2C here)
            self.touch_params()
        for b, d in ((cm, -0.1), (cp, +0.1)):
            def cb(e, dd=d):
                cal_adj(e, dd)
            b.add_event_cb(cb, lv.EVENT.CLICKED, None)
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
        """Show the exact former tuning control in the shared bottom slot."""
        self._mode_expanded = 3
        self.ui.get("mode-bar").add_flag(lv.obj.FLAG.HIDDEN)
        self.ui.get("tuning-row").remove_flag(lv.obj.FLAG.HIDDEN)

    def open_filter_menu(self):
        self._open_bottom_choices(2, BW_CHOICES[self.p["m"]], self.cur_bw())

    def toggle_spectrum_view(self):
        """Switch the left native panel SPEC <-> WF."""
        if not self._spec_native or not hasattr(self._spec_lcd, "spectrum"):
            return
        new_view = 1 - self._spectrum_view
        try:
            if self._spec_lcd.spectrum(new_view):
                self._spectrum_view = new_view
                self.ui.get("btn-mode-view").get_child(0).set_text(
                    "WF" if new_view else "SPEC")
        except Exception as e:
            self.be.err = "spectrum view: %r" % (e,)

    def toggle_scope_view(self):
        """Switch the right native panel TIME trace <-> I/Q constellation."""
        if not self._spec_native or not hasattr(self._spec_lcd, "scope_view"):
            return
        new_view = 1 - self._scope_view
        try:
            if self._spec_lcd.scope_view(new_view):
                self._scope_view = new_view
                self.ui.get("scope-view").set_text("I-Q" if new_view else "TIME")
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
                self.shift_spectrum(hz - self.p["f"])
                self.p["f"] = hz
                self._hw_pending = True
                self.touch_params()
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

        add("btn-step-down", mk(lambda e: self.tune(-self.p["s"])))
        add("btn-step-up",   mk(lambda e: self.tune(self.p["s"])))
        add("btn-fine-down", mk(lambda e: self.fine(-max(self.p["s"] // 10, 1))))
        add("btn-fine-up",   mk(lambda e: self.fine(max(self.p["s"] // 10, 1))))
        add("step-display",  mk(lambda e: self._set_mode_bar(False)))
        add("freq-digits", mk(lambda e: self.open_entry()))
        for k in (0, 1):
            def alt_cb(e, kk=k):
                self.switch_vfo(self._alt[kk])
            add("vfo-alt-%d" % k, mk(alt_cb))
        def brand_cb(e):
            if self._mode_expanded:
                self._set_mode_bar(False)       # SDR RECEIVER exits any bottom control
            else:
                self.open_route_menu()          # level 2: VFO routing + CAL + BACKEND button
        add("brand-row", mk(brand_cb))

        def spec_cb(e):
            indev = lv.indev_active()
            if indev is None:
                return
            pt = lv.point_t()
            indev.get_point(pt)
            a = lv.area_t()
            ui.get("spectrum-waterfall").get_coords(a)
            local_x = pt.x - a.x1
            # The native surface is physically 256 px spectrum + 4 px gap +
            # 128 px right panel.  A left tap tunes; a right tap is the zero-widget
            # TIME/I-Q control.  This also fixes the former frequency mapping, which
            # incorrectly stretched the 256 spectrum pixels across all 388 pixels.
            if self._spec_native:
                if 0 <= local_x < 256:
                    self.spec_jump(local_x / 255.0)
                elif 260 <= local_x < 388:
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
            # The collapsed slider adjusts whichever gain (RF/AF/AGC) is currently active,
            # not always volume; _apply_gain routes AF through the deferred _vol_pending path.
            v = ui.get("vol-slider").get_value()
            self._apply_gain(self._active_gain, v)
            _lo, _hi, _cur, fmt = self._gain_spec(self._active_gain)
            ui.get("vol-value").set_text(fmt(v))
        _volcb = mk(vol_cb)
        _vsl = ui.get("vol-slider")
        _vsl.add_event_cb(_volcb, lv.EVENT.RELEASED, None)
        _vsl.add_event_cb(_volcb, lv.EVENT.PRESS_LOST, None)
        add("vol-header", mk(lambda e: self.toggle_gains_panel()))

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
                if self._hw_pending:
                    self._hw_pending = False
                    self.hw_tune()          # the ONLY place Si5351 I2C happens
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
        for key in ("settings", "route_menu", "gains_panel"):
            _KEEP.pop(key, None)
        old_app._drop_settings_screen()
        old_app._drop_route_screen()
        if had_gains:
            old_app._gain_vlbls = {}
            old_app._bind_active_slider()
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

    # Cold/soft-reset build path.  _KEEP is a module global that survives a soft
    # restart, so transient nav/overlay open-flags can outlive the widget trees they
    # referred to.  A stale "settings" flag makes open_settings() short-circuit
    # (BACKEND looks dead), so clear the transient open-flags before rebuilding.  The
    # widget trees they pointed at are gone with the old UI; only the flags linger.
    # Do NOT touch the hardware/loop handles (lcd, loop, dd, ui, app, vs_lcd, vs_cb,
    # sdr_poll, cbs, blink).
    for _k in ("settings", "route_menu", "pick_menu", "gains_panel"):
        _KEEP.pop(_k, None)

    gc.collect()  # release source/compiler temporaries before building the widget tree

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
                _lcd.vsync(20)

            dd.add_event_cb(_vs_cb, ev, None)
            _KEEP.update(vs_lcd=_lcd, vs_cb=_vs_cb)
    except Exception as e:
        print("vsync gate unavailable:", repr(e))

    ui = build()
    app = SdrApp(ui)
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
