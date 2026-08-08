"""Provision LoRaWAN credentials в Data Flash (CRED, block 0) на VK_RA4M2.

Стартиране:
    mpremote connect COMxx run provision_credentials.py

Ключовете НЕ са в този файл (политика: нула креденшъли в кодовата флаш).
Идват по време на изпълнение, по реда на приоритет:
  1. Модул `lora_secrets.py` (в .gitignore, НЕ се freeze-ва):
         DevEUI       = "70B3D57ED0077416"
         JoinEUI      = "0000000000000000"
         AppKey       = "DC2EC645A240B46AA1DB54C16AC35ED9"
         DeviceNumber = 1
  2. Ако липсва → интерактивен `input()` през REPL.

Запис: през `dataflash.region("CRED")` (region-aware). Ако region() още
не съществува → пада към `dataflash.write(0, ...)` (цял чип, преди guard-а).

CRED layout v2 (44 bytes в block 0, адрес 0x40100000):
  0..3   "LWCR" magic
  4      version = 0x02
  5      reserved
  6..13  DevEUI       (8 bytes MSB)
  14..21 JoinEUI      (8 bytes MSB)
  22..37 AppKey       (16 bytes MSB)
  38..41 device_number (uint32 big-endian)
  42..43 CRC16-CCITT BE над bytes 0..41
"""

MAGIC       = b"LWCR"
VERSION     = 2
RECORD_LEN  = 44
PAYLOAD_LEN = 42


def load_credentials():
    """Връща (DevEUI, JoinEUI, AppKey, DeviceNumber) от lora_secrets или input()."""
    try:
        import lora_secrets
        return (lora_secrets.DevEUI, lora_secrets.JoinEUI,
                lora_secrets.AppKey, int(lora_secrets.DeviceNumber))
    except ImportError:
        print("lora_secrets.py липсва — въведи стойностите ръчно:")
        dev = input("  DevEUI  (8 bytes hex) : ")
        join = input("  JoinEUI (8 bytes hex) : ")
        app = input("  AppKey  (16 bytes hex): ")
        num = int(input("  DeviceNumber (цяло)   : "))
        return (dev, join, app, num)


def parse_hex(s, n_bytes, name):
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


def build_record(dev_eui, join_eui, app_key, dev_num):
    if len(dev_eui) != 8 or len(join_eui) != 8 or len(app_key) != 16:
        raise ValueError("dev_eui/join_eui must be 8 bytes, app_key must be 16 bytes")
    if not (0 <= dev_num <= 0xFFFFFFFF):
        raise ValueError("device_number must fit in uint32")
    payload = bytearray(PAYLOAD_LEN)
    payload[0:4]   = MAGIC
    payload[4]     = VERSION
    payload[5]     = 0x00
    payload[6:14]  = dev_eui
    payload[14:22] = join_eui
    payload[22:38] = app_key
    payload[38:42] = bytes([(dev_num >> 24) & 0xFF, (dev_num >> 16) & 0xFF,
                            (dev_num >> 8) & 0xFF, dev_num & 0xFF])
    crc = crc16_ccitt(payload)
    return bytes(payload) + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def open_cred():
    """Връща (writer, label). Предпочита dataflash.region('CRED'), иначе целия dataflash."""
    import dataflash
    region = getattr(dataflash, "region", None)
    if region is not None:
        return (region("CRED"), "dataflash.region('CRED')")
    return (dataflash, "dataflash (цял чип, fallback)")


def main():
    dev_s, join_s, app_s, dev_num = load_credentials()
    dev_eui  = parse_hex(dev_s,  8,  "DevEUI")
    join_eui = parse_hex(join_s, 8,  "JoinEUI")
    app_key  = parse_hex(app_s,  16, "AppKey")

    print("=== Provision LoRaWAN credentials -> Data Flash (CRED v2) ===")
    print("  DevEUI       : %s" % dev_eui.hex())
    print("  JoinEUI      : %s" % join_eui.hex())
    print("  AppKey       : %s" % app_key.hex())
    print("  DeviceNumber : %d" % dev_num)

    if dev_eui == b"\x00" * 8 or app_key == b"\x00" * 16:
        print("REFUSED: DevEUI или AppKey са нули.")
        return

    record = build_record(dev_eui, join_eui, app_key, dev_num)
    crc = (record[42] << 8) | record[43]
    print("Record: %d bytes, CRC16=0x%04X" % (len(record), crc))

    cred, label = open_cred()
    print("Запис през %s ..." % label)
    cred.erase_block(0)
    n = cred.write(0, record)
    print("  wrote %d bytes" % n)

    print("Verify read-back...")
    rb = bytes(cred.read(0, len(record)))
    ok = True
    if rb != record:
        print("  [FAIL] raw bytes mismatch")
        print("    expected:", record.hex())
        print("    got:    ", rb.hex())
        ok = False
    else:
        print("  [OK] raw %d bytes match" % len(record))
    rb_crc_stored = (rb[42] << 8) | rb[43]
    rb_crc_calc = crc16_ccitt(rb[:42])
    if rb_crc_stored != rb_crc_calc:
        print("  [FAIL] CRC stored=0x%04X calc=0x%04X" % (rb_crc_stored, rb_crc_calc))
        ok = False
    else:
        print("  [OK] CRC16 0x%04X" % rb_crc_stored)
    rb_num = (rb[38] << 24) | (rb[39] << 16) | (rb[40] << 8) | rb[41]
    if rb_num == dev_num:
        print("  [OK] DeviceNumber: %d" % rb_num)
    else:
        print("  [FAIL] DeviceNumber read=%d expected=%d" % (rb_num, dev_num))
        ok = False

    print("=" * 50)
    if ok:
        print("RESULT: SUCCESS -- credentials provisioned & verified.")
        print("        Reset платката за да активира новите credentials.")
    else:
        print("RESULT: FAIL -- credentials NOT correctly provisioned!")


main()
