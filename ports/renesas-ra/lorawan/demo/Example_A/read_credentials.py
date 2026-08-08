# read_credentials.py — HOST-RUN diagnostic, SELF-CONTAINED.
#
# Reads + validates the CRED v2 record (and the CONFIG record) from VK_RA4M2
# data flash and prints them. Run from the host:
#
#   mpremote connect COM34 run read_credentials.py
#
# Single-file-per-deliverable policy: this file imports ONLY built-ins plus the
# board `dataflash` module. CRC16, the partition region names, and the CRED /
# CONFIG record layouts are inlined below — no cross-imports of project helpers.

import dataflash

# ----------------------------------------------------------------------------
# CRC16-CCITT (XModem variant): poly 0x1021, init 0xFFFF, no reflection, no
# final XOR. Vector: crc16_ccitt(b"123456789") == 0x29B1. The writer and reader
# share this exact routine so the verify path can never drift.
# ----------------------------------------------------------------------------
def crc16_ccitt(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


# ----------------------------------------------------------------------------
# Data-flash region names — Python mirror of dataflash_partition.h. Access to
# CRED / CONFIG is exclusively through dataflash.region(name).
# ----------------------------------------------------------------------------
REGION_CRED = "CRED"
REGION_CONFIG = "CONFIG"

# CRED v2 record (44 of 64 B, region "CRED" offset 0):
#   off  0 : "LWCR" magic            (4 B)
#   off  4 : version 0x02            (1 B)
#   off  5 : reserved                (1 B)
#   off  6 : DevEUI  MSB-first        (8 B)
#   off 14 : JoinEUI MSB-first        (8 B)
#   off 22 : AppKey  MSB-first       (16 B)
#   off 38 : device_number uint32 BE  (4 B)
#   off 42 : CRC16-CCITT over 0..41   (2 B, BE)
CRED_MAGIC = b"LWCR"
CRED_VERSION = 0x02
CRED_RECORD_LEN = 44

OFF_MAGIC = 0
OFF_VERSION = 4
OFF_RESERVED = 5
OFF_DEVEUI = 6
OFF_JOINEUI = 14
OFF_APPKEY = 22
OFF_DEVNUM = 38
OFF_CRC = 42

# CONFIG record (region "CONFIG" offset 0, 64 B block):
#   off  0 : magic "LWCF"             (4 B)
#   off  4 : interval_s uint32 LE     (4 B)
#   off  8 : last_write_ts uint32 LE  (4 B)
#   off 12 : CRC16-CCITT over 0..11   (2 B, BE)
CONFIG_MAGIC = b"LWCF"
CONFIG_RECORD_LEN = 14
CFG_OFF_MAGIC = 0
CFG_OFF_INTERVAL = 4
CFG_OFF_TS = 8
CFG_OFF_CRC = 12


def _u32le(buf, off):
    return (buf[off] | (buf[off + 1] << 8) | (buf[off + 2] << 16) | (buf[off + 3] << 24))


def load_credentials():
    # Returns (deveui, joineui, appkey, device_number) on success — the three
    # keys are 8/8/16-byte `bytes` MSB-first, device_number is an int. Returns
    # None if the record is blank, has the wrong magic/version, or fails CRC.
    cred = dataflash.region(REGION_CRED)
    rec = cred.read(0, CRED_RECORD_LEN)

    if rec[OFF_MAGIC:OFF_MAGIC + 4] != CRED_MAGIC:
        return None
    if rec[OFF_VERSION] != CRED_VERSION:
        return None

    stored = (rec[OFF_CRC] << 8) | rec[OFF_CRC + 1]
    if crc16_ccitt(rec[0:OFF_CRC]) != stored:
        return None

    deveui = bytes(rec[OFF_DEVEUI:OFF_DEVEUI + 8])
    joineui = bytes(rec[OFF_JOINEUI:OFF_JOINEUI + 8])
    appkey = bytes(rec[OFF_APPKEY:OFF_APPKEY + 16])
    device_number = ((rec[OFF_DEVNUM] << 24) | (rec[OFF_DEVNUM + 1] << 16)
                     | (rec[OFF_DEVNUM + 2] << 8) | rec[OFF_DEVNUM + 3])

    return (deveui, joineui, appkey, device_number)


def load_config():
    # Returns (interval_s, last_write_ts) or None if blank/invalid.
    cfg = dataflash.region(REGION_CONFIG)
    rec = cfg.read(0, CONFIG_RECORD_LEN)
    if rec[CFG_OFF_MAGIC:CFG_OFF_MAGIC + 4] != CONFIG_MAGIC:
        return None
    stored = (rec[CFG_OFF_CRC] << 8) | rec[CFG_OFF_CRC + 1]
    if crc16_ccitt(rec[0:CFG_OFF_CRC]) != stored:
        return None
    return (_u32le(rec, CFG_OFF_INTERVAL), _u32le(rec, CFG_OFF_TS))


def _hx(b):
    return "".join("%02X" % x for x in b)


def main():
    print("=== CRED (region \"%s\") ===" % REGION_CRED)
    creds = load_credentials()
    if creds is None:
        cred = dataflash.region(REGION_CRED)
        raw = cred.read(0, CRED_RECORD_LEN)
        if raw[OFF_MAGIC:OFF_MAGIC + 4] != CRED_MAGIC:
            print("  BLANK / no LWCR magic — device not provisioned")
        else:
            print("  INVALID — magic OK but version/CRC failed")
            print("  raw     :", _hx(raw))
    else:
        deveui, joineui, appkey, devnum = creds
        print("  DevEUI        :", _hx(deveui))
        print("  JoinEUI       :", _hx(joineui))
        print("  AppKey        :", _hx(appkey))
        print("  device_number :", devnum)
        print("  VALID")

    print("=== CONFIG (region \"%s\") ===" % REGION_CONFIG)
    cfg = load_config()
    if cfg is None:
        print("  BLANK / INVALID")
    else:
        interval_s, ts = cfg
        print("  interval_s    :", interval_s)
        print("  last_write_ts :", ts)
        print("  VALID")


if __name__ == "__main__":
    main()
