# test_p302_toggle.py  -- изолиран тест на Timer toggle P302
# Цел: проверяваме дали P302 може да се toggle-ва с Timer callback
# Очакваме ~1000 callbacks за 500 ms при freq=2000

from machine import Pin, Timer
import time

tpin = Pin("P302", Pin.OUT, value=0)
_tval = 0
_cnt  = 0

def _toggle(t):
    global _tval, _cnt
    _tval = 1 - _tval
    tpin.value(_tval)
    _cnt += 1

# --- Тест 1: hard=True (ISR контекст) ---
print("--- Test hard=True ---")
tmr = Timer(6)
tmr.init(freq=2000, callback=_toggle, hard=True)
time.sleep_ms(500)
tmr.deinit()
print("cnt=%d  (expect ~1000)" % _cnt)

tpin.value(0)
_cnt  = 0
_tval = 0
time.sleep_ms(100)

# --- Тест 2: hard=False (scheduler) ---
print("--- Test hard=False ---")
tmr2 = Timer(5)
tmr2.init(freq=2000, callback=_toggle, hard=False)
time.sleep_ms(500)
tmr2.deinit()
print("cnt=%d  (expect ~1000)" % _cnt)

tpin.value(0)
print("Done.")
