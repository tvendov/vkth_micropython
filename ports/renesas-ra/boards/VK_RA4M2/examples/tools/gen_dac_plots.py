#!/usr/bin/env python3
"""Generate oscilloscope-style DAC plots for Chapter 13 (DAC)."""
import os, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT, exist_ok=True)
DPI = 150
np.random.seed(13)

# ── Noise helpers ────────────────────────────────────────────────
def scope_noise(y, sigma=0.03):
    """Add gentle Gaussian noise like a real oscilloscope trace."""
    return y + np.random.normal(0, sigma * np.max(np.abs(y) + 1), len(y))

def dac_quantize_noise(y, bits=12):
    """Add subtle quantisation jitter typical of a real DAC."""
    step = 1.0  # 1 LSB
    return y + np.random.uniform(-step/2, step/2, len(y))


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=DPI, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)
    print(f"  OK {name}")


def plot_static_vs_wave():
    """Static DC level vs sine wave from wavetable — with noise."""
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))

    # Left: static
    t = np.linspace(0, 10, 500)
    y_static = np.ones_like(t) * 2048
    y_static_n = dac_quantize_noise(scope_noise(y_static, 0.004))
    axes[0].plot(t, y_static_n, "b-", linewidth=1.5, alpha=0.7)
    axes[0].axhline(2048, color="gray", linewidth=0.5, linestyle="--")
    axes[0].set_ylim(0, 4095)
    axes[0].set_title("dac.write(2048) — постоянно 1.65V", fontsize=13,
                      fontweight="bold", color="#636e72")
    axes[0].set_ylabel("DAC стойност (0–4095)", fontsize=10)
    axes[0].set_xlabel("Време", fontsize=10)
    axes[0].fill_between(t, 0, y_static, alpha=0.06, color="blue")
    axes[0].text(5, 2200, "ТИШИНА", ha="center", fontsize=16, color="#636e72",
                 fontweight="bold", alpha=0.5)

    # Right: sine wave from wavetable
    TABLE_LEN = 16
    MID = 2048
    AMP = 1800
    wt_x = np.arange(TABLE_LEN)
    wt_y = np.array([MID + int(AMP * math.sin(2 * math.pi * i / TABLE_LEN))
                     for i in range(TABLE_LEN)])

    t_full = np.arange(TABLE_LEN * 3)
    y_full = np.tile(wt_y, 3)
    y_full_n = dac_quantize_noise(scope_noise(y_full.astype(float), 0.003))

    t_smooth = np.linspace(0, TABLE_LEN * 3, 500)
    y_smooth = MID + AMP * np.sin(2 * np.pi * t_smooth / TABLE_LEN)

    axes[1].plot(t_smooth, y_smooth, "b-", alpha=0.2, linewidth=1, label="идеален синус")
    axes[1].step(t_full, y_full_n, "g-", linewidth=1.5, where="mid", label="wavetable (16 точки)")
    axes[1].plot(t_full, y_full_n, "go", markersize=3)
    axes[1].axhline(MID, color="gray", linewidth=0.5, linestyle="--")
    axes[1].axhline(0, color="red", linewidth=0.5, linestyle=":", alpha=0.5)
    axes[1].axhline(4095, color="red", linewidth=0.5, linestyle=":", alpha=0.5)
    axes[1].set_ylim(-200, 4300)
    axes[1].set_title("write_timed(buf, rate) — звукова вълна", fontsize=13,
                      fontweight="bold", color="#2ecc71")
    axes[1].set_ylabel("DAC стойност (0–4095)", fontsize=10)
    axes[1].set_xlabel("Семпъл номер", fontsize=10)
    axes[1].legend(fontsize=9, loc="upper right")

    axes[1].annotate("MID = 2048\n(тишина)", xy=(TABLE_LEN*1.5, MID),
                     fontsize=9, color="gray", ha="center",
                     xytext=(TABLE_LEN*1.5, MID+600),
                     arrowprops=dict(arrowstyle="->", color="gray"))

    fig.suptitle("DAC: статично ниво vs звукова вълна", fontsize=15, fontweight="bold", y=1.02)
    fig.tight_layout()
    save(fig, "14_dac_static_vs_wave.png")


def plot_four_waveforms():
    """Four waveforms: sine, square, triangle, sawtooth — with noise."""
    TABLE_LEN = 128
    MID = 2048
    AMP = 1800
    t = np.arange(TABLE_LEN)

    sine = MID + AMP * np.sin(2 * np.pi * t / TABLE_LEN)
    square = np.where(t < TABLE_LEN // 2, MID + AMP, MID - AMP).astype(float)
    triangle = MID + AMP * (2 * np.abs(2 * t / TABLE_LEN - 1) - 1)
    saw = MID + AMP * (2 * t / TABLE_LEN - 1)

    fig, axes = plt.subplots(2, 2, figsize=(12, 7), sharex=True, sharey=True)

    for ax, wave, name, color in [
        (axes[0, 0], sine,     "Синус — мек, чист тон",       "#3498db"),
        (axes[0, 1], square,   "Квадратна — остър, чиптюн",   "#e74c3c"),
        (axes[1, 0], triangle, "Триъгълна — мека флейта",     "#2ecc71"),
        (axes[1, 1], saw,      "Назъбена — ярък, стъргащ",    "#f39c12"),
    ]:
        wave_n = dac_quantize_noise(scope_noise(wave, 0.003))
        ax.plot(t, wave_n, color=color, linewidth=1.5)
        ax.axhline(MID, color="gray", linewidth=0.5, linestyle="--")
        ax.axhline(0, color="red", linewidth=0.3, linestyle=":")
        ax.axhline(4095, color="red", linewidth=0.3, linestyle=":")
        ax.fill_between(t, MID, wave, alpha=0.10, color=color)
        ax.set_title(name, fontsize=12, fontweight="bold", color=color)
        ax.set_ylim(-200, 4300)
        ax.set_xlim(0, TABLE_LEN - 1)
        ax.set_ylabel("DAC (0–4095)")
        ax.set_xlabel("Семпъл")

    fig.suptitle("Четири вълнови форми — 128 семпъла, 12-bit DAC",
                 fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()
    save(fig, "14_dac_waveforms.png")


def plot_sample_rate():
    """Same wavetable at different sample rates — with noise."""
    TABLE_LEN = 32
    MID = 2048
    AMP = 1800
    wt = MID + AMP * np.sin(2 * np.pi * np.arange(TABLE_LEN) / TABLE_LEN)

    fig, axes = plt.subplots(1, 3, figsize=(14, 3.5), sharey=True)

    for ax, freq, color, label in [
        (axes[0], 220,  "#3498db", "220 Hz (La₃)\nSR = 220×32 = 7040 Hz"),
        (axes[1], 440,  "#2ecc71", "440 Hz (La₄)\nSR = 440×32 = 14080 Hz"),
        (axes[2], 880,  "#e74c3c", "880 Hz (La₅)\nSR = 880×32 = 28160 Hz"),
    ]:
        duration_s = 0.005
        sr = freq * TABLE_LEN
        n_samples = int(sr * duration_s)
        samples = np.array([wt[i % TABLE_LEN] for i in range(n_samples)])
        samples_n = dac_quantize_noise(scope_noise(samples, 0.003))
        t_ms = np.arange(n_samples) / sr * 1000

        ax.step(t_ms, samples_n, color=color, linewidth=1.2, where="mid")
        ax.axhline(MID, color="gray", linewidth=0.5, linestyle="--")
        ax.set_title(label, fontsize=11, fontweight="bold", color=color)
        ax.set_xlabel("Време (ms)")
        ax.set_ylim(-200, 4300)
        ax.set_xlim(0, 5)

    axes[0].set_ylabel("DAC (0–4095)")
    fig.suptitle("Една и съща wavetable, различен sample rate = различна честота",
                 fontsize=13, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "14_dac_sample_rate.png")


def plot_dac_pipeline():
    """Visual pipeline: numbers → DAC → voltage → speaker."""
    fig, ax = plt.subplots(figsize=(12, 2.5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 3)
    ax.axis("off")

    boxes = [
        (0.5, 1, 2, 1, "#a29bfe", "Масив\n[2048, 2500,\n3000, ...]", "white"),
        (3.0, 1, 1.5, 1, "#ff9f43", "AGT\nТаймер\n22050 Hz", "white"),
        (5.0, 1, 1.5, 1, "#ee5a24", "DTC →\nDAC12", "white"),
        (7.0, 1, 1.5, 1, "#2ecc71", "P014\n0–3.3V", "white"),
        (9.0, 1, 1, 1, "#636e72", "Звук", "white"),
    ]
    for x, y, w, h, fc, txt, tc in boxes:
        rect = plt.Rectangle((x, y), w, h, facecolor=fc, edgecolor="none",
                              alpha=0.9, linewidth=0)
        ax.add_patch(rect)
        ax.text(x + w/2, y + h/2, txt, ha="center", va="center",
                fontsize=9, fontweight="bold", color=tc)

    for x1, x2 in [(2.5, 3.0), (4.5, 5.0), (6.5, 7.0), (8.5, 9.0)]:
        ax.annotate("", xy=(x2, 1.5), xytext=(x1, 1.5),
                    arrowprops=dict(arrowstyle="->", lw=2, color="#2d3436"))

    ax.text(3.75, 2.3, "тик!", fontsize=8, color="#ff9f43", ha="center")
    ax.text(5.75, 2.3, "число→V", fontsize=8, color="#ee5a24", ha="center")
    ax.text(0.5, 0.7, "CPU свободно!", fontsize=9, color="#636e72",
            fontstyle="italic")

    fig.suptitle("Hardware DAC pipeline: AGT → DTC → DAC → P014",
                 fontsize=13, fontweight="bold")
    save(fig, "14_dac_pipeline.png")


if __name__ == "__main__":
    print("Generating DAC plots...")
    plot_static_vs_wave()
    plot_four_waveforms()
    plot_sample_rate()
    plot_dac_pipeline()
    print("Done.")
