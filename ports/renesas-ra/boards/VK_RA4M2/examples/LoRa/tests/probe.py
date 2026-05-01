# Електрическа диагностика на Wio-SX1262 wiring без init на драйвера.
# Стартиране:
#   import probe; probe.run()

from machine import Pin
import time
import _config as cfg


def _read_pin(name, pin_name):
    p = Pin(pin_name, Pin.IN)
    v = p.value()
    print("  %-6s %-6s = %d" % (name, pin_name, v))
    return v


def run():
    print("=== Wio-SX1262 wiring probe ===")
    print("Pin levels at boot (без reset, без SPI):")
    busy = _read_pin("BUSY",  cfg.PIN_BUSY)
    dio1 = _read_pin("DIO1",  cfg.PIN_DIO1)

    # CS и RESET са изходи; четем като входове засега
    _read_pin("CS",    cfg.PIN_CS)
    _read_pin("RESET", cfg.PIN_RST)
    _read_pin("MISO",  cfg.PIN_MISO)
    _read_pin("MOSI",  cfg.PIN_MOSI)
    _read_pin("SCK",   cfg.PIN_SCK)

    print()
    print("Reset последователност:")
    rst = Pin(cfg.PIN_RST, Pin.OUT, value=1)
    cs  = Pin(cfg.PIN_CS,  Pin.OUT, value=1)
    busy_p = Pin(cfg.PIN_BUSY, Pin.IN)
    dio1_p = Pin(cfg.PIN_DIO1, Pin.IN)

    print("  BUSY before reset =", busy_p.value())
    rst.value(0)
    time.sleep_ms(20)
    print("  BUSY during reset =", busy_p.value())
    rst.value(1)

    # Чакаме BUSY да падне до 100 ms
    t0 = time.ticks_ms()
    while busy_p.value() and time.ticks_diff(time.ticks_ms(), t0) < 200:
        pass
    dt = time.ticks_diff(time.ticks_ms(), t0)
    final = busy_p.value()
    print("  BUSY after reset  =", final, "(", dt, "ms след освобождаване на RESET)")

    if final == 0:
        print("OK — BUSY падна, чипът отговаря")
    else:
        print("FAIL — BUSY остана HIGH; проверете 3V3, RESET, BUSY кабелите")

    print("DIO1 final =", dio1_p.value())

    print()
    print("Базов SPI тест (получаване на статус) — SoftSPI:")
    from machine import SoftSPI
    spi = SoftSPI(baudrate=1_000_000, polarity=0, phase=0,
                  sck=Pin(cfg.PIN_SCK), mosi=Pin(cfg.PIN_MOSI), miso=Pin(cfg.PIN_MISO))
    # SX126x команда GetStatus: 0xC0 + 1 NOP байт; връща статус в MISO
    cs.value(0)
    rx = bytearray(2)
    spi.write_readinto(b"\xC0\x00", rx)
    cs.value(1)
    print("  SPI raw:", [hex(b) for b in rx])
    # Втория байт е статуса
    status = rx[1]
    chip_mode = (status >> 4) & 0x07
    cmd_status = (status >> 1) & 0x07
    print("  status byte = 0x%02X, chip_mode=%d, cmd_status=%d" % (status, chip_mode, cmd_status))

    if status not in (0x00, 0xFF):
        print("OK — чипът отговаря със смислен статус")
    else:
        print("WARN — статусът %02X е подозрителен (0x00=BUSY, 0xFF=липсва MISO)" % status)
