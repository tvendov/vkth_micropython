#!/usr/bin/env python3
"""Generate real signal-processing diagrams for kids DSP course."""
import os, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT, exist_ok=True)
DPI = 150

def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=DPI, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)
    print(f"  OK {name}")


def plot_01_sampling():
    """Sampling: continuous vs sampled."""
    t = np.linspace(0, 1, 300)
    y = np.sin(2 * np.pi * t)

    fig = plt.figure(figsize=(10, 6))

    # Top: continuous
    ax1 = fig.add_axes([0.07, 0.55, 0.88, 0.38])
    ax1.plot(t, y, "b-", linewidth=2)
    ax1.set_title("Continuous signal", fontsize=12)
    ax1.set_ylim(-1.4, 1.4)
    ax1.set_xlim(0, 1)

    # Bottom left: 16 samples
    ax2 = fig.add_axes([0.07, 0.08, 0.42, 0.38])
    ax2.plot(t, y, "b-", alpha=0.2)
    n16 = np.linspace(0, 1, 16, endpoint=False)
    y16 = np.sin(2 * np.pi * n16)
    for xi, yi in zip(n16, y16):
        ax2.plot([xi, xi], [0, yi], "g-", linewidth=1.5)
    ax2.plot(n16, y16, "go", markersize=5)
    ax2.set_title("16 samples - accurate", fontsize=11, color="green")
    ax2.set_ylim(-1.4, 1.4)
    ax2.set_xlim(0, 1)

    # Bottom right: 4 samples
    ax3 = fig.add_axes([0.55, 0.08, 0.42, 0.38])
    ax3.plot(t, y, "b-", alpha=0.2)
    n4 = np.linspace(0, 1, 4, endpoint=False)
    y4 = np.sin(2 * np.pi * n4)
    for xi, yi in zip(n4, y4):
        ax3.plot([xi, xi], [0, yi], "r-", linewidth=1.5)
    ax3.plot(n4, y4, "ro", markersize=5)
    ax3.set_title("4 samples - lost detail", fontsize=11, color="red")
    ax3.set_ylim(-1.4, 1.4)
    ax3.set_xlim(0, 1)

    save(fig, "01_sampling.png")


def plot_02_quantisation():
    """Quantisation: 1-bit, 3-bit, 12-bit."""
    t = np.linspace(0, 1, 500)
    y = np.sin(2 * np.pi * t)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3), sharey=True)

    for ax, bits, col, label in [
        (axes[0], 1, "red",   "1 бит (2 нива)"),
        (axes[1], 3, "orange","3 бита (8 нива)"),
        (axes[2], 12,"green", "12 бита (4096 нива)\n🎯 Нашият DAC"),
    ]:
        levels = 2 ** bits
        yq = np.round(y * (levels/2 - 1)) / (levels/2 - 1)
        ax.plot(t, y, "b-", alpha=0.2, linewidth=1)
        ax.step(t, yq, color=col, linewidth=2, where="mid")
        ax.set_title(label, fontsize=11, color=col, fontweight="bold")
        ax.set_ylim(-1.4, 1.4)
        ax.axhline(0, color="gray", linewidth=0.5)

    fig.suptitle("Квантизация (Quantisation)", fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "02_quantisation.png")


def plot_03_aliasing():
    """Aliasing: correct vs aliased sampling."""
    t = np.linspace(0, 1, 1000)
    f_real = 5
    y = np.sin(2 * np.pi * f_real * t)
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.5), sharey=True)

    # Good sampling
    n_good = np.linspace(0, 1, 32, endpoint=False)
    y_good = np.sin(2 * np.pi * f_real * n_good)
    axes[0].plot(t, y, "b-", alpha=0.3)
    for xi, yi in zip(n_good, y_good):
        axes[0].plot([xi, xi], [0, yi], "g-", linewidth=1)
    axes[0].plot(n_good, y_good, "go", markersize=4)
    axes[0].set_title("32 семпъла: вярна форма ✓", fontsize=12, color="green",
                      fontweight="bold")

    # Bad sampling — aliased
    n_bad = np.linspace(0, 1, 6, endpoint=False)
    y_bad = np.sin(2 * np.pi * f_real * n_bad)
    axes[1].plot(t, y, "b-", alpha=0.3)
    for xi, yi in zip(n_bad, y_bad):
        axes[1].plot([xi, xi], [0, yi], "r-", linewidth=1)
    axes[1].plot(n_bad, y_bad, "ro", markersize=4)
    # Show aliased reconstruction
    t_interp = np.linspace(0, 1, 500)
    y_alias = np.sin(2 * np.pi * 1 * t_interp)  # alias at f=1
    axes[1].plot(t_interp, y_alias, "r--", linewidth=2, alpha=0.6,
                 label="фалшива честота")
    axes[1].legend(fontsize=9)
    axes[1].set_title("6 семпъла: ALIASING ✗", fontsize=12, color="red",
                      fontweight="bold")

    for ax in axes:
        ax.set_ylim(-1.5, 1.5)
        ax.axhline(0, color="gray", linewidth=0.5)
        ax.set_xlabel("Време")

    fig.suptitle("Aliasing — фалшиви честоти при нисък sample rate",
                 fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "03_aliasing.png")


def plot_06_waveforms():
    """Four waveform shapes: sine, square, triangle, saw."""
    t = np.linspace(0, 2, 500)
    fig, axes = plt.subplots(1, 4, figsize=(14, 3), sharey=True)

    # Sine
    axes[0].plot(t, np.sin(2*np.pi*t), "b-", linewidth=2)
    axes[0].set_title("Синусоида\nМек, чист", fontsize=11, color="#3498db")

    # Square
    sq = np.sign(np.sin(2*np.pi*t))
    axes[1].plot(t, sq, "r-", linewidth=2)
    axes[1].set_title("Квадратна\nОстър, 8-битов", fontsize=11, color="#e74c3c")

    # Triangle
    tri = 2*np.abs(2*(t % 1) - 1) - 1
    axes[2].plot(t, tri, "g-", linewidth=2)
    axes[2].set_title("Триъгълна\nМек, флейта", fontsize=11, color="#2ecc71")

    # Saw
    saw = 2*(t % 1) - 1
    axes[3].plot(t, saw, color="#f39c12", linewidth=2)
    axes[3].set_title("Трионовидна\nЯрък, стъргащ", fontsize=11, color="#f39c12")

    for ax in axes:
        ax.set_ylim(-1.5, 1.5)
        ax.axhline(0, color="gray", linewidth=0.5)
        ax.set_xlabel("Време")

    fig.suptitle("Вълнови форми", fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "06_waveforms.png")


def plot_08_clipping():
    """Clipping: original vs clipped signal."""
    t = np.linspace(0, 1, 500)
    y = 1.5 * np.sin(2 * np.pi * t)  # exceeds [-1, 1]
    y_clip = np.clip(y, -1, 1)
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.5), sharey=True)

    axes[0].plot(t, y, "b-", linewidth=2)
    axes[0].axhline(1, color="red", linestyle="--", alpha=0.5, label="4095")
    axes[0].axhline(-1, color="red", linestyle="--", alpha=0.5, label="0")
    axes[0].fill_between(t, 1, y, where=(y > 1), color="red", alpha=0.3)
    axes[0].fill_between(t, -1, y, where=(y < -1), color="red", alpha=0.3)
    axes[0].set_title("Оригинал\n(излиза извън обхвата)", fontsize=11, color="blue")
    axes[0].legend(fontsize=9)

    axes[1].plot(t, y_clip, "r-", linewidth=2)
    axes[1].axhline(1, color="red", linestyle="--", alpha=0.5)
    axes[1].axhline(-1, color="red", linestyle="--", alpha=0.5)
    axes[1].set_title("Клипнат (clipped)\nИзкривен звук!", fontsize=11, color="red",
                      fontweight="bold")

    for ax in axes:
        ax.set_ylim(-2, 2)
        ax.axhline(0, color="gray", linewidth=0.5)
        ax.set_xlabel("Време")

    fig.suptitle("Клипване (Clipping)", fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "08_clipping.png")


def plot_09_clicks():
    """Clicks: discontinuity between buffers."""
    t1 = np.linspace(0, 1, 200)
    t2 = np.linspace(1, 2, 200)
    y1 = np.sin(2 * np.pi * 2.7 * t1)   # ends at ~0.95
    y2 = np.sin(2 * np.pi * 1.3 * t2 + 3.0)  # starts at ~-0.6

    fig, ax = plt.subplots(figsize=(10, 3.5))
    ax.plot(t1, y1, "b-", linewidth=2, label="Буфер A")
    ax.plot(t2, y2, "g-", linewidth=2, label="Буфер B")
    # Mark the click
    ax.annotate("ЩРАК!\n(click)", xy=(1, y1[-1]), xytext=(1.15, 1.3),
                fontsize=13, fontweight="bold", color="red",
                arrowprops=dict(arrowstyle="->", color="red", lw=2))
    ax.plot([1, 1], [y1[-1], y2[0]], "r-", linewidth=3, alpha=0.7)
    ax.plot(1, y1[-1], "ro", markersize=8)
    ax.plot(1, y2[0], "ro", markersize=8)
    ax.axhline(0, color="gray", linewidth=0.5)
    ax.axvline(1, color="red", linestyle=":", alpha=0.5)
    ax.set_xlabel("Време")
    ax.legend(fontsize=10)
    ax.set_title("Clicks — скок между буфери причинява щракане",
                 fontsize=14, fontweight="bold")
    fig.tight_layout()
    save(fig, "09_clicks.png")


def plot_10_fourier():
    """Building a square wave from harmonics."""
    t = np.linspace(0, 2, 1000)
    fig, axes = plt.subplots(1, 4, figsize=(14, 3), sharey=True)

    harmonics = [
        (1, "Само f\n(основен тон)"),
        (2, "f + f/3"),
        (4, "f + f/3 + f/5 + f/7"),
        (20, "20 хармоника\n≈ квадратна"),
    ]
    for ax, (n_harm, title) in zip(axes, harmonics):
        y = np.zeros_like(t)
        for k in range(n_harm):
            n = 2 * k + 1
            y += (1.0 / n) * np.sin(2 * np.pi * n * t)
        y *= (4 / np.pi)
        ax.plot(t, y, "b-", linewidth=2)
        ax.axhline(0, color="gray", linewidth=0.5)
        ax.set_ylim(-1.5, 1.5)
        ax.set_title(title, fontsize=10)
        ax.set_xlabel("Време")

    fig.suptitle("Теорема на Фурие — квадратна вълна от синусоиди",
                 fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "10_fourier.png")


def plot_11_additive_mix():
    """Mixing two signals."""
    t = np.linspace(0, 1, 500)
    a = 0.7 * np.sin(2 * np.pi * 3 * t)
    b = 0.7 * np.sin(2 * np.pi * 8 * t)
    mix = a + b

    fig, axes = plt.subplots(1, 3, figsize=(12, 3), sharey=True)
    axes[0].plot(t, a, "b-", linewidth=2)
    axes[0].set_title("Сигнал A\n(ниска нота, AMP=0.7)", fontsize=10, color="blue")

    axes[1].plot(t, b, "g-", linewidth=2)
    axes[1].set_title("Сигнал B\n(висока нота, AMP=0.7)", fontsize=10, color="green")

    axes[2].plot(t, mix, color="#9b59b6", linewidth=2)
    axes[2].set_title("A + B = Акорд\n(макс: 1.4 < 2.0 ✓)", fontsize=10,
                      color="#9b59b6", fontweight="bold")

    for ax in axes:
        ax.set_ylim(-1.8, 1.8)
        ax.axhline(0, color="gray", linewidth=0.5)
        ax.set_xlabel("Време")

    fig.suptitle("Смесване на два сигнала (адитивен синтез)",
                 fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "11_additive_mix.png")


def plot_12_fir_filter():
    """FIR filter: before and after."""
    t = np.linspace(0, 1, 500)
    low = np.sin(2 * np.pi * 2 * t)
    high = 0.3 * np.sin(2 * np.pi * 40 * t)
    noisy = low + high
    # Simple moving average filter
    kernel = np.ones(15) / 15
    filtered = np.convolve(noisy, kernel, mode="same")

    fig, axes = plt.subplots(1, 2, figsize=(10, 3.5), sharey=True)
    axes[0].plot(t, noisy, "r-", linewidth=1, alpha=0.8)
    axes[0].plot(t, low, "b--", linewidth=1, alpha=0.4, label="оригинал")
    axes[0].set_title("ПРЕДИ филтъра\n(ниска честота + шум)", fontsize=11, color="red")
    axes[0].legend(fontsize=9)

    axes[1].plot(t, filtered, "g-", linewidth=2)
    axes[1].plot(t, low, "b--", linewidth=1, alpha=0.4, label="оригинал")
    axes[1].set_title("СЛЕД FIR филтъра ✓\n(шумът е премахнат)", fontsize=11,
                      color="green", fontweight="bold")
    axes[1].legend(fontsize=9)

    for ax in axes:
        ax.set_ylim(-1.8, 1.8)
        ax.axhline(0, color="gray", linewidth=0.5)
        ax.set_xlabel("Време")

    fig.suptitle("FIR нискочестотен филтър", fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "12_fir_filter.png")


def plot_14_fft():
    """FFT: time domain signal vs frequency spectrum."""
    N = 128
    t = np.arange(N)
    f1, f2 = 5, 20
    sig = np.sin(2 * np.pi * f1 * t / N) + 0.5 * np.sin(2 * np.pi * f2 * t / N)
    win = sig * np.hamming(N)
    spectrum = np.abs(np.fft.rfft(win))
    freqs = np.arange(len(spectrum))

    fig, axes = plt.subplots(1, 2, figsize=(11, 4))

    axes[0].plot(t, sig, "b-", linewidth=1.5)
    axes[0].set_title("Входен сигнал (време)\n2 честоти смесени", fontsize=11)
    axes[0].set_xlabel("Семпъл")
    axes[0].axhline(0, color="gray", linewidth=0.5)

    axes[1].bar(freqs, spectrum, color="#e74c3c", width=0.8)
    axes[1].annotate(f"Bin {f1}", xy=(f1, spectrum[f1]),
                     xytext=(f1+5, spectrum[f1]+5), fontsize=11, fontweight="bold",
                     arrowprops=dict(arrowstyle="->", color="black"))
    axes[1].annotate(f"Bin {f2}", xy=(f2, spectrum[f2]),
                     xytext=(f2+5, spectrum[f2]+5), fontsize=11, fontweight="bold",
                     arrowprops=dict(arrowstyle="->", color="black"))
    axes[1].set_title("FFT спектър (честоти)\nДва ясни пика!", fontsize=11,
                      color="#e74c3c", fontweight="bold")
    axes[1].set_xlabel("Bin (честота)")
    axes[1].set_ylabel("Магнитуда")

    fig.suptitle("FFT — от време към честота", fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()
    save(fig, "14_fft_spectrum.png")


def plot_15_hamming():
    """Hamming window effect on FFT."""
    N = 64
    t = np.arange(N)
    sig = np.sin(2 * np.pi * 5.3 * t / N)  # non-integer freq → leakage

    fig, axes = plt.subplots(2, 2, figsize=(11, 7))

    # Without window
    axes[0, 0].plot(t, sig, "b-", linewidth=1.5)
    axes[0, 0].set_title("Сигнал БЕЗ прозорец", fontsize=10)
    axes[0, 0].axhline(0, color="gray", linewidth=0.5)
    spec_no_win = np.abs(np.fft.rfft(sig))
    axes[0, 1].bar(np.arange(len(spec_no_win)), spec_no_win, color="red", width=0.8)
    axes[0, 1].set_title("FFT без прозорец\nSpectral leakage!", fontsize=10,
                         color="red", fontweight="bold")

    # With Hamming window
    win = sig * np.hamming(N)
    axes[1, 0].plot(t, win, "g-", linewidth=1.5)
    axes[1, 0].plot(t, np.hamming(N), "g--", alpha=0.4, label="Hamming")
    axes[1, 0].set_title("Сигнал С Hamming прозорец", fontsize=10, color="green")
    axes[1, 0].legend(fontsize=9)
    axes[1, 0].axhline(0, color="gray", linewidth=0.5)
    spec_win = np.abs(np.fft.rfft(win))
    axes[1, 1].bar(np.arange(len(spec_win)), spec_win, color="green", width=0.8)
    axes[1, 1].set_title("FFT с Hamming ✓\nЧист спектър!", fontsize=10,
                         color="green", fontweight="bold")

    for row in axes:
        for ax in row:
            ax.set_xlabel("Семпъл" if ax in axes[:, 0] else "Bin")

    fig.suptitle("Hamming прозорец — защо е нужен?",
                 fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()
    save(fig, "15_hamming_window.png")


def plot_17_double_buffer():
    """Double buffering timing diagram."""
    fig, ax = plt.subplots(figsize=(12, 4))

    # DMAC reads
    for i, (start, label, col) in enumerate([
        (0, "DMAC чете A", "#3498db"),
        (1, "DMAC чете B", "#2ecc71"),
        (2, "DMAC чете A", "#3498db"),
        (3, "DMAC чете B", "#2ecc71"),
    ]):
        ax.barh(2, 0.9, left=start, color=col, alpha=0.8, edgecolor="black")
        ax.text(start + 0.45, 2, label, ha="center", va="center", fontsize=9,
                fontweight="bold", color="white")

    # CPU writes
    for i, (start, label, col) in enumerate([
        (0, "CPU pише B", "#e74c3c"),
        (1, "CPU пише A", "#f39c12"),
        (2, "CPU пише B", "#e74c3c"),
        (3, "CPU пише A", "#f39c12"),
    ]):
        ax.barh(1, 0.9, left=start, color=col, alpha=0.8, edgecolor="black")
        ax.text(start + 0.45, 1, label, ha="center", va="center", fontsize=9,
                fontweight="bold", color="white")

    ax.set_yticks([1, 2])
    ax.set_yticklabels(["CPU", "DMAC -> DAC"], fontsize=12)
    ax.set_xlabel("Време ->", fontsize=12)
    ax.set_xlim(-0.1, 4.1)
    ax.set_ylim(0.4, 2.7)
    ax.set_title("Double Buffering (Ping-Pong)",
                 fontsize=14, fontweight="bold")
    ax.axhline(1.5, color="gray", linestyle=":", alpha=0.5)
    for x in [0.95, 1.95, 2.95]:
        ax.annotate("", xy=(x+0.05, 2), xytext=(x+0.05, 1),
                    arrowprops=dict(arrowstyle="<->", color="gray", lw=1.5))

    fig.tight_layout()
    save(fig, "17_double_buffer.png")


def plot_19_spectrum_analyzer():
    """Spectrum analyzer pipeline + simulated LED bar display."""
    import colorsys
    fig = plt.figure(figsize=(13, 7))

    # Top: pipeline diagram
    ax_pipe = fig.add_axes([0.05, 0.62, 0.9, 0.32])
    ax_pipe.set_xlim(0, 10)
    ax_pipe.set_ylim(0, 2)
    ax_pipe.axis("off")
    ax_pipe.set_title("Spectrum Analyzer - DSP Pipeline", fontsize=14, fontweight="bold")

    boxes = [
        (0.2, "ADC\nP000\n22050Hz", "#3498db"),
        (2.0, "Hamming\nWindow", "#9b59b6"),
        (3.8, "FFT\n128-pt", "#e74c3c"),
        (5.6, "Magnitude\n65 bins", "#f39c12"),
        (7.4, "Bands\n32 bars", "#2ecc71"),
        (9.0, "WS2812\n56 LED", "#1abc9c"),
    ]
    for x, label, col in boxes:
        ax_pipe.add_patch(plt.Rectangle((x, 0.3), 1.4, 1.4, facecolor=col,
                          edgecolor="black", linewidth=1.5, alpha=0.85))
        ax_pipe.text(x + 0.7, 1.0, label, ha="center", va="center",
                     fontsize=9, fontweight="bold", color="white")
    for i in range(len(boxes) - 1):
        x1 = boxes[i][0] + 1.4
        x2 = boxes[i+1][0]
        ax_pipe.annotate("", xy=(x2, 1.0), xytext=(x1, 1.0),
                         arrowprops=dict(arrowstyle="->", lw=2, color="#333"))

    # Bottom left: FFT spectrum (simulated)
    ax_fft = fig.add_axes([0.05, 0.08, 0.42, 0.45])
    N = 128
    t = np.arange(N)
    sig = 0.8 * np.sin(2*np.pi*12*t/N) + 0.3*np.sin(2*np.pi*30*t/N)
    win = sig * np.hamming(N)
    spectrum = np.abs(np.fft.rfft(win))
    ax_fft.bar(np.arange(len(spectrum)), spectrum, color="#e74c3c", width=0.8)
    ax_fft.set_title("FFT Spectrum (65 bins)", fontsize=11)
    ax_fft.set_xlabel("Bin")
    ax_fft.set_ylabel("Magnitude")

    # Bottom right: LED bar simulation
    ax_led = fig.add_axes([0.55, 0.08, 0.42, 0.45])
    n_bands = 32
    bands_val = np.zeros(n_bands)
    bin_per_band = max(1, len(spectrum) // n_bands)
    for b in range(n_bands):
        s = b * bin_per_band
        e = min(s + bin_per_band, len(spectrum))
        bands_val[b] = np.max(spectrum[s:e]) if s < e else 0
    mx = max(bands_val.max(), 1)
    bands_val = (bands_val / mx * 255).astype(int)

    colors = []
    for i in range(n_bands):
        r, g, b = colorsys.hsv_to_rgb(i / n_bands * 0.67, 1.0, bands_val[i] / 255.0)
        colors.append((r, g, b))

    ax_led.bar(range(n_bands), bands_val, color=colors, width=0.9, edgecolor="black",
               linewidth=0.5)
    ax_led.set_title("LED Bar (32 bands, rainbow)", fontsize=11, color="#2ecc71",
                     fontweight="bold")
    ax_led.set_xlabel("Band")
    ax_led.set_ylabel("Level (0-255)")
    ax_led.set_ylim(0, 280)

    save(fig, "19_spectrum_analyzer.png")


if __name__ == "__main__":
    print("Generating signal plots...")
    plot_01_sampling()
    plot_02_quantisation()
    plot_03_aliasing()
    plot_06_waveforms()
    plot_08_clipping()
    plot_09_clicks()
    plot_10_fourier()
    plot_11_additive_mix()
    plot_12_fir_filter()
    plot_14_fft()
    plot_15_hamming()
    plot_17_double_buffer()
    plot_19_spectrum_analyzer()
    print("All done! 13 diagrams generated.")
