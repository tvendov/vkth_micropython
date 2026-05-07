"""SX1262 LoRa packet transmitter for VK_RA4M2 + Wio-SX1262.

Sends a 32-bit big-endian counter every ~1 s on 868.1 MHz, SF7/BW125/CR4-5.
Button P014: short=cycle power, long=TX on/off.
Hard-stops 30 min after first TX; only hardware RESET recovers from STOP.
"""
import time, framebuf, gc, asyncio
from machine import Pin, SPI, SoftI2C

# ---- Hardware ----
spi   = SPI(3, baudrate=8_000_000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
dio1  = Pin('P015', Pin.IN)
rst   = Pin('P001', Pin.OUT, value=0)   # held low at boot; chip_reset releases
rf_en = Pin('P100', Pin.OUT, value=0)   # deferred 2 s into main()
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

# ---- Async SPI helpers ----
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

async def chip_reset():
    rst(0)
    await asyncio.sleep_ms(1)
    rst(1)
    await wait_busy("after reset")

async def radio_init():
    await cmd(0x80, b'\x00')                       # 1  SetStandby(STDBY_RC)
    await cmd(0x96, b'\x00')                       # 2  SetRegulatorMode(LDO)
    await cmd(0x97, b'\x02\x00\x01\x40')           # 3  SetDio3AsTcxoCtrl(1.8V, 320 ticks)
    await asyncio.sleep_ms(10)                     # 4  TCXO stabilisation >=5 ms
    await cmd(0x89, b'\x7F')                       # 5  Calibrate(all blocks)
    await asyncio.sleep_ms(5)                      # 6a calibration settling
    await wait_busy("post calibrate")              # 6b
    await cmd(0x07, b'\x00\x00')                   # 7  ClearDeviceErrors
    await cmd(0x9D, b'\x01')                       # 8  SetDio2AsRfSwitchCtrl
    await cmd(0x8A, b'\x01')                       # 9  SetPacketType(LoRa)
    await cmd(0x86, b'\x36\x41\x99\x9A')           # 10 SetRfFrequency(868.1 MHz)
    await cmd(0x95, b'\x04\x07\x00\x01')           # 11 SetPaConfig(HP PA, 22 dBm, SX1262)
    await cmd(0x8E, bytes([POWERS[pwr_idx] & 0xFF, 0x02]))  # 12 SetTxParams(power, ramp=40us)
    await cmd(0x8B, b'\x07\x04\x01\x00')           # 13 SetModulationParams(SF7, BW125, CR4/5, LDRO=0)
    await cmd(0x8C, b'\x00\x08\x00\x04\x01\x00')   # 14 SetPacketParams(preamble=8, explicit, 4B, CRC, IQ=std)
    await cmd(0x8F, b'\x00\x00')                   # 15 SetBufferBaseAddress(TX=0, RX=0)
    await cmd(0x0D, b'\x07\x40\x34')               # 16 WriteRegister(0x0740, 0x34) sync word MSB
    await cmd(0x0D, b'\x07\x41\x44')               # 17 WriteRegister(0x0741, 0x44) sync word LSB
    await cmd(0x08, b'\x00\x01\x00\x01\x00\x00\x00\x00')  # 18 SetDioIrqParams(TxDone on DIO1)

# ---- State ----
POWERS  = (-1, 5, 10, 14, 17, 20, 22)
pwr_idx = 3             # default +14 dBm

state          = 'IDLE'   # 'IDLE' | 'TX' | 'STOP'
counter        = 0
T_first_tx_ms  = None    # ticks_ms() of first IDLE->TX transition
tx_entry_ms    = None    # ticks_ms() when TX state was entered (uptime display)
last_airtime   = 36      # measured ms; initialised to conservative budget

LONG_MS = 1000

# ---- Render ----
def render(force=False):
    pwr_str = "%+d dBm" % POWERS[pwr_idx]
    oled.fb.fill(0)
    if state == 'IDLE':
        oled.fb.text("IDLE " + pwr_str,    0,  0, 1)
        oled.fb.text("868.1 MHz LoRa",     0, 11, 1)
        oled.fb.text("hold P014 -> TX",    0, 22, 1)
    elif state == 'TX':
        now   = time.ticks_ms()
        el_s  = time.ticks_diff(now, tx_entry_ms) // 1000 if tx_entry_ms else 0
        oled.fb.text("TX   " + pwr_str,                                   0,  0, 1)
        oled.fb.text("pkt #%d" % counter,                                 0, 11, 1)
        oled.fb.text("%dms  %02d:%02d" % (last_airtime, el_s // 60, el_s % 60), 0, 22, 1)
    elif state == 'STOP':
        oled.fb.text("STOP 30min HW",   0,  0, 1)
        oled.fb.text("safety auto-off", 0, 11, 1)
        oled.fb.text("RESET to restart", 0, 22, 1)
    oled.show()

# ---- Per-packet TX ----
async def tx_once():
    global counter, last_airtime

    payload   = counter.to_bytes(4, 'big')
    pkt_start = time.ticks_ms()

    # WriteBuffer — payload into TX FIFO
    await cmd(0x0E, bytes([0x00]) + payload)
    # SetPacketParams — redundant but safe; ensures payload length consistent
    await cmd(0x8C, b'\x00\x08\x00\x04\x01\x00')

    tx_fired = time.ticks_ms()
    # SetTx single-shot, no auto-timeout
    await cmd(0x83, b'\x00\x00\x00')

    # Poll DIO1 for TxDone (max 200 ms)
    t0 = time.ticks_ms()
    timed_out = False
    while not dio1.value():
        if time.ticks_diff(time.ticks_ms(), t0) >= 200:
            print("TX timeout -- missed TxDone on pkt #%d" % counter)
            timed_out = True
            break
        await asyncio.sleep_ms(2)

    if not timed_out:
        last_airtime = time.ticks_diff(time.ticks_ms(), tx_fired)

    await cmd(0x02, b'\xFF\xFF')    # ClearIrqStatus
    await cmd(0x80, b'\x00')        # SetStandby(STDBY_RC)

    counter = (counter + 1) & 0xFFFFFFFF

    elapsed = time.ticks_diff(time.ticks_ms(), pkt_start)
    gap     = max(0, 1000 - elapsed)
    await asyncio.sleep_ms(gap)

# ---- TX task ----
async def tx_task():
    global state, T_first_tx_ms, tx_entry_ms

    while True:
        # Hard-stop: 30 min after first TX
        if T_first_tx_ms is not None and state != 'STOP':
            if time.ticks_diff(time.ticks_ms(), T_first_tx_ms) >= 30 * 60 * 1000:
                await cmd(0x80, b'\x00')
                rf_en(0)
                state = 'STOP'
                render(force=True)

        if state == 'TX':
            await tx_once()
            render(force=True)
            gc.collect()
        elif state == 'IDLE':
            await asyncio.sleep_ms(100)
        elif state == 'STOP':
            await asyncio.sleep_ms(500)

# ---- Button task ----
async def button_task():
    global state, pwr_idx, T_first_tx_ms, tx_entry_ms, counter

    btn_prev = 1
    press_t  = 0
    while True:
        bc  = btn.value()
        now = time.ticks_ms()
        if btn_prev == 1 and bc == 0:
            press_t = now
        elif btn_prev == 0 and bc == 1:
            held = time.ticks_diff(now, press_t)
            await asyncio.sleep_ms(80)   # debounce
            if state != 'STOP':
                if held < LONG_MS:       # short press — cycle power
                    pwr_idx = (pwr_idx + 1) % len(POWERS)
                    if state == 'TX':
                        await radio_init()   # re-init applies new power immediately
                    render(force=True)
                    print("tx_lora_pkt: power -> %+d dBm" % POWERS[pwr_idx])
                else:                    # long press — toggle IDLE<->TX
                    if state == 'IDLE':
                        if T_first_tx_ms is None:
                            T_first_tx_ms = time.ticks_ms()
                        tx_entry_ms = time.ticks_ms()
                        counter     = 0
                        await radio_init()
                        state = 'TX'
                        render(force=True)
                        print("tx_lora_pkt: TX %+d dBm" % POWERS[pwr_idx])
                    elif state == 'TX':
                        await cmd(0x80, b'\x00')   # SetStandby
                        state = 'IDLE'
                        render(force=True)
                        print("tx_lora_pkt: IDLE")
        btn_prev = bc
        await asyncio.sleep_ms(20)

# ---- Boot ----
async def main():
    render(force=True)                  # wipe stale display
    await asyncio.sleep_ms(2000)        # USB CDC enumeration window
    rf_en(1)
    await asyncio.sleep_ms(50)
    await chip_reset()
    print("tx_lora_pkt: init...")
    print("tx_lora_pkt: IDLE %+d dBm" % POWERS[pwr_idx])
    asyncio.create_task(button_task())
    await tx_task()

if __name__ == "__main__":
    asyncio.run(main())
