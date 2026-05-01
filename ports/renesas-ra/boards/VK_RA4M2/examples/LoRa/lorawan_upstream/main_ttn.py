# main_ttn.py — Минимален LoRaWAN end-node за VK_RA4M2 + Wio-SX1262 + TTN.
#
# Стартиране:
#   1. Попълни DevEUI/JoinEUI/AppKey в LoRaConfig_TTN.py
#   2. mpremote cp main_ttn.py :
#   3. mpremote run main_ttn.py
#
# Какво прави:
#   - OTAA join с TTN
#   - Изпраща counter всяка минута на FPort=1
#   - Логва RSSI/SNR от gateway-а (видими в TTN Console → Live data)

import time
import gc
import micropython
from machine import Pin

# ВАЖНО: rezerve heap буфер за IRQ exception handlers (downlink RX callback).
micropython.alloc_emergency_exception_buf(200)
gc.collect()

from LoRaConfig_TTN import LoRaConfig
from LoRaWANHandler import LoRaWANHandler   # оригиналът — patched inline за VK_RA4M2

# LED1 за визуална индикация
led = Pin("LED1", Pin.OUT, value=0)

print("=" * 50)
print("VK_RA4M2 + Wio-SX1262 + TTN — LoRaWAN demo")
print("=" * 50)

# Init радиото и MAC слоя
lh = LoRaWANHandler(LoRaConfig)

# OTAA join
print()
print("Joining TTN via OTAA...")
led.on()
ok = lh.otaa()
led.off()
if not ok:
    print(">>> JOIN FAILED. Провери:")
    print("    - DevEUI/JoinEUI/AppKey в LoRaConfig_TTN.py")
    print("    - Дали gateway-ът е в обхват и регистриран в TTN")
    print("    - LIVE data в TTN Console: https://eu1.cloud.thethings.network")
    raise SystemExit(1)

print(">>> JOIN ACCEPTED!")
print("    DevAddr:", lh.DevAddr.hex() if hasattr(lh.DevAddr, "hex") else lh.DevAddr)
print()

# Цикъл — uplink всяка минута
counter = 0
while True:
    msg = "VK_RA4M2 #%05d" % counter
    print("[%d] uplink: %r" % (counter, msg))
    led.on()
    try:
        lh.send(msg, False)        # unconfirmed uplink
    except Exception as e:
        print("    send error:", e)
    led.off()
    counter += 1
    time.sleep(60)                  # 60 s между uplink-и (TTN fair use)
