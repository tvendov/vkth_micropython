# LoRaConfig_TTN.py — TTN credentials за VK_RA4M2 + Wio-SX1262.
#
# Регистрирай устройство в TTN Console (https://eu1.cloud.thethings.network):
#   1. Create Application
#   2. Add end device → manual entry:
#       - Frequency plan: EU_863_870_TTN (стандартен EU868)
#       - LoRaWAN version: MAC V1.0.3
#       - Activation: OTAA
#       - Device class: Class A
#   3. TTN ще генерира DevEUI / JoinEUI / AppKey — копирай ги тук в MSB ред.
#
# Стойностите по-долу са PLACEHOLDER. Замени с реалните от TTN Console.

class LoRaConfig:
    """LoRaWAN credentials for OTAA join with TTN."""

    # ABP не се ползва (OTAA only за TTN); празни placeholder-и за compat.
    DevAddrABP = [0x00, 0x00, 0x00, 0x00]
    NwkSKeyABP = [0x00] * 16
    AppSKeyABP = [0x00] * 16

    # OTAA — попълни от TTN Console (виж началото на файла за инструкции).
    # Стойностите по-долу са PLACEHOLDER. Замени с реалните от TTN Console.
    DevEUI  = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    JoinEUI = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    AppKey  = [0x00] * 16
