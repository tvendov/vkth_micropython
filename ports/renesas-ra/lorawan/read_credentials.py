"""Чете и показва CRED записа от Data Flash (block 0) на VK_RA4M2.

Стартиране:
    mpremote connect COMxx run read_credentials.py

Чете през `dataflash.region("CRED")` (region-aware). Ако region() още
не съществува → пада към `dataflash.read(0, ...)` (цял чип).

Показва DevEUI / JoinEUI / AppKey / device_number и валидира magic + CRC.
CRED layout v2 (виж provision_credentials.py / DATA_FLASH.md).
"""

MAGIC       = b"LWCR"
RECORD_LEN  = 44
PAYLOAD_LEN = 42


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


def open_cred():
    import dataflash
    region = getattr(dataflash, "region", None)
    if region is not None:
        return (region("CRED"), "dataflash.region('CRED')")
    return (dataflash, "dataflash (цял чип, fallback)")


def main():
    cred, label = open_cred()
    print("=== Read CRED <- Data Flash (%s) ===" % label)
    rb = bytes(cred.read(0, RECORD_LEN))

    if rb[0:4] != MAGIC:
        print("  [FAIL] magic = %r (очаквано 'LWCR') — CRED празен/невалиден" % rb[0:4])
        return
    print("  [OK] magic 'LWCR'")

    version = rb[4]
    print("  version: 0x%02X" % version)

    crc_stored = (rb[42] << 8) | rb[43]
    crc_calc = crc16_ccitt(rb[:42])
    if crc_stored != crc_calc:
        print("  [FAIL] CRC stored=0x%04X calc=0x%04X — данните са повредени" %
              (crc_stored, crc_calc))
        return
    print("  [OK] CRC16 0x%04X" % crc_stored)

    dev_eui  = rb[6:14]
    join_eui = rb[14:22]
    app_key  = rb[22:38]
    dev_num  = (rb[38] << 24) | (rb[39] << 16) | (rb[40] << 8) | rb[41]

    print("-" * 50)
    print("  DevEUI       : %s" % dev_eui.hex())
    print("  JoinEUI      : %s" % join_eui.hex())
    print("  AppKey       : %s" % app_key.hex())
    print("  DeviceNumber : %d" % dev_num)
    print("-" * 50)
    print("RESULT: CRED валиден.")


main()
