# VK_RA4M2 retro music demo using the reusable retro_synth helper.

from retro_synth import RetroSynth


DAC_PIN = "P014"  # J19-5 / A4 / DA0
synth = RetroSynth(DAC_PIN)


MELODY = [
    ("E5", 110, "pulse25"),
    ("B4", 110, "pulse25"),
    ("C5", 110, "pulse25"),
    ("D5", 110, "pulse25"),
    ("C5", 110, "pulse25"),
    ("B4", 110, "pulse25"),
    ("A4", 180, "pulse25"),
    (None, 40, "pulse25"),
    ("A4", 110, "pulse25"),
    ("C5", 110, "pulse25"),
    ("E5", 180, "pulse25"),
    ("D5", 110, "pulse25"),
    ("C5", 110, "pulse25"),
    ("B4", 220, "pulse25"),
]


def demo():
    print("Retro music demo on {} (J19-5 / A4 / DA0)".format(DAC_PIN))
    print("melody -> arpeggio bed -> coin")
    synth.play_melody(MELODY, gap_ms=12)
    synth.play_arpeggio("A3", (0, 4, 7, 12), step_ms=55, cycles=6, wave="triangle", gap_ms=5)
    synth.coin()
    synth.stop()
    print("Done.")


demo()
