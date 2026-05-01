# Пример: SX1262 LoRa инициализация на VK_RA4M2 с Wio-SX1262 модул.
#
# Алтернативна реализация чрез ehong-tl/micropySX126X.
# (Виж 01_lora_init.py за вариант с официалния lora-sx126x.)
#
# Източник на драйвера: https://github.com/ehong-tl/micropySX126X
# Файлове за копиране в /flash/lib/ :
#   _sx126x.py, sx126x.py, sx1262.py
#
# Свързване (XIAO RA4M1 еквивалент → VK_RA4M2):
#   D8  SCK   -> P111
#   D10 MOSI  -> P109
#   D9  MISO  -> P110
#   D4  NSS   -> P006
#   D2  RESET -> P001
#   D3  BUSY  -> P002
#   D1  DIO1  -> P000  (IRQ0 канал)
#   D5  DIO2  -> не се свързва (вътрешен RF switch)

from sx1262 import SX1262

# Pin map за VK_RA4M2 (XIAO RA4M1 еквивалент)
SPI_BUS  = 1
PIN_SCK  = "P111"
PIN_MOSI = "P109"
PIN_MISO = "P110"
PIN_CS   = "P006"
PIN_RST  = "P001"
PIN_BUSY = "P002"
PIN_DIO1 = "P000"

# micropySX126X конструира SPI вътрешно от подадените пин-имена
lora = SX1262(
    spi_bus=SPI_BUS,
    clk=PIN_SCK, mosi=PIN_MOSI, miso=PIN_MISO,
    cs=PIN_CS, irq=PIN_DIO1, rst=PIN_RST, gpio=PIN_BUSY,
)

err = lora.begin(
    freq=868.0,             # EU868
    bw=125.0,               # 125 kHz
    sf=7,                   # spreading factor
    cr=5,                   # CR 4/5 (стойност 5..8)
    syncWord=0x12,          # 0x12 = частен LoRa (НЕ LoRaWAN!)
    power=14,               # +14 dBm
    currentLimit=60.0,      # 60 mA
    preambleLength=8,
    implicit=False,
    crcOn=True,
    txIq=False,
    rxIq=False,
    tcxoVoltage=1.8,        # Wio-SX1262: TCXO 1.8 V (НЕ default 1.6 V!)
    useRegulatorLDO=False,
    blocking=True,
)

print("=== SX1262 LoRa инициализация (micropySX126X) ===")
print("Pins: SCK={} MOSI={} MISO={} CS={} RST={} BUSY={} DIO1={}".format(
    PIN_SCK, PIN_MOSI, PIN_MISO, PIN_CS, PIN_RST, PIN_BUSY, PIN_DIO1))
print("Frequency: 868.0 MHz, SF7, BW125, CR4/5, +14 dBm, TCXO 1.8 V")
print("begin() err =", err, "(0 = ERR_NONE)")

# Изпращане на тестов пакет
payload = b"hello VK_RA4M2"
print("TX:", payload)
lora.send(payload)

# Кратко RX чакане
print("RX listening...")
msg, rx_err = lora.recv(timeout_en=True, timeout_ms=2000)
if rx_err == 0 and msg:
    print("RX:", msg, "RSSI=", lora.getRSSI(), "SNR=", lora.getSNR())
else:
    print("Никакъв пакет в RX прозореца. err =", rx_err)
