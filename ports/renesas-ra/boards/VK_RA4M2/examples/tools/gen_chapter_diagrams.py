#!/usr/bin/env python3
"""Generate PNG diagrams for book chapters 23,24,27,29,41-45,47,48,50.

Uses matplotlib patches/arrows for clean schematic-style diagrams.
Output: img/ directory (same level as book/).
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, ArrowStyle
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(SCRIPT_DIR, "..", "img")
os.makedirs(OUT, exist_ok=True)
DPI = 150

# ── Color palette ──
C_BLUE = "#2980b9"
C_GREEN = "#27ae60"
C_RED = "#e74c3c"
C_ORANGE = "#f39c12"
C_PURPLE = "#8e44ad"
C_GRAY = "#7f8c8d"
C_DARK = "#2c3e50"
C_LIGHT = "#ecf0f1"
C_YELLOW = "#f1c40f"


def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, dpi=DPI, bbox_inches="tight", facecolor="white", edgecolor="none")
    plt.close(fig)
    print(f"  OK {name}")


def box(ax, x, y, w, h, text, color=C_BLUE, fc=None, fs=10, bold=False):
    """Draw a rounded box with centered text."""
    if fc is None:
        fc = color + "22"
    rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.15",
                          facecolor=fc, edgecolor=color, linewidth=2)
    ax.add_patch(rect)
    weight = "bold" if bold else "normal"
    ax.text(x + w/2, y + h/2, text, ha="center", va="center",
            fontsize=fs, color=C_DARK, fontweight=weight)


def arrow(ax, x1, y1, x2, y2, text="", color=C_DARK, style="->"):
    ax.annotate("", xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, color=color, lw=1.8))
    if text:
        mx, my = (x1+x2)/2, (y1+y2)/2
        ax.text(mx, my + 0.15, text, ha="center", va="bottom",
                fontsize=8, color=color, fontstyle="italic")


def setup_ax(ax, xlim, ylim):
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    ax.set_aspect("equal")
    ax.axis("off")


# ════════════════════════════════════════════════════════════════
# Ch23: Hardware I2C vs SoftI2C + I2CTarget
# ════════════════════════════════════════════════════════════════
def ch23_i2c_comparison():
    fig, ax = plt.subplots(figsize=(10, 5))
    fig.suptitle("Hardware I2C vs SoftI2C + I2CTarget", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # Hardware I2C side
    box(ax, 0.5, 3.5, 2.5, 1, "Hardware I2C(1)\nP100=SCL, P101=SDA",
        C_BLUE, bold=True)
    box(ax, 0.5, 1.8, 2.5, 1, "SoftI2C\nGPIO bit-bang\nвсеки два пина",
        C_GREEN, bold=True)
    box(ax, 0.5, 0.2, 2.5, 1, "I2CTarget(1)\nmem buffer[16]\naddr=0x42",
        C_PURPLE, bold=True)

    # Bus
    ax.plot([4, 4], [0.5, 4.2], color=C_DARK, lw=3, solid_capstyle="round")
    ax.text(4.2, 4.0, "SCL", fontsize=9, color=C_DARK, fontweight="bold")
    ax.plot([5, 5], [0.5, 4.2], color=C_DARK, lw=3, solid_capstyle="round")
    ax.text(5.2, 4.0, "SDA", fontsize=9, color=C_DARK, fontweight="bold")

    # Pull-ups
    for xp in [4, 5]:
        ax.annotate("", xy=(xp, 4.7), xytext=(xp, 4.2),
                    arrowprops=dict(arrowstyle="-", color=C_RED, lw=1.5))
        ax.text(xp, 4.8, "3.3V\n4.7k", ha="center", fontsize=7, color=C_RED)

    # Connections
    arrow(ax, 3.0, 4.0, 3.9, 4.0, "auto OD", C_BLUE)
    arrow(ax, 3.0, 2.3, 3.9, 2.3, "Pin.OPEN_DRAIN", C_GREEN)
    arrow(ax, 3.0, 0.7, 3.9, 0.7, "responds", C_PURPLE)

    # Slaves
    for i, (addr, name) in enumerate([(0x48, "Temp"), (0x76, "Baro"), (0x3C, "OLED")]):
        yy = 3.2 - i * 1.2
        box(ax, 6, yy, 2, 0.8, f"Slave\n[{addr:#04x}] {name}", C_ORANGE)
        ax.plot([5.1, 6], [yy + 0.4, yy + 0.4], color=C_DARK, lw=1.2, ls="--")

    # Labels
    ax.text(1.75, 4.7, "VK_RA4M2 (Master / Target)", ha="center",
            fontsize=11, fontweight="bold", color=C_DARK)

    setup_ax(ax, (0, 9), (-0.3, 5.5))
    save(fig, "23_i2c_comparison.png")


# ════════════════════════════════════════════════════════════════
# Ch24: SPI wiring
# ════════════════════════════════════════════════════════════════
def ch24_spi_wiring():
    fig, ax = plt.subplots(figsize=(9, 4.5))
    fig.suptitle("SPI свързване — full-duplex, 4 линии", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # MCU box
    box(ax, 0.5, 0.5, 3, 3.5, "", C_BLUE)
    ax.text(2, 3.7, "VK_RA4M2\n(Master)", ha="center", fontsize=11,
            fontweight="bold", color=C_BLUE)

    # Slave box
    box(ax, 6.5, 0.5, 3, 3.5, "", C_ORANGE)
    ax.text(8, 3.7, "Slave\n(Sensor/Flash)", ha="center", fontsize=11,
            fontweight="bold", color=C_ORANGE)

    # Signal lines
    signals = [
        ("SCK  (P102)", "SCK",  3.0, C_PURPLE, "->"),
        ("MOSI (P101)", "MOSI", 2.3, C_GREEN,  "->"),
        ("MISO (P100)", "MISO", 1.6, C_RED,    "<-"),
        ("CS   (P103)", "CS",   0.9, C_DARK,   "->"),
    ]
    for label_l, label_r, y, color, direction in signals:
        ax.text(3.6, y + 0.05, label_l, fontsize=9, color=color,
                fontweight="bold", ha="left", va="center")
        ax.text(6.4, y + 0.05, label_r, fontsize=9, color=color,
                fontweight="bold", ha="right", va="center")
        if direction == "->":
            ax.annotate("", xy=(6.5, y), xytext=(3.5, y),
                        arrowprops=dict(arrowstyle="->", color=color, lw=2))
        else:
            ax.annotate("", xy=(3.5, y), xytext=(6.5, y),
                        arrowprops=dict(arrowstyle="->", color=color, lw=2))

    ax.text(5, 0.15, "Full-duplex: MOSI + MISO едновременно",
            ha="center", fontsize=9, color=C_GRAY, fontstyle="italic")

    setup_ax(ax, (0, 10), (-0.2, 4.5))
    save(fig, "24_spi_wiring.png")


# ════════════════════════════════════════════════════════════════
# Ch27: Frame structure
# ════════════════════════════════════════════════════════════════
def ch27_frame_structure():
    fig, ax = plt.subplots(figsize=(10, 3))
    fig.suptitle("Двоичен пакет — header / payload / checksum", fontsize=14,
                 fontweight="bold", color=C_DARK)

    fields = [
        ("Header\n0xAA", 1.5, C_PURPLE),
        ("Length\n(1 byte)", 1.5, C_BLUE),
        ("Payload Low\nstruct.pack('<H')", 2.5, C_GREEN),
        ("Payload High", 2.0, C_GREEN),
        ("Tag\n(1 byte)", 1.5, C_ORANGE),
        ("XOR Checksum", 2.0, C_RED),
    ]
    x = 0.3
    y = 0.8
    h = 1.2
    for label, w, color in fields:
        rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.08",
                              facecolor=color + "22", edgecolor=color, linewidth=2)
        ax.add_patch(rect)
        ax.text(x + w/2, y + h/2, label, ha="center", va="center",
                fontsize=9, color=C_DARK, fontweight="bold")
        # byte offset
        ax.text(x + w/2, y - 0.15, f"[{int(x*2 - 0.6):.0f}]", ha="center",
                fontsize=7, color=C_GRAY)
        x += w + 0.15

    # Bracket for checksum range
    ax.annotate("", xy=(0.3, 2.2), xytext=(x - 2.15, 2.2),
                arrowprops=dict(arrowstyle="|-|", color=C_RED, lw=1.5))
    ax.text((0.3 + x - 2.15)/2, 2.4, "XOR обхват (frame[:-1])",
            ha="center", fontsize=8, color=C_RED)

    # memoryview hint
    ax.text(5.5, 0.3, "memoryview(frame)[2:5]  →  payload без копиране",
            ha="center", fontsize=9, color=C_GRAY, fontstyle="italic")

    setup_ax(ax, (0, 12), (-0.1, 2.8))
    save(fig, "27_frame_structure.png")


# ════════════════════════════════════════════════════════════════
# Ch29: Memory map
# ════════════════════════════════════════════════════════════════
def ch29_memory_map():
    fig, ax = plt.subplots(figsize=(6, 6))
    fig.suptitle("VK_RA4M2 — карта на паметта", fontsize=14,
                 fontweight="bold", color=C_DARK)

    regions = [
        ("RAM (heap)\n128 KB", 4.0, C_BLUE,    "volatile — губи се при reset"),
        ("/flash (FAT FS)\n94 KB", 2.8, C_GREEN, ".py файлове, данни"),
        ("Firmware\n290 KB", 1.5, C_PURPLE,     "MicroPython интерпретатор"),
        ("Data Flash\n8 KB", 0.8, C_ORANGE,     "конфигурация, ~100K записа"),
    ]

    x, w = 1.5, 3.5
    y = 0.5
    for label, h, color, note in regions:
        rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.1",
                              facecolor=color + "22", edgecolor=color, linewidth=2)
        ax.add_patch(rect)
        ax.text(x + w/2, y + h/2, label, ha="center", va="center",
                fontsize=11, fontweight="bold", color=C_DARK)
        ax.text(x + w + 0.2, y + h/2, note, va="center", fontsize=8, color=C_GRAY)
        y += h + 0.15

    # Arrows
    ax.text(x + w/2, y + 0.2, "Висок адрес (0x2001FFFF)", ha="center",
            fontsize=8, color=C_GRAY)
    ax.text(x + w/2, 0.25, "Нисък адрес (0x00000000)", ha="center",
            fontsize=8, color=C_GRAY)

    setup_ax(ax, (0, 8.5), (0, y + 0.6))
    ax.set_aspect("auto")
    save(fig, "29_memory_map.png")


# ════════════════════════════════════════════════════════════════
# Ch41: Lamp FSM
# ════════════════════════════════════════════════════════════════
def ch41_fsm_lamp():
    fig, ax = plt.subplots(figsize=(8, 4))
    fig.suptitle("FSM: Лампа ON/OFF с бутон", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # States
    off_c = (2, 2)
    on_c = (6, 2)
    r = 0.9

    for cx, cy, label, color in [(2, 2, "OFF", C_BLUE), (6, 2, "ON", C_GREEN)]:
        circle = plt.Circle((cx, cy), r, facecolor=color + "22",
                            edgecolor=color, linewidth=3)
        ax.add_patch(circle)
        ax.text(cx, cy, label, ha="center", va="center", fontsize=16,
                fontweight="bold", color=C_DARK)

    # Initial arrow
    ax.annotate("", xy=(2 - r, 2), xytext=(0.3, 2),
                arrowprops=dict(arrowstyle="-|>", color=C_DARK, lw=2))
    ax.text(0.3, 2.3, "start", fontsize=9, color=C_GRAY)

    # Transitions
    ax.annotate("", xy=(6 - r, 2.4), xytext=(2 + r, 2.4),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2.5,
                                connectionstyle="arc3,rad=-0.2"))
    ax.text(4, 3.0, "BUTTON", ha="center", fontsize=11, fontweight="bold",
            color=C_RED)

    ax.annotate("", xy=(2 + r, 1.6), xytext=(6 - r, 1.6),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2.5,
                                connectionstyle="arc3,rad=-0.2"))
    ax.text(4, 0.9, "BUTTON", ha="center", fontsize=11, fontweight="bold",
            color=C_RED)

    # Actions
    ax.text(2, 0.7, "led.value(1)  # гаси", ha="center", fontsize=8,
            color=C_GRAY, fontstyle="italic")
    ax.text(6, 0.7, "led.value(0)  # светва", ha="center", fontsize=8,
            color=C_GRAY, fontstyle="italic")

    setup_ax(ax, (-0.2, 8.2), (0.2, 3.8))
    save(fig, "41_fsm_lamp.png")


# ════════════════════════════════════════════════════════════════
# Ch42: FSM with timeout
# ════════════════════════════════════════════════════════════════
def ch42_fsm_timeout():
    fig, ax = plt.subplots(figsize=(9, 5))
    fig.suptitle("FSM с timeout — автоматично изгасване", fontsize=14,
                 fontweight="bold", color=C_DARK)

    states = [(2, 3, "OFF", C_BLUE), (6, 3, "ON", C_GREEN), (6, 0.8, "DIMMING", C_ORANGE)]
    r = 0.85

    for cx, cy, label, color in states:
        circle = plt.Circle((cx, cy), r, facecolor=color + "22",
                            edgecolor=color, linewidth=3)
        ax.add_patch(circle)
        ax.text(cx, cy, label, ha="center", va="center", fontsize=13,
                fontweight="bold", color=C_DARK)

    # start
    ax.annotate("", xy=(2 - r, 3), xytext=(0.2, 3),
                arrowprops=dict(arrowstyle="-|>", color=C_DARK, lw=2))

    # OFF -> ON (BUTTON)
    ax.annotate("", xy=(6 - r, 3.3), xytext=(2 + r, 3.3),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2.2,
                                connectionstyle="arc3,rad=-0.15"))
    ax.text(4, 3.85, "BUTTON", ha="center", fontsize=10, fontweight="bold", color=C_RED)

    # ON -> DIMMING (TIMEOUT 5s)
    ax.annotate("", xy=(6, 0.8 + r), xytext=(6, 3 - r),
                arrowprops=dict(arrowstyle="-|>", color=C_ORANGE, lw=2.2))
    ax.text(6.6, 1.9, "TIMEOUT\n(5 sec)", ha="left", fontsize=9,
            fontweight="bold", color=C_ORANGE)

    # DIMMING -> OFF (TIMEOUT 2s)
    ax.annotate("", xy=(2 + r, 2.7), xytext=(6 - r, 0.8),
                arrowprops=dict(arrowstyle="-|>", color=C_PURPLE, lw=2.2,
                                connectionstyle="arc3,rad=0.2"))
    ax.text(3.5, 1.2, "TIMEOUT\n(2 sec)", ha="center", fontsize=9,
            fontweight="bold", color=C_PURPLE)

    # ON -> OFF (BUTTON)
    ax.annotate("", xy=(2 + r, 2.7), xytext=(6 - r, 2.7),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2.2,
                                connectionstyle="arc3,rad=0.15"))
    ax.text(4, 2.2, "BUTTON", ha="center", fontsize=10, fontweight="bold", color=C_RED)

    setup_ax(ax, (-0.3, 9), (-0.2, 4.8))
    save(fig, "42_fsm_timeout.png")


# ════════════════════════════════════════════════════════════════
# Ch43: Condition vs Event
# ════════════════════════════════════════════════════════════════
def ch43_condition_vs_event():
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    fig.suptitle("Condition (ниво) vs Event (момент)", fontsize=14,
                 fontweight="bold", color=C_DARK)

    t = np.linspace(0, 5, 500)

    # Left: Condition (continuous level)
    ax = axes[0]
    level = np.where(t < 1.5, 0, np.where(t < 3.5, 1, 0))
    ax.fill_between(t, 0, level, alpha=0.3, color=C_BLUE, step="mid")
    ax.step(t, level, color=C_BLUE, lw=2.5, where="mid")
    ax.axhline(0.5, ls="--", color=C_RED, lw=1, alpha=0.6)
    ax.text(2.5, 1.1, "Condition: button.value() == 0\n(проверявам постоянно)",
            ha="center", fontsize=9, color=C_BLUE, fontweight="bold")
    ax.set_title("Condition (ниво)", fontsize=12, color=C_BLUE)
    ax.set_xlabel("Време (s)")
    ax.set_ylabel("Стойност")
    ax.set_ylim(-0.2, 1.5)

    # Right: Event (discrete)
    ax = axes[1]
    ax.axhline(0, color=C_GRAY, lw=0.5)
    events = [1.5, 3.5]
    labels = ["PRESS\n(1→0)", "RELEASE\n(0→1)"]
    colors = [C_GREEN, C_RED]
    for ev, lab, col in zip(events, labels, colors):
        ax.annotate("", xy=(ev, 0.8), xytext=(ev, 0.05),
                    arrowprops=dict(arrowstyle="-|>", color=col, lw=3))
        ax.text(ev, 0.9, lab, ha="center", fontsize=9, color=col, fontweight="bold")
    ax.set_title("Event (момент)", fontsize=12, color=C_GREEN)
    ax.set_xlabel("Време (s)")
    ax.set_ylim(-0.2, 1.5)
    ax.set_xlim(0, 5)
    ax.text(2.5, 1.35, "Event: еднократен сигнал\n(реагирам само при промяна)",
            ha="center", fontsize=9, color=C_GREEN, fontweight="bold")

    for ax in axes:
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)

    fig.tight_layout()
    save(fig, "43_condition_vs_event.png")


# ════════════════════════════════════════════════════════════════
# Ch44: Guard function
# ════════════════════════════════════════════════════════════════
def ch44_guard():
    fig, ax = plt.subplots(figsize=(9, 4))
    fig.suptitle("Guard функция — портиер на прехода", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # Event source
    box(ax, 0.5, 1.5, 2, 1, "Събитие\nBUTTON", C_RED, bold=True)

    # Guard gate
    gx, gy, gw, gh = 3.5, 1.0, 2.5, 2
    rect = FancyBboxPatch((gx, gy), gw, gh, boxstyle="round,pad=0.15",
                          facecolor=C_ORANGE + "22", edgecolor=C_ORANGE, linewidth=2.5)
    ax.add_patch(rect)
    ax.text(gx + gw/2, gy + gh - 0.3, "Guard", ha="center", fontsize=12,
            fontweight="bold", color=C_ORANGE)
    ax.text(gx + gw/2, gy + gh/2 - 0.2, "is_dark()?\nhas_motion()?",
            ha="center", fontsize=9, color=C_DARK)

    # Target state
    box(ax, 7, 1.5, 2, 1, "Ново\nсъстояние ON", C_GREEN, bold=True)

    # Arrows
    arrow(ax, 2.5, 2, 3.5, 2, "", C_DARK)
    # Pass
    ax.annotate("", xy=(7, 2.1), xytext=(6, 2.1),
                arrowprops=dict(arrowstyle="-|>", color=C_GREEN, lw=2.5))
    ax.text(6.5, 2.4, "True ✓", fontsize=10, color=C_GREEN, fontweight="bold", ha="center")
    # Block
    ax.annotate("", xy=(4.75, 0.6), xytext=(4.75, 1.0),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2.5))
    ax.text(4.75, 0.3, "False ✗  (преходът НЕ се случва)", fontsize=9,
            color=C_RED, fontweight="bold", ha="center")

    setup_ax(ax, (0, 9.5), (-0.1, 3.8))
    save(fig, "44_guard.png")


# ════════════════════════════════════════════════════════════════
# Ch45: Priority event queue
# ════════════════════════════════════════════════════════════════
def ch45_event_queue():
    fig, ax = plt.subplots(figsize=(9, 4.5))
    fig.suptitle("Event queue с приоритети", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # Incoming events
    events_in = [
        ("TIMEOUT", C_GRAY, 0.5),
        ("BUTTON", C_BLUE, 1.2),
        ("ERROR", C_RED, 1.9),
    ]
    for label, color, y in events_in:
        box(ax, 0.3, y, 1.8, 0.5, label, color, bold=True, fs=9)
        arrow(ax, 2.1, y + 0.25, 3.0, y + 0.25, "", color)

    # Priority sorter
    box(ax, 3.0, 0.5, 2, 2.0, "Приоритет\nсортиране\n\nERROR > BUTTON\n> TIMEOUT",
        C_ORANGE, bold=True, fs=9)

    # Queue
    queue_items = [
        ("1. ERROR", C_RED),
        ("2. BUTTON", C_BLUE),
        ("3. TIMEOUT", C_GRAY),
    ]
    for i, (label, color) in enumerate(queue_items):
        x = 6 + i * 1.5
        box(ax, x, 1.0, 1.3, 0.8, label, color, bold=True, fs=8)

    arrow(ax, 5.0, 1.5, 6.0, 1.5, "", C_DARK)

    # FSM consumer
    box(ax, 10.2, 1.0, 1.5, 0.8, "FSM\nhandle()", C_PURPLE, bold=True, fs=9)
    arrow(ax, 9.3, 1.4, 10.2, 1.4, "", C_DARK)

    ax.text(7.5, 0.4, "Опашка (първи = най-висок приоритет)", ha="center",
            fontsize=9, color=C_GRAY, fontstyle="italic")

    setup_ax(ax, (0, 12), (0, 3.2))
    save(fig, "45_event_queue.png")


# ════════════════════════════════════════════════════════════════
# Ch47: Error handling flowchart
# ════════════════════════════════════════════════════════════════
def ch47_error_handling():
    fig, ax = plt.subplots(figsize=(8, 6))
    fig.suptitle("Обработка на грешки — flowchart", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # Boxes
    box(ax, 2.5, 5, 3, 0.8, "Нормална работа", C_GREEN, bold=True)
    box(ax, 2.5, 3.5, 3, 0.8, "try: операция()", C_BLUE, bold=True)

    # Diamond for error
    diamond_x, diamond_y = 4, 2.5
    diamond = plt.Polygon([(4, 2.0), (5.2, 2.5), (4, 3.0), (2.8, 2.5)],
                          facecolor=C_ORANGE + "22", edgecolor=C_ORANGE, lw=2)
    ax.add_patch(diamond)
    ax.text(4, 2.5, "OSError?", ha="center", va="center", fontsize=10,
            fontweight="bold", color=C_DARK)

    box(ax, 0, 1.0, 2.5, 0.8, "retry += 1\nsleep(500ms)", C_ORANGE, bold=True)
    box(ax, 5.5, 1.0, 2.5, 0.8, "Продължава\nнормално", C_GREEN, bold=True)

    # Diamond retry limit
    diamond2 = plt.Polygon([(1.25, -0.2), (2.5, 0.3), (1.25, 0.8), (0, 0.3)],
                           facecolor=C_RED + "22", edgecolor=C_RED, lw=2)
    ax.add_patch(diamond2)
    ax.text(1.25, 0.3, "retry\n> 3?", ha="center", va="center", fontsize=8,
            fontweight="bold", color=C_DARK)

    box(ax, 2.5, -0.8, 3, 0.8, "FAIL-SAFE\nLED мига, спри I/O", C_RED, bold=True)

    # Arrows
    arrow(ax, 4, 5.0, 4, 4.35, "", C_DARK)
    arrow(ax, 4, 3.5, 4, 3.05, "", C_DARK)
    # No error -> right
    ax.annotate("", xy=(5.5, 1.4), xytext=(5.2, 2.5),
                arrowprops=dict(arrowstyle="-|>", color=C_GREEN, lw=2))
    ax.text(5.5, 2.1, "Не", fontsize=9, color=C_GREEN, fontweight="bold")
    # Error -> left
    ax.annotate("", xy=(2.5, 1.4), xytext=(2.8, 2.5),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2))
    ax.text(2.2, 2.1, "Да", fontsize=9, color=C_RED, fontweight="bold")
    # Retry -> back up
    ax.annotate("", xy=(2.8, 3.5), xytext=(1.25, 1.8),
                arrowprops=dict(arrowstyle="-|>", color=C_ORANGE, lw=1.8,
                                connectionstyle="arc3,rad=-0.3"))
    # Retry check
    arrow(ax, 1.25, 1.0, 1.25, 0.85, "", C_DARK)
    # retry > 3 -> fail safe
    ax.annotate("", xy=(2.5, -0.4), xytext=(2.5, 0.3),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2))
    ax.text(2.7, 0.0, "Да", fontsize=8, color=C_RED, fontweight="bold")

    setup_ax(ax, (-0.5, 8.5), (-1.2, 6.2))
    ax.set_aspect("auto")
    save(fig, "47_error_handling.png")


# ════════════════════════════════════════════════════════════════
# Ch48: Watchdog
# ════════════════════════════════════════════════════════════════
def ch48_watchdog():
    fig, ax = plt.subplots(figsize=(10, 3.5))
    fig.suptitle("Watchdog — feed или reset", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # Timeline
    ax.axhline(1.5, color=C_GRAY, lw=1, ls="-", alpha=0.3)

    # Normal feeds
    feed_times = [0.5, 1.5, 2.5, 3.5, 4.5]
    for t in feed_times:
        ax.annotate("", xy=(t, 1.8), xytext=(t, 1.2),
                    arrowprops=dict(arrowstyle="-|>", color=C_GREEN, lw=2))
        ax.text(t, 1.05, "feed()", fontsize=7, ha="center", color=C_GREEN, fontweight="bold")

    # WDT counter (sawtooth)
    t_arr = np.linspace(0, 7, 700)
    wdt = np.zeros_like(t_arr)
    timeout = 1.2
    last_feed = 0
    for i, t in enumerate(t_arr):
        if t < 5.0:
            closest = max([f for f in feed_times if f <= t], default=0)
            wdt[i] = (t - closest) / timeout
        else:
            wdt[i] = (t - 4.5) / timeout

    wdt_y = 1.5 + wdt * 0.8
    ax.plot(t_arr, wdt_y, color=C_ORANGE, lw=2)
    ax.axhline(1.5 + 0.8, color=C_RED, lw=1.5, ls="--")
    ax.text(7.2, 2.35, "TIMEOUT", fontsize=9, color=C_RED, fontweight="bold")

    # Stuck zone
    ax.axvspan(5.0, 7.0, alpha=0.1, color=C_RED)
    ax.text(6.0, 2.6, "Програмата увисна!\nНяма feed()", ha="center",
            fontsize=9, color=C_RED, fontweight="bold")

    # Reset
    ax.annotate("RESET!", xy=(6.2, 2.3), xytext=(6.2, 2.9),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=3),
                fontsize=12, fontweight="bold", color=C_RED, ha="center")

    # Labels
    ax.text(2.5, 2.7, "Нормална работа\n(feed() нулира брояча)",
            ha="center", fontsize=9, color=C_GREEN, fontweight="bold")

    ax.set_xlim(-0.2, 7.5)
    ax.set_ylim(0.7, 3.2)
    ax.set_xlabel("Време (s)")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_visible(False)
    ax.set_yticks([])
    save(fig, "48_watchdog.png")


# ════════════════════════════════════════════════════════════════
# Ch50: Class hierarchy
# ════════════════════════════════════════════════════════════════
def ch50_class_hierarchy():
    fig, ax = plt.subplots(figsize=(10, 5))
    fig.suptitle("Капсулация — от периферия до приложение", fontsize=14,
                 fontweight="bold", color=C_DARK)

    # App layer
    box(ax, 3, 4, 4, 0.9, "app.py\nrun_forever()", C_DARK, bold=True, fs=11)

    # Driver layer
    box(ax, 0.5, 2.2, 3, 1.2, "class StatusLed\n.on()  .off()  .blink()",
        C_GREEN, bold=True, fs=9)
    box(ax, 4, 2.2, 3.2, 1.2, "class ThresholdADC\n.read()  .above()?",
        C_BLUE, bold=True, fs=9)
    box(ax, 7.8, 2.2, 2.5, 1.2, "class RetroSynth\n.coin()  .stop()",
        C_PURPLE, bold=True, fs=9)

    # Hardware layer
    box(ax, 0.5, 0.3, 2, 0.8, "Pin(LED1)\nP204", C_GRAY, bold=True, fs=9)
    box(ax, 3.5, 0.3, 2, 0.8, "ADC(P000)\n12-bit", C_GRAY, bold=True, fs=9)
    box(ax, 6.5, 0.3, 2.3, 0.8, "DAC(P014)\nDTC + AGT", C_GRAY, bold=True, fs=9)
    box(ax, 9, 0.3, 1.5, 0.8, "Timer(1)", C_GRAY, bold=True, fs=9)

    # Arrows app -> drivers
    arrow(ax, 3.5, 4.0, 2, 3.45, "", C_DARK)
    arrow(ax, 5, 4.0, 5.5, 3.45, "", C_DARK)
    arrow(ax, 6.5, 4.0, 9, 3.45, "", C_DARK)

    # Arrows drivers -> hardware
    arrow(ax, 2, 2.2, 1.5, 1.15, "", C_DARK)
    arrow(ax, 5.5, 2.2, 4.5, 1.15, "", C_DARK)
    arrow(ax, 9, 2.2, 7.6, 1.15, "", C_DARK)
    arrow(ax, 9.5, 2.2, 9.5, 1.15, "", C_DARK)

    # Layer labels
    ax.text(10.5, 4.4, "Приложение", fontsize=9, color=C_DARK,
            fontweight="bold", rotation=0)
    ax.text(10.5, 2.7, "Драйвери\n(класове)", fontsize=9, color=C_BLUE,
            fontweight="bold", rotation=0)
    ax.text(10.5, 0.6, "Хардуер", fontsize=9, color=C_GRAY,
            fontweight="bold", rotation=0)

    setup_ax(ax, (0, 12), (-0.2, 5.3))
    save(fig, "50_class_hierarchy.png")


# ════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print("Generating chapter diagrams...")
    ch23_i2c_comparison()
    ch24_spi_wiring()
    ch27_frame_structure()
    ch29_memory_map()
    ch41_fsm_lamp()
    ch42_fsm_timeout()
    ch43_condition_vs_event()
    ch44_guard()
    ch45_event_queue()
    ch47_error_handling()
    ch48_watchdog()
    ch50_class_hierarchy()
    print(f"\nDone — {12} diagrams in {OUT}")
