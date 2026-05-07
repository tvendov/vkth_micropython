import lorawan, time
mac = lorawan.Mac(region=lorawan.EU868)
mac.lorawan_init()
print("SPI ID:", mac.dbg_spi_id())
# Don't reset — let lorawan_init handle everything
time.sleep_ms(50)
print("busy:", mac.radio_busy(), "status:", hex(mac.radio_get_status()))
s0 = mac.radio_read_reg(0x0740)
s1 = mac.radio_read_reg(0x0741)
print("sync after init: 0x%02X 0x%02X" % (s0, s1))
# Read XTA/XTB trim — should be non-zero (0x12 default per datasheet)
print("XTA trim (0x0911):", hex(mac.radio_read_reg(0x0911)))
print("XTB trim (0x0912):", hex(mac.radio_read_reg(0x0912)))
# Try writing a known pattern to a scratch register & read back
mac.radio_write_reg(0x0740, 0x55)
mac.radio_write_reg(0x0741, 0xAA)
print("after write 0x55/0xAA -> read:", hex(mac.radio_read_reg(0x0740)),
      hex(mac.radio_read_reg(0x0741)))
