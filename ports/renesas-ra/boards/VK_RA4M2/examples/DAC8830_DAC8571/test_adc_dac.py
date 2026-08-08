# dac_adc_sandy.py
# DAC-ADC voltmeter с OLED дисплей (SSD1306 128x32).
# Pins: P015=internal DAC, P001=ADC, P100=SCL, P206=SDA,
#       P109=SPI_OUT/SDI, P111=SPI_CLK, P002=SPI_CS, P013=dummy MISO,
#       P302/P301=encoder A/B, P110=mode button.

from machine import DAC, ADC, Pin, SoftI2C, SoftSPI, Timer, Encoder
import framebuf
import time
import sys
import select

ADC_VREF = 3.284
DAC_VREF = 2.048
BITS = 12
FULL = 1 << BITS
DAC16_FULL = 1 << 16

# ---- Startup configuration ----
# DAC_MODE: "external" = DAC8571 + DAC8830, "internal" = P015 DAC.
DAC_MODE = "external"
START_DAC_VALUE = 32768       # 16-bit value, 0..65535.

INTERNAL_DAC_PIN = "P015"
ADC_PIN = "P001"

I2C_SCL_PIN = "P100"
I2C_SDA_PIN = "P206"
I2C_FREQ = 400000
DAC8571_ADDR = 0x4C

SPI_OUT_PIN = "P109"
SPI_CLK_PIN = "P111"
SPI_CS_PIN = "P002"
SPI_BAUDRATE = 1000000
SPI_MISO_PIN = "P013"        # Dummy input; DAC8830 is write-only.

ENCODER_A_PIN = "P302"
ENCODER_B_PIN = "P301"
ENCODER_FILTER = Encoder.FILTER_64
ENCODER_MODE = Encoder.X1

BUTTON_PIN = "P110"
BUTTON_POLL_MS = 20
BUTTON_DEBOUNCE_TICKS = 3

STEP_SLOW = 1
STEP_FAST = 100
ENCODER_POLL_MS = 5
DISPLAY_UPDATE_MS = 40
ADC_UPDATE_MS = 100
ADC_SAMPLES = 32


def clip_u16(value):
    value = int(value)
    if value < 0:
        return 0
    if value > 0xFFFF:
        return 0xFFFF
    return value


def code16_from_volts(volts):
    return clip_u16(int(float(volts) * DAC16_FULL / DAC_VREF + 0.5))


def adc_code_from_volts(volts):
    value = int(float(volts) * FULL / ADC_VREF + 0.5)
    if value < 0:
        return 0
    if value > FULL - 1:
        return FULL - 1
    return value


def volts_from_u16(value):
    return clip_u16(value) * DAC_VREF / DAC16_FULL


def parse_repl_line(line):
    line = line.strip()
    if not line:
        return None

    is_voltage = line[-1] in "Vv" or "." in line
    if is_voltage:
        if line[-1] in "Vv":
            line = line[:-1]
        volts = float(line)
        if 0.0 <= volts <= DAC_VREF:
            return code16_from_volts(volts)
        print("\n[ERR] volts 0..{:.3f}V".format(DAC_VREF))
        return None

    code = int(line, 10)
    if 0 <= code <= 0xFFFF:
        return code
    print("\n[ERR] code 0..65535")
    return None


class DAC8571:
    CONTROL_UPDATE = 0x10

    def __init__(self, i2c, addr=DAC8571_ADDR):
        self.i2c = i2c
        self.addr = addr
        self._buf = bytearray(3)

    def write_u16(self, value):
        value = clip_u16(value)
        self._buf[0] = self.CONTROL_UPDATE
        self._buf[1] = value >> 8
        self._buf[2] = value & 0xFF
        self.i2c.writeto(self.addr, self._buf)


class DAC8830:
    def __init__(self, sdi_pin=SPI_OUT_PIN, sclk_pin=SPI_CLK_PIN,
                 cs_pin=SPI_CS_PIN, baudrate=SPI_BAUDRATE,
                 miso_pin=SPI_MISO_PIN):
        self.cs = Pin(cs_pin, Pin.OUT)
        self.cs.value(1)
        kwargs = {
            "baudrate": baudrate,
            "polarity": 0,
            "phase": 0,
            "sck": Pin(sclk_pin),
            "mosi": Pin(sdi_pin),
        }
        if miso_pin is not None:
            kwargs["miso"] = Pin(miso_pin)
        self.spi = SoftSPI(**kwargs)
        self._buf = bytearray(2)

    def write_u16(self, value):
        value = clip_u16(value)
        self._buf[0] = value >> 8
        self._buf[1] = value & 0xFF
        self.cs.value(0)
        try:
            self.spi.write(self._buf)
        finally:
            self.cs.value(1)

adc = ADC(Pin(ADC_PIN), bits=12)
encoder = Encoder(pin_a=Pin(ENCODER_A_PIN), pin_b=Pin(ENCODER_B_PIN),
                  mode=ENCODER_MODE, filter=ENCODER_FILTER, value=0,
                  debounce=0)

button = Pin(BUTTON_PIN, Pin.IN, Pin.PULL_UP)
fast_mode = False
mode_changed = True
_button_raw = button.value()
_button_stable = _button_raw
_button_ticks = 0


def step_size():
    return STEP_FAST if fast_mode else STEP_SLOW


def mode_name():
    return "FAST" if fast_mode else "SLOW"


def button_poll(_timer):
    global _button_raw, _button_stable, _button_ticks
    global fast_mode, mode_changed

    raw = button.value()
    if raw == _button_raw:
        if _button_ticks < BUTTON_DEBOUNCE_TICKS:
            _button_ticks += 1
            return
        if raw != _button_stable:
            _button_stable = raw
            if raw == 0:
                fast_mode = not fast_mode
                mode_changed = True
    else:
        _button_raw = raw
        _button_ticks = 0


button_timer = Timer(-1)
button_timer.init(period=BUTTON_POLL_MS, mode=Timer.PERIODIC,
                  callback=button_poll)


# --- OLED (SSD1306 128x32) ---
OLED_W    = 128
OLED_H    = 32
OLED_ADDR = 0x3C

class SSD1306:
    _INIT_CMDS = (
        0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    )
    def __init__(self, i2c, addr=OLED_ADDR):
        self.i2c = i2c
        self.addr = addr
        self._tx_buf = bytearray(1 + OLED_W * OLED_H // 8)
        self._tx_buf[0] = 0x40
        self.fb = framebuf.FrameBuffer(memoryview(self._tx_buf)[1:],
                                       OLED_W, OLED_H, framebuf.MONO_VLSB)
        self._cmd1 = bytearray(2)
        self._show_cmd = bytearray(b"\x00\x21\x00\x7F\x22\x00\x03")
        for c in self._INIT_CMDS:
            self._cmd1[1] = c
            self.i2c.writeto(self.addr, self._cmd1)
        self.fb.fill(0)
        self.show()
    def show(self):
        self.i2c.writeto(self.addr, self._show_cmd)
        self.i2c.writeto(self.addr, self._tx_buf)


i2c = SoftI2C(scl=Pin(I2C_SCL_PIN, Pin.OPEN_DRAIN),
              sda=Pin(I2C_SDA_PIN, Pin.OPEN_DRAIN),
              freq=I2C_FREQ)
oled = None
devs = i2c.scan()
print("I2C scan:", devs)
if 0x3C in devs:
    oled = SSD1306(i2c, 0x3C)
elif 0x3D in devs:
    oled = SSD1306(i2c, 0x3D)

mode = DAC_MODE.lower()
if mode == "internal":
    internal_dac = DAC(Pin(INTERNAL_DAC_PIN))
    dac8571 = None
    dac8830 = None
elif mode == "external":
    internal_dac = None
    dac8571 = DAC8571(i2c)
    dac8830 = DAC8830()
else:
    raise ValueError("DAC_MODE must be 'internal' or 'external'")


def write_dac(value16):
    value16 = clip_u16(value16)
    if mode == "internal":
        internal_dac.write(value16 >> 4)
    else:
        dac8571.write_u16(value16)
        dac8830.write_u16(value16)
    return value16


dac_value = write_dac(START_DAC_VALUE)
print("DAC mode:", mode, "16-bit input")
print("SoftI2C: SCL={}, SDA={}".format(I2C_SCL_PIN, I2C_SDA_PIN))
print("Encoder: A={}, B={}, button={} poll={}ms".format(
    ENCODER_A_PIN, ENCODER_B_PIN, BUTTON_PIN, BUTTON_POLL_MS))


# --- Non-blocking REPL input ---
_poll = select.poll()
_poll.register(sys.stdin, select.POLLIN)
_in_buf = ""

def poll_repl():
    # Returns a 16-bit DAC code or None.
    global _in_buf
    new_code = None
    while _poll.poll(0):
        ch = sys.stdin.read(1)
        if ch == "\n" or ch == "\r":
            line = _in_buf
            _in_buf = ""
            if line:
                try:
                    new_code = parse_repl_line(line)
                except ValueError:
                    print("\n[ERR] use 1.5V or 0..65535")
        elif ch == "\x03":
            raise KeyboardInterrupt
        elif ch == "\x08" or ch == "\x7f":
            _in_buf = _in_buf[:-1]
        else:
            _in_buf += ch
    return new_code


def read_adc_avg():
    total = 0
    for _ in range(ADC_SAMPLES):
        total += adc.read()
    return total // ADC_SAMPLES


print("\nDAC-ADC Voltmeter (live + OLED)")
print("Type volts 0..{:.3f}V (e.g. 1.5V) or code 0..65535 + Enter\n".format(DAC_VREF))
print("[MODE {} step {}]".format(mode_name(), step_size()))
mode_changed = False

raw = 0
volts_adc = 0.0
err = 0
display_dirty = True
now = time.ticks_ms()
next_adc_tick = now
next_display_tick = now

while True:
    nv = poll_repl()
    if nv is not None:
        dac_value = write_dac(nv)
        print("\n[DAC16 <- {} / 0x{:04X}]".format(dac_value, dac_value))
        display_dirty = True

    delta = encoder.value()
    if delta:
        encoder.value(0)
        current_step = step_size()
        dac_value = write_dac(dac_value + delta * current_step)
        display_dirty = True

    if mode_changed:
        mode_changed = False
        print("\n[MODE {} step {}]".format(mode_name(), step_size()))
        display_dirty = True

    now = time.ticks_ms()
    if time.ticks_diff(now, next_adc_tick) >= 0:
        raw = read_adc_avg()
        next_adc_tick = time.ticks_add(now, ADC_UPDATE_MS)
        display_dirty = True

    volts_dac = volts_from_u16(dac_value)
    if display_dirty and time.ticks_diff(now, next_display_tick) >= 0:
        volts_adc = raw * ADC_VREF / FULL
        expected_adc = adc_code_from_volts(volts_dac)
        err = raw - expected_adc

        # ---- REPL single-line overwrite ----
        print(
            "\r{} x{:<3d} | DAC:{:5d} 0x{:04X} ({:.3f}V) | ADC:{:4d} ({:.3f}V) | ERR:{:+5d} LSB   "
            .format(mode_name(), step_size(), dac_value, dac_value,
                    volts_dac, raw, volts_adc, err),
            end=""
        )

        # ---- OLED ----
        if oled is not None:
            oled.fb.fill(0)
            oled.fb.text("{} x{}".format(mode_name(), step_size()), 0, 0, 1)
            oled.fb.text("D {:5d} {:1.3f}V".format(dac_value, volts_dac), 0, 8, 1)
            oled.fb.text("A {:4d} {:1.3f}V".format(raw, volts_adc), 0, 16, 1)
            oled.fb.text("ERR {:+5d}".format(err), 0, 24, 1)
            oled.show()

        display_dirty = False
        next_display_tick = time.ticks_add(now, DISPLAY_UPDATE_MS)

    time.sleep_ms(ENCODER_POLL_MS)

