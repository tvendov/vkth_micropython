import time
from retro_synth import RetroSynth


DAC_PIN = "P014"  # J19-5 / A4 / DA0
synth = RetroSynth(DAC_PIN)


def demo():
    print("DAC retro SFX demo on {} (J19-5 / A4 / DA0)".format(DAC_PIN))
    print("coin -> jump -> laser -> explosion")
    synth.coin()
    time.sleep_ms(120)
    synth.jump()
    time.sleep_ms(120)
    synth.laser()
    time.sleep_ms(120)
    synth.explosion()
    synth.stop()
    print("Done.")


demo()
