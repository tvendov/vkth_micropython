# ws2812_async_test.py — верификация на write_async() + Timer(-1) + sync()
#
# ЦЕЛЬ: доказва, че CPU НЕ блокира по време на WS2812 предаване.
#
# КАК ДА ЧЕТЕШ РЕЗУЛТАТА:
#   Свържи логически анализатор или осцилоскоп на TEST_PIN (P107).
#
#   С write_async() (очаквано):
#     [HIGH ~5µs] [LOW ~5µs] [rapid toggle ~100µs период] [HIGH ~5µs] ...
#     → НЯМА пауза > 100µs → CPU е свободен!
#
#   Ако write_async() блокира (регрес):
#     [HIGH] ---- 1550µs ПАУЗА (нищо) ---- [LOW] ...
#     → Ясно видима мъртва зона на scope-а
#
# ХАРДУЕР: WS2812 лента на P112 (SCI2), тест пин P107.

from machine import Pin, WS2812, Timer
import time

# ---------------------------------------------------------------------------
TEST_PIN  = Pin("P107", Pin.OUT, value=0)   # Scope/LA тук
DATA_PIN  = Pin("P112")
N_PIXELS  = 32
# ---------------------------------------------------------------------------

strip = WS2812(pixel_count=N_PIXELS, pin=DATA_PIN, channels=3)

# Тестов pixel buffer — постепенен червен градиент
pixel_buf = bytearray(N_PIXELS * 3)
for i in range(N_PIXELS):
    pixel_buf[i * 3] = i * 8          # R нараства
    pixel_buf[i * 3 + 1] = 0          # G
    pixel_buf[i * 3 + 2] = 0          # B

# Timer(-1) нотификация — нула AGT канали, нула IRQ вектори
ws_done  = bytearray(1)               # mutable → callback може да пише
latch_t  = Timer(-1)

def ws_cb(t):
    ws_done[0] = 1                     # Python scheduler context — безопасно

# ---------------------------------------------------------------------------
# Статистика
frames   = 0
errors   = 0
t_start  = time.ticks_ms()

print("=== WS2812 async test ===")
print("TEST_PIN =", TEST_PIN, " DATA_PIN =", DATA_PIN)
print("Свържи scope на TEST_PIN:")
print("  write_async OK  → НЯМА пауза >200µs между тогловете")
print("  write() регрес  → ПАУЗА ~1550µs ясно видима")
print("Ctrl+C за спиране.\n")

try:
    while True:
        # ── Маркер START ─────────────────────────────────────────────────
        TEST_PIN.value(1)              # Вдигаме → маркер "write_async start"

        strip.write_buf(pixel_buf)
        strip.write_async()            # ← ТРЯБВА да е <20µs! (DTC старт + return)

        TEST_PIN.value(0)              # Сваляме ВЕДНАГА → gap HIGH = write_async time
                                       # Ако gap > 200µs → write_async блокира!

        # ── Нотификация чрез Timer(-1) ────────────────────────────────────
        ws_done[0] = 0
        latch_t.init(mode=Timer.ONE_SHOT, period=2, callback=ws_cb)

        # ── CPU СВОБОДЕН — toggle докато чакаме ─────────────────────────
        # Тук DTC предава 518 байта @ 2.857MHz = ~1.455ms
        # Timer(-1) ще fire след ~2ms и ще сети ws_done[0]=1
        deadline = time.ticks_add(time.ticks_ms(), 6)
        while not ws_done[0]:
            TEST_PIN.toggle()          # Бързи тоглове → CPU свободен!
            if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
                errors += 1
                print("ГРЕШКА: ws_done не е сетнат след 6ms! кадър=", frames)
                break

        # ── Sync — почистване, обикновено no-op (TEND вече е set) ────────
        TEST_PIN.value(1)              # Маркер "sync start"
        strip.sync()                   # Disable SCI TX + pin→LOW (<<10µs)
        TEST_PIN.value(0)              # Маркер "sync done"

        frames += 1

        # ── Периодичен отчет ─────────────────────────────────────────────
        if frames % 500 == 0:
            elapsed = time.ticks_diff(time.ticks_ms(), t_start)
            fps = frames * 1000 // elapsed if elapsed > 0 else 0
            print(f"OK: {frames} кадъра, {fps} fps, грешки={errors}")

except KeyboardInterrupt:
    latch_t.deinit()
    strip.sync()
    strip.fill((0, 0, 0))
    strip.write()
    strip.deinit()
    elapsed = time.ticks_diff(time.ticks_ms(), t_start)
    fps = frames * 1000 // elapsed if elapsed > 0 else 0
    print(f"\nСпряно. {frames} кадъра, {fps} fps, грешки={errors}")
