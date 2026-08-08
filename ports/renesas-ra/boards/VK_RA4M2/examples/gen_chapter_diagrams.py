#!/usr/bin/env python3
"""Generate matplotlib diagrams replacing ASCII art in ch23-ch50.

Produces: 23_i2c_arch.png, 24_spi_wiring.png, 27_frame_structure.png,
29_memory_map.png, 41_fsm_lamp.png, 42_fsm_timeout.png, 43_cond_vs_event.png,
44_guard_gate.png, 45_event_queue.png, 47_error_flow.png,
48_watchdog_cycle.png, 50_class_hierarchy.png
"""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, ArrowStyle

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "img")
os.makedirs(OUT, exist_ok=True)
DPI = 150

def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=DPI, bbox_inches="tight",
                facecolor="white", edgecolor="none")
    plt.close(fig)
    print(f"  OK {name}")

def box(ax, x, y, w, h, text, color="#3498db", tc="white", fs=9, alpha=0.9):
    r = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.15",
                       facecolor=color, edgecolor="white", linewidth=1.5, alpha=alpha)
    ax.add_patch(r)
    ax.text(x + w/2, y + h/2, text, ha="center", va="center",
            fontsize=fs, fontweight="bold", color=tc, linespacing=1.3)

def arrow(ax, x1, y1, x2, y2, color="#2d3436", style="->"):
    ax.annotate("", xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, lw=2, color=color))


# ── Ch23: I2C Architecture ───────────────────────────────────────
def plot_i2c_arch():
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.set_xlim(0, 12); ax.set_ylim(0, 4); ax.axis("off")

    # Hardware I2C
    box(ax, 0.2, 2.2, 2.5, 1.5, "Hardware I2C\nконтролер\n(фиксирани пинове)", "#3498db")
    ax.text(3.0, 3.3, "SCL (auto OD)", fontsize=8, color="#2d3436")
    ax.text(3.0, 2.8, "SDA (auto OD)", fontsize=8, color="#2d3436")
    arrow(ax, 2.7, 3.2, 3.8, 3.2, "#3498db")
    arrow(ax, 2.7, 2.7, 3.8, 2.7, "#3498db")

    # SoftI2C
    box(ax, 0.2, 0.2, 2.5, 1.5, "SoftI2C\nbit-bang GPIO\n(произволни пинове)", "#e74c3c")
    ax.text(3.0, 1.3, "SCL (OPEN_DRAIN)", fontsize=8, color="#2d3436")
    ax.text(3.0, 0.8, "SDA (OPEN_DRAIN)", fontsize=8, color="#2d3436")
    arrow(ax, 2.7, 1.2, 3.8, 1.2, "#e74c3c")
    arrow(ax, 2.7, 0.7, 3.8, 0.7, "#e74c3c")

    # I2C bus
    ax.plot([4.5, 4.5], [0.5, 3.5], color="#2d3436", linewidth=3, alpha=0.3)
    ax.text(4.7, 2.0, "I2C\nшина", fontsize=10, fontweight="bold", color="#636e72")

    # I2CTarget
    box(ax, 6, 1.5, 3, 1.5, "I2CTarget\naddr=0x42\nmem=bytearray(16)", "#27ae60")
    arrow(ax, 4.6, 2.2, 6, 2.2, "#2d3436", "<->")

    # Master
    box(ax, 9.5, 1.5, 2, 1.5, "Master\nscan()\nread/write", "#f39c12")
    arrow(ax, 9, 2.2, 9.5, 2.2, "#2d3436", "<->")

    fig.suptitle("I2C: Hardware vs SoftI2C + I2CTarget буфер",
                 fontsize=13, fontweight="bold")
    save(fig, "23_i2c_arch.png")


# ── Ch24: SPI Wiring ─────────────────────────────────────────────
def plot_spi_wiring():
    fig, ax = plt.subplots(figsize=(11, 4))
    ax.set_xlim(0, 11); ax.set_ylim(0, 4); ax.axis("off")

    box(ax, 0.3, 0.3, 3, 3.2, "MCU (Master)\n\nSCK  (P102)\nMOSI (P101)\nMISO (P100)\nCS   (P103)", "#3498db", fs=10)
    box(ax, 7.5, 0.3, 3, 3.2, "Slave\nустройство\n\nSCK\nMOSI (вход)\nMISO (изход)\nCS", "#e74c3c", fs=10)

    signals = [
        (3.3, 3.0, 7.5, 3.0, "SCK", "#2ecc71", "->"),
        (3.3, 2.4, 7.5, 2.4, "MOSI", "#f39c12", "->"),
        (7.5, 1.8, 3.3, 1.8, "MISO", "#9b59b6", "->"),
        (3.3, 1.2, 7.5, 1.2, "CS (active LOW)", "#e74c3c", "->"),
    ]
    for x1, y1, x2, y2, label, color, style in signals:
        arrow(ax, x1, y1, x2, y2, color, style)
        mx = (x1 + x2) / 2
        ax.text(mx, y1 + 0.15, label, ha="center", fontsize=9,
               fontweight="bold", color=color)

    ax.text(5.4, 0.15, "full-duplex: MOSI и MISO работят едновременно",
           ha="center", fontsize=9, color="#636e72", fontstyle="italic")

    fig.suptitle("SPI: 4-проводна full-duplex комуникация",
                 fontsize=13, fontweight="bold")
    save(fig, "24_spi_wiring.png")


# ── Ch27: Frame Structure ────────────────────────────────────────
def plot_frame_structure():
    fig, ax = plt.subplots(figsize=(11, 3))
    ax.set_xlim(0, 11); ax.set_ylim(0, 3); ax.axis("off")

    fields = [
        (0.5, 2, "Header\n0xAA", "#e74c3c"),
        (2.5, 2, "Payload\nlow byte", "#3498db"),
        (4.5, 2, "Payload\nhigh byte", "#3498db"),
        (6.5, 2, "...", "#95a5a6"),
        (8.0, 2, "Checksum\nXOR", "#27ae60"),
    ]
    widths = [1.8, 1.8, 1.8, 1.3, 1.8]
    x = 0.5
    for i, (_, _, label, color) in enumerate(fields):
        w = widths[i]
        box(ax, x, 1.2, w, 1.2, label, color, fs=9)
        ax.text(x + w/2, 0.9, f"byte[{i}]" if i < 4 else "byte[-1]",
               ha="center", fontsize=8, color="#636e72")
        x += w + 0.15

    # Annotations
    ax.annotate("", xy=(0.5, 0.5), xytext=(x - 0.15, 0.5),
               arrowprops=dict(arrowstyle="<->", color="#2d3436", lw=1.5))
    ax.text(4.5, 0.2, "bytearray(N)", ha="center", fontsize=10,
           fontweight="bold", color="#2d3436")

    ax.annotate("", xy=(2.5, 2.6), xytext=(6.3, 2.6),
               arrowprops=dict(arrowstyle="<->", color="#3498db", lw=1.5))
    ax.text(4.4, 2.75, "struct.pack_into()", ha="center", fontsize=9,
           fontweight="bold", color="#3498db")

    fig.suptitle("Двоичен кадър (frame): Header → Payload → Checksum",
                 fontsize=13, fontweight="bold")
    save(fig, "27_frame_structure.png")


# ── Ch29: Memory Map ─────────────────────────────────────────────
def plot_memory_map():
    fig, ax = plt.subplots(figsize=(6, 6))
    ax.set_xlim(0, 6); ax.set_ylim(0, 8); ax.axis("off")

    regions = [
        (0.5, 5.5, 5, 2, "Firmware / Application\n~400 KB", "#3498db"),
        (0.5, 3.8, 5, 1.5, "/flash файлова система\nFAT, .py файлове (~112 KB)", "#f39c12"),
        (0.5, 2.5, 5, 1.1, "Data Flash (8 KB)\nконфигурация, state", "#e74c3c"),
        (0.5, 0.5, 5, 1.8, "RAM (128 KB)\nheap, буфери, стек\n(volatile — губи се при reset)", "#27ae60"),
    ]
    for x, y, w, h, label, color in regions:
        box(ax, x, y, w, h, label, color, fs=10)

    # Address labels
    ax.text(0.3, 7.6, "0x0000_0000", fontsize=8, color="#636e72", ha="right",
           fontfamily="monospace")
    ax.text(5.7, 5.5, "code flash\n(512 KB)", fontsize=8, color="#636e72",
           va="center")
    ax.text(5.7, 2.5, "data flash", fontsize=8, color="#636e72", va="center")
    ax.text(5.7, 1.4, "SRAM", fontsize=8, color="#636e72", va="center")

    fig.suptitle("VK_RA4M2: Memory Map (RA4M2)",
                 fontsize=14, fontweight="bold")
    save(fig, "29_memory_map.png")


# ── Ch41: FSM Lamp ON/OFF ────────────────────────────────────────
def plot_fsm_lamp():
    fig, ax = plt.subplots(figsize=(10, 4))
    ax.set_xlim(0, 10); ax.set_ylim(0, 4); ax.axis("off")

    # States
    box(ax, 1, 1, 2.5, 2, "OFF\n(лампа\nизгасена)", "#636e72", fs=12)
    box(ax, 6, 1, 2.5, 2, "ON\n(лампа\nсветеща)", "#f39c12", fs=12)

    # Transitions
    ax.annotate("", xy=(6, 2.5), xytext=(3.5, 2.5),
               arrowprops=dict(arrowstyle="-|>", lw=2.5, color="#27ae60",
                              connectionstyle="arc3,rad=0.3"))
    ax.text(4.75, 3.3, "BUTTON", fontsize=11, fontweight="bold",
           color="#27ae60", ha="center")

    ax.annotate("", xy=(3.5, 1.5), xytext=(6, 1.5),
               arrowprops=dict(arrowstyle="-|>", lw=2.5, color="#e74c3c",
                              connectionstyle="arc3,rad=0.3"))
    ax.text(4.75, 0.5, "BUTTON", fontsize=11, fontweight="bold",
           color="#e74c3c", ha="center")

    # Initial state
    ax.annotate("", xy=(1, 2), xytext=(0.3, 2),
               arrowprops=dict(arrowstyle="-|>", lw=2, color="#2d3436"))
    ax.plot(0.3, 2, "o", color="#2d3436", markersize=10)

    fig.suptitle("FSM: Лампа ON/OFF — превключване с бутон",
                 fontsize=13, fontweight="bold")
    save(fig, "41_fsm_lamp.png")


# ── Ch42: FSM with Timeout ───────────────────────────────────────
def plot_fsm_timeout():
    fig, ax = plt.subplots(figsize=(10, 4.5))
    ax.set_xlim(0, 10); ax.set_ylim(0, 4.5); ax.axis("off")

    box(ax, 1, 1.2, 2.5, 2, "OFF\n(изгасена)", "#636e72", fs=12)
    box(ax, 6, 1.2, 2.5, 2, "ON\n(светеща)", "#f39c12", fs=12)

    # BUTTON → ON
    ax.annotate("", xy=(6, 2.7), xytext=(3.5, 2.7),
               arrowprops=dict(arrowstyle="-|>", lw=2.5, color="#27ae60",
                              connectionstyle="arc3,rad=0.3"))
    ax.text(4.75, 3.5, "BUTTON", fontsize=11, fontweight="bold",
           color="#27ae60", ha="center")

    # BUTTON → OFF
    ax.annotate("", xy=(3.5, 1.7), xytext=(6, 1.7),
               arrowprops=dict(arrowstyle="-|>", lw=2, color="#e74c3c",
                              connectionstyle="arc3,rad=0.3"))
    ax.text(4.75, 0.7, "BUTTON", fontsize=10, fontweight="bold",
           color="#e74c3c", ha="center")

    # TIMEOUT → OFF (self-loop from ON back to OFF, curved above)
    ax.annotate("", xy=(3.5, 2.2), xytext=(6, 2.2),
               arrowprops=dict(arrowstyle="-|>", lw=2, color="#9b59b6",
                              connectionstyle="arc3,rad=-0.5"))
    ax.text(4.75, 1.95, "TIMEOUT", fontsize=10, fontweight="bold",
           color="#9b59b6", ha="center",
           bbox=dict(boxstyle="round,pad=0.2", facecolor="white", alpha=0.8))

    # Initial
    ax.plot(0.3, 2.2, "o", color="#2d3436", markersize=10)
    ax.annotate("", xy=(1, 2.2), xytext=(0.3, 2.2),
               arrowprops=dict(arrowstyle="-|>", lw=2, color="#2d3436"))

    ax.text(5, 0.15, "TIMEOUT = ако няма BUTTON за N ms → автоматичен OFF",
           ha="center", fontsize=9, color="#9b59b6", fontstyle="italic")

    fig.suptitle("FSM с timeout: автоматично изгасване",
                 fontsize=13, fontweight="bold")
    save(fig, "42_fsm_timeout.png")


# ── Ch43: Condition vs Event ─────────────────────────────────────
def plot_cond_vs_event():
    fig, axes = plt.subplots(1, 2, figsize=(12, 3.5))
    np.random.seed(43)

    # Left: Condition (level)
    t = np.linspace(0, 10, 1000)
    light = 600 + 300 * np.sin(2 * np.pi * 0.2 * t) + np.random.normal(0, 30, len(t))
    axes[0].plot(t, light, color="#f39c12", linewidth=1.2)
    axes[0].axhline(800, color="#e74c3c", linewidth=2, linestyle="--",
                   label="праг = 800")
    axes[0].fill_between(t, 0, light, where=light < 800, alpha=0.2, color="#f39c12")
    axes[0].set_title("Condition (ниво)\nlight_level < 800?",
                      fontsize=12, fontweight="bold", color="#f39c12")
    axes[0].set_ylabel("ADC стойност")
    axes[0].set_xlabel("Време (s)")
    axes[0].legend(fontsize=9)
    axes[0].text(5, 300, "вярно дълго време", ha="center", fontsize=10,
                color="#636e72", fontstyle="italic")
    axes[0].grid(True, alpha=0.15)

    # Right: Event (discrete)
    t2 = np.linspace(0, 10, 1000)
    btn = np.ones_like(t2) * 3.3
    btn[(t2 >= 3.0) & (t2 < 3.1)] = 0
    btn[(t2 >= 6.5) & (t2 < 6.6)] = 0
    btn_n = btn + np.random.normal(0, 0.03, len(t2))
    axes[1].plot(t2, btn_n, color="#3498db", linewidth=1.2)
    axes[1].set_title("Event (дискретен факт)\nBUTTON натиснат?",
                      fontsize=12, fontweight="bold", color="#3498db")
    axes[1].set_ylabel("V")
    axes[1].set_xlabel("Време (s)")
    for tx in [3.0, 6.5]:
        axes[1].annotate("EVENT", xy=(tx, 0), xytext=(tx + 0.5, 1.5),
                        fontsize=10, fontweight="bold", color="#e74c3c",
                        arrowprops=dict(arrowstyle="->", color="#e74c3c"))
    axes[1].text(5, 1.0, "еднократен момент", ha="center", fontsize=10,
                color="#636e72", fontstyle="italic")
    axes[1].grid(True, alpha=0.15)

    fig.suptitle("Condition (ниво) vs Event (момент) — два различни вида тригер за FSM",
                 fontsize=13, fontweight="bold", y=1.05)
    fig.tight_layout()
    save(fig, "43_cond_vs_event.png")


# ── Ch44: Guard Gate ─────────────────────────────────────────────
def plot_guard_gate():
    fig, ax = plt.subplots(figsize=(10, 3.5))
    ax.set_xlim(0, 10); ax.set_ylim(0, 3.5); ax.axis("off")

    box(ax, 0.3, 1, 2, 1.5, "OFF", "#636e72", fs=14)
    box(ax, 7.5, 1, 2, 1.5, "ON", "#f39c12", fs=14)

    # Guard gate
    box(ax, 4, 0.5, 2, 2.5, "Guard Gate\n\nis_dark()?\nhas_motion()?", "#9b59b6", fs=9)

    # Event arrow → gate
    arrow(ax, 2.3, 1.75, 4, 1.75, "#27ae60")
    ax.text(3.15, 2.0, "MOTION", fontsize=10, fontweight="bold", color="#27ae60")

    # Gate → ON (true)
    arrow(ax, 6, 2.2, 7.5, 2.2, "#27ae60")
    ax.text(6.7, 2.5, "True", fontsize=10, fontweight="bold", color="#27ae60")

    # Gate → blocked (false)
    ax.annotate("", xy=(5, 0.2), xytext=(5, 0.5),
               arrowprops=dict(arrowstyle="-|>", lw=2, color="#e74c3c"))
    ax.text(5, 0.0, "False → блокиран\n(оставаме в OFF)", ha="center",
           fontsize=9, color="#e74c3c", fontweight="bold")

    fig.suptitle("Guard функция: портал, който разрешава или блокира прехода",
                 fontsize=13, fontweight="bold")
    save(fig, "44_guard_gate.png")


# ── Ch45: Event Queue ────────────────────────────────────────────
def plot_event_queue():
    fig, ax = plt.subplots(figsize=(11, 4))
    ax.set_xlim(0, 11); ax.set_ylim(0, 4); ax.axis("off")

    # Incoming events
    box(ax, 0.3, 2.5, 2, 0.8, "BUTTON", "#3498db", fs=10)
    box(ax, 0.3, 1.5, 2, 0.8, "ERROR", "#e74c3c", fs=10)
    box(ax, 0.3, 0.5, 2, 0.8, "TIMEOUT", "#f39c12", fs=10)
    ax.text(1.3, 3.5, "Входящи\nсъбития", ha="center", fontsize=10,
           fontweight="bold", color="#636e72")

    # Sort arrow
    arrow(ax, 2.3, 1.9, 3.8, 1.9, "#2d3436")
    ax.text(3.0, 2.2, "sort()", fontsize=9, fontweight="bold", color="#2d3436")

    # Sorted queue
    box(ax, 4, 2.5, 2.8, 0.8, "0: ERROR", "#e74c3c", fs=10)
    box(ax, 4, 1.5, 2.8, 0.8, "1: BUTTON", "#3498db", fs=10)
    box(ax, 4, 0.5, 2.8, 0.8, "2: TIMEOUT", "#f39c12", fs=10)
    ax.text(5.4, 3.5, "Опашка\n(по приоритет)", ha="center", fontsize=10,
           fontweight="bold", color="#636e72")

    # FSM processing
    arrow(ax, 6.8, 2.9, 8, 2.9, "#e74c3c")
    box(ax, 8, 1.2, 2.5, 2, "FSM\nобработва\nпо ред", "#27ae60", fs=10)
    ax.text(9.25, 3.5, "обработва\nпърво", ha="center", fontsize=9,
           color="#e74c3c", fontweight="bold")
    ax.text(5.4, 0.15, "priority: ERROR=0 > BUTTON=1 > TIMEOUT=2",
           ha="center", fontsize=9, color="#636e72", fontstyle="italic")

    fig.suptitle("Event Queue с приоритети: ERROR винаги се обработва пръв",
                 fontsize=13, fontweight="bold")
    save(fig, "45_event_queue.png")


# ── Ch47: Error Handling Flow ────────────────────────────────────
def plot_error_flow():
    fig, ax = plt.subplots(figsize=(11, 4.5))
    ax.set_xlim(0, 11); ax.set_ylim(0, 4.5); ax.axis("off")

    box(ax, 0.3, 2, 2.2, 1.5, "Операция\n(I2C, UART...)", "#3498db", fs=10)

    # OK path
    arrow(ax, 2.5, 3.0, 4, 3.0, "#27ae60")
    ax.text(3.2, 3.2, "OK", fontsize=10, fontweight="bold", color="#27ae60")
    box(ax, 4, 2.5, 2.5, 1, "Продължи\nнормално", "#27ae60", fs=10)

    # Error path
    arrow(ax, 2.5, 2.3, 4, 1.3, "#e74c3c")
    ax.text(2.8, 1.6, "OSError", fontsize=9, fontweight="bold", color="#e74c3c")

    # Retry loop
    box(ax, 4, 0.3, 2.2, 1.2, "Изчакай 50ms\nretry < 3", "#f39c12", fs=9)
    ax.annotate("", xy=(2.5, 2.3), xytext=(4, 1.0),
               arrowprops=dict(arrowstyle="-|>", lw=1.5, color="#f39c12",
                              connectionstyle="arc3,rad=-0.4"))
    ax.text(2.5, 0.8, "опитай\nотново", fontsize=8, color="#f39c12",
           ha="center")

    # Fail-safe
    arrow(ax, 6.2, 0.9, 7.5, 0.9, "#e74c3c")
    ax.text(6.8, 1.15, "retry >= 3", fontsize=8, color="#e74c3c")
    box(ax, 7.5, 0.3, 2.5, 1.2, "Log +\nFail-safe\nstate", "#e74c3c", fs=10)

    fig.suptitle("Error handling: retry → fail-safe",
                 fontsize=13, fontweight="bold")
    save(fig, "47_error_flow.png")


# ── Ch48: Watchdog Cycle ─────────────────────────────────────────
def plot_watchdog_cycle():
    fig, axes = plt.subplots(1, 2, figsize=(12, 3.5))

    for ax in axes:
        ax.set_xlim(0, 6); ax.set_ylim(0, 3.5); ax.axis("off")

    # Left: Normal
    axes[0].set_title("Нормална работа", fontsize=12, fontweight="bold",
                     color="#27ae60")
    box(axes[0], 0.2, 1, 1.5, 1.2, "Main\nloop", "#3498db", fs=10, alpha=0.9)
    box(axes[0], 2.2, 1, 1.5, 1.2, "Health\ncheck", "#27ae60", fs=10)
    box(axes[0], 4.2, 1, 1.5, 1.2, "WDT\nfeed()\ntimer=0", "#f39c12", fs=10)
    arrow(axes[0], 1.7, 1.6, 2.2, 1.6, "#27ae60")
    arrow(axes[0], 3.7, 1.6, 4.2, 1.6, "#27ae60")
    axes[0].annotate("", xy=(0.9, 2.4), xytext=(4.9, 2.4),
                    arrowprops=dict(arrowstyle="<-", lw=1.5, color="#636e72",
                                   connectionstyle="arc3,rad=0.4"))
    axes[0].text(2.9, 2.9, "цикъл продължава", fontsize=9,
                color="#636e72", ha="center")

    # Right: Stuck
    axes[1].set_title("Заседнала система", fontsize=12, fontweight="bold",
                     color="#e74c3c")
    box(axes[1], 0.2, 1, 1.5, 1.2, "Main\nloop\nSTUCK!", "#e74c3c", fs=10)
    box(axes[1], 2.5, 1, 1.5, 1.2, "Без\nfeed()", "#636e72", fs=10)
    box(axes[1], 4.5, 1, 1.2, 1.2, "WDT\nRESET!", "#e74c3c", fs=11)
    axes[1].text(1.0, 2.5, "XXXXXXX", fontsize=14, color="#e74c3c",
                fontweight="bold", ha="center", alpha=0.5)
    arrow(axes[1], 4, 1.6, 4.5, 1.6, "#e74c3c")
    axes[1].text(4.2, 2.5, "timeout!", fontsize=10, color="#e74c3c",
                fontweight="bold")

    fig.suptitle("Watchdog: feed() нулира таймера, без feed() → RESET",
                 fontsize=13, fontweight="bold", y=1.02)
    fig.tight_layout()
    save(fig, "48_watchdog_cycle.png")


# ── Ch50: Class Hierarchy ────────────────────────────────────────
def plot_class_hierarchy():
    fig, ax = plt.subplots(figsize=(10, 4.5))
    ax.set_xlim(0, 10); ax.set_ylim(0, 4.5); ax.axis("off")

    # App layer
    box(ax, 1, 3.2, 8, 1, "Application Logic\n(FSM, main loop, asyncio tasks)", "#636e72", fs=11)

    # Driver layer
    box(ax, 1, 1.5, 3.5, 1.5, "StatusLed\n\n.on()  .off()  .toggle()\nPin(\"LED1\")", "#3498db", fs=9)
    box(ax, 5.5, 1.5, 3.5, 1.5, "ThresholdADC\n\n.is_active()  .read_raw()\nADC(pin) + threshold", "#27ae60", fs=9)

    # Arrows
    arrow(ax, 2.75, 3.2, 2.75, 3.0, "#2d3436")
    arrow(ax, 7.25, 3.2, 7.25, 3.0, "#2d3436")

    # Hardware layer
    box(ax, 1.5, 0.2, 2.5, 0.8, "GPIO хардуер", "#e74c3c", fs=9)
    box(ax, 6, 0.2, 2.5, 0.8, "ADC хардуер", "#e74c3c", fs=9)
    arrow(ax, 2.75, 1.5, 2.75, 1.0, "#e74c3c")
    arrow(ax, 7.25, 1.5, 7.25, 1.0, "#e74c3c")

    # Labels
    ax.text(0.3, 3.7, "App", fontsize=9, color="#636e72", fontweight="bold")
    ax.text(0.3, 2.2, "Driver", fontsize=9, color="#3498db", fontweight="bold")
    ax.text(0.3, 0.5, "HW", fontsize=9, color="#e74c3c", fontweight="bold")

    fig.suptitle("Капсулиране: App → Driver → Hardware",
                 fontsize=13, fontweight="bold")
    save(fig, "50_class_hierarchy.png")


if __name__ == "__main__":
    print("Generating chapter diagrams...")
    plot_i2c_arch()
    plot_spi_wiring()
    plot_frame_structure()
    plot_memory_map()
    plot_fsm_lamp()
    plot_fsm_timeout()
    plot_cond_vs_event()
    plot_guard_gate()
    plot_event_queue()
    plot_error_flow()
    plot_watchdog_cycle()
    plot_class_hierarchy()
    print("Done — 12 diagrams.")
