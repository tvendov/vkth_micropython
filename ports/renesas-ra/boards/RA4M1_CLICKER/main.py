from machine import I2C, Pin; import time
i2c0 = I2C(0, freq=100_000)
i2c1 = I2C(1, freq=100_000)
trig = Pin("P409", Pin.OUT)  # LED1 като trigger
while True:
    trig.value(1)
    try: i2c0.writeto(0x55, b"\x00\x11\x22\x33")
    except OSError: pass
    try: i2c1.writeto(0x55, b"\xAA\xBB\xCC\xDD")
    except OSError: pass
    trig.value(0)
    time.sleep_ms(50)