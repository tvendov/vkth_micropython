"""SX1262 EU868 RSSI channel scanner (async) for VK_RA4M2 + Wio-SX1262.

Sweeps 8 EU868 channels in continuous-RX mode, samples RSSI, renders to
SSD1306 128x32 OLED. Fully asyncio — every wait yields to the scheduler so
USB CDC and other tasks always get serviced.
"""
import time, framebuf, gc, asyncio
from machine import Pin, SPI, SoftI2C

# ---- Hardware ----
spi   = SPI(3, baudrate=8000000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
dio1  = Pin('P015', Pin.IN)
rst   = Pin('P001', Pin.OUT, value=0)   # hold chip in reset until main()
rf_en = Pin('P100', Pin.OUT, value=0)   # deferred — see main(), avoid USB inrush
btn   = Pin('P014', Pin.IN, Pin.PULL_UP)

OLED_W, OLED_H, OLED_ADDR = 128, 32, 0x3C
i2c  = SoftI2C(scl=Pin('P301', Pin.IN), sda=Pin('P302', Pin.IN), freq=400_000)

class SSD1306:
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c, self.addr = i2c, addr
        self.pages = OLED_H // 8
        self._tx = bytearray(1 + OLED_W * self.pages)
        self._tx[0] = 0x40
        self.fb = framebuf.FrameBuffer(memoryview(self._tx)[1:],
                                        OLED_W, OLED_H, framebuf.MONO_VLSB)
        cmd = bytearray(2); cmd[0] = 0x00
        for c in (0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
                  0x8D, 0x14, 0x20, 0x00, 0xA0, 0xC0, 0xDA, 0x02,
                  0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF):
            cmd[1] = c; self.i2c.writeto(self.addr, cmd)
        self._show = bytearray(b"\x00\x21\x00\x7F\x22\x00"
                                + bytes([self.pages - 1]))
        self.fb.fill(0)
    def show(self):
        self.i2c.writeto(self.addr, self._show)
        self.i2c.writeto(self.addr, self._tx)

oled = SSD1306(i2c)

CHANNELS = [
    ("7.1", 867_100_000, bytes([0x36, 0x31, 0x99, 0x9A])),
    ("7.3", 867_300_000, bytes([0x36, 0x34, 0xCC, 0xCD])),
    ("7.5", 867_500_000, bytes([0x36, 0x38, 0x00, 0x00])),
    ("7.7", 867_700_000, bytes([0x36, 0x3B, 0x33, 0x33])),
    ("7.9", 867_900_000, bytes([0x36, 0x3E, 0x66, 0x66])),
    ("8.1", 868_100_000, bytes([0x36, 0x41, 0x99, 0x9A])),
    ("8.3", 868_300_000, bytes([0x36, 0x44, 0xCC, 0xCD])),
    ("8.5", 868_500_000, bytes([0x36, 0x48, 0x00, 0x00])),
]
WF_CHAN = 5

# ---- Async SPI helpers (every wait yields) ----
_BUSY_TIMEOUT_MS = 200

async def wait_busy(tag=""):
    t0 = time.ticks_ms()
    while busy.value():
        if time.ticks_diff(time.ticks_ms(), t0) >= _BUSY_TIMEOUT_MS:
            print("BUSY timeout", tag)
            return False
        await asyncio.sleep_ms(1)
    return True

async def cmd(op, payload=b''):
    if not await wait_busy("pre 0x%02X" % op):
        return
    nss(0)
    spi.write(bytes([op]) + payload)
    nss(1)
    await wait_busy("post 0x%02X" % op)

async def read_rssi_inst():
    if not await wait_busy("rssi pre"):
        return -127
    buf = bytearray(3)
    nss(0)
    spi.write_readinto(bytes([0x15, 0x00, 0x00]), buf)
    nss(1)
    await wait_busy("rssi post")
    return -buf[2] // 2

async def chip_reset():
    rst(0)
    await asyncio.sleep_ms(1)
    rst(1)
    await wait_busy("after reset")

async def write_register(addr, value):
    # WriteRegister opcode 0x0D, 16-bit addr MSB-first + 1 data byte
    await cmd(0x0D, bytes([(addr >> 8) & 0xFF, addr & 0xFF, value]))

async def apply_rx_gain():
    # SX1262 Reg_RxGain @ 0x08AC: 0x96 = Boosted (+3dB sens), 0x94 = Standard.
    # Persists across STDBY but resets on Sleep — re-apply after wake.
    await write_register(0x08AC, 0x96 if rx_boosted else 0x94)

async def radio_init():
    await cmd(0x80, b'\x00')                # SetStandby(STDBY_RC)
    await cmd(0x96, b'\x00')                # SetRegulatorMode(LDO)
    await cmd(0x97, b'\x02\x00\x01\x40')    # SetDio3AsTcxoCtrl(1.8V, 320 ticks)
    await asyncio.sleep_ms(10)
    await cmd(0x89, b'\x7F')                # Calibrate(all blocks)
    await asyncio.sleep_ms(5)
    await wait_busy("post calibrate")
    await cmd(0x07, b'\x00\x00')            # ClearDeviceErrors
    await cmd(0x9D, b'\x01')                # SetDio2AsRfSwitchCtrl
    await cmd(0x8A, b'\x01')                # SetPacketType(LoRa)
    await cmd(0x8B, b'\x07\x04\x01\x00')    # SetModulationParams SF7/BW125/CR4_5
    await cmd(0x8C, b'\x00\x08\x00\xFF\x01\x00')  # SetPacketParams
    await cmd(0x8F, b'\x80\x00')            # SetBufferBaseAddress
    await apply_rx_gain()

# ---- State ----
rssi       = [-120] * 8
wf_buf     = [31] * 128
mode       = 0           # 0=BAR 1=NUM 2=WF
paused     = False
rx_boosted = False       # toggled by short-press; re-applied via apply_rx_gain()

app_mode         = 'SCAN'  # 'SCAN' | 'PKT'
pkt_counter_last = None    # int or None
pkt_miss_count   = 0
pkt_last_delta   = 1
pkt_last_rx_ms   = 0
pkt_last_rssi    = -120
pkt_last_snr     = 0

LONG_MS = 1000

async def sweep_once():
    for idx, (_lbl, _hz, fword) in enumerate(CHANNELS):
        await cmd(0x86, fword)              # SetRfFrequency
        await cmd(0x82, b'\xFF\xFF\xFF')    # SetRx continuous
        await asyncio.sleep_ms(16)          # AGC + LNA settle after channel hop
        samples = []
        for _ in range(5):
            samples.append(await read_rssi_inst())
            await asyncio.sleep_ms(2)       # small spacing between samples
        samples.sort()
        rssi[idx] = samples[2]              # median of 5 — rejects transient spurs
        await cmd(0x80, b'\x00')            # SetStandby
        await asyncio.sleep_ms(5)           # rest before next freq hop
        await render()                      # live per-channel update

# ---- Render ----
_RSSI_FLOOR = -120   # bottom of bar scale
_RSSI_CEIL  = -20    # top of bar scale — covers RX saturation plateau
_RSSI_SPAN  = _RSSI_CEIL - _RSSI_FLOOR

def render_bar():
    rmax = max(rssi)
    peak_idx = rssi.index(rmax)
    label = CHANNELS[peak_idx][0]      # e.g. "8.1"
    if paused:
        prefix = "PAU"
    elif rx_boosted:
        prefix = "BST"                 # LNA boosted (+3 dB sens)
    else:
        prefix = "STD"                 # standard gain
    hdr = "%s %s %ddBm" % (prefix, label, rmax)
    oled.fb.text(hdr[:16], 0, 0, 1)
    for idx in range(8):
        h = int((rssi[idx] - _RSSI_FLOOR) * 23 // _RSSI_SPAN)
        h = max(0, min(23, h))
        if h > 0:
            oled.fb.fill_rect(idx * 16, 8 + (23 - h), 14, h, 1)

def _bar_h(r):    # used by render_wf for waterfall y mapping
    return max(0, min(23, int((r + 120) * 23 // 70)))

def render_num():
    hdr = "NUM  top-4 dBm  "
    if paused: hdr = hdr[:13] + " [P]"
    oled.fb.text(hdr[:16], 0, 0, 1)
    ranked = sorted(range(8), key=lambda i: rssi[i], reverse=True)[:4]
    for cell, idx in enumerate(ranked):
        x = (cell % 2) * 64
        y = 8 + (cell // 2) * 16
        oled.fb.text(CHANNELS[idx][0], x, y, 1)
        oled.fb.text("%-4d" % rssi[idx], x, y + 8, 1)

def render_wf():
    r5 = rssi[WF_CHAN]
    hdr = "WF 868.1 %4ddBm" % r5
    if paused: hdr = hdr[:13] + " [P]"
    oled.fb.text(hdr[:16], 0, 0, 1)
    if not paused:
        wf_buf[:-1] = wf_buf[1:]
        wf_buf[127]  = 31 - _bar_h(r5)
    for x in range(128):
        oled.fb.pixel(x, wf_buf[x], 1)

def render_pkt():
    if pkt_counter_last is None:
        oled.fb.text("PKT 868.1 MHz", 0,  0, 1)
        oled.fb.text("awaiting...",   0, 11, 1)
    else:
        now_ms  = time.ticks_ms()
        age_ms  = time.ticks_diff(now_ms, pkt_last_rx_ms)
        hdr     = "PKT #%d %ddBm" % (pkt_counter_last, pkt_last_rssi)
        seq_ln  = "seq%d miss%d" % (pkt_last_delta, pkt_miss_count)
        age_ln  = "last%dms SNR%d" % (age_ms, pkt_last_snr)
        oled.fb.text(hdr[:16],    0,  0, 1)
        oled.fb.text(seq_ln[:16], 0, 11, 1)
        oled.fb.text(age_ln[:16], 0, 22, 1)

async def render():
    oled.fb.fill(0)
    if app_mode == 'PKT':
        render_pkt()
    elif mode == 0: render_bar()
    elif mode == 1: render_num()
    else:           render_wf()
    oled.show()
    await asyncio.sleep_ms(0)

# ---- PKT-mode helpers ----
async def enter_pkt_mode():
    global app_mode, pkt_counter_last, pkt_miss_count, pkt_last_delta
    await cmd(0x80, b'\x00')                         # SetStandby(STDBY_RC)
    await cmd(0x86, b'\x36\x41\x99\x9A')             # SetRfFrequency 868.1 MHz
    await cmd(0x8B, b'\x07\x04\x01\x00')             # SetModulationParams SF7/BW125/CR4_5/LDRO=0
    await cmd(0x8C, b'\x00\x08\x00\xFF\x01\x00')     # SetPacketParams preamble=8, explicit, any len, CRC, std IQ
    await cmd(0x0D, b'\x07\x40\x34')                 # WriteRegister(0x0740, 0x34) sync word MSB
    await cmd(0x0D, b'\x07\x41\x44')                 # WriteRegister(0x0741, 0x44) sync word LSB
    await cmd(0x02, b'\xFF\xFF')                      # ClearIrqStatus
    # IRQ bits per SX1262 DS Table 13-29: bit1=RxDone, bit6=CrcErr → 0x0042
    await cmd(0x08, b'\x00\x42\x00\x42\x00\x00\xFF\xFF')  # SetDioIrqParams RxDone|CrcErr on DIO1
    await cmd(0x82, b'\xFF\xFF\xFF')                  # SetRx continuous
    await apply_rx_gain()
    pkt_counter_last = None
    pkt_miss_count   = 0
    pkt_last_delta   = 1
    app_mode = 'PKT'   # flip after all SPI ops — sweep_task gates on this
    await render()

async def enter_scan_mode():
    global app_mode
    await cmd(0x80, b'\x00')   # SetStandby — stops continuous RX
    app_mode = 'SCAN'
    await render()

async def pkt_rx_once():
    global pkt_counter_last, pkt_miss_count, pkt_last_delta
    global pkt_last_rx_ms, pkt_last_rssi, pkt_last_snr

    # GetIrqStatus: opcode + NOP, response: [status, irq_H, irq_L]
    buf = bytearray(4)
    nss(0); spi.write_readinto(b'\x12\x00\x00\x00', buf); nss(1)
    await wait_busy("GetIrqStatus")
    irq = (buf[2] << 8) | buf[3]

    # Clear IRQ before ReadBuffer so a back-to-back packet re-asserts DIO1 correctly
    await cmd(0x02, b'\xFF\xFF')

    rx_done = bool(irq & 0x0002)  # bit1 = RxDone per SX1262 DS Table 13-29
    crc_err = bool(irq & 0x0040)  # bit6 = CrcErr

    # GetRxBufferStatus: [status, payloadLen, rxStartAddr]
    buf3 = bytearray(4)
    nss(0); spi.write_readinto(b'\x13\x00\x00\x00', buf3); nss(1)
    await wait_busy("GetRxBufferStatus")
    payload_len   = buf3[2]
    rx_start_addr = buf3[3]

    # GetPacketStatus: [status, rssiPkt, snrPkt, signalRssiPkt]
    buf4 = bytearray(5)
    nss(0); spi.write_readinto(b'\x14\x00\x00\x00\x00', buf4); nss(1)
    await wait_busy("GetPacketStatus")
    pkt_last_rssi = -(buf4[2] >> 1)
    snr_raw       = buf4[3] if buf4[3] < 128 else buf4[3] - 256  # signed int8, units 0.25 dB
    pkt_last_snr  = snr_raw >> 2

    if not rx_done or crc_err or payload_len < 4:
        print("PKT: irq=0x%04X rx_done=%s crc_err=%s len=%d rssi=%d — skip" % (
              irq, rx_done, crc_err, payload_len, pkt_last_rssi))
        return

    # ReadBuffer: write [0x1E, offset], read 1 status byte + N data bytes
    rbuf = bytearray(1 + payload_len)
    nss(0)
    spi.write(bytes([0x1E, rx_start_addr]))
    spi.readinto(rbuf)
    nss(1)
    await wait_busy("ReadBuffer")
    # rbuf[0] = status NOP; payload begins at rbuf[1]
    payload = rbuf[1:1 + payload_len]
    counter = int.from_bytes(payload[:4], 'big')

    if pkt_counter_last is None:
        delta = 1
    else:
        delta = int((counter - pkt_counter_last) & 0xFFFFFFFF)
        if delta == 0:
            delta = 1
        pkt_miss_count += max(0, delta - 1)

    pkt_last_delta   = delta
    pkt_counter_last = counter
    pkt_last_rx_ms   = time.ticks_ms()
    print("PKT #%d rssi=%d snr=%d delta=%d miss=%d" % (
          counter, pkt_last_rssi, pkt_last_snr, delta, pkt_miss_count))

async def pkt_task():
    while True:
        if app_mode != 'PKT':
            await asyncio.sleep_ms(50)
            continue
        if dio1.value():
            await pkt_rx_once()
            await render()
        else:
            await asyncio.sleep_ms(5)

# ---- Button task ----
# Short press: SCAN=toggle BST/STD gain; PKT=reset counters.
# Long press: toggle SCAN<->PKT mode.
async def button_task():
    global paused, rx_boosted, pkt_counter_last, pkt_miss_count, pkt_last_delta
    btn_prev = 1
    press_t  = 0
    while True:
        bc = btn.value()
        now = time.ticks_ms()
        if btn_prev == 1 and bc == 0:
            press_t = now
        elif btn_prev == 0 and bc == 1:
            held = time.ticks_diff(now, press_t)
            await asyncio.sleep_ms(80)
            if held >= LONG_MS:
                if app_mode == 'SCAN':
                    await enter_pkt_mode()
                else:
                    await enter_scan_mode()
            else:
                if app_mode == 'SCAN':
                    rx_boosted = not rx_boosted
                    await apply_rx_gain()
                    print("rx_boosted =", rx_boosted)
                else:
                    pkt_counter_last = None
                    pkt_miss_count   = 0
                    pkt_last_delta   = 1
                    print("PKT: counters reset")
                await render()
        btn_prev = bc
        await asyncio.sleep_ms(20)

# ---- Sweep task ----
async def sweep_task():
    while True:
        if app_mode == 'PKT':
            await asyncio.sleep_ms(100)   # idle — pkt_task owns the chip
            continue
        if not paused:
            await sweep_once()
            print("RSSI:", " ".join("%s=%d" % (CHANNELS[i][0], rssi[i])
                                    for i in range(8)))
            gc.collect()
            await asyncio.sleep_ms(50)
        else:
            await asyncio.sleep_ms(200)

# ---- Boot ----
async def main():
    await render()                 # wipe stale display from previous boot
    await asyncio.sleep_ms(2000)   # USB CDC enumerates first
    rf_en(1)
    await asyncio.sleep_ms(50)
    print("rssi_scanner: init...")
    await chip_reset()
    await radio_init()
    await enter_pkt_mode()         # start in PKT mode (long-press to switch to SCAN)
    print("rssi_scanner: running PKT mode")
    asyncio.create_task(button_task())
    asyncio.create_task(pkt_task())
    await sweep_task()


# Run only when launched directly (Thonny F5 sets __name__ == '__main__').
# main.py imports this module and explicitly calls asyncio.run(main()).
if __name__ == "__main__":
    asyncio.run(main())
