"""Step-by-step chip diagnosis via direct Python machine.SPI.

Bypasses our LoRaMac adapter completely. Sends raw SX126x commands and
reads back chip status + errors after each step. If THIS path also
returns broken values (constant RSSI, stuck XoscStart), the issue is
NOT our adapter — it's chip-level (hardware or missing init step).
"""
import time
from machine import Pin, SPI

spi   = SPI(3, baudrate=8000000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
rst   = Pin('P001', Pin.OUT, value=0)
rf_en = Pin('P100', Pin.OUT, value=1)   # rf switch ON

def wait_busy(tag="", timeout_ms=200):
    t0 = time.ticks_ms()
    while busy.value():
        if time.ticks_diff(time.ticks_ms(), t0) >= timeout_ms:
            print("    ! BUSY timeout", tag)
            return False
    return True

def cmd(op, payload=b'', tag=""):
    wait_busy("pre " + tag)
    nss(0)
    spi.write(bytes([op]) + payload)
    nss(1)
    wait_busy("post " + tag)

def get_status():
    wait_busy("status pre")
    buf = bytearray(2)
    nss(0); spi.write_readinto(b'\xC0\x00', buf); nss(1)
    return buf[1]

def get_errors():
    wait_busy("errs pre")
    buf = bytearray(4)
    nss(0); spi.write_readinto(b'\x17\x00\x00\x00', buf); nss(1)
    # buf[0]=stale, buf[1]=status, buf[2]=err_hi, buf[3]=err_lo
    return (buf[2] << 8) | buf[3]

def decode_status(s):
    mode = (s >> 4) & 0x07
    cmd_st = (s >> 1) & 0x07
    return "0x%02X mode=%d cmd=%d" % (s, mode, cmd_st)

# --- Hard reset ---
print("=== reset ===")
rst(0); time.sleep_ms(2); rst(1); time.sleep_ms(20)
wait_busy("after reset")
print("status post-reset: ", decode_status(get_status()))
print("errors post-reset: 0x%04X" % get_errors())

# --- Init like rssi_scanner.py ---
print()
print("=== init sequence (rssi_scanner.py reference) ===")

cmd(0x80, b'\x00', "SetStandby(STDBY_RC)")
print("after SetStandby:    ", decode_status(get_status()))

cmd(0x96, b'\x00', "SetRegulatorMode(LDO)")
print("after SetRegulator:  ", decode_status(get_status()))

cmd(0x97, b'\x02\x00\x01\x40', "SetDio3AsTcxoCtrl(1.8V, 320t=5ms)")
time.sleep_ms(10)
print("after SetDio3AsTcxo: ", decode_status(get_status()),
      "errs=0x%04X" % get_errors())

cmd(0x89, b'\x7F', "Calibrate(0x7F)")
time.sleep_ms(5)
wait_busy("post calibrate")
print("after Calibrate:     ", decode_status(get_status()),
      "errs=0x%04X" % get_errors())

cmd(0x07, b'\x00\x00', "ClearDeviceErrors")
print("after ClearErrors:   ", decode_status(get_status()),
      "errs=0x%04X" % get_errors())

cmd(0x9D, b'\x01', "SetDio2AsRfSwitchCtrl")
cmd(0x8A, b'\x01', "SetPacketType(LoRa)")
print("after SetPacketType: ", decode_status(get_status()))

cmd(0x8B, b'\x07\x04\x01\x00', "SetModulationParams SF7/BW125/CR4_5")
print("after SetModParams:  ", decode_status(get_status()))

cmd(0x8C, b'\x00\x08\x00\xFF\x01\x00', "SetPacketParams")
print("after SetPktParams:  ", decode_status(get_status()))

cmd(0x8F, b'\x80\x00', "SetBufferBaseAddress")
print("after SetBufferBase: ", decode_status(get_status()))

# --- Set frequency ---
print()
print("=== set freq + start RX continuous ===")
# 868.1 MHz: freq = (868100000 * 2^25) / 32_000_000 = 0x363999 9A
cmd(0x86, b'\x36\x41\x99\x9A', "SetRfFrequency(868.1 MHz)")
print("after SetRfFreq:     ", decode_status(get_status()),
      "errs=0x%04X" % get_errors())

cmd(0x82, b'\xFF\xFF\xFF', "SetRx(0xFFFFFF=continuous)")
time.sleep_ms(10)
print("after SetRx:         ", decode_status(get_status()),
      "errs=0x%04X" % get_errors(),
      "busy=%s" % busy.value())

# --- Sample RSSI ---
print()
print("=== RSSI samples (5 reads, 100ms apart) ===")
for i in range(5):
    wait_busy("rssi pre")
    buf = bytearray(3)
    nss(0); spi.write_readinto(b'\x15\x00\x00', buf); nss(1)
    raw = buf[2]
    rssi_dbm = -raw // 2
    print("  read %d: raw=%d (-> %d dBm) status=%s" % (
        i, raw, rssi_dbm, decode_status(buf[1])))
    time.sleep_ms(100)

print()
print("=== final state ===")
print("status: ", decode_status(get_status()))
print("errors: 0x%04X" % get_errors())
print("busy:   ", busy.value())
