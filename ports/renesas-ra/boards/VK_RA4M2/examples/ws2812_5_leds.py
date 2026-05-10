# Пример: WS2812 тест за 5 LED-а върху VK_RA4M2.
# Важно за този setup:
# - P500 включва захранването/enable-а на WS2812 модула.
# - P112 е data/DIN линията към първия LED.
# - machine.WS2812 използва SCI2 TX-only backend, затова не ползвай UART(2)/SPI(2) едновременно.

from machine import Pin, WS2812
import time

POWER_PIN = "P500"
DATA_PIN = "P112"
PIXEL_COUNT = 5
SYMBOL_BITS = 6
BRIGHT = 40
STEP_MS = 150
HOLD_MS = 700

vcc = Pin(POWER_PIN, Pin.OUT, value=1)
time.sleep_ms(200)

strip = WS2812(pixel_count=PIXEL_COUNT, pin=Pin(DATA_PIN), channels=3, symbol_bits=SYMBOL_BITS)

colors = [
    (BRIGHT, 0, 0),
    (0, BRIGHT, 0),
    (0, 0, BRIGHT),
    (BRIGHT, BRIGHT, 0),
    (0, BRIGHT, BRIGHT),
    (BRIGHT, 0, BRIGHT),
    (BRIGHT // 2, BRIGHT // 2, BRIGHT // 2),
]


def set_all(color):
    for i in range(PIXEL_COUNT):
        strip[i] = color


def all_off():
    set_all((0, 0, 0))
    strip.write()


def show_all(color):
    set_all(color)
    strip.write()


def chase(color):
    for i in range(PIXEL_COUNT):
        set_all((0, 0, 0))
        strip[i] = color
        strip.write()
        time.sleep_ms(STEP_MS)


print("=== WS2812 5 LEDs test ===")
print("POWER =", POWER_PIN, "DATA =", DATA_PIN, "PIXELS =", PIXEL_COUNT)
print("SYMBOL_BITS =", SYMBOL_BITS, "baudrate = auto")
print("P500 must stay HIGH while testing.")

try:
    while True:
        for color in colors:
            show_all(color)
            time.sleep_ms(HOLD_MS)
        for color in colors[:3]:
            chase(color)
        all_off()
        time.sleep_ms(HOLD_MS)
except KeyboardInterrupt:
    all_off()
    strip.deinit()
    print("WS2812 5 LEDs test stopped; LEDs are off.")
