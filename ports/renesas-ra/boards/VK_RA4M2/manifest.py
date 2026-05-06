# We do not want to include default frozen modules,
# sdcard removed to save flash space
import os

include("$(MPY_DIR)/extmod/asyncio")
require("neopixel")

# Single selector — стойността идва от mpconfigboard.mk (export-ва се от
# Makefile като MICROPY_HW_LORA_STACK). Само "python" stack изисква freeze
# на тези модули; "renesas" експонира `lorawan` C-модул директно, без
# Python freeze. Backward compat: ако stack-ът не е зададен, четем стария
# MICROPY_HW_ENABLE_LORA.
_LORA_STACK = os.environ.get("MICROPY_HW_LORA_STACK", "")
if not _LORA_STACK:
    _LORA_STACK = "python" if os.environ.get("MICROPY_HW_ENABLE_LORA", "0") == "1" else "none"

if _LORA_STACK == "renesas":
    # Phase 6c — async wrapper around the C `lorawan` module. Provides
    # AsyncMac with `await join()` / `await send()` / `await wait_recv()`
    # built on the C event_callback infrastructure.
    _LORAWAN_PY = "$(PORT_DIR)/lorawan/python"
    freeze(_LORAWAN_PY, "lorawan_async.py")

if _LORA_STACK == "python":
    # Freeze micropySX126X (PHY) и GereZoltan/LoRaWAN (MAC) в firmware,
    # защото комбинираният import footprint е > 65 KB heap (Wio-SX1262 + LoRaWAN).
    # Така ROM ги съхранява и heap остава свободен за runtime state.
    _LORA_DIR = "$(BOARD_DIR)/examples/LoRa/_upstream"
    _LORAWAN_DIR = "$(BOARD_DIR)/examples/LoRa/lorawan_upstream"
    freeze(_LORA_DIR, "_sx126x.py")
    freeze(_LORA_DIR, "sx126x.py")        # patched: SoftSPI вместо hardware SPI(1)
    freeze(_LORA_DIR, "sx1262.py")
    freeze(_LORAWAN_DIR, "LoRaWANHandler.py")
    # LoRaWAN package — пътищата трябва да са относителни към _LORAWAN_DIR
    # за да се frozen-ат с правилния namespace (LoRaWAN.AES_CMAC и т.н.).
    freeze(_LORAWAN_DIR, "LoRaWAN/__init__.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/AES_CMAC.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/DataPayload.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/Direction.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/FHDR.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/JoinAcceptPayload.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/JoinRequestPayload.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/MHDR.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/MacPayload.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/MalformedPacketException.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/PhyPayload.py")
    freeze(_LORAWAN_DIR, "LoRaWAN/maes.py")
