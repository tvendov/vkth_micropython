# class_a_demo.py — LoRaWAN Class A demo for Grafana pipeline (VK_RA4M2 + Wio-SX1262 COM34)
# Ported 1:1 from ../Example_C/class_c_demo.py — Class A wake/sense/uplink/sleep cycle
# Frame contract: ../provision/codecs/codec_A.js  (UL fPort 10, 8 bytes; DL fPort 20)

import gc
import time
import struct
import framebuf
from machine import Pin, SoftI2C, WS2812
import lorawan

DEVEUI  = bytes.fromhex("70B3D57ED0070001")
JOINEUI = bytes.fromhex("0000000000000000")
APPKEY  = bytes.fromhex("9A7F263557E26259B7061BD6FC8EBA27")

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

# (tag, status) where tag 1 == mcps_indication (DL ready)
_MCPS_IND = 1

_events = []

def _ev_cb(packed, *_):
    _events.append((time.ticks_ms(), packed))


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
    fb.text("UL%4d DL%4d" % (_ul_total, _dl_total), 0, 16, 1)
    fb.text("R%4d S%3d i%3d" % (_last_rx_rssi, _last_rx_snr, _uplink_interval_s),
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
    return struct.pack("<hBHBbb",
        temp_c100, hum_p2, battery_mv, flags, _last_rx_rssi, _last_rx_snr)


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


def main():
    global _adr_enabled, _rejoin_flag, _state, _ul_total, _dl_total
    global _temp_c100, _hum_p2, _last_rx_rssi, _last_rx_snr

    gc.collect()

    rgb = RgbStatus()
    print("boot: VK_RA4M2 Class A demo")

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

    # lorawan.Mac owns and creates SPI(3); pre-opening it here causes "SPI bus busy".
    mac = lorawan.Mac(region=lorawan.EU868)
    mac.set_event_callback(_ev_cb)
    mac.lorawan_init()
    mac.init_defaults()
    mac.set_keys(DEVEUI, JOINEUI, APPKEY)

    while True:
        _rejoin_flag = False
        _events.clear()

        _state = "JOIN"
        rgb.set(RGB_JOIN)
        _render(oled)
        print("joining...")
        try:
            mac.set_datarate(5)
        except AttributeError:
            pass
        status = mac.join(5)

        if status != 0 or not mac.is_joined():
            print("join failed status=%d — retrying in 10s" % status)
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

            # Drain DL events inside the loop (same as C demo pattern)
            while _events:
                t_ev, packed = _events.pop(0)
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

            due = time.ticks_diff(time.ticks_ms(), last_uplink_t) >= _uplink_interval_s * 1000
            if due:
                _state = "TX"
                _render(oled)
                pkt = _build_payload(sensor)
                print("uplink fPort=10 len=%d" % len(pkt))
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
                    # Drain DLs that arrive during the RX windows
                    while _events:
                        t_ev, packed = _events.pop(0)
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

            time.sleep_ms(50)

        mac.nvm_store()
        print("force_rejoin — restarting")
        gc.collect()


if __name__ == "__main__":
    main()
