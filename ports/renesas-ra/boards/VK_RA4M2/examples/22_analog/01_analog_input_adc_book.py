from machine import ADC
import time


ADC_INPUTS = (
    ("ADC_+3.3V_R_UL", "P014"),
    ("ADC_+3.3V_R", "P000"),
    ("ADC_+5V_R", "P001"),
    ("ADC_+5V_R_UR", "P002"),
    ("ADC_+3V_R", "P013"),
    ("ADC_+1.8V_R", "P015"),
    ("ADC_+1.2V_R", "P500"),
)


def read_channel(name, channel):
    adc = ADC(channel)
    raw = adc.read()
    u16 = adc.read_u16()
    print(name, "raw =", raw, "u16 =", u16)


print("=== VK_RA4M2 ADC smoke test ===")
print("Schematic ADC inputs:")
for sample_index in range(2):
    print("Sample", sample_index + 1)
    for label, pin_name in ADC_INPUTS:
        read_channel(label + " " + pin_name, pin_name)
    time.sleep_ms(300)

print("ADC smoke test complete.")
