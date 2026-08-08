"""Provision LoRaWAN credentials в Data Flash на VK_RA4M2.

Стартиране:
    mpremote connect COMxx run provision_credentials.py

Edit-вай DevEUI / JoinEUI / AppKey константите по-долу преди да го пуснеш
за дадена платка. Скриптът записва record в Data Flash блок 0; lorawan_app
го чете на boot.

Layout (40 bytes в блок 0):
  0..3   "LWCR" magic
  4      version = 0x01
  5      reserved
  6..13  DevEUI  (8 bytes MSB)
  14..21 JoinEUI (8 bytes MSB)
  22..37 AppKey  (16 bytes MSB)
  38..39 CRC16-CCITT BE над bytes 0..37
"""

import dataflash

# === Credentials — edit за дадена платка преди стартиране ===
# Формат: hex string точно както в TTN Console (MSB ред).
# Допустимо: с или без интервали, главни/малки букви, с или без 0x префикс.
# Празни байтове за JoinEUI (=AppEUI) → "0000000000000000".

DevEUI  = "70B3D57ED0077416"
JoinEUI = "0000000000000000"
AppKey  = "DC2EC645A240B46AA1DB54C16AC35ED9"

# === Implementation ===

MAGIC       = b"LWCR"
VERSION     = 1
RECORD_LEN  = 40
PAYLOAD_LEN = 38


def parse_hex(s, n_bytes, name):
    """Hex string -> bytes(n_bytes). Игнорира whitespace и 0x prefix; case-insensitive."""
    cleaned = s.replace(" ", "").replace("\t", "").replace("\n", "").replace("-", "").replace(":", "")
    if cleaned.lower().startswith("0x"):
        cleaned = cleaned[2:]
    if len(cleaned) != n_bytes * 2:
        raise ValueError("%s: очаквам %d hex chars, получих %d (%r)" %
                         (name, n_bytes * 2, len(cleaned), s))
    try:
        return bytes.fromhex(cleaned)
    except ValueError:
        raise ValueError("%s: невалиден hex %r" % (name, s))


def crc16_ccitt(data, crc=0xFFFF):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def build_record(dev_eui, join_eui, app_key):
    if len(dev_eui) != 8 or len(join_eui) != 8 or len(app_key) != 16:
        raise ValueError("dev_eui/join_eui must be 8 bytes, app_key must be 16 bytes")
    payload = bytearray(PAYLOAD_LEN)
    payload[0:4]   = MAGIC
    payload[4]     = VERSION
    payload[5]     = 0x00
    payload[6:14]  = dev_eui
    payload[14:22] = join_eui
    payload[22:38] = app_key
    crc = crc16_ccitt(payload)
    return bytes(payload) + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def main():
    dev_eui  = parse_hex(DevEUI,  8,  "DevEUI")
    join_eui = parse_hex(JoinEUI, 8,  "JoinEUI")
    app_key  = parse_hex(AppKey,  16, "AppKey")

    print("=== Provision LoRaWAN credentials -> Data Flash ===")
    print("  DevEUI : %s" % dev_eui.hex())
    print("  JoinEUI: %s" % join_eui.hex())
    print("  AppKey : %s" % app_key.hex())

    if dev_eui == b"\x00" * 8 or app_key == b"\x00" * 16:
        print("REFUSED: DevEUI или AppKey са нули. Edit-ни константите в скрипта.")
        return

    record = build_record(dev_eui, join_eui, app_key)
    crc    = (record[38] << 8) | record[39]

    df_size  = dataflash.size()
    df_block = dataflash.block_size()
    df_wsize = dataflash.write_size()
    print("Data Flash: size=%d block_size=%d write_size=%d" %
          (df_size, df_block, df_wsize))
    print("Record: %d bytes, CRC16=0x%04X" % (len(record), crc))

    print("Erasing block 0...")
    dataflash.erase_block(0)
    print("Writing record at offset 0...")
    n = dataflash.write(0, record)
    print("  wrote %d bytes" % n)

    print("Verify read-back...")
    rb = bytes(dataflash.read(0, len(record)))

    ok = True

    # 1. Bytewise compare на целия record
    if rb != record:
        print("  [FAIL] raw bytes mismatch")
        print("    expected:", record.hex())
        print("    got:    ", rb.hex())
        ok = False
    else:
        print("  [OK] raw 40 bytes match")

    # 2. Magic + version
    if rb[0:4] != MAGIC:
        print("  [FAIL] magic = %r (очаквано 'LWCR')" % rb[0:4])
        ok = False
    else:
        print("  [OK] magic 'LWCR'")
    if rb[4] != VERSION:
        print("  [FAIL] version = 0x%02X (очаквано 0x%02X)" % (rb[4], VERSION))
        ok = False
    else:
        print("  [OK] version 0x%02X" % rb[4])

    # 3. CRC re-compute от read-back
    rb_crc_stored = (rb[38] << 8) | rb[39]
    rb_crc_calc   = crc16_ccitt(rb[:38])
    if rb_crc_stored != rb_crc_calc:
        print("  [FAIL] CRC stored=0x%04X calc=0x%04X" %
              (rb_crc_stored, rb_crc_calc))
        ok = False
    else:
        print("  [OK] CRC16 0x%04X (stored == calc)" % rb_crc_stored)

    # 4. Field-by-field extraction & compare с оригиналните входни стойности
    rb_dev_eui  = rb[6:14]
    rb_join_eui = rb[14:22]
    rb_app_key  = rb[22:38]

    if rb_dev_eui == dev_eui:
        print("  [OK] DevEUI  : %s" % rb_dev_eui.hex())
    else:
        print("  [FAIL] DevEUI  read=%s expected=%s" %
              (rb_dev_eui.hex(), dev_eui.hex()))
        ok = False

    if rb_join_eui == join_eui:
        print("  [OK] JoinEUI : %s" % rb_join_eui.hex())
    else:
        print("  [FAIL] JoinEUI read=%s expected=%s" %
              (rb_join_eui.hex(), join_eui.hex()))
        ok = False

    if rb_app_key == app_key:
        print("  [OK] AppKey  : %s" % rb_app_key.hex())
    else:
        print("  [FAIL] AppKey  read=%s expected=%s" %
              (rb_app_key.hex(), app_key.hex()))
        ok = False

    print("=" * 50)
    if ok:
        print("RESULT: SUCCESS -- credentials provisioned & verified.")
        print("        Reset платката за да активира новите credentials.")
    else:
        print("RESULT: FAIL -- credentials NOT correctly provisioned!")


main()
