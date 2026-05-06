import lorawan, time
from machine import Pin, SPI
spi = SPI(3, baudrate=8000000, polarity=0, phase=0,
          sck=Pin('P111'), mosi=Pin('P109'), miso=Pin('P110'))

events = []
def ev_cb(ev):
    events.append(ev)
    t = time.ticks_diff(time.ticks_ms(), T0)
    print("  +%4dms event:" % t, ev)

mac = lorawan.Mac(region=lorawan.EU868)
# Wipe persisted NVM (MAC params include MaxRxError/MinRxSymbols
# from earlier sessions — we set them too high and that made
# RxWindow2Delay collapse below the TX-done offset).
mac.nvm_factory_reset()
mac.lorawan_init()
# Bump min_rx_symbols from the default 6 to 12 — at SF7 the default
# gives only ~6.14 ms RX window which is shorter than the LoRaWAN
# downlink preamble (8 symbols × 1.024 ms = 8.2 ms). 12 symbols
# = 12.3 ms at SF7 leaves margin for clock drift to lock onto
# the preamble. Keep MaxRxError at default 10 ms so WindowOffset
# stays small (we burned a session earlier with 500 ms).
mac.set_min_rx_symbols(64)   # SF7=65ms / SF12=2.1s — overshoot to rule out timing
# Load credentials from dataflash Block 0 (LWCR record from provision_credentials.py).
deveui, joineui, appkey = mac.load_credentials()
print("DevEUI :", deveui.hex())
print("JoinEUI:", joineui.hex())
print("AppKey :", appkey.hex())

mac.set_keys(deveui, joineui, appkey)
mac.set_event_callback(ev_cb)

# Sanity-check chip is alive
print("radio status:", hex(mac.radio_get_status()), "busy:", mac.radio_busy())

print("counters before:", mac.dbg_sync_counters())
print("=== JOIN (instrumented) ===")
T0 = time.ticks_ms()
req_st = mac.join()
print("  join() request status:", req_st)
deadline = time.ticks_add(T0, 12000)
while time.ticks_diff(deadline, time.ticks_ms()) > 0:
    mac.process()
    if mac.is_joined():
        break
    time.sleep_ms(20)
elapsed = time.ticks_diff(time.ticks_ms(), T0)
print("  joined:", mac.is_joined(), "elapsed:", elapsed, "ms")
print("  events:", events)

if mac.is_joined():
    print()
    print("=== UPLINK after join ===")
    payload = b"VK_RA4M2 hello"
    fport = 1
    try:
        st = mac.send(fport, payload, False)   # confirmed=False
        print("  send() request status:", st)
    except Exception as e:
        print("  send() raised:", e)
    T1 = time.ticks_ms()
    deadline2 = time.ticks_add(T1, 10000)
    while time.ticks_diff(deadline2, time.ticks_ms()) > 0:
        mac.process()
        time.sleep_ms(20)
    print("  events after uplink:", events)

c = mac.dbg_sync_counters()
print("counters:")
print("  pub@SetModem:%d priv@SetModem:%d  pub@RxConfig:%d priv@RxConfig:%d" %
      (c[0], c[1], c[2], c[3]))
print("  SetRx calls: %d  sync at SetRx: 0x%02X%02X" % (c[4], c[5], c[6]))
print("  iq_inverted (RxCfg arg): %d (1=INVERTED expected)" % c[7])
print("  REG_IQ_POLARITY (0x0736) after RxCfg: 0x%02X bit2=%d (0=INVERTED expected)"
      % (c[8], (c[8] >> 2) & 1))
print("  RX freq at RxCfg: %d Hz   datarate: %d" % (c[9], c[10]))
print("  symbTimeout (symbols): %d   preambleLen: %d   rxContinuous: %d" %
      (c[11], c[14], c[13]))
print("  RxTimeout at SetRx (15.625us ticks): %d  =  %.1f ms" %
      (c[12], c[12] * 0.015625))
print("  rx1_err_count: %d   TimerExists(RX2): %d  IsRunning(RX2): %d" %
      (c[16], c[15], c[17]))
print("  RX2 ReloadValue: %d ms   RX2 Timestamp: %d   now: %d   delta=%d ms" %
      (c[18], c[19], c[20], c[19] - c[20]))
print("  TX-done snapshot:")
print("    RxWindow1Delay (RX1 absolute): %d ms" % c[21])
print("    RxWindow2Delay (RX2 absolute): %d ms" % c[22])
print("    offset (now - TxDoneTime):     %d ms" % c[23])
print("    rxWin1TimeVal (relative wait): %d ms" % c[24])
print("    rxWin2TimeVal (relative wait): %d ms" % c[25])
print("  TX-DONE IRQ timestamps:")
print("    OnRadioTxDone count:     %d" % c[27])
print("    OnRadioTxDone CurTime:   %d ms (s_tick_ms when TX-done IRQ fired)" % c[26])
print("    ProcessRadioTxDone now:  %d ms" % c[28])
print("    ProcessRadioTxDone CurTime read: %d ms" % c[29])
print("    latency IRQ→Process:     %d ms" % (c[28] - c[26]))
print("    ProcessRadioTxDone count: %d" % c[30])
print("    LoRaMacProcess total calls: %d" % c[31])
print("    LoRaMacProcess 1st-call-after-TxDone-IRQ now: %d ms" % c[32])
print("    →latency from TxDone IRQ (%d) to first LMP run after: %d ms" %
      (c[26], c[32] - c[26]))
print("    OnRadioTxDone exit time: %d ms" % c[33])
print("    LoRaMacHandleIrqEvents calls where TxDone flag was set: %d" % c[34])
print("    First HandleIrqEvents-with-TxDone time: %d ms" % c[35])
print("    ProcessRadioTxDone entry time:    %d ms" % c[36])
print("    ProcessRadioTxDone after-sleep:   %d ms" % c[37])
print("    →Radio.SleepWarm took:            %d ms" % (c[37] - c[36]))
