# provision_credentials.py — HOST-RUN one-shot, SELF-CONTAINED.
#
# Writes the CRED v2 record (and optionally an initial CONFIG record) into
# VK_RA4M2 data flash. Run once per device (or whenever keys change). This file
# is NOT frozen into firmware: it must be safe to leave off the device, and it
# NEVER hardcodes keys.
#
# Single-file-per-deliverable policy: imports ONLY built-ins (sys) plus the
# board `dataflash` module. CRC16, region names, and the CRED / CONFIG layouts
# are inlined below — no cross-imports of project helpers, including no import
# of read_credentials.py (the validator is inlined here for read-back verify).
#
# Key source, in priority order (NO hardcoded keys anywhere):
#   1. command line:
#        mpremote run provision_credentials.py
#      does not pass argv; to pass argv use the local micropython unix port:
#        micropython provision_credentials.py <deveui> <joineui> <appkey> [device_number] [interval_s]
#      (each key as hex, MSB-first, e.g. 70B3D57ED0061234)
#   2. a gitignored secrets_local.py next to this file:
#        DEVEUI        = "70B3D57ED0061234"
#        JOINEUI       = "0000000000000000"
#        APPKEY        = "00112233445566778899AABBCCDDEEFF"
#        DEVICE_NUMBER = 1     # optional, default 0
#        INTERVAL_S    = 30    # optional; if set, an initial CONFIG is written
#   3. interactive input() prompts

import sys
import dataflash

# ----------------------------------------------------------------------------
# CRC16-CCITT (XModem): poly 0x1021, init 0xFFFF. crc16_ccitt(b"123456789")==0x29B1
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
# Data-flash region names + record layouts (mirror of dataflash_partition.h).
# ----------------------------------------------------------------------------
REGION_CRED = "CRED"
REGION_CONFIG = "CONFIG"
DF_WRITE_UNIT = 4

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

CONFIG_MAGIC = b"LWCF"
CONFIG_RECORD_LEN = 14
CFG_OFF_MAGIC = 0
CFG_OFF_INTERVAL = 4
CFG_OFF_TS = 8
CFG_OFF_CRC = 12


def _pad4(buf):
    # Pad to the 4-byte write unit with 0xFF (erased state) so a partial
    # trailing word never forces a 0->1 bit transition on RMW.
    n = len(buf)
    a = (n + (DF_WRITE_UNIT - 1)) & ~(DF_WRITE_UNIT - 1)
    if a == n:
        return bytes(buf)
    return bytes(buf) + b"\xff" * (a - n)


def _u32le(buf, off):
    return (buf[off] | (buf[off + 1] << 8) | (buf[off + 2] << 16) | (buf[off + 3] << 24))


def _put_u32le(buf, off, val):
    buf[off] = val & 0xFF
    buf[off + 1] = (val >> 8) & 0xFF
    buf[off + 2] = (val >> 16) & 0xFF
    buf[off + 3] = (val >> 24) & 0xFF


def _hex_to_bytes(s, nbytes, label):
    s = s.strip().replace(" ", "").replace(":", "")
    if len(s) != nbytes * 2:
        raise ValueError("%s: expected %d hex chars, got %d" % (label, nbytes * 2, len(s)))
    return bytes(int(s[i:i + 2], 16) for i in range(0, len(s), 2))


def _gather_inputs():
    # Returns (deveui, joineui, appkey, devnum, interval_s_or_None).
    if len(sys.argv) >= 4:
        deveui = _hex_to_bytes(sys.argv[1], 8, "DevEUI")
        joineui = _hex_to_bytes(sys.argv[2], 8, "JoinEUI")
        appkey = _hex_to_bytes(sys.argv[3], 16, "AppKey")
        devnum = int(sys.argv[4]) if len(sys.argv) >= 5 else 0
        interval_s = int(sys.argv[5]) if len(sys.argv) >= 6 else None
        return deveui, joineui, appkey, devnum, interval_s

    try:
        import secrets_local as sl
        deveui = _hex_to_bytes(sl.DEVEUI, 8, "DevEUI")
        joineui = _hex_to_bytes(sl.JOINEUI, 8, "JoinEUI")
        appkey = _hex_to_bytes(sl.APPKEY, 16, "AppKey")
        devnum = int(getattr(sl, "DEVICE_NUMBER", 0))
        interval_s = getattr(sl, "INTERVAL_S", None)
        if interval_s is not None:
            interval_s = int(interval_s)
        return deveui, joineui, appkey, devnum, interval_s
    except ImportError:
        pass

    deveui = _hex_to_bytes(input("DevEUI  (16 hex, MSB-first): "), 8, "DevEUI")
    joineui = _hex_to_bytes(input("JoinEUI (16 hex, MSB-first): "), 8, "JoinEUI")
    appkey = _hex_to_bytes(input("AppKey  (32 hex, MSB-first): "), 16, "AppKey")
    s = input("device_number (decimal, default 0): ").strip()
    devnum = int(s) if s else 0
    s = input("interval_s (decimal, blank = no CONFIG): ").strip()
    interval_s = int(s) if s else None
    return deveui, joineui, appkey, devnum, interval_s


def build_cred_record(deveui, joineui, appkey, device_number):
    if len(deveui) != 8 or len(joineui) != 8 or len(appkey) != 16:
        raise ValueError("key length: DevEUI/JoinEUI 8 B, AppKey 16 B")
    if device_number < 0 or device_number > 0xFFFFFFFF:
        raise ValueError("device_number out of uint32 range")

    rec = bytearray(CRED_RECORD_LEN)
    rec[OFF_MAGIC:OFF_MAGIC + 4] = CRED_MAGIC
    rec[OFF_VERSION] = CRED_VERSION
    rec[OFF_RESERVED] = 0x00
    rec[OFF_DEVEUI:OFF_DEVEUI + 8] = deveui
    rec[OFF_JOINEUI:OFF_JOINEUI + 8] = joineui
    rec[OFF_APPKEY:OFF_APPKEY + 16] = appkey
    rec[OFF_DEVNUM] = (device_number >> 24) & 0xFF
    rec[OFF_DEVNUM + 1] = (device_number >> 16) & 0xFF
    rec[OFF_DEVNUM + 2] = (device_number >> 8) & 0xFF
    rec[OFF_DEVNUM + 3] = device_number & 0xFF

    crc = crc16_ccitt(rec[0:OFF_CRC])
    rec[OFF_CRC] = (crc >> 8) & 0xFF
    rec[OFF_CRC + 1] = crc & 0xFF
    return bytes(rec)


def build_config_record(interval_s, ts=0):
    rec = bytearray(CONFIG_RECORD_LEN)
    rec[CFG_OFF_MAGIC:CFG_OFF_MAGIC + 4] = CONFIG_MAGIC
    _put_u32le(rec, CFG_OFF_INTERVAL, interval_s & 0xFFFFFFFF)
    _put_u32le(rec, CFG_OFF_TS, ts & 0xFFFFFFFF)
    crc = crc16_ccitt(rec[0:CFG_OFF_CRC])
    rec[CFG_OFF_CRC] = (crc >> 8) & 0xFF
    rec[CFG_OFF_CRC + 1] = crc & 0xFF
    return bytes(rec)


# ---- inlined validator (read-back verify; mirrors read_credentials.py) ------

def _load_credentials():
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


def provision(deveui, joineui, appkey, device_number, interval_s=None):
    rec = build_cred_record(deveui, joineui, appkey, device_number)
    cred = dataflash.region(REGION_CRED)
    cred.erase()
    cred.write(0, rec)

    creds = _load_credentials()
    if creds is None:
        raise ValueError("verify FAILED: CRED did not validate after write")
    if creds[0] != deveui or creds[1] != joineui or creds[2] != appkey or creds[3] != device_number:
        raise ValueError("verify FAILED: CRED read-back mismatch")

    if interval_s is not None:
        cfg_rec = build_config_record(interval_s, 0)
        cfg = dataflash.region(REGION_CONFIG)
        cfg.erase()
        cfg.write(0, _pad4(cfg_rec))

    return creds, interval_s


def _hx(b):
    return "".join("%02X" % x for x in b)


def main():
    deveui, joineui, appkey, devnum, interval_s = _gather_inputs()
    creds, interval_s = provision(deveui, joineui, appkey, devnum, interval_s)
    print("CRED v2 written + verified OK")
    print("  DevEUI        :", _hx(creds[0]))
    print("  JoinEUI       :", _hx(creds[1]))
    print("  AppKey        :", _hx(creds[2]))
    print("  device_number :", creds[3])
    if interval_s is not None:
        print("CONFIG written: interval_s =", interval_s)
    else:
        print("CONFIG not written (no interval_s supplied)")


if __name__ == "__main__":
    main()
