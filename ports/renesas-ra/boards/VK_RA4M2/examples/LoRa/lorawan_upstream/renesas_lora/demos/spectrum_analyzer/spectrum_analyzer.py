"""Spectrum analyzer experiment — drive the SX1262 in continuous-wave
(CW) mode to validate carrier frequency, TX power, spectral purity
and channel allocation against a lab spectrum analyzer.

The chip is held in CW for `timeout_s` seconds, transmitting an
unmodulated tone at the requested frequency and power. Connect an
external SA (or an RTL-SDR for rough measurements) to the antenna
output (or near-field probe) and observe:

  * Center frequency accuracy (should be within +/-10 kHz of target)
  * TX power (matches setting +/-2 dB at room temp)
  * Spurious emissions (none above -36 dBm in typical EU868 band)
  * Phase noise / spectral mask (LoRa modulator off in CW mode)

Sweep example below cycles through the 8 EU868 channels at +14 dBm
for 5 s each. Run with the spectrum analyzer set to span 5 MHz around
868 MHz and waterfall mode — 8 short bright lines, evenly spaced
200 kHz apart, will appear in turn."""
import lorawan, time
from machine import Pin, SPI

spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()

EU868_CHANNELS = [
    867_100_000, 867_300_000, 867_500_000, 867_700_000, 867_900_000,
    868_100_000, 868_300_000, 868_500_000,
]
POWER_DBM = 14    # EU868 max ERP (without duty-cycle override)
DWELL_S = 5

print("CW sweep over EU868 — 8 channels x %ds @ %ddBm" %
      (DWELL_S, POWER_DBM))
for f in EU868_CHANNELS:
    print("  CW @ %d Hz (%.3f MHz) for %ds" % (f, f / 1e6, DWELL_S))
    st = mac.tx_cw(f, POWER_DBM, DWELL_S)
    if st != 0:
        print("    tx_cw() status:", st)
        break
    # Wait for the requested dwell + a 200 ms guard for chip to return
    # to standby before next CW. The MLME_TXCW timeout itself is what
    # actually stops transmission; we just sleep here to avoid issuing
    # the next request while the chip is still in TX.
    time.sleep(DWELL_S + 1)

print("done. Chip back in standby.")
