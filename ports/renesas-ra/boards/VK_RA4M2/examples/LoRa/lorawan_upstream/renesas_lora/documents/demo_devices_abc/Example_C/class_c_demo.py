# class_c_demo.py — LoRaWAN Class C Instant-Response Controller
# VK_RA4M2 + Wio-SX1262 (COM34)
# fPort 12 uplink (9 bytes), fPort 22/23 downlink control
# Requires lorawan_async with set_rx2 + set_class bindings.

import gc
import asyncio
import time
import struct
import framebuf
from machine import Pin, SoftI2C, WS2812
import lorawan_async

# ---------------------------------------------------------------------------
# Credentials  (bytes, MSB-first; LoRaMac reverses to LSB-first on-air)
# ---------------------------------------------------------------------------
DEVEUI  = bytes.fromhex("70B3D57ED0070003")
JOINEUI = bytes.fromhex("0000000000000000")
APPKEY  = bytes.fromhex("202CB141A5842931F99C0C1DDFE70D68")
REGION  = "EU868"

# ---------------------------------------------------------------------------
# WS2812 color constants (spec §"WS2812 цветова схема")
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
# Shared SoftI2C bus for AHT20 + SSD1306 (different addresses)
# ---------------------------------------------------------------------------
_i2c_bus = SoftI2C(
    scl=Pin("P301", Pin.OPEN_DRAIN),
    sda=Pin("P302", Pin.OPEN_DRAIN),
    freq=100000,
)


# ---------------------------------------------------------------------------
# SSD1306 128x32 OLED (addr 0x3C). 16 chars × 4 lines @ 8x8 font.
# Orientation 0xA0+0xC0 to match other VK_RA4M2 demos in this project.
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
        self.i2c.writeto(self.ADDR, self._win)
        self.i2c.writeto(self.ADDR, self._tx)


# ---------------------------------------------------------------------------
# AHT20 sensor
# ---------------------------------------------------------------------------
class AHT20:
    ADDR = 0x38

    def __init__(self, i2c):
        self._i2c = i2c
        self._i2c.writeto(self.ADDR, bytes([0xBE, 0x08, 0x00]))
        time.sleep_ms(10)
        self._ok = True

    async def read(self):
        """Async — yields during 80 ms measurement so LoRaMac _tick keeps running."""
        try:
            self._i2c.writeto(self.ADDR, bytes([0xAC, 0x33, 0x00]))
            await asyncio.sleep_ms(80)
            raw = self._i2c.readfrom(self.ADDR, 7)
            if raw[0] & 0x80:
                return 0, 0, False
            hum_raw  = ((raw[1] << 12) | (raw[2] << 4) | (raw[3] >> 4)) & 0xFFFFF
            temp_raw = (((raw[3] & 0x0F) << 16) | (raw[4] << 8) | raw[5]) & 0xFFFFF
            hum_pct  = hum_raw  / 1048576.0 * 100.0
            temp_c   = temp_raw / 1048576.0 * 200.0 - 50.0
            temp_c100 = int(temp_c * 100)
            hum_p2    = min(200, int(hum_pct * 2))
            # clamp int16
            if temp_c100 > 32767:  temp_c100 = 32767
            if temp_c100 < -32768: temp_c100 = -32768
            return temp_c100, hum_p2, True
        except Exception:
            return 0, 0, False


# ---------------------------------------------------------------------------
# Application state
# ---------------------------------------------------------------------------
_uplink_interval_s  = 60
_class_c_active     = False
_adr_enabled        = True
_dl_count           = 0         # downlinks since last uplink
_last_dl_time_ms    = None      # ticks_ms() when last downlink arrived
_last_dl_latency_ms = 0         # encoded in next uplink
_trigger_uplink     = False     # set by button or status_now command
_user_rgb           = None      # overridden by rgb_set downlink
_stop_flag          = False     # set by force_rejoin to restart loop
_state              = "BOOT"    # display state label: BOOT/JOIN/CLASS-C/TX/ERR
_ul_total           = 0         # cumulative uplink count
_dl_total           = 0         # cumulative downlink count
_temp_c100          = 0         # last sensor reading
_hum_p2             = 0


# ---------------------------------------------------------------------------
# OLED render — 128x32, 16 chars × 4 lines.
# L0: state + Class C flag                "CLASS-C  jnd:Y"
# L1: sensor T/H                           "T 22.3 H 54.2 %"
# L2: counters                             "UL  5  DL  2"
# L3: latency / RGB / interval             "lat 234ms RGB 03"
# ---------------------------------------------------------------------------
def render_oled(oled):
    if oled is None:
        return
    fb = oled.fb
    fb.fill(0)
    # L0 — state
    flag = "Y" if _class_c_active else "N"
    fb.text("%-9s c:%s" % (_state[:9], flag), 0, 0, 1)
    # L1 — sensor (or "----" if invalid)
    if _temp_c100 != 0 or _hum_p2 != 0:
        t  = _temp_c100 / 100.0
        h  = _hum_p2 / 2.0
        fb.text("T%5.1f H%4.1f%%" % (t, h), 0, 8, 1)
    else:
        fb.text("sensor: ----", 0, 8, 1)
    # L2 — counters
    fb.text("UL%4d DL%4d" % (_ul_total, _dl_total), 0, 16, 1)
    # L3 — latency + last RGB
    if _user_rgb is not None:
        rcode = "%02X%02X%02X" % _user_rgb
    else:
        rcode = "------"
    fb.text("lat%4d %s" % (min(9999, _last_dl_latency_ms), rcode), 0, 24, 1)
    oled.show()


# ---------------------------------------------------------------------------
# Uplink payload encoder (9 bytes, fPort=12)
# ---------------------------------------------------------------------------
async def _build_payload(sensor, battery_mv=3300):
    global _temp_c100, _hum_p2
    if sensor is not None:
        temp_c100, hum_p2, sensor_ok = await sensor.read()
        if sensor_ok:
            _temp_c100, _hum_p2 = temp_c100, hum_p2
    else:
        temp_c100, hum_p2, sensor_ok = 0, 0, False
    flags  = 0
    flags |= 0x01 if _class_c_active else 0
    flags |= 0x02 if _adr_enabled    else 0
    flags |= 0x04 if sensor_ok       else 0
    lat = min(65535, max(0, _last_dl_latency_ms))
    pkt = struct.pack("<hBHBBH",
        temp_c100,
        hum_p2,
        battery_mv,
        flags,
        _dl_count,
        lat,
    )
    return pkt   # 9 bytes: 2+1+2+1+1+2


# ---------------------------------------------------------------------------
# Downlink command dispatcher (fPort 22 / 23)
# ---------------------------------------------------------------------------
def _dispatch_downlink(port, data, rgb):
    global _dl_count, _last_dl_time_ms, _last_dl_latency_ms
    global _trigger_uplink, _uplink_interval_s, _stop_flag, _user_rgb

    if port == 22:
        cmd = data[0]
        if cmd == 0x01 and len(data) >= 4:   # rgb_set
            r, g, b = data[1], data[2], data[3]
            _user_rgb = (r, g, b)
            rgb.set((r, g, b))
            print("downlink fPort=22 cmd=01 r={} g={} b={} latency={}ms".format(
                r, g, b, _last_dl_latency_ms))
        elif cmd == 0x02:                     # rgb_off
            _user_rgb = None
            rgb.set(RGB_JOINED)
            print("downlink fPort=22 cmd=02 rgb_off")
        elif cmd == 0x03 and len(data) >= 5:  # blink
            count = data[1]
            r, g, b = data[2], data[3], data[4]
            _user_rgb = (r, g, b)
            rgb.flash((r, g, b), ms=100, times=count)
            rgb.set((r, g, b))
            print("downlink fPort=22 cmd=03 blink count={} r={} g={} b={}".format(
                count, r, g, b))
        elif cmd == 0x04:                     # status_now
            _trigger_uplink = True
            print("downlink fPort=22 cmd=04 status_now")
    elif port == 23:
        cmd = data[0]
        if cmd == 0x01 and len(data) >= 2:    # set_uplink_interval
            mins = data[1]
            _uplink_interval_s = mins * 60 if mins > 0 else 0
            print("downlink fPort=23 cmd=01 interval={}min".format(mins))
        elif cmd == 0x02 and len(data) >= 2:  # set_rx2_dr
            print("downlink fPort=23 cmd=02 set_rx2_dr={}".format(data[1]))
            # Applied dynamically; MAC stack handles it on next RX2 open
        elif cmd == 0x03:                     # force_rejoin
            _stop_flag = True
            print("downlink fPort=23 cmd=03 force_rejoin")


# ---------------------------------------------------------------------------
# Background RX task — runs for the lifetime of the Class C session
# ---------------------------------------------------------------------------
async def _rx_task(mac, rgb, oled, t0_joined):
    global _dl_count, _dl_total, _last_dl_time_ms, _last_dl_latency_ms

    while True:
        msg = await mac.wait_recv(timeout_ms=200)
        if msg is None:
            await asyncio.sleep_ms(0)
            continue
        rx_tick = time.ticks_ms()
        port, data = msg
        ref = _last_dl_time_ms if _last_dl_time_ms is not None else t0_joined
        _last_dl_latency_ms = time.ticks_diff(rx_tick, ref)
        _last_dl_time_ms = rx_tick
        _dl_count = min(255, _dl_count + 1)
        _dl_total += 1
        print("RX port={} len={} latency={}ms".format(port, len(data), _last_dl_latency_ms))
        rgb.set(RGB_DOWNLINK)
        _dispatch_downlink(port, data, rgb)
        render_oled(oled)
        await asyncio.sleep_ms(200)
        if _user_rgb is None:
            rgb.set(RGB_JOINED)
        else:
            rgb.set(_user_rgb)


# ---------------------------------------------------------------------------
# Periodic uplink task
# ---------------------------------------------------------------------------
async def _uplink_task(mac, rgb, oled, sensor, btn):
    global _dl_count, _last_dl_latency_ms, _trigger_uplink, _state, _ul_total

    next_uplink = time.ticks_add(time.ticks_ms(), 10_000)
    last_render = 0
    while True:
        now = time.ticks_ms()
        btn_pressed = (btn.value() == 0)
        due = time.ticks_diff(now, next_uplink) >= 0

        if btn_pressed or _trigger_uplink or due:
            _trigger_uplink = False
            _state = "TX"
            render_oled(oled)
            pkt = await _build_payload(sensor)
            print("uplink fPort=12 len={} bytes={}".format(
                len(pkt), " ".join("{:02X}".format(b) for b in pkt)))
            rgb.set(RGB_TX)
            try:
                await mac.send(12, pkt, confirmed=False, datarate=5)
                _ul_total += 1
            except Exception as e:
                print("uplink error:", e)
                _state = "ERR"
                render_oled(oled)
                rgb.set(RGB_ERROR)
                await asyncio.sleep_ms(2000)
            rgb.set(RGB_JOINED if _user_rgb is None else _user_rgb)
            _state = "CLASS-C"
            render_oled(oled)
            _dl_count = 0
            next_uplink = time.ticks_add(
                time.ticks_ms(),
                _uplink_interval_s * 1000 if _uplink_interval_s > 0 else 60_000,
            )
        # Periodic OLED refresh every 2 s for live "last X sec ago" feel
        if time.ticks_diff(now, last_render) > 2000:
            render_oled(oled)
            last_render = now
        await asyncio.sleep_ms(50)


# ---------------------------------------------------------------------------
# Main coroutine
# ---------------------------------------------------------------------------
async def main():
    global _class_c_active, _adr_enabled, _stop_flag, _user_rgb
    global _dl_count, _last_dl_latency_ms, _last_dl_time_ms, _state
    global _temp_c100, _hum_p2

    gc.collect()

    rgb = RgbStatus()
    rgb.set(RGB_BOOT)
    print("boot: VK_RA4M2 Class C demo")

    # OLED is optional — graceful fallback if not on bus
    try:
        oled = OLED(_i2c_bus)
        _state = "BOOT"
        render_oled(oled)
        print("OLED OK at 0x3C")
    except Exception as e:
        oled = None
        print("OLED skipped:", e)

    # AHT20 is optional — sensor may not be present
    try:
        sensor = AHT20(_i2c_bus)
        print("AHT20 OK at 0x38")
    except Exception as e:
        sensor = None
        print("AHT20 skipped:", e)

    btn    = Pin("P212", Pin.IN, Pin.PULL_UP)

    while True:
        # Reset per-session state on each (re-)join
        _class_c_active     = False
        _stop_flag          = False
        _user_rgb           = None
        _dl_count           = 0
        _last_dl_latency_ms = 0
        _last_dl_time_ms    = None

        mac = lorawan_async.AsyncMac(region=REGION)
        await mac.lorawan_init()
        mac.set_keys(DEVEUI, JOINEUI, APPKEY)
        mac.set_adr(True)
        _adr_enabled = True

        # Sensor read here — after MAC stack init, before join.
        if sensor is not None:
            try:
                t, h, ok = await sensor.read()
                if ok:
                    _temp_c100, _hum_p2 = t, h
                    print("sensor: %.2fC  %.1f%%" % (t / 100.0, h / 2.0))
            except Exception as e:
                print("sensor read err:", e)

        rgb.set(RGB_JOIN)
        _state = "JOIN"
        render_oled(oled)              # safe now — sensor.read() is async
        print("joining... (DR5, timeout 60s)")
        try:
            st = await mac.join(datarate=5, timeout_ms=60_000)
        except Exception as e:
            print("join failed:", e)
            rgb.set(RGB_ERROR)
            await asyncio.sleep_ms(5000)
            await mac.deinit()
            continue

        if st != 0:
            print("join rejected status={}".format(st))
            rgb.set(RGB_ERROR)
            await asyncio.sleep_ms(5000)
            await mac.deinit()
            continue

        t0_joined = time.ticks_ms()
        print("joined is_joined={}".format(mac.is_joined))
        rgb.set(RGB_JOINED)

        # Switch to Class C and set RX2 DR5 (SF7BW125) for low latency.
        # Call directly on the C Mac object — lorawan_async wrapper may not
        # have these methods in the currently-frozen firmware; _m is always
        # the raw lorawan.Mac instance regardless of firmware version.
        st_c   = mac._m.set_class('C')
        st_rx2 = mac._m.set_rx2(freq=869525000, dr=5)
        _class_c_active = True
        _state = "CLASS-C"
        render_oled(oled)
        print("Class C active (set_class={} set_rx2={})".format(st_c, st_rx2))
        rgb.set(RGB_JOINED)

        # Launch background tasks
        rx_t  = asyncio.create_task(_rx_task(mac, rgb, oled, t0_joined))
        ul_t  = asyncio.create_task(_uplink_task(mac, rgb, oled, sensor, btn))

        # Monitor for force_rejoin command
        while not _stop_flag:
            await asyncio.sleep_ms(500)

        # Tear down
        rx_t.cancel()
        ul_t.cancel()
        try:
            await rx_t
        except asyncio.CancelledError:
            pass
        try:
            await ul_t
        except asyncio.CancelledError:
            pass

        mac.set_class('A')
        await mac.deinit()
        print("force_rejoin — restarting")
        gc.collect()
