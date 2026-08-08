#!/usr/bin/env python3
"""Generate oscilloscope-style plots for the book chapters.

All signals include realistic low-level noise to look like real scope captures.
Covers: Part III intro, Ch 16 (IRQ), Ch 17 (debounce), Ch 18 (button events),
Ch 20 (asyncio), Ch 35 (filtering), Ch 54 (FFT).
"""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT, exist_ok=True)
DPI = 150
np.random.seed(77)

# ── Noise helpers ────────────────────────────────────────────────
def scope_noise(y, sigma=0.04):
    """Add gentle Gaussian noise like a real oscilloscope trace."""
    return y + np.random.normal(0, sigma, len(y))

def edge_ringing(y, tau=0.003, ring_amp=0.10, ring_freq=80):
    """Add tiny overshoot / ringing at every sharp transition."""
    dy = np.diff(y, prepend=y[0])
    out = y.copy()
    edges = np.where(np.abs(dy) > 0.5)[0]
    n = len(y)
    for e in edges:
        length = min(60, n - e)
        tt = np.arange(length) / 1000.0
        envelope = ring_amp * np.exp(-tt / tau)
        osc = np.sin(2 * np.pi * ring_freq * tt) * np.sign(dy[e])
        out[e:e+length] += envelope * osc
    return out

def gpio_noise(y, sigma=0.035):
    """Noise for digital GPIO-level signals with edge ringing."""
    return np.clip(scope_noise(edge_ringing(y), sigma), -0.15, 3.5)


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=DPI, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)
    print(f"  OK {name}")


# ── Part III intro: digital vs analog ──────────────────────────────
def plot_digital_vs_analog():
    """Digital (0/3.3V square) vs analog (smooth sine) signals."""
    fig, axes = plt.subplots(1, 2, figsize=(12, 3.5), sharey=True)
    t = np.linspace(0, 10, 2000)

    # Digital with noise + ringing
    digital = np.where(np.sin(2 * np.pi * 0.5 * t) >= 0, 3.3, 0.0)
    digital_n = gpio_noise(digital)
    axes[0].plot(t, digital_n, color="#e74c3c", linewidth=1.2)
    axes[0].fill_between(t, 0, digital, alpha=0.12, color="#e74c3c", step="mid")
    axes[0].axhline(0, color="gray", linewidth=0.5, linestyle=":")
    axes[0].axhline(3.3, color="gray", linewidth=0.5, linestyle=":")
    axes[0].set_title("Цифров сигнал (GPIO)", fontsize=13, fontweight="bold",
                      color="#e74c3c")
    axes[0].set_ylabel("Напрежение (V)", fontsize=11)
    axes[0].set_xlabel("Време (ms)", fontsize=10)
    axes[0].set_ylim(-0.3, 3.8)
    axes[0].text(5, 1.65, "само 0V или 3.3V\n(2 състояния)",
                ha="center", fontsize=11, color="#636e72", fontweight="bold",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))

    # Analog with noise
    analog = 1.65 + 1.5 * np.sin(2 * np.pi * 0.3 * t)
    analog_n = scope_noise(analog, 0.025)
    axes[1].plot(t, analog_n, color="#2ecc71", linewidth=1.8)
    axes[1].fill_between(t, 1.65, analog_n, alpha=0.12, color="#2ecc71")
    axes[1].axhline(0, color="gray", linewidth=0.5, linestyle=":")
    axes[1].axhline(3.3, color="gray", linewidth=0.5, linestyle=":")
    axes[1].axhline(1.65, color="gray", linewidth=0.5, linestyle="--", alpha=0.4)
    axes[1].set_title("Аналогов сигнал (ADC/DAC)", fontsize=13,
                      fontweight="bold", color="#2ecc71")
    axes[1].set_xlabel("Време (ms)", fontsize=10)
    axes[1].set_ylim(-0.3, 3.8)
    axes[1].text(5, 0.3, "0.00V ... 3.30V\n(безброй стойности)",
                ha="center", fontsize=11, color="#636e72", fontweight="bold",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))

    for ax in axes:
        ax.set_yticks([0, 1.65, 3.3])
        ax.set_yticklabels(["0V", "1.65V", "3.3V"])
        ax.grid(True, alpha=0.15)
        ax.set_xlim(0, 10)

    fig.suptitle("Два вида информация: цифрова (0/1) и аналогова (непрекъснат диапазон)",
                 fontsize=14, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "10_digital_vs_analog.png")


# ── Ch 16: Polling vs IRQ timing ──────────────────────────────────
def plot_polling_vs_irq():
    """Timing diagram: polling detects late, IRQ reacts at the exact edge."""
    fig, axes = plt.subplots(3, 1, figsize=(12, 5), sharex=True,
                             gridspec_kw={"height_ratios": [1, 1.2, 1.2]})
    t = np.linspace(0, 20, 4000)

    # Button press: goes LOW at t=6 and stays LOW (active-low, real scenario)
    event = np.where(t >= 6.0, 0.0, 3.3)
    event_n = gpio_noise(event, 0.03)
    axes[0].plot(t, event_n, color="#e74c3c", linewidth=1.2)
    axes[0].fill_between(t, 0, np.where(t >= 6.0, 3.3, 0.0), alpha=0.12,
                        color="#e74c3c", step="mid")
    axes[0].set_ylabel("Бутон", fontsize=10)
    axes[0].set_title("Бутон натиснат в момент t = 6 ms (active-low: HIGH→LOW)",
                      fontsize=11, fontweight="bold", color="#e74c3c")
    axes[0].set_ylim(-0.3, 4.0)
    axes[0].set_yticks([0, 3.3])
    axes[0].set_yticklabels(["0V (натиснат)", "3.3V"])
    axes[0].axvline(6.0, color="#e74c3c", linewidth=1, linestyle=":", alpha=0.6)

    # Polling checks every 2.5ms: 0, 2.5, 5.0, 7.5, 10, ...
    # Event at t=6 → first detection at t=7.5 → latency = 1.5ms
    check_times = np.arange(0, 20, 2.5)
    for ct in check_times:
        if ct >= 7.5 and ct <= 7.5:
            clr = "#27ae60"  # first detection
        else:
            clr = "#3498db"
        axes[1].axvline(ct, color=clr, linewidth=1.5, alpha=0.5, linestyle="--")
        # Show check result: miss or hit
        if ct < 6.0:
            axes[1].plot(ct, 3.3, "v", color="#3498db", markersize=7)
        else:
            axes[1].plot(ct, 0.0, "v", color="#27ae60", markersize=9)

    # Latency annotation
    axes[1].annotate("", xy=(7.5, 2.0), xytext=(6.0, 2.0),
                    arrowprops=dict(arrowstyle="<->", color="#e74c3c", lw=2))
    axes[1].text(6.75, 2.3, "латентност\n~1.5 ms", fontsize=10,
                color="#e74c3c", fontweight="bold", ha="center")
    axes[1].axvline(6.0, color="#e74c3c", linewidth=1, linestyle=":", alpha=0.4)
    axes[1].set_ylabel("Polling", fontsize=10)
    axes[1].set_title("Polling: проверява на всеки 2.5 ms — открива със закъснение",
                      fontsize=11, fontweight="bold", color="#3498db")
    axes[1].set_ylim(-0.5, 4.0)
    axes[1].set_yticks([0, 3.3])
    axes[1].set_yticklabels(["0V", "3.3V"])

    # IRQ: reacts at the exact falling edge (t=6.0)
    irq_response = np.where(t >= 6.0, 3.3, 0.0)
    irq_n = gpio_noise(irq_response, 0.03)
    axes[2].plot(t, irq_n, color="#27ae60", linewidth=1.2)
    axes[2].fill_between(t, 0, irq_response, alpha=0.15, color="#27ae60", step="mid")
    axes[2].axvline(6.0, color="#e74c3c", linewidth=1, linestyle=":", alpha=0.4)
    axes[2].annotate("IRQ_FALLING", xy=(6.0, 3.5), xytext=(8, 3.7),
                    fontsize=11, color="#27ae60", fontweight="bold",
                    arrowprops=dict(arrowstyle="->", color="#27ae60", lw=1.5))
    axes[2].annotate("", xy=(6.05, 1.8), xytext=(6.0, 1.8),
                    arrowprops=dict(arrowstyle="<->", color="#27ae60", lw=2))
    axes[2].text(7.5, 1.5, "латентност < 1 us", fontsize=10,
                color="#27ae60", fontweight="bold")
    axes[2].set_ylabel("IRQ", fontsize=10)
    axes[2].set_title("IRQ: реагира на фронта — латентност < 1 us",
                      fontsize=11, fontweight="bold", color="#27ae60")
    axes[2].set_ylim(-0.3, 4.5)
    axes[2].set_xlabel("Време (ms)", fontsize=10)

    for ax in axes:
        ax.grid(True, alpha=0.15)
        ax.set_xlim(0, 20)

    fig.tight_layout()
    save(fig, "16_polling_vs_irq.png")


# ── Ch 17: Contact bounce + debounce ─────────────────────────────
def plot_bounce_debounce():
    """Oscilloscope view of mechanical bounce and clean debounced output."""
    fig, axes = plt.subplots(2, 1, figsize=(12, 5), sharex=True)

    np.random.seed(42)
    t = np.linspace(0, 30, 6000)

    # Build bounce signal
    signal = np.ones_like(t) * 3.3

    bounce_times = [5.0, 5.3, 5.5, 5.7, 5.9, 6.2, 6.5, 6.8, 7.2, 7.8]
    bounce_ends =  [5.2, 5.4, 5.6, 5.8, 6.1, 6.4, 6.7, 7.1, 7.7, 30.0]
    for bs, be in zip(bounce_times, bounce_ends):
        signal[(t >= bs) & (t < be)] = 0.0

    release_times = [22.0, 22.3, 22.5, 22.7, 22.9, 23.2, 23.5]
    release_ends =  [22.2, 22.4, 22.6, 22.8, 23.1, 23.4, 30.0]
    for i, (rs, re) in enumerate(zip(release_times, release_ends)):
        signal[(t >= rs) & (t < re)] = 3.3 if i % 2 == 0 else 0.0
    signal[t >= 23.5] = 3.3

    # Realistic noise + ringing at every bounce transition
    signal_noisy = gpio_noise(signal, 0.06)

    axes[0].plot(t, signal_noisy, color="#e74c3c", linewidth=0.8)
    axes[0].set_title("Суров сигнал от бутон — контактен bounce",
                      fontsize=12, fontweight="bold", color="#e74c3c")
    axes[0].set_ylabel("V", fontsize=11)
    axes[0].set_ylim(-0.5, 4.0)
    axes[0].set_yticks([0, 3.3])
    axes[0].set_yticklabels(["0V\n(натиснат)", "3.3V\n(отпуснат)"])
    axes[0].axvspan(5, 7.8, alpha=0.1, color="#e74c3c")
    axes[0].text(6.4, 3.7, "bounce ~3ms", fontsize=10, color="#e74c3c",
                ha="center", fontweight="bold")
    axes[0].axvspan(22, 23.5, alpha=0.1, color="#e74c3c")
    axes[0].text(22.75, 3.7, "bounce ~1.5ms", fontsize=10, color="#e74c3c",
                ha="center", fontweight="bold")

    # Bottom: debounced clean signal with very slight noise
    clean = np.ones_like(t) * 3.3
    clean[(t >= 7.8) & (t < 22.0)] = 0.0
    clean_n = scope_noise(clean, 0.015)

    axes[1].plot(t, clean_n, color="#27ae60", linewidth=2)
    axes[1].fill_between(t, 0, clean, alpha=0.12, color="#27ae60", step="mid")
    axes[1].set_title("След debounce (40ms прозорец) — чист сигнал",
                      fontsize=12, fontweight="bold", color="#27ae60")
    axes[1].set_ylabel("V", fontsize=11)
    axes[1].set_xlabel("Време (ms)", fontsize=10)
    axes[1].set_ylim(-0.5, 4.0)
    axes[1].set_yticks([0, 3.3])
    axes[1].set_yticklabels(["0V\n(натиснат)", "3.3V\n(отпуснат)"])

    axes[1].annotate("PRESS", xy=(7.8, 0), xytext=(10, -0.3),
                    fontsize=11, fontweight="bold", color="#27ae60",
                    arrowprops=dict(arrowstyle="->", color="#27ae60", lw=1.5))
    axes[1].annotate("RELEASE", xy=(22, 3.3), xytext=(24, 3.6),
                    fontsize=11, fontweight="bold", color="#3498db",
                    arrowprops=dict(arrowstyle="->", color="#3498db", lw=1.5))

    for ax in axes:
        ax.grid(True, alpha=0.15)
        ax.set_xlim(0, 30)

    fig.suptitle("Дебаунс: от шумен механичен сигнал до чисто логическо събитие",
                 fontsize=14, fontweight="bold", y=1.03)
    fig.tight_layout()
    save(fig, "17_bounce_debounce.png")


# ── Ch 18: Raw signal → events ───────────────────────────────────
def plot_raw_to_events():
    """Raw button level → PRESS/RELEASE/CLICK/LONG_PRESS events."""
    fig, axes = plt.subplots(2, 1, figsize=(12, 4.5), sharex=True,
                             gridspec_kw={"height_ratios": [1, 1.2]})
    t = np.linspace(0, 50, 5000)

    raw = np.ones_like(t) * 3.3
    raw[(t >= 5) & (t < 8)] = 0.0
    raw[(t >= 18) & (t < 35)] = 0.0
    raw_n = gpio_noise(raw, 0.03)

    axes[0].plot(t, raw_n, color="#3498db", linewidth=1.2)
    axes[0].fill_between(t, 0, raw, alpha=0.08, color="#3498db", step="mid")
    axes[0].set_title("Суров сигнал от бутона (active-low, с pull-up)",
                      fontsize=12, fontweight="bold", color="#3498db")
    axes[0].set_ylabel("V", fontsize=10)
    axes[0].set_ylim(-0.5, 4.5)
    axes[0].set_yticks([0, 3.3])
    axes[0].set_yticklabels(["0V (натиснат)", "3.3V (отпуснат)"])
    axes[0].grid(True, alpha=0.15)

    axes[1].set_ylim(-0.5, 5)
    axes[1].set_xlim(0, 50)
    axes[1].set_yticks([])
    axes[1].set_xlabel("Време (ms)", fontsize=10)
    axes[1].set_title("Логически събития — генерирани от debounce логиката",
                      fontsize=12, fontweight="bold", color="#27ae60")

    events = [
        (5, "PRESS", "#27ae60", 1),
        (8, "RELEASE", "#e74c3c", 1),
        (8.5, "CLICK", "#f39c12", 2),
        (18, "PRESS", "#27ae60", 1),
        (26, "LONG_PRESS", "#9b59b6", 3),
        (29, "HOLD_REPEAT", "#9b59b6", 3.5),
        (32, "HOLD_REPEAT", "#9b59b6", 3.5),
        (35, "RELEASE", "#e74c3c", 1),
    ]

    for tx, label, color, y in events:
        axes[1].plot(tx, y, "o", color=color, markersize=10, zorder=5)
        axes[1].annotate(label, xy=(tx, y), xytext=(tx, y + 0.6),
                        fontsize=9, fontweight="bold", color=color,
                        ha="center", rotation=30)
        axes[0].axvline(tx, color=color, linewidth=0.8, linestyle=":", alpha=0.5)

    axes[1].axvspan(18, 35, alpha=0.08, color="#9b59b6")
    axes[1].text(26.5, 4.5, "задържане > 800ms", fontsize=10,
                color="#9b59b6", ha="center", fontstyle="italic")
    axes[1].annotate("", xy=(8, 0.3), xytext=(5, 0.3),
                    arrowprops=dict(arrowstyle="<->", color="#f39c12", lw=1.5))
    axes[1].text(6.5, -0.1, "< 300ms", fontsize=9, color="#f39c12",
                ha="center", fontweight="bold")
    axes[1].grid(True, alpha=0.15)

    fig.tight_layout()
    save(fig, "18_raw_to_events.png")


# ── Ch 20: Blocking vs async ─────────────────────────────────────
def plot_blocking_vs_async():
    """Gantt-like timing: blocking vs asyncio cooperative multitasking."""
    fig, axes = plt.subplots(2, 1, figsize=(12, 4.5),
                             gridspec_kw={"height_ratios": [1, 1]})

    colors = {"blink": "#e74c3c", "button": "#3498db", "report": "#27ae60"}

    ax = axes[0]
    ax.set_title("Блокиращ код — само една задача работи, останалите чакат",
                 fontsize=12, fontweight="bold", color="#636e72")
    ax.set_ylim(-0.5, 3)
    ax.set_xlim(0, 20)
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["report", "button", "blink"], fontsize=10)

    for start in range(0, 20, 4):
        ax.barh(2, 2, left=start, height=0.6, color=colors["blink"], alpha=0.8)
        ax.barh(2, 2, left=start+2, height=0.6, color=colors["blink"], alpha=0.3)

    ax.barh(1, 20, left=0, height=0.6, color="#bdc3c7", alpha=0.5)
    ax.text(10, 1, "ЗАКЛЮЧЕН (чака blink да свърши)", ha="center",
           fontsize=10, color="#636e72", fontweight="bold")
    ax.barh(0, 20, left=0, height=0.6, color="#bdc3c7", alpha=0.5)
    ax.text(10, 0, "ЗАКЛЮЧЕН", ha="center", fontsize=10,
           color="#636e72", fontweight="bold")
    ax.grid(True, alpha=0.15, axis="x")

    ax = axes[1]
    ax.set_title("asyncio — задачите се редуват кооперативно, всички работят",
                 fontsize=12, fontweight="bold", color="#27ae60")
    ax.set_ylim(-0.5, 3)
    ax.set_xlim(0, 20)
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["report", "button", "blink"], fontsize=10)

    for start in np.arange(0, 20, 2):
        ax.barh(2, 0.4, left=start, height=0.6, color=colors["blink"], alpha=0.85)
    for start in np.arange(0.4, 20, 1):
        ax.barh(1, 0.2, left=start, height=0.6, color=colors["button"], alpha=0.85)
    for start in np.arange(0, 20, 5):
        ax.barh(0, 0.6, left=start, height=0.6, color=colors["report"], alpha=0.85)

    ax.set_xlabel("Време (ms)", fontsize=10)
    ax.grid(True, alpha=0.15, axis="x")

    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor=colors["blink"], label="blink (LED)"),
        Patch(facecolor=colors["button"], label="button (SW1)"),
        Patch(facecolor=colors["report"], label="report (print)"),
        Patch(facecolor="#bdc3c7", label="заключен/чака"),
    ]
    axes[1].legend(handles=legend_elements, loc="upper right", fontsize=9, ncol=4)

    fig.tight_layout()
    save(fig, "20_blocking_vs_async.png")


# ── Ch 35: Filtering / hysteresis ────────────────────────────────
def plot_filtering_hysteresis():
    """Noisy ADC signal + moving average + hysteresis thresholds."""
    fig, axes = plt.subplots(2, 1, figsize=(12, 5.5), sharex=True)

    np.random.seed(7)
    t = np.linspace(0, 200, 2000)

    real = 2048 + 800 * np.sin(2 * np.pi * t / 100) + 300 * np.sin(2 * np.pi * t / 33)
    noise = np.random.normal(0, 120, len(t))
    noisy = real + noise

    window = 20
    filtered = np.convolve(noisy, np.ones(window)/window, mode="same")

    axes[0].plot(t, noisy, color="#bdc3c7", linewidth=0.5, label="суров ADC (шумен)")
    axes[0].plot(t, filtered, color="#e74c3c", linewidth=2, label=f"moving average (N={window})")
    axes[0].plot(t, real, color="#3498db", linewidth=1.5, linestyle="--",
                alpha=0.7, label="реален сигнал")
    axes[0].legend(fontsize=9, loc="upper right")
    axes[0].set_title("Филтриране: отстраняване на шум от ADC измерването",
                      fontsize=12, fontweight="bold", color="#e74c3c")
    axes[0].set_ylabel("ADC стойност", fontsize=10)
    axes[0].set_ylim(1000, 3200)

    threshold_high, threshold_low = 2400, 1700
    state = np.zeros_like(t)
    current = 0
    for i in range(len(t)):
        if current == 0 and filtered[i] > threshold_high:
            current = 1
        elif current == 1 and filtered[i] < threshold_low:
            current = 0
        state[i] = current

    axes[1].plot(t, filtered, color="#e74c3c", linewidth=1.5, alpha=0.5,
                label="филтриран сигнал")
    axes[1].axhline(threshold_high, color="#27ae60", linewidth=1.5,
                   linestyle="--", label=f"горен праг = {threshold_high}")
    axes[1].axhline(threshold_low, color="#f39c12", linewidth=1.5,
                   linestyle="--", label=f"долен праг = {threshold_low}")
    axes[1].fill_between(t, 1000, 3200, where=state > 0.5,
                        alpha=0.1, color="#27ae60")
    ax2 = axes[1].twinx()
    # Add slight noise to the digital output too
    state_noisy = scope_noise(state * 3.3, 0.02)
    ax2.plot(t, state_noisy, color="#9b59b6", linewidth=1.5)
    ax2.set_ylabel("Изход (V)", fontsize=10, color="#9b59b6")
    ax2.set_ylim(-0.5, 4.5)
    ax2.set_yticks([0, 3.3])
    ax2.set_yticklabels(["OFF", "ON"], color="#9b59b6")

    axes[1].legend(fontsize=9, loc="upper right")
    axes[1].set_title("Хистерезис: различни прагове за включване и изключване",
                      fontsize=12, fontweight="bold", color="#27ae60")
    axes[1].set_ylabel("ADC стойност", fontsize=10)
    axes[1].set_xlabel("Семпъл", fontsize=10)
    axes[1].set_ylim(1000, 3200)

    for ax in axes:
        ax.grid(True, alpha=0.15)

    fig.suptitle("Глава 35: Филтриране, калибриране и хистерезис",
                 fontsize=14, fontweight="bold", y=1.03)
    fig.tight_layout()
    save(fig, "35_filtering_hysteresis.png")


# ── Ch 54: FFT time → freq domain ────────────────────────────────
def plot_fft_time_freq():
    """Time domain mixed signal → FFT → frequency spectrum bars."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.5))

    N = 256
    fs = 22050
    t = np.arange(N) / fs

    signal = (0.7 * np.sin(2 * np.pi * 440 * t) +
              0.4 * np.sin(2 * np.pi * 1000 * t) +
              0.3 * np.sin(2 * np.pi * 2500 * t))

    # Realistic ADC noise
    np.random.seed(123)
    signal += np.random.normal(0, 0.04, N)

    axes[0].plot(t * 1000, signal, color="#3498db", linewidth=1.2)
    axes[0].fill_between(t * 1000, 0, signal, alpha=0.12, color="#3498db")
    axes[0].axhline(0, color="gray", linewidth=0.5)
    axes[0].set_title("Времева област — смесен аудио сигнал",
                      fontsize=12, fontweight="bold", color="#3498db")
    axes[0].set_xlabel("Време (ms)", fontsize=10)
    axes[0].set_ylabel("Амплитуда", fontsize=10)
    axes[0].set_xlim(0, t[-1] * 1000)
    axes[0].grid(True, alpha=0.15)
    axes[0].text(t[-1]*1000*0.5, signal.max()*0.85,
                "440 Hz + 1000 Hz + 2500 Hz\n(смесени заедно)",
                ha="center", fontsize=10, color="#636e72",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))

    # FFT with noise floor visible
    fft_vals = np.abs(np.fft.rfft(signal)) / (N / 2)
    freqs = np.fft.rfftfreq(N, 1.0 / fs)

    bar_width = freqs[1] - freqs[0]
    colors_fft = []
    for f, m in zip(freqs, fft_vals):
        if abs(f - 440) < 50:
            colors_fft.append("#e74c3c")
        elif abs(f - 1000) < 50:
            colors_fft.append("#27ae60")
        elif abs(f - 2500) < 80:
            colors_fft.append("#f39c12")
        else:
            colors_fft.append("#bdc3c7")

    axes[1].bar(freqs, fft_vals, width=bar_width * 0.9, color=colors_fft,
               alpha=0.8, edgecolor="none")
    axes[1].set_title("Честотна област — FFT спектър",
                      fontsize=12, fontweight="bold", color="#e74c3c")
    axes[1].set_xlabel("Честота (Hz)", fontsize=10)
    axes[1].set_ylabel("Магнитуд", fontsize=10)
    axes[1].set_xlim(0, 5000)
    axes[1].grid(True, alpha=0.15)

    for freq_peak, label, color in [(440, "440 Hz", "#e74c3c"),
                                      (1000, "1 kHz", "#27ae60"),
                                      (2500, "2.5 kHz", "#f39c12")]:
        idx = np.argmin(np.abs(freqs - freq_peak))
        axes[1].annotate(label, xy=(freqs[idx], fft_vals[idx]),
                        xytext=(freqs[idx] + 300, fft_vals[idx] + 0.05),
                        fontsize=10, fontweight="bold", color=color,
                        arrowprops=dict(arrowstyle="->", color=color, lw=1.5))

    fig.text(0.49, 0.5, "FFT", fontsize=16, fontweight="bold",
            ha="center", va="center", color="#2d3436",
            bbox=dict(boxstyle="rarrow,pad=0.3", facecolor="#dfe6e9",
                     edgecolor="#2d3436", linewidth=2))

    fig.suptitle("FFT: от времева област към честотен спектър (CMSIS-DSP)",
                 fontsize=14, fontweight="bold", y=1.03)
    fig.tight_layout()
    save(fig, "54_fft_time_freq.png")


# ── Ch 53: Ping-pong DTC buffer ──────────────────────────────────
def plot_pingpong_buffer():
    """Visual explanation of ping-pong double buffering for ADC/DAC."""
    fig, ax = plt.subplots(figsize=(12, 3.5))
    ax.set_xlim(0, 24)
    ax.set_ylim(0, 5)
    ax.axis("off")

    ax.annotate("", xy=(23, 0.3), xytext=(1, 0.3),
                arrowprops=dict(arrowstyle="->", lw=2, color="#2d3436"))
    ax.text(12, 0, "Време", ha="center", fontsize=10, color="#2d3436")

    for start in [1, 9, 17]:
        rect = plt.Rectangle((start, 2.5), 4, 1.5,
                             facecolor="#3498db", alpha=0.8, edgecolor="white", lw=2)
        ax.add_patch(rect)
        ax.text(start + 2, 3.25, f"Буфер A\n(DTC пълни)", ha="center",
               va="center", fontsize=9, fontweight="bold", color="white")

    for start in [5, 13, 21]:
        rect = plt.Rectangle((start, 2.5), 4, 1.5,
                             facecolor="#e74c3c", alpha=0.8, edgecolor="white", lw=2)
        ax.add_patch(rect)
        ax.text(start + 2, 3.25, f"Буфер B\n(DTC пълни)", ha="center",
               va="center", fontsize=9, fontweight="bold", color="white")

    for start in [5, 13, 21]:
        rect = plt.Rectangle((start, 0.8), 4, 1.2,
                             facecolor="#27ae60", alpha=0.6, edgecolor="white", lw=2)
        ax.add_patch(rect)
        ax.text(start + 2, 1.4, "CPU обработва A", ha="center",
               va="center", fontsize=8, fontweight="bold", color="white")

    for start in [9, 17]:
        rect = plt.Rectangle((start, 0.8), 4, 1.2,
                             facecolor="#f39c12", alpha=0.6, edgecolor="white", lw=2)
        ax.add_patch(rect)
        ax.text(start + 2, 1.4, "CPU обработва B", ha="center",
               va="center", fontsize=8, fontweight="bold", color="white")

    ax.text(0.3, 3.25, "DTC:", fontsize=11, fontweight="bold",
           color="#2d3436", va="center")
    ax.text(0.3, 1.4, "CPU:", fontsize=11, fontweight="bold",
           color="#2d3436", va="center")

    fig.suptitle("Ping-pong буфериране: DTC пълни единия, CPU обработва другия — без загуба на семпли",
                 fontsize=13, fontweight="bold", y=0.98)
    save(fig, "53_pingpong_buffer.png")


# ── Ch 55: 7-band spectrum analyzer ──────────────────────────────
def plot_spectrum_7band():
    """MSGEQ7-style 7-band spectrum display."""
    fig, ax = plt.subplots(figsize=(10, 5))

    bands = ["63 Hz", "160 Hz", "400 Hz", "1 kHz", "2.5 kHz", "6.3 kHz", "16 kHz"]
    levels = [0.55, 0.75, 0.40, 0.90, 0.85, 0.60, 0.30]
    n_rows = 8
    bar_width = 0.7

    for i, (band, level) in enumerate(zip(bands, levels)):
        n_lit = int(level * n_rows)
        for row in range(n_rows):
            if row < n_lit:
                if row >= 6:
                    color = "#e74c3c"
                elif row >= 3:
                    color = "#f39c12"
                else:
                    color = "#27ae60"
                alpha = 0.85
            else:
                color = "#dfe6e9"
                alpha = 0.3

            rect = plt.Rectangle((i - bar_width/2, row), bar_width, 0.85,
                                facecolor=color, alpha=alpha, edgecolor="white",
                                linewidth=1.5)
            ax.add_patch(rect)

    ax.set_xlim(-0.8, len(bands) - 0.2)
    ax.set_ylim(-0.5, n_rows + 1)
    ax.set_xticks(range(len(bands)))
    ax.set_xticklabels(bands, fontsize=10, fontweight="bold")
    ax.set_yticks([])
    ax.set_xlabel("Честотна лента", fontsize=11)
    ax.set_title("7-лентов спектрален анализатор (MSGEQ7 емулация)\n"
                 "Зелено < 37% | Жълто 37-75% | Червено > 75%",
                 fontsize=13, fontweight="bold")

    for i, level in enumerate(levels):
        ax.text(i, int(level * n_rows) + 0.3, f"{int(level*100)}%",
               ha="center", fontsize=9, fontweight="bold", color="#2d3436")

    ax.set_aspect("equal")
    fig.tight_layout()
    save(fig, "55_spectrum_7band.png")


if __name__ == "__main__":
    print("Generating book plots...")
    plot_digital_vs_analog()
    plot_polling_vs_irq()
    plot_bounce_debounce()
    plot_raw_to_events()
    plot_blocking_vs_async()
    plot_filtering_hysteresis()
    plot_fft_time_freq()
    plot_pingpong_buffer()
    plot_spectrum_7band()
    print("Done.")
