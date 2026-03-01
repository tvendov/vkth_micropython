from machine import Pin, PWM
import time

# GPT0 A/B on VK_RA4M2
pwm_a = PWM(Pin("P107"), freq=1000, duty=25)
pwm_b = PWM(Pin("P106"), freq=1000, duty=75)

print("STEP1: A=25%, B=75% @ 1kHz (same GPT channel)")
time.sleep(2)

# Stop only B. A must keep running.
pwm_b.duty(0)
print("STEP2: B stopped, A must still run")
time.sleep(2)

# Restart B without touching A.
pwm_b.duty(40)
print("STEP3: B restarted at 40%, A must stay active")
time.sleep(2)

# Change channel frequency from A side. B must stay active and follow same freq.
pwm_a.freq(2000)
print("STEP4: both outputs now 2kHz")
time.sleep(2)

# Deinit A only. B must keep running.
pwm_a.deinit()
print("STEP5: A deinit, B must still run")

while True:
    time.sleep(1)
    print("alive")
