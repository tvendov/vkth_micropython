"""Dump & decode LoRaWAN Data Flash on VK_RA4M2 (RA4M2, 8 KB @ 0x40100000).

Companion to provision_credentials.py. READ-ONLY — never erases/writes.

    mpremote connect COMxx run dump_dataflash.py

Decodes the three regions the firmware keeps in data flash:

  Block 0  (off 0..63)    "LWCR" credentials record (DevEUI/JoinEUI/AppKey + CRC16)
                          — written by provision_credentials.py
  Block 1  (off 64..127)  session-state log (reserved; raw hex only)
  Bank A / Bank B         LoRaMac NVM ping-pong banks: 32-byte header +
                          packed module blobs [mac|region|crypto|se|cmds|classb|cq]

Counters live in the CRYPTO group (LoRaMacCryptoNvmCtx_t, 40 B, LoRaWAN 1.0.x,
LORAMAC_MAX_MC_CTX=4). Field offsets within the crypto slice:
  +0  LrWanVersion (4B: Rev,Minor,Major,Rfu)
  +4  DevNonce   u16   (counter: +1 per JoinRequest)
  +8  JoinNonce  u32   (counter: server value, +1 per JoinAccept)
  +12 FCntUp     u32   (uplink frame counter)
  +16 FCntDown   u32   (1.0 downlink frame counter)
  +20 McFCntDown0..3   u32 x4 (multicast)
  +36 LastDownFCnt     ptr (RAM pointer; meaningless in flash)

Bank header (dflash_header_t, 32 B, little-endian):
  +0 sequence u32 | +4 valid_magic u32 ("NVM1"=0x4E564D31) | +8 crc32 u32
  +12 reserved u32 | +16 mac_size u16 | +18 region_size | +20 crypto_size
  +22 se_size | +24 cmds_size | +26 classb_size | +28 cq_size | +30 reserved2
"""

import struct
import dataflash

MAGIC_CRED = b"LWCR"
NVM1 = 0x4E564D31
HDR_LEN = 32
CRED_LEN = 40
RESERVED_BLOCKS = 2          # Block 0 (creds) + Block 1 (session log)
GROUPS = ("mac", "region", "crypto", "se", "cmds", "classb", "cq")


def hexdump(data, base=0, width=16):
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        hexs = " ".join("%02X" % b for b in chunk)
        asci = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("    %04X  %-*s  %s" % (base + i, width * 3 - 1, hexs, asci))


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def crc16_ccitt(data, crc=0xFFFF):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def geometry():
    size = dataflash.size()
    blk = dataflash.block_size()
    reserved = RESERVED_BLOCKS * blk
    available = size - reserved
    bank_blocks = (available // blk) // 2
    bank_size = bank_blocks * blk
    bank_a = reserved
    bank_b = reserved + bank_size
    return size, blk, reserved, bank_size, bank_a, bank_b


def dump_credentials():
    print("=" * 64)
    print("BLOCK 0 — credentials (LWCR record, 40 B)")
    print("=" * 64)
    rec = bytes(dataflash.read(0, CRED_LEN))
    hexdump(rec, 0)
    if rec[0:4] != MAGIC_CRED:
        print("  magic = %r  (blank / not provisioned)" % rec[0:4])
        return
    ver = rec[4]
    dev_eui = rec[6:14]
    join_eui = rec[14:22]
    app_key = rec[22:38]
    crc_stored = (rec[38] << 8) | rec[39]
    crc_calc = crc16_ccitt(rec[:38])
    print("  magic       : LWCR")
    print("  version     : 0x%02X" % ver)
    print("  DevEUI  [6:14]  : %s" % dev_eui.hex())
    print("  JoinEUI [14:22] : %s" % join_eui.hex())
    print("  AppKey  [22:38] : %s" % app_key.hex())
    print("  CRC16   [38:40] : stored=0x%04X calc=0x%04X %s"
          % (crc_stored, crc_calc, "OK" if crc_stored == crc_calc else "FAIL"))


def parse_header(b):
    return {
        "sequence":    u32(b, 0),
        "valid_magic": u32(b, 4),
        "crc32":       u32(b, 8),
        "mac":         u16(b, 16),
        "region":      u16(b, 18),
        "crypto":      u16(b, 20),
        "se":          u16(b, 22),
        "cmds":        u16(b, 24),
        "classb":      u16(b, 26),
        "cq":          u16(b, 28),
    }


def dump_bank(name, addr, bank_size):
    raw = bytes(dataflash.read(addr, HDR_LEN))
    h = parse_header(raw)
    valid = h["valid_magic"] == NVM1
    print("-" * 64)
    print("BANK %s @ off 0x%04X (size %d B)   %s"
          % (name, addr, bank_size, "VALID 'NVM1'" if valid else "empty/invalid"))
    print("  sequence=%d  valid_magic=0x%08X  crc32=0x%08X"
          % (h["sequence"], h["valid_magic"], h["crc32"]))
    if not valid:
        return None, 0
    total = sum(h[g] for g in GROUPS)
    print("  module sizes:  " + "  ".join("%s=%d" % (g, h[g]) for g in GROUPS)
          + "   (payload=%d B)" % total)
    return h, addr + HDR_LEN


def decode_crypto(slice_b):
    print("  CRYPTO group counters (LoRaMacCryptoNvmCtx_t, 1.0.x):")
    if len(slice_b) < 40:
        print("    [slice %d B < 40 — unexpected, dumping raw only]" % len(slice_b))
        hexdump(slice_b)
        return
    ver = slice_b[0:4]
    print("    LrWanVersion : %d.%d.%d (rfu=%d)" % (ver[2], ver[1], ver[0], ver[3]))
    print("    DevNonce     : %d        (JoinRequest counter)" % u16(slice_b, 4))
    print("    JoinNonce    : %d        (JoinAccept counter)" % u32(slice_b, 8))
    print("    FCntUp       : %d        (uplink frame counter)" % u32(slice_b, 12))
    print("    FCntDown     : %d        (downlink frame counter)" % u32(slice_b, 16))
    print("    McFCntDown0-3: %d %d %d %d"
          % (u32(slice_b, 20), u32(slice_b, 24), u32(slice_b, 28), u32(slice_b, 32)))


def dump_active(h, payload_addr):
    off = payload_addr
    slices = {}
    for g in GROUPS:
        n = h[g]
        slices[g] = bytes(dataflash.read(off, n)) if n else b""
        off += n
    # counters first (what the operator wants front-and-centre)
    decode_crypto(slices["crypto"])
    # then every module slice as labelled hex
    print("  --- module slices (hex) ---")
    for g in GROUPS:
        s = slices[g]
        print("  [%s] %d B" % (g, len(s)))
        if s:
            hexdump(s)


def main():
    size, blk, reserved, bank_size, bank_a, bank_b = geometry()
    print("Data Flash: size=%d  block=%d  reserved=%d (blocks 0-1)" % (size, blk, reserved))
    print("Banks: A @ 0x%04X, B @ 0x%04X, each %d B" % (bank_a, bank_b, bank_size))

    dump_credentials()

    print("=" * 64)
    print("BLOCK 1 — session-state log (reserved; raw)")
    print("=" * 64)
    hexdump(bytes(dataflash.read(blk, blk)), blk)

    print("=" * 64)
    print("LoRaMac NVM ping-pong banks")
    print("=" * 64)
    ha, pa = dump_bank("A", bank_a, bank_size)
    hb, pb = dump_bank("B", bank_b, bank_size)

    # pick active = valid bank with the higher sequence
    active = None
    if ha and hb:
        if ha["sequence"] >= hb["sequence"]:
            active = ("A", ha, pa)
        else:
            active = ("B", hb, pb)
    elif ha:
        active = ("A", ha, pa)
    elif hb:
        active = ("B", hb, pb)

    print("=" * 64)
    if active is None:
        print("ACTIVE BANK: none (fresh / factory-reset NVM — no session persisted)")
        return
    name, h, payload_addr = active
    print("ACTIVE BANK: %s (sequence=%d)" % (name, h["sequence"]))
    print("=" * 64)
    dump_active(h, payload_addr)


main()
