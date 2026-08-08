"""P-C-100 probe (r13 Phase 2 ladder, Option A'): 5 cold-join iterations
at SF7 with set_max_rx_error(100) — moves RX1 open ~90 ms earlier."""
import time, sys
import lorawan
from machine import Pin, SPI
from credentials import DEV_EUI_A, JOIN_EUI, APP_KEY_A

print("PC100_BOOT")
spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin("P111"), mosi=Pin("P109"), miso=Pin("P110"))

joined_count = 0
rx1_open_sum = 0
rx2_skipped_total = 0
rx1_first = 0

for i in range(5):
    mac = lorawan.Mac(region=lorawan.EU868)
    mac.nvm_factory_reset()
    mac.lorawan_init()
    mac.init_defaults()
    mac.set_adr(False)
    mac.set_event_callback(lambda ev: None)
    mac.set_keys(DEV_EUI_A, JOIN_EUI, APP_KEY_A)

    t0 = time.ticks_ms()
    mac.join(5)
    deadline = time.ticks_add(t0, 15000)
    joined = False
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        mac.process()
        if mac.is_joined():
            joined = True
            break
        time.sleep_ms(100)

    diag = mac.rx_window_diag()
    elapsed = time.ticks_diff(time.ticks_ms(), t0)
    print("PC100_ITER %d joined=%s diag=%r elapsed_ms=%d" %
          (i, joined, diag, elapsed))

    if joined:
        joined_count += 1
    rx1_open_sum += diag[0]
    if i == 0:
        rx1_first = diag[0]
    if diag[5] > rx2_skipped_total:
        rx2_skipped_total = diag[5]

    # Settle 10 s before next iter (gateway TX queue cooldown).
    if i < 4:
        end = time.ticks_add(time.ticks_ms(), 10000)
        while time.ticks_diff(end, time.ticks_ms()) > 0:
            mac.process()
            time.sleep_ms(100)

PA_RX1_OPEN_US = 5260000   # P-A baseline reference (~5260 ms)
rx1_avg = rx1_open_sum // 5
shift_ms = (PA_RX1_OPEN_US - rx1_avg) // 1000
print("VERDICT_PC100 joined_count=%d/5 rx1_open_avg_us=%d "
      "rx1_open_shift_vs_pa_ms=%d rx2_skipped_total=%d" %
      (joined_count, rx1_avg, shift_ms, rx2_skipped_total))
sys.exit(0)
