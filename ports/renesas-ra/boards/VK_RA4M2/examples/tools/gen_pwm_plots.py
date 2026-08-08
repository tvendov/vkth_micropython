#!/usr/bin/env python3
"""Generate oscilloscope-style PWM plots for Chapter 14 (PWM)."""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT, exist_ok=True)
DPI = 150
np.random.seed(14)

# ── Noise helpers ────────────────────────────────────────────────
def scope_noise(y, sigma=0.04):
    """Add gentle Gaussian noise like a real oscilloscope trace."""
    return y + np.random.normal(0, sigma, len(y))

def edge_ringing(y, t, period, rise_tau=0.02, ring_amp=0.12, ring_freq=25):
    """Add tiny ringing / overshoot at rising and falling edges."""
    dy = np.diff(y, prepend=y[0])
    dt = t[1] - t[0]
    ring = np.zeros_like(y)
    edges = np.where(np.abs(dy) > 1.0)[0]
    for e in edges:
        tt = t[e:] - t[e]
        envelope = ring_amp * np.exp(-tt / rise_tau)
        osc = np.sin(2 * np.pi * ring_freq * tt) * np.sign(dy[e])
        ring[e:] += envelope * osc
    return y + ring


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=DPI, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)
    print(f"  OK {name}")


def plot_pwm_duty():
    """Three duty cycles: 25%, 50%, 75% — oscilloscope style with noise."""
    fig, axes = plt.subplots(3, 1, figsize=(12, 6), sharex=True, sharey=True)

    freq = 1000
    period = 1.0 / freq
    t = np.linspace(0, 4 * period, 4000)

    for ax, duty, color, avg_color in [
        (axes[0], 25,  "#e74c3c", "#e74c3c"),
        (axes[1], 50,  "#f39c12", "#f39c12"),
        (axes[2], 75,  "#2ecc71", "#2ecc71"),
    ]:
        phase = (t % period) / period
        y = np.where(phase < duty / 100.0, 3.3, 0.0)
        y_noisy = np.clip(scope_noise(edge_ringing(y, t * 1000, period * 1000), 0.035), -0.15, 3.45)
        avg = 3.3 * duty / 100.0

        ax.fill_between(t * 1000, 0, y, alpha=0.18, color=color, step="mid")
        ax.plot(t * 1000, y_noisy, color=color, linewidth=1.2)
        ax.axhline(avg, color=color, linewidth=1.5, linestyle="--", alpha=0.7)

        ax.set_ylabel("V", fontsize=11)
        ax.set_ylim(-0.3, 3.8)
        ax.set_yticks([0, avg, 3.3])
        ax.set_yticklabels(["0V", f"{avg:.2f}V", "3.3V"], fontsize=9)

        p_start, p_end = 0, period * 1000
        on_end = p_end * duty / 100.0
        ax.annotate("", xy=(on_end, 3.5), xytext=(p_start, 3.5),
                    arrowprops=dict(arrowstyle="<->", color=color, lw=1.5))
        ax.text((p_start + on_end) / 2, 3.6, f"ON ({duty}%)",
                ha="center", fontsize=9, color=color, fontweight="bold")
        ax.annotate("", xy=(p_end, 3.5), xytext=(on_end, 3.5),
                    arrowprops=dict(arrowstyle="<->", color="gray", lw=1))
        ax.text((on_end + p_end) / 2, 3.6, f"OFF ({100-duty}%)",
                ha="center", fontsize=8, color="gray")
        ax.text(3.7, avg + 0.15, f"duty={duty}%  →  средно {avg:.2f}V",
                fontsize=11, fontweight="bold", color=color)
        ax.grid(True, alpha=0.2)

    axes[2].set_xlabel("Време (ms)", fontsize=11)
    axes[0].set_xlim(0, 4)
    axes[0].annotate("", xy=(1.0, -0.2), xytext=(0, -0.2),
                    arrowprops=dict(arrowstyle="<->", color="#2d3436", lw=2))
    axes[0].text(0.5, -0.5, "1 период = 1/freq",
                ha="center", fontsize=10, color="#2d3436", fontweight="bold")
    fig.suptitle("PWM: duty cycle определя средното напрежение\n"
                 "freq = 1000 Hz (период = 1 ms)",
                 fontsize=14, fontweight="bold", y=1.04)
    fig.tight_layout()
    save(fig, "14_pwm_duty.png")


def plot_pwm_freq():
    """Same duty=50% at different frequencies — with noise."""
    fig, axes = plt.subplots(1, 3, figsize=(14, 3), sharey=True)

    duty = 50
    duration = 0.005

    for ax, freq, color in [
        (axes[0], 500,  "#3498db"),
        (axes[1], 1000, "#2ecc71"),
        (axes[2], 4000, "#e74c3c"),
    ]:
        period = 1.0 / freq
        t = np.linspace(0, duration, 5000)
        phase = (t % period) / period
        y = np.where(phase < duty / 100.0, 3.3, 0.0)
        y_noisy = np.clip(scope_noise(edge_ringing(y, t * 1000, period * 1000), 0.03), -0.15, 3.45)

        ax.fill_between(t * 1000, 0, y, alpha=0.15, color=color, step="mid")
        ax.plot(t * 1000, y_noisy, color=color, linewidth=1)
        ax.axhline(1.65, color=color, linewidth=1, linestyle="--", alpha=0.5)
        ax.set_title(f"freq = {freq} Hz\nпериод = {1000/freq:.1f} ms",
                     fontsize=11, fontweight="bold", color=color)
        ax.set_xlabel("Време (ms)", fontsize=9)
        ax.set_ylim(-0.3, 3.8)
        ax.set_xlim(0, 5)
        ax.grid(True, alpha=0.2)

    axes[0].set_ylabel("V", fontsize=11)
    fig.suptitle("PWM: duty=50%, различна честота — среднотото е винаги 1.65V",
                 fontsize=13, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "14_pwm_freq.png")


def plot_pwm_vs_dac():
    """PWM vs real DAC — comparison with noise."""
    fig, axes = plt.subplots(1, 2, figsize=(12, 3.5))

    t = np.linspace(0, 5, 5000)

    # Left: PWM approximation of sine
    freq = 1000
    period = 1.0 / freq
    duty_wave = 50 + 40 * np.sin(2 * np.pi * 2 * t / 5)
    phase = (t % (period * 1000)) / (period * 1000)
    y_pwm = np.where(phase < duty_wave / 100.0, 3.3, 0.0)
    y_pwm_noisy = np.clip(scope_noise(y_pwm, 0.04), -0.15, 3.45)

    axes[0].fill_between(t, 0, y_pwm, alpha=0.18, color="#e74c3c", step="mid")
    axes[0].plot(t, y_pwm_noisy, color="#e74c3c", linewidth=0.3)
    window = 50
    avg = np.convolve(y_pwm, np.ones(window)/window, mode="same")
    axes[0].plot(t, avg, "k-", linewidth=2, label="средна стойност")
    axes[0].set_title("PWM — цифров сигнал, аналогова средна стойност",
                      fontsize=11, fontweight="bold", color="#e74c3c")
    axes[0].legend(fontsize=9)
    axes[0].set_ylim(-0.3, 3.8)
    axes[0].set_ylabel("V")
    axes[0].set_xlabel("Време (ms)")

    # Right: real DAC sine with noise
    t_dac = np.linspace(0, 5, 500)
    y_dac = 1.65 + 1.3 * np.sin(2 * np.pi * 2 * t_dac / 5)
    y_dac_noisy = scope_noise(y_dac, 0.025)

    axes[1].plot(t_dac, y_dac_noisy, color="#2ecc71", linewidth=2)
    axes[1].fill_between(t_dac, 1.65, y_dac_noisy, alpha=0.15, color="#2ecc71")
    axes[1].axhline(1.65, color="gray", linewidth=0.5, linestyle="--")
    axes[1].set_title("DAC — истински аналогов изход",
                      fontsize=11, fontweight="bold", color="#2ecc71")
    axes[1].set_ylim(-0.3, 3.8)
    axes[1].set_ylabel("V")
    axes[1].set_xlabel("Време (ms)")

    fig.suptitle("PWM имитира аналогово ниво, DAC го генерира директно",
                 fontsize=13, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "14_pwm_vs_dac.png")


if __name__ == "__main__":
    print("Generating PWM plots...")
    plot_pwm_duty()
    plot_pwm_freq()
    plot_pwm_vs_dac()
    print("Done.")
