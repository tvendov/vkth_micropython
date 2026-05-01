# join_minimal.py — Минимален OTAA join: TX JoinRequest + RX Join Accept.
# Стъпваме на T3 working radio config + sync 0x34 + rxIq за LoRaWAN downlink.

from machine import Pin
from sx1262 import SX1262
from LoRaWAN.AES_CMAC import AES_CMAC
import time

RF_SW = Pin("P100", Pin.OUT, value=1)

sx = SX1262(spi_bus=1, clk="P111", mosi="P109", miso="P110",
            cs="P206", irq="P015", rst="P001", gpio="P002")
sx.begin(
    freq=868.1, bw=125.0, sf=7, cr=5,
    syncWord=0x34,                          # LoRaWAN public
    power=14, currentLimit=60.0,
    preambleLength=8,
    implicit=False, implicitLen=0xFF,
    crcOn=True, txIq=False, rxIq=True,      # rxIq=True задължително за RX downlink
    tcxoVoltage=1.8, useRegulatorLDO=False,
    blocking=True,
)
print("Radio init OK (SF7, sync=0x34, rxIq=True)")

# Credentials — попълни от TTN Console (placeholder-и тук!)
DevEUI  = bytes(8)
JoinEUI = bytes(8)
AppKey  = bytes(16)

# JoinRequest frame (LoRaWAN 1.0.x format)
MHDR = bytes([0x00])
JoinEUI_LE = bytes(reversed(JoinEUI))
DevEUI_LE  = bytes(reversed(DevEUI))
DevNonce   = 2                                  # increment всеки път
DevNonce_LE = bytes([DevNonce & 0xFF, (DevNonce >> 8) & 0xFF])
mic_input = MHDR + JoinEUI_LE + DevEUI_LE + DevNonce_LE
mic = bytes(AES_CMAC().encode(AppKey, mic_input))[:4]
frame = mic_input + mic
print("JoinRequest frame (%d B): %s" % (len(frame), frame.hex()))

# TX
print("\nSending JoinRequest @ 868.1 MHz SF7...")
t0 = time.ticks_ms()
result = sx.send(frame)
tx_end = time.ticks_diff(time.ticks_ms(), t0)
print("send returned:", result, "(TX took %d ms)" % tx_end)

# RX Join Accept window — TTN изпраща при RX1 = +5s, RX2 = +6s.
# Слушаме до 8 секунди от TX края.
print("\nWaiting for Join Accept (up to 8 s)...")
msg, err = sx.recv(0, True, 8000)        # timeout_en=True, timeout_ms=8000
elapsed = time.ticks_diff(time.ticks_ms(), t0)
if msg and len(msg) > 0:
    print("*** JOIN ACCEPT RECEIVED! at +%d ms" % elapsed)
    print("    raw:", bytes(msg).hex())
    print("    len:", len(msg), "bytes")
    print("    RSSI:", sx.getRSSI(), "dBm")
    print("    SNR:", sx.getSNR(), "dB")
else:
    print(">>> No JoinAccept (err=%d)" % err)
    print(">>> TTN log show JoinAccept downlink? Кажи и ще диагностицирам timing.")
