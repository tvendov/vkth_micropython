"""
VK_RA6M3 SDR full-chain scope test.

Streams the demodulated audio to DA0 (P014) continuously so an oscilloscope on
P014 shows the live output while a signal is applied to the I/Q inputs.

Pins (ARCH-ADC-002):  I = P000 (AN000), Q = P004 (AN100), audio out = P014 (DA0).

Chain:  I/Q capture -> DC removal + x2 decimation + I/Q imbalance ->
        demod (am/usb/lsb/cw) -> RMS AGC (PI) -> volume -> DAC.stream_from (DMAC).

Edit the CONFIG block, then:  mpremote connect COM18 run sdr_scope_test.py
Do a J-Link reset before running (this board needs it). Ctrl-C / reset to stop.
"""

from machine import IQADC, ADC, DAC
import time

# ---- CONFIG -------------------------------------------------------------
RATE   = 48000          # ADC sample rate (audio = RATE/2 = 24 kHz)
BLOCK  = 128
DEMOD  = "am"           # "am" | "usb" | "lsb" | "cw" | "off"
AGC    = "off"          # "off" | "fast" | "slow" | "manual"
AGC_GAIN = 1.0          # used only when AGC == "manual"
VOLUME = 1.0            # master output level, 0..8
IQ_CORR = None          # None, or (amp, phase) e.g. (1.0, 0.0)
# ------------------------------------------------------------------------


def main():
    iq = IQADC("P000", "P004", rate=RATE, block=BLOCK, pga=ADC.PGA_BYPASS)
    iq.start()

    if IQ_CORR is not None:
        iq.iq_correction(enable=True, amp=IQ_CORR[0], phase=IQ_CORR[1])

    iq.demod(DEMOD)
    iq.volume(VOLUME)
    if AGC == "manual":
        iq.agc("manual", gain=AGC_GAIN)
    else:
        iq.agc(AGC)

    dac = DAC("P014")
    dac.stream_from(iq)

    print("streaming: demod=%s agc=%s vol=%.2f  (scope on P014)" % (DEMOD, AGC, VOLUME))
    try:
        while True:
            time.sleep_ms(1000)
            print("blocks=%d dsp=%s audio=%s agc=%s" % (
                iq.blocks(), iq.dsp_status()["dsp_blocks"],
                iq.audio_status(), iq.agc_status()))
    except KeyboardInterrupt:
        pass
    finally:
        dac.stop()
        iq.demod("off")
        iq.stop()
        iq.deinit()
        print("stopped")


if __name__ == "__main__":
    main()
