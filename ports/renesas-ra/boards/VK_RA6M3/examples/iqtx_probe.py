"""RA6M3 IQTX bench helpers. Import does not construct or start a transmitter.

Use only with the RF power stage disconnected/muted and appropriate I/Q loads.
No GPIO is assigned to the Morse key. No boot.py/main.py changes are needed.
"""


def available():
    import machine
    return hasattr(machine, "IQTX")


def prepare(mode="CW", **config):
    """Explicitly acquire hardware and set neutral DAC levels; do not start."""
    from machine import IQTX
    modes = {"CW": IQTX.CW, "AM": IQTX.AM, "FM": IQTX.FM}
    if mode not in modes:
        raise ValueError("choose CW, AM or FM; SSB is not implemented")
    return IQTX(mode=modes[mode], **config)


def show(tx):
    """Read diagnostics only; no delay, trigger start, or key operation."""
    s = tx.status()
    for name in sorted(s):
        print(name, s[name])
    fs = s["actual_rate"]
    if s["mode"] == 2:
        scale = fs * s["fm_gain"] / 65536
        print("FM Hz/ADC-code", scale)
        print("FM endpoint Hz", -s["adc_mid"] * scale,
              (4095 - s["adc_mid"]) * scale)
    return s


def release():
    """Explicit checked release, including after a failed constructor."""
    from machine import IQTX
    IQTX.release()
