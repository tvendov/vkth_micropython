# Споделена конфигурация за всички LoRa тестове на VK_RA4M2 + Wio-SX1262.
#
# Pin map: XIAO RA4M1 еквивалент → VK_RA4M2 чип-пинове.

# Pin map (потвърдено от работещия demos/tx_cw/tx_cw_demo.py):
#   LORA_DIO1   → P015
#   LORA_RST    → P001
#   LORA_BUSY   → P002
#   LORA_NSS    → P206
#   LORA_RF_SW1 → P100   (MUST drive HIGH за enable на RF switch — Seeed forum)
#   SCK/MOSI/MISO → P111/P109/P110 on SPI(3)

SPI_BUS    = 3
PIN_SCK    = "P111"
PIN_MOSI   = "P109"
PIN_MISO   = "P110"
PIN_CS     = "P206"           # NSS — корекция от P006
PIN_RST    = "P001"
PIN_BUSY   = "P002"
PIN_DIO1   = "P015"
PIN_RF_SW  = "P100"           # /CTRL на PE4529 RF switch — drive HIGH постоянно

# Радио параметри (EU868)
FREQ_KHZ      = 868100        # 868.1 MHz — EU868 канал 0
FREQ_MHZ      = 868.1
SF            = 7             # spreading factor 7..12
BW_KHZ        = 125           # 125 / 250 / 500
BW_STR        = "125"         # за lora-sx126x ("125" / "250" / "500")
CR            = 5             # 5 = 4/5, 6 = 4/6, 7 = 4/7, 8 = 4/8
TX_POWER_DBM  = 14            # +14 dBm — макс. EU868 ERP
PREAMBLE_LEN  = 8
CRC_EN        = True
SYNC_WORD     = 0x12          # 0x12 = частен LoRa, 0x34 = LoRaWAN public

# Wio-SX1262 хардуерни специфики
TCXO_MV       = 1800          # 1.8 V TCXO през DIO3 (Wio-SX1262)
DIO2_RF_SW    = True          # Wio-SX1262 използва DIO2 за RF switch

# Регулаторни параметри (ETSI EU868)
DUTY_CYCLE_PCT = 1.0          # 1% за g1 подканала
MIN_TX_GAP_MS  = 1000         # минимум 1 s между пакети при SF7 32B (ToA ~36 ms)

# Тестови параметри
DEFAULT_PAYLOAD = b"VK_RA4M2 hello %05d"
PINGPONG_TIMEOUT_MS = 2000
STRESS_PACKETS = 1000
