#!/usr/bin/env python3
"""Generate a printable PDF ratings table for the book chapters."""
import os
import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)

from fpdf import FPDF

FONT_DIR = "C:/Windows/Fonts"
OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "BOOK_BG_ratings.pdf")

CHAPTERS = [
    (  "0", "Първо свързване — от кутията до REPL",          9, 9, 8,10, 9, "Pinout, COM порт, troubleshooting, навигация"),
    (  "1", "Първа програма",                                 8, 9, 9, 9, 9, "Active-low с таблица и аналогия"),
    (  "2", "Първи асинхронни примери",                       8, 9, 9, 8, 9, "Asyncio рано, бележка за Гл. 20-21"),
    (  "3", "Променливи и константи",                         8, 9, 8, 7, 8, "const() мотивиран; кратка глава"),
    (  "4", "Оператори, условия и цикли",                     9, 9, 9, 8, 9, "Битови оператори + три форми на цикли"),
    (  "5", "Enums в MicroPython",                            8, 9, 8, 7, 8, "const() + dict — добра идиома"),
    (  "6", "Масиви и таблици",                               9, 9, 9, 8, 9, "list/array/bytearray с RAM анализ"),
    (  "7", "Структури в MicroPython",                        9, 9, 9, 8, 9, "Три варианта + мини класове"),
    (  "8", "Указатели и еквиваленти",                        8, 8, 9, 8, 8, "memoryview и alias семантика"),
    (  "9", "Функции, callbacks и таблици от функции",         8, 9, 9, 9, 9, "Function table — ценна за FSM"),
    ( "10", "Цифров изход",                                    7, 8, 8, 8, 8, "Припокрива Гл. 1; GPIO токове липсват"),
    ( "11", "Цифров вход",                                     7, 8, 8, 8, 8, "Pull-up диаграма; кратка но правилна"),
    ( "12", "Аналогов вход и ADC",                             9, 9, 9, 9, 9, "ADC timing + резолюции"),
    ( "13", "PWM",                                             8, 9, 8, 8, 8, "GPT споделена честота обяснена"),
    ( "14", "DAC изход, timed playback и retro synth",        10, 9,10,10,10, "DTC/DMAC, RetroSynth, GC, dual DAC"),
    ( "15", "Закъснения, хардуерни таймери и Timer(-1)",      10, 9,10,10,10, "AGT 5 режима изчерпателно"),
    ( "16", "Прекъсвания",                                     8, 9, 8, 9, 9, "IRQ правила и disable_irq"),
    ( "17", "Дебаунс на бутон",                                8, 9, 8, 9, 9, "Два подхода + ASCII bounce"),
    ( "18", "Четене на бутон: polling → ASM IRQ",             10, 9,10,10,10, "5 подхода — от polling до ring buffer"),
    ( "19", "RTC",                                             7, 8, 7, 7, 7, "Кратка; липсва wake-from-RTC"),
    ( "20", "Основи на asyncio",                               8, 9, 8, 9, 9, "Три задачи на реален хардуер"),
    ( "21", "asyncio Event синхронизация",                     8, 9, 8, 8, 8, "Producer-consumer; липсва timeout"),
    ( "22", "I2C master и runtime избор на пинове",            8, 9, 8, 9, 9, "I2C(0)/SW1 конфликт; scan тест"),
    ( "23", "SoftI2C и I2CTarget",                             7, 8, 7, 7, 7, "I2CTarget рядко срещан; кратка"),
    ( "24", "Hardware SPI и SoftSPI",                          7, 8, 7, 7, 7, "Сравнителна таблица; SoftSPI слаб"),
    ( "25", "Базова UART комуникация",                         7, 8, 8, 8, 8, "Таблица с UART канали и пинове"),
    ( "26", "Работа с външни модули по интерфейси",            8, 8, 8, 9, 8, "4 въпроса преди свързване"),
    ( "27", "Байтове, bytearray, struct, memoryview",          7, 8, 8, 8, 8, "Бинарни протоколи; препраща назад"),
    ( "28", "TouchPad: базово четене",                         7, 8, 7, 7, 7, "CTSU специфичен; TSCAP бегло"),
    ( "29", "TouchPad: диагностика, cached API",               7, 8, 7, 7, 7, "Cached API и cooperative sampler"),
    ( "30", "renesas.Flash, /flash и dataflash",               9, 9, 9, 9, 9, "Три зони + write cycles + LED toggle"),
    ( "31", "Управление на паметта в MicroPython",             8, 8, 8, 8, 8, "GC + heap_lock + gc.threshold"),
    ( "32", "machine модулът",                                 7, 8, 7, 7, 7, "info/freq/unique_id; sleep предпазливо"),
    ( "33", "boot.py, main.py и жизнен цикъл",                7, 8, 7, 8, 8, "boot/main разделение"),
    ( "34", "Организация на проект",                           7, 8, 7, 8, 8, "pins.py/config.py/drivers"),
    ( "35", "Таблични данни и файлови формати",                6, 7, 6, 7, 7, "JSON конфиг; кратка"),
    ( "36", "Филтриране, калибриране и хистерезис",            7, 8, 7, 8, 8, "Moving average + хистерезис"),
    ( "37", "Измерване с осцилоскоп и logic analyzer",         7, 7, 6, 8, 7, "Debug pin pulse; липсват снимки"),
    ( "38", "Ниска консумация и събуждане",                    6, 7, 6, 7, 7, "Sleep предпазлив; без измервания"),
    ( "39", "Динамична 7-сегментна индикация",                 7, 8, 8, 8, 8, "Добре кодирана; изисква хардуер"),
    ( "40", "NeoPixel",                                        8, 9, 8, 9, 9, "P500 enable + безопасен тест"),
    ( "41", "WS2812 през machine.WS2812 и SCI TX-only",       9, 9, 9, 9, 9, "SCI 5-bit + DTC на дълбочина"),
    ( "42", "Минимална FSM и таблична FSM",                    8, 9, 8, 9, 9, "до 3=if/elif, при 4+=таблица"),
    ( "43", "FSM с timeout",                                   7, 8, 7, 8, 8, "Timeout + ticks грешка спомената"),
    ( "44", "Conditions vs Events",                            7, 8, 7, 7, 7, "Концептуално важно; синтетичен пример"),
    ( "45", "Guard функции",                                   7, 8, 7, 7, 7, "Чисти предикати; малко академично"),
    ( "46", "Приоритети и event queue",                         8, 8, 8, 8, 8, "deque O(1); event queue мотивиран"),
    ( "47", "Tickless timers и sleep идея",                    7, 8, 7, 7, 7, "Идея ясна; синтетичен пример"),
    ( "48", "Обработка на грешки, recovery и fail-safe",       8, 9, 8, 9, 9, "Retry + sys.print_exception"),
    ( "49", "Watchdog и самовъзстановяване",                   7, 8, 6, 6, 7, "WDT не е поддържан; учебен паттерн"),
    ( "50", "Heartbeat, health checks и debug",                7, 8, 7, 7, 7, "last_error; повтаряща Гл. 48"),
    ( "51", "Капсуловане на периферии като класове",           7, 8, 7, 8, 8, "StatusLed минимален но ясен"),
    ( "52", "От пример към завършено приложение",              7, 8, 7, 8, 8, "7-стъпков skeleton"),
    ( "53", "RGB_Guardian — казус: WS2812 игра",              10, 9,10,10,10, "DTC race, bytearray пулове, FSM"),
    ("X",   "Курс по елементарна сигнална обработка",          8, 8, 8, 8, 8, "Навигационен обзор + 12 урока"),
    ( "54", "Storm ADC — DTC аудио семплиране",               10, 9,10,10,10, "GPT→ADC→DTC + ping-pong"),
    ( "55", "FFT — честотен анализ с CMSIS-DSP",              10, 9,10,10,10, "Hamming, CMSIS-DSP, dBFS"),
    ( "56", "MSGEQ7 емулация — спектрален анализатор",         9, 9, 9, 9, 9, "Геометрични средни + render"),
    ( "57", "DSP Synthesis — Ретро синтезатор",               10, 9,10,10,10, "LFSR, sweep, 4 mixing подхода"),
]


def score_color(score):
    """Return RGB fill color based on score."""
    if score >= 10: return (46, 204, 113)    # green
    if score >= 9:  return (88, 214, 141)    # light green
    if score >= 8:  return (174, 214, 241)   # light blue
    if score >= 7:  return (249, 231, 159)   # yellow
    return (245, 183, 177)                    # light red


class RatingsPDF(FPDF):
    def __init__(self):
        super().__init__(orientation="L", format="A4")  # Landscape for wide table
        self.set_auto_page_break(auto=True, margin=15)
        self.set_margins(10, 10, 10)
        self.add_font("body", "", os.path.join(FONT_DIR, "arial.ttf"))
        self.add_font("body", "B", os.path.join(FONT_DIR, "arialbd.ttf"))
        self.add_font("body", "I", os.path.join(FONT_DIR, "ariali.ttf"))
        self.add_font("mono", "", os.path.join(FONT_DIR, "consola.ttf"))

    def header(self):
        if self.page_no() == 1:
            self.set_font("body", "B", 16)
            self.set_text_color(20, 60, 120)
            self.cell(0, 12, "BOOK_BG.md — Оценка по глави", align="C", new_x="LMARGIN", new_y="NEXT")
            self.set_font("body", "", 9)
            self.set_text_color(100, 100, 100)
            self.cell(0, 6, "MicroPython за VK_RA4M2 чрез демонстрационни примери  |  Обща оценка: 9/10  |  57 глави + 12 урока", align="C", new_x="LMARGIN", new_y="NEXT")
            self.ln(4)

    def footer(self):
        self.set_y(-12)
        self.set_font("body", "I", 7)
        self.set_text_color(150, 150, 150)
        self.cell(0, 8, f"стр. {self.page_no()}", align="C")


def main():
    pdf = RatingsPDF()
    pdf.add_page()

    # Column widths for landscape A4 (297mm - 20mm margins = 277mm)
    W = [12, 82, 14, 14, 14, 14, 14, 113]  # total = 277
    H_HDR = 8
    H_ROW = 6.5

    headers = ["Гл.", "Име на главата", "Пълн.", "Ясн.", "Код", "Практ.", "Обща", "Бележка"]

    # Header row
    pdf.set_font("body", "B", 8)
    pdf.set_fill_color(44, 62, 80)
    pdf.set_text_color(255, 255, 255)
    for i, h in enumerate(headers):
        pdf.cell(W[i], H_HDR, h, border=1, fill=True, align="C" if i < 7 else "L")
    pdf.ln()
    pdf.set_text_color(0, 0, 0)

    # Data rows
    for row_idx, (ch, name, p, y, k, pr, total, note) in enumerate(CHAPTERS):
        # Check page break
        if pdf.get_y() + H_ROW > pdf.h - 20:
            pdf.add_page()
            # Reprint header
            pdf.set_font("body", "B", 8)
            pdf.set_fill_color(44, 62, 80)
            pdf.set_text_color(255, 255, 255)
            for i, h in enumerate(headers):
                pdf.cell(W[i], H_HDR, h, border=1, fill=True, align="C" if i < 7 else "L")
            pdf.ln()
            pdf.set_text_color(0, 0, 0)

        # Alternating background
        alt_bg = row_idx % 2 == 1
        if alt_bg:
            pdf.set_fill_color(245, 245, 250)
        else:
            pdf.set_fill_color(255, 255, 255)

        pdf.set_font("body", "B" if total >= 10 else "", 7.5)

        # Chapter number
        pdf.cell(W[0], H_ROW, str(ch), border=1, fill=alt_bg, align="C")

        # Name
        pdf.set_font("body", "B" if total >= 10 else "", 7)
        pdf.cell(W[1], H_ROW, name[:48] + ("..." if len(name) > 48 else ""), border=1, fill=alt_bg)

        # Score cells with color
        pdf.set_font("body", "B", 8)
        for score in [p, y, k, pr]:
            r, g, b = score_color(score)
            pdf.set_fill_color(r, g, b)
            pdf.cell(W[2], H_ROW, str(score), border=1, fill=True, align="C")

        # Total with stronger color
        r, g, b = score_color(total)
        pdf.set_fill_color(r, g, b)
        pdf.set_font("body", "B", 9)
        pdf.cell(W[6], H_ROW, str(total), border=1, fill=True, align="C")

        # Note
        if alt_bg:
            pdf.set_fill_color(245, 245, 250)
        else:
            pdf.set_fill_color(255, 255, 255)
        pdf.set_font("body", "", 6.5)
        pdf.cell(W[7], H_ROW, note[:68] + ("..." if len(note) > 68 else ""), border=1, fill=alt_bg)
        pdf.ln()

    # Summary section
    pdf.ln(6)
    pdf.set_font("body", "B", 11)
    pdf.set_text_color(20, 60, 120)
    pdf.cell(0, 8, "Резюме", new_x="LMARGIN", new_y="NEXT")
    pdf.set_text_color(0, 0, 0)

    pdf.set_font("body", "B", 9)
    pdf.cell(50, 7, "Обща оценка на книгата:")
    pdf.set_font("body", "B", 14)
    pdf.set_text_color(46, 204, 113)
    pdf.cell(20, 7, "9 / 10", new_x="LMARGIN", new_y="NEXT")
    pdf.set_text_color(0, 0, 0)

    pdf.ln(2)
    pdf.set_font("body", "B", 9)
    pdf.cell(0, 7, "Топ 5 най-силни глави:", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("body", "", 8)
    for ch, desc in [("14", "DAC — DTC/DMAC, RetroSynth, GC, dual DAC"),
                     ("15", "Таймери — AGT 5 режима изчерпателно"),
                     ("18", "Четене на бутон — 5 подхода от polling до ASM IRQ"),
                     ("53", "RGB_Guardian — production-level интеграция"),
                     ("57", "DSP Synthesis — LFSR, sweep, 4 mixing подхода")]:
        pdf.set_fill_color(46, 204, 113)
        pdf.cell(8, 6, ch, border=1, fill=True, align="C")
        pdf.set_fill_color(255, 255, 255)
        pdf.cell(140, 6, "  " + desc, border=0)
        pdf.ln()

    pdf.ln(3)
    pdf.set_font("body", "B", 9)
    pdf.cell(0, 7, "Топ 5 за подобряване:", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("body", "", 8)
    for ch, desc in [("35", "Таблични данни — JSON конфиг кратка, заслужава по-голям пример"),
                     ("38", "Ниска консумация — sleep предпазлив, липсват токови измервания"),
                     ("47", "Tickless timers — идея ясна, но синтетичен пример"),
                     ("49", "Watchdog — WDT не е поддържан, само учебен паттерн"),
                     ("50", "Heartbeat — кратка, повтаряща Гл. 48")]:
        pdf.set_fill_color(249, 231, 159)
        pdf.cell(8, 6, ch, border=1, fill=True, align="C")
        pdf.set_fill_color(255, 255, 255)
        pdf.cell(140, 6, "  " + desc, border=0)
        pdf.ln()

    # Legend
    pdf.ln(5)
    pdf.set_font("body", "B", 8)
    pdf.cell(0, 6, "Легенда:", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("body", "", 7.5)
    for score, label in [(10, "Публикационно ниво"), (9, "Отличен с минимални забележки"),
                          (8, "Добър — работещ с дребни пропуски"), (7, "Ползваем, но заслужава допълване")]:
        r, g, b = score_color(score)
        pdf.set_fill_color(r, g, b)
        pdf.cell(8, 5.5, str(score), border=1, fill=True, align="C")
        pdf.set_fill_color(255, 255, 255)
        pdf.cell(55, 5.5, "  " + label, border=0)

    pdf.output(OUTPUT)
    print(f"PDF saved: {OUTPUT}")
    print(f"Pages: {pdf.pages_count}")


if __name__ == "__main__":
    main()
