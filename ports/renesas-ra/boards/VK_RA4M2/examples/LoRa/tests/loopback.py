# Loopback тест: предполага P109 (MOSI) ↔ P110 (MISO) са свързани с жица.
# Първо SoftSPI, после hardware SPI(1).

from machine import SPI, SoftSPI, Pin
import time
import _config as cfg


def _check(label, sent, recv):
    ok = bytes(sent) == bytes(recv)
    print("%s: sent=%s recv=%s -> %s" % (
        label,
        [hex(b) for b in sent],
        [hex(b) for b in recv],
        "PASS" if ok else "FAIL",
    ))
    return ok


def soft_loopback():
    print()
    print("=== SoftSPI loopback (P109↔P110, SCK=P111) ===")
    sck  = Pin(cfg.PIN_SCK,  Pin.OUT, value=0)
    mosi = Pin(cfg.PIN_MOSI, Pin.OUT, value=0)
    miso = Pin(cfg.PIN_MISO, Pin.IN)

    spi = SoftSPI(baudrate=500_000, polarity=0, phase=0,
                  sck=sck, mosi=mosi, miso=miso)

    patterns = [
        b"\xDE\xAD\xBE\xEF",
        b"\x55\xAA\x55\xAA",
        b"\x12\x34\x56\x78\x9A\xBC\xDE\xF0",
        b"\x00\xFF\x01\x80",
    ]
    fails = 0
    for p in patterns:
        rx = bytearray(len(p))
        spi.write_readinto(p, rx)
        if not _check("Soft  pattern len=%d" % len(p), p, rx):
            fails += 1
    return fails


def hard_loopback():
    print()
    print("=== Hardware SPI(1) loopback (P109↔P110, SCK=P111) ===")
    print(">>> ВНИМАНИЕ: ако SPI(1) виси, ще трябва reset.")
    spi = SPI(1, baudrate=1_000_000, polarity=0, phase=0, bits=8,
              sck=Pin(cfg.PIN_SCK), mosi=Pin(cfg.PIN_MOSI), miso=Pin(cfg.PIN_MISO))
    print("ctor OK:", spi)

    patterns = [
        b"\xDE\xAD\xBE\xEF",
        b"\x55\xAA\x55\xAA",
    ]
    fails = 0
    for p in patterns:
        rx = bytearray(len(p))
        spi.write_readinto(p, rx)
        if not _check("Hard  pattern len=%d" % len(p), p, rx):
            fails += 1
    return fails


def run():
    soft_fails = soft_loopback()
    print()
    print("=== Soft loopback summary: %s ===" % ("PASS" if soft_fails == 0 else "FAIL %d" % soft_fails))

    hard_fails = hard_loopback()
    print()
    print("=== Hard loopback summary: %s ===" % ("PASS" if hard_fails == 0 else "FAIL %d" % hard_fails))
