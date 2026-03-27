# Интегриран пример: 30 WS2812 + 3 бутона + RetroSynth звуци.
# SW1 (P400) = смяна на цвят + coin, P301 = смяна на анимация + jump,
# P302 = on/off toggle + explosion.
# Ресурси: WS2812 data=P112, power=P500, DAC=P014 (DA0).
from machine import Pin, WS2812
import time
from retro_synth import RetroSynth

# --- Хардуерни константи ---
LED_COUNT = 30
DATA_PIN = "P112"
POWER_PIN = "P500"
DAC_PIN = "P014"

# --- Инициализация ---
vcc = Pin(POWER_PIN, Pin.OUT, value=1)
time.sleep_ms(100)
strip = WS2812(pixel_count=LED_COUNT, pin=Pin(DATA_PIN), channels=3)
synth = RetroSynth(DAC_PIN)

btn1 = Pin("SW1", Pin.IN, Pin.PULL_UP)   # Бутон 1: цвят
btn2 = Pin("P301", Pin.IN, Pin.PULL_UP)  # Бутон 2: анимация
btn3 = Pin("P302", Pin.IN, Pin.PULL_UP)  # Бутон 3: on/off

# --- Shift-register debounce (8-bit) ---
SAMPLE_MS = 10
sr1 = 0xFF
sr2 = 0xFF
sr3 = 0xFF

def debounce_press(sr_val, raw):
    sr_val = ((sr_val << 1) | raw) & 0xFF
    pressed = (sr_val == 0x00)
    return sr_val, pressed

# --- Цветова палитра ---
COLORS = [
    (40, 0, 0),    # Червено
    (0, 40, 0),    # Зелено
    (0, 0, 40),    # Синьо
    (0, 40, 40),   # Циан
    (40, 0, 40),   # Магента
    (40, 40, 0),   # Жълто
]
color_idx = 0

# --- Анимационни режими ---
MODE_CHASE = 0
MODE_PULSE = 1
MODE_RAINBOW = 2
MODE_NAMES = ["chase", "pulse", "rainbow"]
anim_mode = MODE_CHASE

# --- Състояние ---
leds_on = True
chase_pos = 0
pulse_brightness = 0
pulse_dir = 1
rainbow_offset = 0

def wheel(pos):
    """Цветово колело 0-255 -> (R, G, B) с плавни преходи."""
    pos = pos % 256
    if pos < 85:
        return (pos * 3 // 8, (255 - pos * 3) // 8, 0)
    elif pos < 170:
        pos -= 85
        return ((255 - pos * 3) // 8, 0, pos * 3 // 8)
    else:
        pos -= 170
        return (0, pos * 3 // 8, (255 - pos * 3) // 8)

def clear_strip():
    for i in range(LED_COUNT):
        strip[i] = (0, 0, 0)

def anim_chase(color, pos):
    clear_strip()
    for j in range(3):
        idx = (pos + j) % LED_COUNT
        r = color[0] * (3 - j) // 3
        g = color[1] * (3 - j) // 3
        b = color[2] * (3 - j) // 3
        strip[idx] = (r, g, b)

def anim_pulse(color, brightness):
    scale = brightness
    for i in range(LED_COUNT):
        strip[i] = (color[0] * scale // 255,
                     color[1] * scale // 255,
                     color[2] * scale // 255)

def anim_rainbow(offset):
    for i in range(LED_COUNT):
        strip[i] = wheel((i * 256 // LED_COUNT + offset) % 256)

# --- Главен цикъл ---
print("=== Integrated: 30 LEDs + 3 buttons + SFX ===")
print("SW1=color+coin, P301=anim+jump, P302=on/off+explosion")
print("Ctrl+C to stop.")

frame_counter = 0
FRAMES_PER_ANIM = 3  # update animation every 3 polls (30ms)

try:
    while True:
        # Debounce трите бутона
        raw1 = btn1.value()
        raw2 = btn2.value()
        raw3 = btn3.value()
        sr1, press1 = debounce_press(sr1, raw1)
        sr2, press2 = debounce_press(sr2, raw2)
        sr3, press3 = debounce_press(sr3, raw3)

        if press1:
            color_idx = (color_idx + 1) % len(COLORS)
            print("Color:", color_idx, COLORS[color_idx])
            synth.coin()
            sr1 = 0xFF

        if press2:
            anim_mode = (anim_mode + 1) % 3
            print("Anim:", MODE_NAMES[anim_mode])
            synth.jump()
            sr2 = 0xFF

        if press3:
            leds_on = not leds_on
            print("LEDs:", "ON" if leds_on else "OFF")
            synth.explosion()
            sr3 = 0xFF

        # Анимация на всеки 3-ти poll (~30ms)
        frame_counter += 1
        if frame_counter >= FRAMES_PER_ANIM:
            frame_counter = 0
            if leds_on:
                c = COLORS[color_idx]
                if anim_mode == MODE_CHASE:
                    anim_chase(c, chase_pos)
                    chase_pos = (chase_pos + 1) % LED_COUNT
                elif anim_mode == MODE_PULSE:
                    anim_pulse(c, pulse_brightness)
                    pulse_brightness += pulse_dir * 15
                    if pulse_brightness >= 255:
                        pulse_brightness = 255
                        pulse_dir = -1
                    elif pulse_brightness <= 0:
                        pulse_brightness = 0
                        pulse_dir = 1
                elif anim_mode == MODE_RAINBOW:
                    anim_rainbow(rainbow_offset)
                    rainbow_offset = (rainbow_offset + 3) % 256
                strip.write()
            else:
                clear_strip()
                strip.write()

        time.sleep_ms(SAMPLE_MS)

except KeyboardInterrupt:
    pass

synth.deinit()
clear_strip()
strip.write()
strip.deinit()
print("Done.")

