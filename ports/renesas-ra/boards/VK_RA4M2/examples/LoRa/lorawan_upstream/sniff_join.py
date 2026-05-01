# sniff_join.py — слуша на LoRaWAN параметри, за да хваща JoinRequest от
# друга платка. Полезно за диагностика когато gateway не вижда пакетите.

from machine import Pin
from sx1262 import SX1262
from sx126x import SX126X_SYNC_WORD_PUBLIC

# ENABLE RF switch (Wio-SX1262)
rf_sw = Pin("P100", Pin.OUT, value=1)

# Init радиото с LoRaWAN параметри (frequency, SF, BW, sync) за EU868 канал 0
sx = SX1262(spi_bus=1, clk="P111", mosi="P109", miso="P110",
            cs="P206", irq="P015", rst="P001", gpio="P002")
sx.begin(
    freq=868.1, bw=125.0, sf=12, cr=5,             # DR0 = SF12 BW125
    syncWord=SX126X_SYNC_WORD_PUBLIC,                # 0x34 LoRaWAN public
    power=-5,                                        # ниско, само RX
    currentLimit=60.0, preambleLength=8,
    implicit=False, implicitLen=0xFF,
    crcOn=True, txIq=False, rxIq=True,
    tcxoVoltage=1.8, useRegulatorLDO=True, blocking=True,
)

print("=" * 50)
print("Sniffer: 868.1 MHz, SF12, BW125, sync=0x34 (PUBLIC)")
print("Чакам JoinRequest пакети от другата платка...")
print("=" * 50)

n = 0
while True:
    msg, err = sx.recv()
    if msg and len(msg) > 0:
        n += 1
        rssi = sx.getRSSI()
        snr = sx.getSNR()
        print("[%d] len=%d RSSI=%d SNR=%.1f" % (n, len(msg), rssi, snr))
        # JoinRequest format: MHDR(1) + JoinEUI(8) + DevEUI(8) + DevNonce(2) + MIC(4) = 23 байта
        if len(msg) == 23 and (msg[0] & 0xE0) == 0x00:   # MType = JoinRequest (0)
            print("    *** JoinRequest detected!")
            print("    JoinEUI:", bytes(msg[1:9][::-1]).hex())   # LE → MSB
            print("    DevEUI :", bytes(msg[9:17][::-1]).hex())  # LE → MSB
            print("    DevNonce:", int.from_bytes(msg[17:19], "little"))
        else:
            print("    raw:", bytes(msg).hex())
