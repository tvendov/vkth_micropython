# class_a_demo.py — LoRaWAN Class A demo for Grafana pipeline (VK_RA4M2 + Wio-SX1262 COM34)
# Ported 1:1 from ../Example_C/class_c_demo.py — Class A wake/sense/uplink/sleep cycle
# Frame contract: UL fPort 10, 9 bytes; DL fPort 20.
#
# SELF-CONTAINED single-file deliverable: imports ONLY built-ins plus the board
# `lorawan` / `dataflash` modules. Credentials, NVM ping-pong persistence, the
# NONCE journal, CRC16, and the data-flash partition constants are ALL inlined
# below — no cross-imports of project helper modules. Credentials come from the
# CRED record in data flash (provision with provision_credentials.py); there is
# deliberately NO hardcoded-key fallback.

import gc
import time
import struct
import array
import framebuf
from machine import Pin, SoftI2C, WS2812
import lorawan
import dataflash

# ============================================================================
# Zero-alloc hot-path buffers. Operator rule: after init everything is static —
# no per-loop object allocation. These are allocated ONCE at module import and
# reused for the lifetime of the demo. A MemoryError (allocating 2344 B) once
# crashed the NVM save under demo memory pressure; eliminating the hot-path
# blob+pad churn is the standing fix.
# ============================================================================

# NVM blob scratch. mac.nvm_blob_into() packs the ~1.4 KB v2 blob in place; the
# 4-byte tail pad is written in place too — one buffer, zero fresh allocations.
# NVM_A/NVM_B partition is 2048 B, blob measured ~1385 B; 1500 B leaves margin.
_NVM_BUF = bytearray(1500)

# Uplink payload scratch (frame is a fixed 9 bytes; struct.pack_into in place).
_PKT = bytearray(9)

# ============================================================================
# Inlined data-flash layer (CRC16 + partition constants + CRED/CONFIG/NVM/NONCE)
# ============================================================================

# CRC16-CCITT (XModem): poly 0x1021, init 0xFFFF. crc16_ccitt(b"123456789")==0x29B1
def crc16_ccitt(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


# Region names — Python mirror of dataflash_partition.h.
REGION_CRED = "CRED"
REGION_NVM_A = "NVM_A"
REGION_NVM_B = "NVM_B"
REGION_NONCE = "NONCE"
REGION_CONFIG = "CONFIG"
DF_WRITE_UNIT = 4

# CRED v2 record (44 B, region "CRED" off 0).
CRED_MAGIC = b"LWCR"
CRED_VERSION = 0x02
CRED_RECORD_LEN = 44
OFF_MAGIC = 0
OFF_VERSION = 4
OFF_DEVEUI = 6
OFF_JOINEUI = 14
OFF_APPKEY = 22
OFF_DEVNUM = 38
OFF_CRC = 42

# CONFIG record (region "CONFIG" off 0, 64 B block).
CONFIG_MAGIC = b"LWCF"
CONFIG_RECORD_LEN = 14
CFG_OFF_MAGIC = 0
CFG_OFF_INTERVAL = 4
CFG_OFF_TS = 8
CFG_OFF_CRC = 12

# NVM bank ping-pong header (16 B, 4-byte aligned) at offset 0 of NVM_A/NVM_B.
# The validating magic occupies the LAST word and is written in its own program
# op AFTER the payload, so a torn save leaves the bank un-validated and the old
# bank stays authoritative (power-loss atomicity).
BANK_MAGIC = b"NVMB"
BANK_HDR_LEN = 16
BANK_DATA_OFF = BANK_HDR_LEN
H_SEQ = 0          # uint32 LE; 0xFFFFFFFF == blank/erased
H_LEN = 4          # uint32 LE blob length
H_MAGIC = 12       # "NVMB", written LAST
SEQ_BLANK = 0xFFFFFFFF
_NVM_REGIONS = (REGION_NVM_A, REGION_NVM_B)

# After a power loss we may lose up to a day of unsaved FCnt advances, so on
# restore we skip the frame counter forward by N_MAX to stay ahead of the
# network's last-seen FCnt and dodge replay rejection. N_MAX must stay under the
# LoRaWAN MAX_FCNT_GAP (16384) or the server treats the device as out of sync.
MIN_INTERVAL_S = 10
N_MAX = 86400 // MIN_INTERVAL_S       # = 8640
if N_MAX >= 16384:
    raise ValueError("N_MAX (%d) >= MAX_FCNT_GAP (16384)" % N_MAX)

# NONCE journal: append-only 4-byte slots in region "NONCE" (128 B = 32 slots).
# DevNonce is uint16; stored in the low 2 bytes, high 2 bytes zero. Erased word
# 0xFFFFFFFF == blank, so 0xFFFF is the one nonce indistinguishable from blank
# (acceptable: device rejoins long before exhausting the nonce space).
#
# NOTE: the canonical DevNonce is already persisted INSIDE the NVM blob
# (mod_lorawan.c persists DevNonce/FCnt/session via the blob). This standalone
# journal is therefore an optional secondary record; it stays dormant until the
# C layer exposes a `mac.devnonce()` accessor to feed _nonce_append() at join.
# The helpers below are inlined and ready so wiring is a one-line change.
NONCE_REC_LEN = 4
NONCE_BLANK = 0xFFFFFFFF

# 24 h save cadence. Shorten this module constant for bench testing.
_NVM_SAVE_PERIOD_MS = 15 * 1000   # DF-5 bench: 15s (production = 24h)


def _u32le(buf, off):
    return (buf[off] | (buf[off + 1] << 8) | (buf[off + 2] << 16) | (buf[off + 3] << 24))


def _put_u32le(buf, off, val):
    buf[off] = val & 0xFF
    buf[off + 1] = (val >> 8) & 0xFF
    buf[off + 2] = (val >> 16) & 0xFF
    buf[off + 3] = (val >> 24) & 0xFF


def _pad4(buf):
    n = len(buf)
    a = (n + (DF_WRITE_UNIT - 1)) & ~(DF_WRITE_UNIT - 1)
    if a == n:
        return bytes(buf)
    return bytes(buf) + b"\xff" * (a - n)


def _load_credentials():
    # Returns (deveui, joineui, appkey, device_number) or None if blank/invalid.
    cred = dataflash.region(REGION_CRED)
    rec = cred.read(0, CRED_RECORD_LEN)
    if rec[OFF_MAGIC:OFF_MAGIC + 4] != CRED_MAGIC:
        return None
    if rec[OFF_VERSION] != CRED_VERSION:
        return None
    stored = (rec[OFF_CRC] << 8) | rec[OFF_CRC + 1]
    if crc16_ccitt(rec[0:OFF_CRC]) != stored:
        return None
    deveui = bytes(rec[OFF_DEVEUI:OFF_DEVEUI + 8])
    joineui = bytes(rec[OFF_JOINEUI:OFF_JOINEUI + 8])
    appkey = bytes(rec[OFF_APPKEY:OFF_APPKEY + 16])
    device_number = ((rec[OFF_DEVNUM] << 24) | (rec[OFF_DEVNUM + 1] << 16)
                     | (rec[OFF_DEVNUM + 2] << 8) | rec[OFF_DEVNUM + 3])
    return (deveui, joineui, appkey, device_number)


def _config_save(interval_s, ts=0):
    rec = bytearray(CONFIG_RECORD_LEN)
    rec[CFG_OFF_MAGIC:CFG_OFF_MAGIC + 4] = CONFIG_MAGIC
    _put_u32le(rec, CFG_OFF_INTERVAL, interval_s & 0xFFFFFFFF)
    _put_u32le(rec, CFG_OFF_TS, ts & 0xFFFFFFFF)
    crc = crc16_ccitt(rec[0:CFG_OFF_CRC])
    rec[CFG_OFF_CRC] = (crc >> 8) & 0xFF
    rec[CFG_OFF_CRC + 1] = crc & 0xFF
    cfg = dataflash.region(REGION_CONFIG)
    cfg.erase()
    cfg.write(0, _pad4(rec))


def _config_load():
    # Returns (interval_s, last_write_ts) or None if blank/invalid.
    cfg = dataflash.region(REGION_CONFIG)
    rec = cfg.read(0, CONFIG_RECORD_LEN)
    if rec[CFG_OFF_MAGIC:CFG_OFF_MAGIC + 4] != CONFIG_MAGIC:
        return None
    stored = (rec[CFG_OFF_CRC] << 8) | rec[CFG_OFF_CRC + 1]
    if crc16_ccitt(rec[0:CFG_OFF_CRC]) != stored:
        return None
    return (_u32le(rec, CFG_OFF_INTERVAL), _u32le(rec, CFG_OFF_TS))


def _read_bank_header(region_name):
    # Returns (seq, blob_len) if the bank validates, else None.
    reg = dataflash.region(region_name)
    hdr = reg.read(0, BANK_HDR_LEN)
    if hdr[H_MAGIC:H_MAGIC + 4] != BANK_MAGIC:
        return None
    seq = _u32le(hdr, H_SEQ)
    if seq == SEQ_BLANK:
        return None
    blob_len = _u32le(hdr, H_LEN)
    if blob_len == 0 or blob_len > (reg.size() - BANK_DATA_OFF):
        return None
    return (seq, blob_len)


def _scan_banks():
    out = []
    for name in _NVM_REGIONS:
        h = _read_bank_header(name)
        if h is not None:
            out.append((name, h[0], h[1]))
    return out


def _pick_inactive_and_next_seq():
    # Active = valid bank with highest seq; write the OTHER one. Cold boot ->
    # NVM_A seq 1.
    banks = _scan_banks()
    if not banks:
        return (REGION_NVM_A, 1)
    banks.sort(key=lambda t: t[1])
    active = banks[-1]
    next_seq = active[1] + 1
    if next_seq >= SEQ_BLANK:
        next_seq = 1
    target = REGION_NVM_B if active[0] == REGION_NVM_A else REGION_NVM_A
    return (target, next_seq)


def _nvm_restore(mac):
    # Read both banks, restore newest-valid (fall back to older on ver-mismatch),
    # then advance the frame counter. Returns (status, new_fcnt) or (None, None)
    # on cold boot. Ordering contract: restore THEN advance — advance_fcnt()
    # returns -1 if called before a successful restore this boot.
    banks = _scan_banks()
    if not banks:
        return (None, None)
    banks.sort(key=lambda t: t[1])
    for name, seq, blob_len in reversed(banks):
        reg = dataflash.region(name)
        if blob_len > len(_NVM_BUF):
            continue   # blob can't fit the static buffer — skip this bank
        # Read into the module-scope buffer (no fresh bytes alloc), then hand
        # nvm_restore_blob a memoryview slice (it reads via mp_get_buffer).
        reg.readinto(BANK_DATA_OFF, memoryview(_NVM_BUF)[:blob_len])
        status = mac.nvm_restore_blob(memoryview(_NVM_BUF)[:blob_len])
        if status >= 0:
            new_fcnt = mac.advance_fcnt(N_MAX)
            return (status, new_fcnt)
    return (None, None)


def _nvm_save(mac, interval_s, rtc_ts=0):
    # Persist the MAC NVM blob to the inactive bank (ping-pong) if dirty, then
    # save CONFIG. Returns True if a blob was written, False if nothing dirty.
    #
    # Zero big-buffer churn: the blob packs directly into the module-scope
    # _NVM_BUF (mac.nvm_blob_into, no fresh bytes), the 4-byte tail pad is
    # written in place, and the flash write takes a memoryview slice. This
    # replaces the old blob+_pad4 pattern that held two ~1.4 KB buffers live
    # at once (~2.8 KB peak) and triggered a MemoryError under demo pressure.
    if not mac.nvm_dirty():
        _config_save(interval_s, rtc_ts)
        return False

    blob_len = mac.nvm_blob_into(_NVM_BUF)   # packs in place, returns length
    aligned = (blob_len + (DF_WRITE_UNIT - 1)) & ~(DF_WRITE_UNIT - 1)
    for i in range(blob_len, aligned):
        _NVM_BUF[i] = 0xFF                    # pad tail in place to write unit
    target, seq = _pick_inactive_and_next_seq()
    reg = dataflash.region(target)
    if blob_len > (reg.size() - BANK_DATA_OFF):
        raise ValueError("NVM blob %d B exceeds bank capacity %d B"
                         % (blob_len, reg.size() - BANK_DATA_OFF))

    reg.erase()
    # payload first; bank not yet valid. memoryview slice => no copy/alloc.
    reg.write(BANK_DATA_OFF, memoryview(_NVM_BUF)[:aligned])

    # Header words 0..11 (seq+len) only — must NOT touch the magic word @12.
    # RA4M2 data flash forbids re-programming an already-written 4-byte word
    # (even 0xFF->data), so writing the full 16 B header here then re-writing
    # word @12 below faults EIO. Write seq+len now, magic LAST in its own op.
    pre = bytearray(b"\xff" * H_MAGIC)      # 12 bytes: seq(4)+len(4)+gap(4)
    _put_u32le(pre, H_SEQ, seq)
    _put_u32le(pre, H_LEN, blob_len)
    reg.write(0, bytes(pre))

    reg.write(H_MAGIC, BANK_MAGIC)          # validating magic LAST (word @12)

    mac.nvm_clear_dirty()
    _config_save(interval_s, rtc_ts)
    print("NVM SAVE bank=%s seq=%d blob=%dB ul_total=%d(F0)"
          % (target, seq, blob_len, _ul_total))
    return True


def _nonce_last():
    # Returns the most recently appended DevNonce, or None if blank.
    reg = dataflash.region(REGION_NONCE)
    n = reg.size() // NONCE_REC_LEN
    data = reg.read(0, n * NONCE_REC_LEN)
    last = None
    for i in range(n):
        w = _u32le(data, i * NONCE_REC_LEN)
        if w == NONCE_BLANK:
            break
        last = w & 0xFFFF
    return last


def _nonce_append(devnonce):
    # Append one DevNonce (uint16); erase + restart when the journal is full.
    reg = dataflash.region(REGION_NONCE)
    n = reg.size() // NONCE_REC_LEN
    data = reg.read(0, n * NONCE_REC_LEN)
    slot = n
    for i in range(n):
        if _u32le(data, i * NONCE_REC_LEN) == NONCE_BLANK:
            slot = i
            break
    if slot >= n:
        reg.erase()
        slot = 0
    word = devnonce & 0xFFFF
    reg.write(slot * NONCE_REC_LEN, bytes((word & 0xFF, (word >> 8) & 0xFF, 0x00, 0x00)))
    return slot


# ============================================================================

RGB_OFF      = (0, 0, 0)
RGB_BOOT     = (40, 20, 0)
RGB_JOIN     = (60, 0, 0)
RGB_JOINED   = (0, 40, 0)
RGB_TX       = (60, 0, 0)
RGB_RX       = (60, 60, 0)
RGB_DOWNLINK = (60, 0, 60)
RGB_ERROR    = (60, 0, 40)


class RgbStatus:
    def __init__(self, pin_name="P112", power_pin_name="P500"):
        self.power = Pin(power_pin_name, Pin.OUT, value=1)
        time.sleep_ms(100)
        self.strip = WS2812(pixel_count=1, pin=Pin(pin_name), channels=3)
        self._cur = None
        self.set(RGB_BOOT)

    def set(self, color):
        if color == self._cur:
            return
        self._cur = color
        self.strip[0] = color
        self.strip.write()

    def force(self, color):
        self._cur = color
        self.strip[0] = color
        self.strip.write()

    def flash(self, color, ms=100, times=2):
        prev = self._cur
        for _ in range(times):
            self.force(color); time.sleep_ms(ms)
            self.force(RGB_OFF); time.sleep_ms(ms)
        if prev is not None:
            self.force(prev)


class OLED:
    ADDR = 0x3C
    W, H = 128, 32

    def __init__(self, i2c):
        self.i2c   = i2c
        self.pages = self.H // 8
        self._tx   = bytearray(1 + self.W * self.pages)
        self._tx[0] = 0x40
        self.fb = framebuf.FrameBuffer(memoryview(self._tx)[1:],
                                        self.W, self.H, framebuf.MONO_VLSB)
        cmd = bytearray(2); cmd[0] = 0x00
        for c in (0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
                  0x8D, 0x14, 0x20, 0x00, 0xA0, 0xC0, 0xDA, 0x02,
                  0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF):
            cmd[1] = c; self.i2c.writeto(self.ADDR, cmd)
        self._win = bytearray(b"\x00\x21\x00\x7F\x22\x00" + bytes([self.pages - 1]))
        self.fb.fill(0); self.show()

    def show(self):
        # Retry on ENODEV — MAC stack ISRs can corrupt SoftI2C bit-banging
        # mid-transaction, breaking the slave's ACK. Up to 3 attempts.
        for _ in range(3):
            try:
                self.i2c.writeto(self.ADDR, self._win)
                self.i2c.writeto(self.ADDR, self._tx)
                return
            except OSError:
                time.sleep_ms(2)


class AHT20:
    ADDR = 0x38

    def __init__(self, i2c):
        self._i2c = i2c
        time.sleep_ms(40)
        self._i2c.writeto(self.ADDR, bytes([0xBA]))
        time.sleep_ms(20)
        self._i2c.writeto(self.ADDR, bytes([0xBE, 0x08, 0x00]))
        time.sleep_ms(10)
        st = self._i2c.readfrom(self.ADDR, 1)[0]
        self._ok = bool(st & 0x08)
        print("AHT20 status=0x%02X calibrated=%s" % (st, self._ok))

    def read(self):
        try:
            self._i2c.writeto(self.ADDR, bytes([0xAC, 0x33, 0x00]))
            time.sleep_ms(80)
            raw = self._i2c.readfrom(self.ADDR, 7)
            if raw[0] & 0x80:
                return 0, 0, False
            hum_raw  = ((raw[1] << 12) | (raw[2] << 4) | (raw[3] >> 4)) & 0xFFFFF
            temp_raw = (((raw[3] & 0x0F) << 16) | (raw[4] << 8) | raw[5]) & 0xFFFFF
            hum_pct  = hum_raw  / 1048576.0 * 100.0
            temp_c   = temp_raw / 1048576.0 * 200.0 - 50.0
            temp_c100 = int(temp_c * 100)
            hum_p2    = min(200, int(hum_pct * 2))
            if temp_c100 > 32767:  temp_c100 = 32767
            if temp_c100 < -32768: temp_c100 = -32768
            return temp_c100, hum_p2, True
        except Exception:
            return 0, 0, False


_uplink_interval_s = 30
_adr_enabled       = True
_confirmed         = False
_rejoin_flag       = False
_state             = "BOOT"
_ul_total          = 0
_dl_total          = 0
_temp_c100         = 0
_hum_p2            = 0
_last_rx_rssi      = 0
_last_rx_snr       = 0
_relay_state       = 0   # ON/OFF flag set by DL cmd 0x04, mirrors P103
_relay         = None  # machine.Pin on P103 (init in main)
_force_uplink      = False  # set by relay DL → triggers immediate confirming UL
_device_number     = 0   # from CRED record; identity only (frame is full at 9B)
_last_nvm_save_t   = 0   # ticks_ms of last NVM ping-pong save

# (tag, status) where tag 1 == mcps_indication (DL ready)
_MCPS_IND = 1

# Static SPSC event ring (zero-alloc). Replaces the old list-of-tuples that
# allocated a tuple + triggered a list realloc on every MAC event. Producer is
# _ev_cb (runs in foreground mac.process() context — never a hard ISR, see
# mod_lorawan.c); consumer is the drain loop in main(). One slot is left empty
# to disambiguate full vs empty, so capacity is _EV_N - 1 live events.
_EV_N = 16
_ev_ticks = array.array('I', [0] * _EV_N)   # time.ticks_ms() per event
_ev_packed = array.array('I', [0] * _EV_N)   # packed (tag|status) per event
_ev_head = 0   # producer writes here, then advances (drain reads up to head)
_ev_tail = 0   # consumer reads here, then advances


def _ev_cb(packed, *_):
    # Callback context: no allocation. Drop the event if the ring is full
    # rather than block — the drain runs every mac.process() iteration so the
    # ring only fills if Python is wedged, in which case a dropped MAC-event
    # notification is the least of the problems.
    global _ev_head
    h = _ev_head
    nxt = (h + 1) % _EV_N
    if nxt == _ev_tail:
        return
    _ev_ticks[h] = time.ticks_ms()
    _ev_packed[h] = packed
    _ev_head = nxt


def _render(oled):
    if oled is None:
        return
    fb = oled.fb
    fb.fill(0)
    fb.text("%-9s A" % _state[:9], 0, 0, 1)
    if _temp_c100 != 0 or _hum_p2 != 0:
        t = _temp_c100 / 100.0
        h = _hum_p2 / 2.0
        fb.text("T%5.1f H%4.1f%%" % (t, h), 0, 8, 1)
    else:
        fb.text("sensor: ----", 0, 8, 1)
    relay_txt = "ON " if _relay_state else "OFF"
    fb.text("UL%3d DL%3d %s" % (_ul_total, _dl_total, relay_txt), 0, 16, 1)
    fb.text("R%4d S%3d %2ds" % (_last_rx_rssi, _last_rx_snr, _uplink_interval_s),
            0, 24, 1)
    oled.show()


def _build_payload(sensor, battery_mv=3300):
    global _temp_c100, _hum_p2
    if sensor is not None:
        temp_c100, hum_p2, sensor_ok = sensor.read()
        if sensor_ok:
            _temp_c100, _hum_p2 = temp_c100, hum_p2
    else:
        temp_c100, hum_p2, sensor_ok = _temp_c100, _hum_p2, False
    flags  = 0
    flags |= 0x01 if _confirmed   else 0
    flags |= 0x02 if _adr_enabled else 0
    flags |= 0x04 if sensor_ok    else 0
    flags |= 0x08 if _relay_state else 0
    # Pack in place into the module-scope _PKT — no per-uplink bytes alloc.
    struct.pack_into("<hBHBbbB", _PKT, 0,
        temp_c100, hum_p2, battery_mv, flags, _last_rx_rssi, _last_rx_snr,
        _uplink_interval_s // 5)
    return _PKT


def _dispatch_downlink(port, data, rgb):
    global _uplink_interval_s, _rejoin_flag
    if port != 20 or len(data) < 1:
        return
    cmd = data[0]
    if cmd == 0x01 and len(data) >= 2:           # set_interval (units of 10s)
        secs = data[1] * 10
        if secs > 0:
            _uplink_interval_s = secs
            print("RX port=20 cmd=set_interval s=%d" % secs)
    elif cmd == 0x02:                             # force_rejoin
        _rejoin_flag = True
        print("RX port=20 cmd=force_rejoin")
    elif cmd == 0x03 and len(data) >= 2:          # led_test
        count = data[1]
        rgb.flash(RGB_DOWNLINK, ms=100, times=count)
        print("RX port=20 cmd=led_test count=%d" % count)
    elif cmd == 0x04 and len(data) >= 2:          # set_relay → P103 + flag
        global _relay_state, _force_uplink
        _relay_state = 1 if data[1] else 0
        if _relay is not None:
            _relay.value(_relay_state)
        _force_uplink = True                       # confirm new state ASAP
        print("RX port=20 cmd=set_relay val=%d (force UL)" % _relay_state)
    elif cmd == 0x05 and len(data) >= 2:          # unified set_interval (units 5s)
        secs = data[1] * 5
        if secs >= 10:
            _uplink_interval_s = secs
            print("RX port=20 cmd=set_interval(0x05) s=%d" % secs)
        else:
            print("RX port=20 cmd=set_interval(0x05) REJECTED s=%d (<10)" % secs)


def main():
    global _adr_enabled, _rejoin_flag, _state, _ul_total, _dl_total
    global _temp_c100, _hum_p2, _last_rx_rssi, _last_rx_snr, _relay, _force_uplink
    global _device_number, _uplink_interval_s, _last_nvm_save_t

    gc.collect()

    rgb = RgbStatus()
    print("boot: VK_RA4M2 Class A demo")

    _relay = Pin("P103", Pin.OUT, value=0)
    print("P103 relay init=0")

    i2c = SoftI2C(
        scl=Pin("P301", Pin.OPEN_DRAIN),
        sda=Pin("P302", Pin.OPEN_DRAIN),
        freq=100000,
    )

    try:
        oled = OLED(i2c)
        print("OLED OK")
    except Exception as e:
        oled = None
        print("OLED skipped:", e)

    try:
        sensor = AHT20(i2c)
        print("AHT20 OK")
    except Exception as e:
        sensor = None
        print("AHT20 skipped:", e)

    if sensor is not None:
        try:
            t, h, ok = sensor.read()
            if ok:
                _temp_c100, _hum_p2 = t, h
                print("sensor: %.2fC  %.1f%%" % (t / 100.0, h / 2.0))
        except Exception as e:
            print("sensor init read err:", e)
    _state = "BOOT"
    _render(oled)

    # Credentials come from the CRED record only — NO compiled-key fallback.
    creds = _load_credentials()
    if creds is None:
        print("CRED blank/invalid — provision with provision_credentials.py; refusing to join")
        _state = "NOCRED"
        rgb.set(RGB_ERROR)
        _render(oled)
        return
    deveui, joineui, appkey, _device_number = creds
    print("CRED OK device_number=%d DevEUI=%s"
          % (_device_number, "".join("%02X" % x for x in deveui)))

    cfg = _config_load()
    if cfg is not None and cfg[0] >= MIN_INTERVAL_S:
        _uplink_interval_s = cfg[0]
        print("CONFIG interval_s=%d (restored)" % _uplink_interval_s)

    # lorawan.Mac owns and creates SPI(3); pre-opening it here causes "SPI bus busy".
    mac = lorawan.Mac(region=lorawan.EU868)
    mac.set_event_callback(_ev_cb)
    mac.lorawan_init()
    mac.init_defaults()
    mac.set_keys(deveui, joineui, appkey)

    status, fcnt = _nvm_restore(mac)
    if status is None:
        print("NVM cold boot (no valid bank) — fresh join")
    else:
        print("NVM restore status=%d advance_fcnt(N_MAX=%d)->%d" % (status, N_MAX, fcnt))

    _last_nvm_save_t = time.ticks_ms()
    _resumed = (status == 1)   # restored a JOINED session — skip the join

    global _ev_tail, _ev_head

    while True:
        _rejoin_flag = False
        # Reset the event ring (drop anything queued from a prior join cycle).
        _ev_tail = _ev_head

        if _resumed:
            # NVM restore brought back a live session (keys + DevAddr + FCnt,
            # already advanced by N_MAX). Do NOT re-join — that would reset the
            # frame counter and defeat the cold-boot margin. Go straight to RUN.
            _resumed = False
            print("NVM resume — skipping join, FCnt carried forward")
            _state = "RUN"
            rgb.set(RGB_JOINED)
            _render(oled)
        else:
            _state = "JOIN"
            rgb.set(RGB_JOIN)
            _render(oled)
            print("joining...")
            try:
                mac.set_datarate(5)
            except AttributeError:
                pass
            jstatus = mac.join(5)

            if jstatus != 0 or not mac.is_joined():
                print("join failed status=%d — retrying in 10s" % jstatus)
                _state = "ERR"
                rgb.set(RGB_ERROR)
                _render(oled)
                t_wait = time.ticks_add(time.ticks_ms(), 10_000)
                while time.ticks_diff(t_wait, time.ticks_ms()) > 0:
                    mac.process()
                    time.sleep_ms(50)
                continue

            print("joined!")
            _state = "RUN"
            rgb.set(RGB_JOINED)
            _render(oled)

        last_uplink_t = time.ticks_add(time.ticks_ms(), -_uplink_interval_s * 1000)
        last_render_t = 0

        while not _rejoin_flag:
            mac.process()

            # Drain DL events from the static ring (zero-alloc; SPSC).
            while _ev_tail != _ev_head:
                packed = _ev_packed[_ev_tail]
                _ev_tail = (_ev_tail + 1) % _EV_N
                tag = packed & 0xFF
                if tag != _MCPS_IND:
                    continue
                # Sample RSSI/SNR FIRST — fires for every DL slot incl. MAC-only.
                try:
                    _rssi, _snr, _valid = mac.last_rx_stats()
                    if _valid:
                        _last_rx_rssi, _last_rx_snr = _rssi, _snr
                except (AttributeError, OSError):
                    pass
                rx = mac.recv()
                if rx is None:
                    print("MAC-only DL rssi=%d snr=%d" % (_last_rx_rssi, _last_rx_snr))
                    continue
                port, payload = rx
                _dl_total += 1
                print("RX port=%d len=%d rssi=%d snr=%d" %
                      (port, len(payload), _last_rx_rssi, _last_rx_snr))
                rgb.set(RGB_DOWNLINK)
                _dispatch_downlink(port, payload, rgb)
                _render(oled)
                time.sleep_ms(200)
                rgb.set(RGB_JOINED)

            # DC-safe forced UL: relay-confirm fires early but keeps >=3 s spacing
            # (EU868 1% duty cycle — back-to-back UL on same channel would be
            #  rejected; 3 s lets LoRaMac pick a free channel).
            forced = _force_uplink and \
                time.ticks_diff(time.ticks_ms(), last_uplink_t) >= 3000
            due = forced or \
                time.ticks_diff(time.ticks_ms(), last_uplink_t) >= _uplink_interval_s * 1000
            if due:
                _force_uplink = False
                _state = "TX"
                _render(oled)
                pkt = _build_payload(sensor)
                _dt = time.ticks_diff(time.ticks_ms(), last_uplink_t)
                print("uplink fPort=10 len=%d dt=%dms set=%ds" %
                      (len(pkt), _dt, _uplink_interval_s))
                rgb.set(RGB_TX)
                mac.send(10, pkt, _confirmed)
                _ul_total += 1
                last_uplink_t = time.ticks_ms()
                # Pump through TX + RX1 + RX2 windows (~6 s covers SF12 RX2)
                tx_end = time.ticks_add(time.ticks_ms(), 6_000)
                _state = "RX"
                rgb.set(RGB_RX)
                while time.ticks_diff(tx_end, time.ticks_ms()) > 0:
                    mac.process()
                    # Drain DLs that arrive during the RX windows (static ring)
                    while _ev_tail != _ev_head:
                        packed = _ev_packed[_ev_tail]
                        _ev_tail = (_ev_tail + 1) % _EV_N
                        tag = packed & 0xFF
                        if tag != _MCPS_IND:
                            continue
                        # Sample RSSI/SNR FIRST — fires for every DL slot incl. MAC-only.
                        try:
                            _rssi, _snr, _valid = mac.last_rx_stats()
                            if _valid:
                                _last_rx_rssi, _last_rx_snr = _rssi, _snr
                        except (AttributeError, OSError):
                            pass
                        rx = mac.recv()
                        if rx is None:
                            print("MAC-only DL rssi=%d snr=%d" %
                                  (_last_rx_rssi, _last_rx_snr))
                            continue
                        port, payload = rx
                        _dl_total += 1
                        print("RX port=%d len=%d rssi=%d snr=%d" %
                              (port, len(payload), _last_rx_rssi, _last_rx_snr))
                        rgb.set(RGB_DOWNLINK)
                        _dispatch_downlink(port, payload, rgb)
                        _render(oled)
                        rgb.set(RGB_RX)
                    time.sleep_ms(20)
                _state = "RUN"
                rgb.set(RGB_JOINED)
                _render(oled)

            if time.ticks_diff(time.ticks_ms(), last_render_t) >= 2_000:
                _render(oled)
                last_render_t = time.ticks_ms()

            # 24 h NVM ping-pong save cadence (shorten _NVM_SAVE_PERIOD_MS for
            # bench). Only writes flash when the MAC NVM is actually dirty.
            if time.ticks_diff(time.ticks_ms(), _last_nvm_save_t) >= _NVM_SAVE_PERIOD_MS:
                wrote = _nvm_save(mac, _uplink_interval_s)
                _last_nvm_save_t = time.ticks_ms()
                if wrote:
                    print("NVM save: ping-pong bank + CONFIG written")

            time.sleep_ms(50)

        # On forced rejoin, flush NVM+CONFIG to the inactive bank if dirty.
        _nvm_save(mac, _uplink_interval_s)
        _last_nvm_save_t = time.ticks_ms()
        print("force_rejoin — restarting")
        gc.collect()


if __name__ == "__main__":
    main()
