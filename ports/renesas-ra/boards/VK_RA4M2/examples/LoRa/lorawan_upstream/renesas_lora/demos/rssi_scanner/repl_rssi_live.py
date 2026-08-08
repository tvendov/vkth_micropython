"""Live RSSI monitor via direct Python SPI — watches chip's instantaneous RSSI.

Init chip in continuous RX on 868.1 MHz / SF7 / BW125. Then prints RSSI
every 200ms. Press Ctrl-C to stop.

If RSSI varies (especially when bringing a phone or LoRa transmitter near
the antenna) — chip RX path is alive end-to-end.
If RSSI stays stuck at -127/-128 dBm — chip never enters RX or RF chain
is dead.
"""
import time
from machine import Pin, SPI

spi   = SPI(3, baudrate=8000000, polarity=0, phase=0,
            sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))
nss   = Pin('P206', Pin.OUT, value=1)
busy  = Pin('P002', Pin.IN)
rst   = Pin('P001', Pin.OUT, value=0)
rf_en = Pin('P100', Pin.OUT, value=1)

def wait_busy():
    t0 = time.ticks_ms()
    while busy.value():
        if time.ticks_diff(time.ticks_ms(), t0) >= 200:
            return False
    return True

def cmd(op, payload=b''):
    wait_busy()
    nss(0); spi.write(bytes([op]) + payload); nss(1)
    wait_busy()

def get_status():
    wait_busy()
    buf = bytearray(2)
    nss(0); spi.write_readinto(b'\xC0\x00', buf); nss(1)
    return buf[1]

def get_rssi_raw():
    wait_busy()
    buf = bytearray(3)
    nss(0); spi.write_readinto(b'\x15\x00\x00', buf); nss(1)
    return buf[2]

# Reset
rst(0); time.sleep_ms(2); rst(1); time.sleep_ms(20)
wait_busy()

# Init
cmd(0x80, b'\x00')                      # SetStandby(STDBY_RC)
cmd(0x96, b'\x00')                      # SetRegulatorMode(LDO)
cmd(0x97, b'\x02\x00\x01\x40')          # SetDio3AsTcxoCtrl(1.8V, 5ms)
time.sleep_ms(10)
cmd(0x89, b'\x7F')                      # Calibrate
time.sleep_ms(5); wait_busy()
cmd(0x07, b'\x00\x00')                  # ClearDeviceErrors
cmd(0x9D, b'\x01')                      # SetDio2AsRfSwitchCtrl
cmd(0x8A, b'\x01')                      # SetPacketType(LoRa)
cmd(0x8B, b'\x07\x04\x01\x00')          # SetModulationParams SF7/BW125/CR4_5
cmd(0x8C, b'\x00\x08\x00\xFF\x01\x00')  # SetPacketParams
cmd(0x8F, b'\x80\x00')                  # SetBufferBaseAddress
cmd(0x86, b'\x36\x41\x99\x9A')          # SetRfFrequency 868.1 MHz
cmd(0x82, b'\xFF\xFF\xFF')              # SetRx(continuous)
time.sleep_ms(20)

print("init done. Status:", hex(get_status()))
print("Live RSSI on 868.1 MHz SF7 BW125 — Ctrl-C to stop:")
print()

# Live monitor
try:
    while True:
        raw = get_rssi_raw()
        rssi_dbm = -raw // 2
        # Visual bar — covers -127..-30
        clamped = max(-127, min(-30, rssi_dbm))
        pos = (clamped + 127) * 60 // 97
        bar = "#" * pos + "·" * (60 - pos)
        print("\r%4d dBm |%s|" % (rssi_dbm, bar), end="")
        time.sleep_ms(200)
except KeyboardInterrupt:
    print()
    print("stopped.")
finally:
    cmd(0x80, b'\x00')   # SetStandby
