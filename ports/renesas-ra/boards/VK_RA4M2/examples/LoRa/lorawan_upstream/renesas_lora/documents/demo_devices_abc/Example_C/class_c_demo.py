# class_c_demo.py — LoRaWAN Class C Instant-Response Controller
# VK_RA4M2 + Wio-SX1262 (COM34)
# Sync polling pattern — direct extension of LORAWAN_TESTS/testH_classc.py
# fPort 12 uplink (9 bytes), fPort 22/23 downlink control

import gc
import time
import struct
import framebuf
from machine import Pin, SoftI2C, WS2812, SPI
import lorawan

# ---------------------------------------------------------------------------
# Credentials (MSB-first bytes; LoRaMac reverses DevEUI/JoinEUI on-air)
# ---------------------------------------------------------------------------
DEVEUI  = bytes.fromhex("70B3D57ED0070003")
JOINEUI = bytes.fromhex("0000000000000000")
APPKEY  = bytes.fromhex("202CB141A5842931F99C0C1DDFE70D68")

# ---------------------------------------------------------------------------
# WS2812 color constants
# ---------------------------------------------------------------------------
RGB_OFF      = (0, 0, 0)
RGB_BOOT     = (40, 20, 0)
RGB_JOIN     = (60, 0, 0)
RGB_JOINED   = (0, 40, 0)
RGB_TX       = (0, 0, 60)
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


# ---------------------------------------------------------------------------
# SSD1306 128x32 OLED (addr 0x3C). Orientation 0xA0+0xC0.
# ---------------------------------------------------------------------------
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


# ---------------------------------------------------------------------------
# AHT20 sensor (sync — ~80 ms blocking measurement)
# ---------------------------------------------------------------------------
class AHT20:
    ADDR = 0x38

    def __init__(self, i2c):
        self._i2c = i2c
        time.sleep_ms(40)
        self._i2c.writeto(self.ADDR, bytes([0xBA]))   # soft reset
        time.sleep_ms(20)
        self._i2c.writeto(self.ADDR, bytes([0xBE, 0x08, 0x00]))  # calibrate
        time.sleep_ms(10)
        st = self._i2c.readfrom(self.ADDR, 1)[0]
        self._ok = bool(st & 0x08)
        print("AHT20 status=0x%02X calibrated=%s" % (st, self._ok))

    def read(self):
        """Blocking ~80 ms. Call only between mac.process() calls."""
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


# ---------------------------------------------------------------------------
# Application state
# ---------------------------------------------------------------------------
_uplink_interval_ms = 60_000
_class_c_active     = False
_adr_enabled        = True
_dl_count           = 0
_last_dl_latency_ms = 0
_trigger_uplink     = False
_user_rgb           = None
_stop_flag          = False
_state              = "BOOT"
_ul_total           = 0
_dl_total           = 0
_temp_c100          = 0
_hum_p2             = 0

# Event queue — filled by ev_cb (ISR-scheduled), drained in main loop
_events = []

def _ev_cb(ev):
    _events.append((time.ticks_ms(), ev))


# ---------------------------------------------------------------------------
# OLED render — 128x32, 16 chars x 4 lines
# ---------------------------------------------------------------------------
def _render(oled):
    if oled is None:
        return
    fb = oled.fb
    fb.fill(0)
    flag = "Y" if _class_c_active else "N"
    fb.text("%-9s c:%s" % (_state[:9], flag), 0, 0, 1)
    if _temp_c100 != 0 or _hum_p2 != 0:
        t = _temp_c100 / 100.0
        h = _hum_p2 / 2.0
        fb.text("T%5.1f H%4.1f%%" % (t, h), 0, 8, 1)
    else:
        fb.text("sensor: ----", 0, 8, 1)
    fb.text("UL%4d DL%4d" % (_ul_total, _dl_total), 0, 16, 1)
    if _user_rgb is not None:
        rcode = "%02X%02X%02X" % _user_rgb
    else:
        rcode = "------"
    fb.text("lat%4d %s" % (min(9999, _last_dl_latency_ms), rcode), 0, 24, 1)
    oled.show()


# ---------------------------------------------------------------------------
# Uplink payload encoder (9 bytes, fPort=12)
# ---------------------------------------------------------------------------
def _build_payload(sensor, battery_mv=3300):
    global _temp_c100, _hum_p2
    if sensor is not None:
        temp_c100, hum_p2, sensor_ok = sensor.read()
        if sensor_ok:
            _temp_c100, _hum_p2 = temp_c100, hum_p2
    else:
        temp_c100, hum_p2, sensor_ok = _temp_c100, _hum_p2, False
    flags  = 0
    flags |= 0x01 if _class_c_active else 0
    flags |= 0x02 if _adr_enabled    else 0
    flags |= 0x04 if sensor_ok       else 0
    lat = min(65535, max(0, _last_dl_latency_ms))
    return struct.pack("<hBHBBH",
        temp_c100, hum_p2, battery_mv, flags, _dl_count, lat)


# ---------------------------------------------------------------------------
# Downlink command dispatcher (fPort 22 / 23)
# ---------------------------------------------------------------------------
def _dispatch_downlink(port, data, rgb):
    global _dl_count, _last_dl_latency_ms
    global _trigger_uplink, _uplink_interval_ms, _stop_flag, _user_rgb

    if port == 22:
        cmd = data[0]
        if cmd == 0x01 and len(data) >= 4:   # rgb_set
            r, g, b = data[1], data[2], data[3]
            _user_rgb = (r, g, b)
            rgb.set((r, g, b))
            print("RX port=22 cmd=rgb_set r=%d g=%d b=%d lat=%dms" % (
                r, g, b, _last_dl_latency_ms))
        elif cmd == 0x02:                     # rgb_off
            _user_rgb = None
            rgb.set(RGB_JOINED)
            print("RX port=22 cmd=rgb_off")
        elif cmd == 0x03 and len(data) >= 5:  # blink
            count = data[1]
            r, g, b = data[2], data[3], data[4]
            _user_rgb = (r, g, b)
            rgb.flash((r, g, b), ms=100, times=count)
            rgb.set((r, g, b))
            print("RX port=22 cmd=blink count=%d r=%d g=%d b=%d" % (count, r, g, b))
        elif cmd == 0x04:                     # status_now
            _trigger_uplink = True
            print("RX port=22 cmd=status_now")
    elif port == 23:
        cmd = data[0]
        if cmd == 0x01 and len(data) >= 2:    # set_uplink_interval
            mins = data[1]
            _uplink_interval_ms = mins * 60_000 if mins > 0 else 60_000
            print("RX port=23 cmd=set_interval mins=%d" % mins)
        elif cmd == 0x02 and len(data) >= 2:  # set_rx2_dr
            print("RX port=23 cmd=set_rx2_dr dr=%d (no-op, DR5 fixed)" % data[1])
        elif cmd == 0x03:                     # force_rejoin
            _stop_flag = True
            print("RX port=23 cmd=force_rejoin")


# ---------------------------------------------------------------------------
# Button debounce — returns True once per press
# ---------------------------------------------------------------------------
_btn_last = 1

def _btn_pressed(btn):
    global _btn_last
    v = btn.value()
    if v == 0 and _btn_last == 1:
        _btn_last = 0
        return True
    if v == 1:
        _btn_last = 1
    return False


# ---------------------------------------------------------------------------
# Main — sync polling, no asyncio
# ---------------------------------------------------------------------------
def main():
    global _class_c_active, _adr_enabled, _stop_flag, _user_rgb
    global _dl_count, _last_dl_latency_ms, _state, _ul_total, _dl_total
    global _trigger_uplink, _temp_c100, _hum_p2, _uplink_interval_ms

    gc.collect()

    # WS2812 — boot orange
    rgb = RgbStatus()
    print("boot: VK_RA4M2 Class C demo")

    # Shared I2C bus (AHT20 + OLED)
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

    btn = Pin("P212", Pin.IN, Pin.PULL_UP)

    # Initial sensor read BEFORE MAC stack starts — display has temp from boot
    if sensor is not None:
        try:
            t, h, ok = sensor.read()
            if ok:
                _temp_c100, _hum_p2 = t, h
                print("sensor: %.2fC  %.1f%%" % (t / 100.0, h / 2.0))
        except Exception as e:
            print("sensor init read err:", e)
    _state = "BOOT"
    _render(oled)   # show temp on display before MAC/SPI init

    # SPI bus for SX1262 — must exist before lorawan.Mac()
    spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
              sck=Pin("P111"), mosi=Pin("P109"), miso=Pin("P110"))

    # MAC init ONCE — re-init in the loop tries to claim SPI again -> "SPI bus busy"
    mac = lorawan.Mac(region=lorawan.EU868)
    mac.lorawan_init()
    mac.set_min_rx_symbols(24)
    mac.set_keys(DEVEUI, JOINEUI, APPKEY)
    mac.set_event_callback(_ev_cb)

    while True:
        # Reset per-session state on each (re-)join
        _class_c_active     = False
        _stop_flag          = False
        _user_rgb           = None
        _dl_count           = 0
        _last_dl_latency_ms = 0
        _trigger_uplink     = False
        _events.clear()

        # --- JOIN ---
        _state = "JOIN"
        rgb.set(RGB_JOIN)
        _render(oled)
        print("joining...")
        mac.join()
        deadline = time.ticks_add(time.ticks_ms(), 60_000)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            mac.process()
            if mac.is_joined():
                break
            time.sleep_ms(20)

        if not mac.is_joined():
            print("join timeout — retrying in 10s")
            _state = "ERR"
            rgb.set(RGB_ERROR)
            _render(oled)
            # Keep pumping so MAC timers don't stall before retry
            t_wait = time.ticks_add(time.ticks_ms(), 10_000)
            while time.ticks_diff(t_wait, time.ticks_ms()) > 0:
                mac.process()
                time.sleep_ms(50)
            continue

        print("joined!")
        _state = "CLASS-C"
        rgb.set(RGB_JOINED)

        # --- Class C activation ---
        try:
            st_c = mac.set_class('C')
            print("class set to C (st=%d)" % st_c)
        except AttributeError:
            print("set_class missing in firmware — Class A only")
            st_c = -1
        try:
            st_rx2 = mac.set_rx2(869525000, 5)
            print("RX2 DR5 (st=%d)" % st_rx2)
        except AttributeError:
            print("set_rx2 missing in firmware — RX2 stays at default DR0 (SF12, ~991ms airtime)")
            st_rx2 = -1
        _class_c_active = (st_c == 0)
        _render(oled)

        # First uplink to advertise Class C activation to server
        pkt = _build_payload(sensor)
        print("uplink fPort=12 len=%d" % len(pkt))
        rgb.set(RGB_TX)
        mac.send(12, pkt, False)
        _ul_total += 1
        _dl_count = 0
        # Wait for TX + RX windows
        tx_end = time.ticks_add(time.ticks_ms(), 8_000)
        while time.ticks_diff(tx_end, time.ticks_ms()) > 0:
            mac.process()
            time.sleep_ms(20)
        rgb.set(RGB_JOINED)

        last_uplink_t = time.ticks_ms()
        last_render_t = 0

        # --- Main Class C loop ---
        while not _stop_flag:
            mac.process()

            # Drain event queue (filled by _ev_cb)
            while _events:
                t_ev, ev = _events.pop(0)
                rx = mac.recv()
                if rx is not None:
                    port, payload = rx
                    # Latency = time from last uplink TX to this downlink
                    _last_dl_latency_ms = time.ticks_diff(t_ev, last_uplink_t)
                    _dl_count = min(255, _dl_count + 1)
                    _dl_total += 1
                    print("RX port=%d len=%d lat=%dms" % (
                        port, len(payload), _last_dl_latency_ms))
                    rgb.set(RGB_DOWNLINK)
                    _dispatch_downlink(port, payload, rgb)
                    _render(oled)
                    time.sleep_ms(200)
                    if not _stop_flag:
                        rgb.set(_user_rgb if _user_rgb is not None else RGB_JOINED)

            # Periodic uplink or button / status_now trigger
            due = ((_uplink_interval_ms > 0) and
                   (time.ticks_diff(time.ticks_ms(), last_uplink_t) >= _uplink_interval_ms))
            if due or _btn_pressed(btn) or _trigger_uplink:
                _trigger_uplink = False
                _state = "TX"
                _render(oled)
                pkt = _build_payload(sensor)
                print("uplink fPort=12 len=%d" % len(pkt))
                rgb.set(RGB_TX)
                mac.send(12, pkt, False)
                _ul_total += 1
                _dl_count = 0
                last_uplink_t = time.ticks_ms()
                # Pump through TX + RX1/RX2 windows before returning to loop
                tx_end = time.ticks_add(time.ticks_ms(), 8_000)
                while time.ticks_diff(tx_end, time.ticks_ms()) > 0:
                    mac.process()
                    time.sleep_ms(20)
                _state = "CLASS-C"
                rgb.set(_user_rgb if _user_rgb is not None else RGB_JOINED)
                _render(oled)

            # OLED refresh every 2 s
            if time.ticks_diff(time.ticks_ms(), last_render_t) >= 2_000:
                _render(oled)
                last_render_t = time.ticks_ms()

            time.sleep_ms(50)

        # force_rejoin requested — restore Class A, store NVM, then re-enter outer loop
        mac.set_class('A')
        mac.nvm_store()
        print("force_rejoin — restarting")
        gc.collect()
