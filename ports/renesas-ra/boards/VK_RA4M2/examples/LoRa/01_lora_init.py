# Пример: SX1262 LoRa инициализация на VK_RA4M2 с Wio-SX1262 модул.
#
# Адаптиран от varna9000/micropython-reticulum (XIAO ESP32-S3 + Wio-SX1262)
# към VK_RA4M2 + Wio-SX1262 Header Board, използвайки XIAO RA4M1 pin map.
#
# Източник: https://github.com/varna9000/micropython-reticulum
#           firmware/urns/interfaces/lora.py
#
# Драйвер: официалният `lora-sx126x` от micropython-lib
#   (НЕ ehong-tl/micropySX126X — този е по-чист и поддържа
#   dio2_rf_sw и dio3_tcxo_millivolts директно).
#
# Инсталация на драйвера:
#   mpremote mip install lora-sx126x
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

from machine import SPI, Pin
from lora import SX1262

# Pin map за VK_RA4M2 (XIAO RA4M1 еквивалент)
SPI_BUS  = 1
PIN_SCK  = "P111"
PIN_MOSI = "P109"
PIN_MISO = "P110"
PIN_CS   = "P006"
PIN_RST  = "P001"
PIN_BUSY = "P002"
PIN_DIO1 = "P000"

spi = SPI(
    SPI_BUS,
    baudrate=2_000_000,
    sck=Pin(PIN_SCK),
    mosi=Pin(PIN_MOSI),
    miso=Pin(PIN_MISO),
)

modem = SX1262(
    spi=spi,
    cs=Pin(PIN_CS, Pin.OUT, value=1),
    busy=Pin(PIN_BUSY, Pin.IN),
    dio1=Pin(PIN_DIO1, Pin.IN),
    reset=Pin(PIN_RST, Pin.OUT, value=1),
    dio2_rf_sw=True,                  # Wio-SX1262: DIO2 управлява RF switch
    dio3_tcxo_millivolts=1800,        # Wio-SX1262: TCXO на 1.8 V през DIO3
    lora_cfg={
        "freq_khz":     868000,       # EU868
        "sf":           7,            # spreading factor
        "bw":           "125",        # 125 kHz
        "coding_rate":  5,            # CR 4/5
        "output_power": 14,           # +14 dBm (макс. EU868 ERP)
        "preamble_len": 8,
        "crc_en":       True,
        "syncword":     0x12,         # 0x12 = частен LoRa (НЕ LoRaWAN!)
    },
)

print("=== SX1262 LoRa инициализация ===")
print("SPI bus:", SPI_BUS, "@2MHz")
print("Pins: SCK={} MOSI={} MISO={} CS={} RST={} BUSY={} DIO1={}".format(
    PIN_SCK, PIN_MOSI, PIN_MISO, PIN_CS, PIN_RST, PIN_BUSY, PIN_DIO1))
print("Frequency: 868.0 MHz, SF7, BW125, CR4/5, +14 dBm")
print("Modem обектът:", modem)

# Изпращане на тестов пакет
payload = b"hello VK_RA4M2"
print("TX:", payload)
modem.send(payload)

# Слушане в RX режим за следващия пакет
print("RX listening...")
modem.start_recv(continuous=True)
rx = modem.poll_recv()
if rx:
    print("RX:", rx)
else:
    print("Никакъв пакет в краткия RX прозорец.")

modem.sleep()
print("Modem в sleep.")
