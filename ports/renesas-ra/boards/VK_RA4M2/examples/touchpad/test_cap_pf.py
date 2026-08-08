from machine import Pin, TouchPad
import time


# Piecewise P206 calibration for the current CTSU settings:
#   ctsuclk_div=4, prmode=2, prratio=0, atune1=1
#   sdpa=10, snum=11, icog=3, so=0, auto_ssdiv=True
#
# Measured P206 points:
#   0 pF   -> sen=6868
#   33 pF  -> sen=20214
#   66 pF  -> sen=33200
#   99 pF  -> sen=45574
#   181 pF -> sen=62655
#
# This is a channel-specific calibration for P206.
# For P409/P408 it is only a rough equivalent estimate, not a true pF calibration.
P206_CAL_POINTS = (
    (6100, 0.0),
    (17777, 33.0),
    (27400, 68.0),
    (45177, 99.0),
    (62954, 132.0),
)


def estimate_pf_from_p206_sen(sen):
    if sen <= P206_CAL_POINTS[0][0]:
        x0, y0 = P206_CAL_POINTS[0]
        x1, y1 = P206_CAL_POINTS[1]
        return y0 + (sen - x0) * (y1 - y0) / (x1 - x0)

    for i in range(len(P206_CAL_POINTS) - 1):
        x0, y0 = P206_CAL_POINTS[i]
        x1, y1 = P206_CAL_POINTS[i + 1]
        if x0 <= sen <= x1:
            return y0 + (sen - x0) * (y1 - y0) / (x1 - x0)

    x0, y0 = P206_CAL_POINTS[-2]
    x1, y1 = P206_CAL_POINTS[-1]
    return y0 + (sen - x0) * (y1 - y0) / (x1 - x0)


def sample_counts_and_pf(tp):
    sen, ref = tp.read_counts()
    pf = estimate_pf_from_p206_sen(sen)
    return sen, ref, pf


# Pin("P409"), Pin("P206"), Pin("P408") -> pin name string, no numeric range here
tp409 = TouchPad(Pin("P409"))
tp206 = TouchPad(Pin("P206"))
tp408 = TouchPad(Pin("P408"))

TouchPad.cap_global_config(
    # ctsuclk_div: valid values are 1, 2, 4
    ctsuclk_div=4,

    # prmode: 0..2
    # 0 -> 510 pulses
    # 1 -> 126 pulses
    # 2 -> 62 pulses
    prmode=2,

    # prratio: 0..15
    prratio=0,

    # atune1: 0..1
    # 0 = normal
    # 1 = high-current
    atune1=1 ,

    # noise: False/True
    # True  -> noise reduction ON
    # False -> noise reduction OFF
    noise=True,

    # auto_offset: False/True
    # False -> no implicit offset tuning
    # True  -> allows touch-oriented auto tuning path
    auto_offset=False,
)

for tp in (tp409, tp206, tp408):
    tp.cap_config(
        # sdpa: 0..31
        # base_hz = fdrive = ctsuclk_hz / (2 * (sdpa + 1))
        sdpa=10,

        # snum: 0..63
        # groups = snum + 1
        snum=11,

        # icog: 0..3
        # 0 = 100%
        # 1 = 66%
        # 2 = 50%
        # 3 = 40%
        icog=3,

        # so: 0..1023
        # offset current / baseline shift
        so=0,

        # auto_ssdiv: False/True
        # True  -> ssdiv is computed automatically
        # False -> you must provide ssdiv manually
        auto_ssdiv=True,
    )

tim = tp409.timing()

# tim["base_hz"] = fdrive, in Hz
print("fdrive_hz :", tim["base_hz"])

# tim["base_hz"]/1000 = fdrive in kHz
print("fdrive_kHz:", tim["base_hz"] / 1000)

# tim["gate_ns"] = measurement gate per channel, in ns
print("gate_ns   :", tim["gate_ns"])

# timing() dict keys:
# pclkb_hz, ctsuclk_hz, base_hz, base_cycle_ns,
# base_pulses, measurement_pulses, groups, group_ns, gate_ns, stabilize_ns
print("timing    :", tim)
print("Live monitor: 1 sample/sec, single-line update, Ctrl-C to stop")

last_len = 0
while True:
    sen409, ref409, pf409 = sample_counts_and_pf(tp409)
    sen206, ref206, pf206 = sample_counts_and_pf(tp206)
    sen408, ref408, pf408 = sample_counts_and_pf(tp408)

    line = (
        "P409 open sen=%5d ref=%5d pf=%6.1f(eq) | "
        "P206 sen=%5d ref=%5d pf=%6.1f | "
        "P408 sen=%5d ref=%5d pf=%6.1f(eq)"
    ) % (
        sen409, ref409, pf409,
        sen206, ref206, pf206,
        sen408, ref408, pf408,
    )

    pad = ""
    if len(line) < last_len:
        pad = " " * (last_len - len(line))

    print("\r" + line + pad, end="")
    last_len = len(line)
    time.sleep(1)