"""RSSI scanner via OUR adapter + Renesas Radio API.

Mirrors rssi_scanner.py's behaviour but instead of going through
machine.SPI directly, all chip access flows through:

    Python  →  mac.scan_*           (mod_lorawan.c bindings)
            →  Radio.SetChannel/Rx/Rssi/Standby  (radio/radio.c)
            →  SX126x*              (radio/sx126x.c)
            →  SX126xWriteCommand   (glue/sx126x_board.c — OUR adapter)
            →  ra_sci_spi_transfer  (ra/ra_sci_spi.c)
            →  SCI3 + DTC           (hardware)

If RSSI values look sane (negative dBm in -120..-30 range and react to
RF nearby), the adapter+library path is correct → any LoRaMac TX_TIMEOUT
bug is in higher-level (timing / IRQ).
If RSSI values are constant garbage / out-of-range, the adapter is
broken at the chip-command level.

Run via mpremote:
    mpremote connect COM34 run rssi_via_adapter.py
"""
import time
from machine import Pin, SPI
import lorawan

# SPI must be opened before lorawan.Mac() — sx126x_board reuses it.
spi = SPI(3, baudrate=4000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
print("init OK; chip status:", hex(mac.radio_get_status()))

# EU868 channel plan — exactly the 8 channels from rssi_scanner.py.
CHANNELS = [
    ("7.1", 867_100_000),
    ("7.3", 867_300_000),
    ("7.5", 867_500_000),
    ("7.7", 867_700_000),
    ("7.9", 867_900_000),
    ("8.1", 868_100_000),
    ("8.3", 868_300_000),
    ("8.5", 868_500_000),
]

SF = 7        # spreading factor
BW = 0        # bandwidth index 0 = 125 kHz

def decode_status(s):
    """SX126x status byte: bits 7:5 = chip mode, bits 3:1 = cmd_status."""
    mode = (s >> 4) & 0x07
    cmd = (s >> 1) & 0x07
    mode_names = {0: "?", 1: "RFU", 2: "STBY_RC", 3: "STBY_XOSC",
                  4: "FS", 5: "RX", 6: "TX", 7: "?"}
    return "0x%02X mode=%s(%d) cmd=%d" % (s, mode_names.get(mode, "?"), mode, cmd)

def scan_one(label, freq, verbose=False):
    mac.scan_standby()
    if verbose:
        print("    after standby:    ", decode_status(mac.radio_get_status()))
    mac.scan_set_freq(freq)
    if verbose:
        print("    after set_freq:   ", decode_status(mac.radio_get_status()))
    rc = mac.scan_set_lora_rx(SF, BW)
    if rc != 0:
        return None, "set_lora_rx rc=%d" % rc
    if verbose:
        print("    after set_lora_rx:", decode_status(mac.radio_get_status()))
    # Direct SetRx(0xFFFFFF) — true continuous mode per chip datasheet.
    # Radio.Rx(0) calls SX126xSetRx(0) which is "Rx Single" not continuous.
    mac.scan_set_rx_raw(0xFFFFFF)
    time.sleep_ms(16)             # AGC + LNA settle after channel hop
    if verbose:
        print("    in RX (after 16ms):", decode_status(mac.radio_get_status()),
              "busy=%s  errs=0x%04X" % (mac.radio_busy(), mac.scan_get_errors()))
    samples = []
    for _ in range(5):
        samples.append(mac.scan_rssi())
        time.sleep_ms(2)
    samples.sort()
    return samples[2], None       # median rejects transient spurs

print()
print("=== single sweep — verbose status trace on first channel ===")
for idx, (label, freq) in enumerate(CHANNELS):
    verbose = (idx == 0)
    if verbose:
        print("  %s @ %d Hz:" % (label, freq))
    rssi, err = scan_one(label, freq, verbose)
    if err:
        print("  %s @ %d Hz: ERR %s" % (label, freq, err))
    else:
        if verbose:
            print("    RSSI %d dBm" % rssi)
        else:
            print("  %s @ %d Hz: RSSI %d dBm" % (label, freq, rssi))

print()
print("=== continuous sweep — 5 rounds, abort with Ctrl-C ===")
for round_idx in range(5):
    for label, freq in CHANNELS:
        rssi, err = scan_one(label, freq)
        if err:
            print("  %s ERR" % label, end="")
        else:
            print("  %s:%4d" % (label, rssi), end="")
    print()  # newline per round

print()
print("done")
mac.scan_standby()
