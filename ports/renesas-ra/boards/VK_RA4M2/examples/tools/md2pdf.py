#!/usr/bin/env python3
"""Convert BOOK_BG.md to PDF with proper Bulgarian/Cyrillic support using fpdf2."""

import re
import os
import warnings

warnings.filterwarnings("ignore", category=DeprecationWarning)

from fpdf import FPDF

# Emoji/symbol replacements for fonts that lack them
EMOJI_MAP = {
    "\U0001f4a1": "[Tip]",      # 💡
    "\U0001f52c": "[Lab]",      # 🔬
    "\u26a0\ufe0f": "[!]",      # ⚠️
    "\u26a0": "[!]",            # ⚠ (without variation selector)
    "\U0001f4c1": "[File]",     # 📁
    "\U0001f4cc": "[Pin]",      # 📌
    "\U0001f441": "[Eye]",      # 👁
    "\U0001f914": "[?]",        # 🤔
    "\U0001f3b5": "[Note]",     # 🎵
    "\u2b1b": "[X]",            # ⬛
    "\u2705": "[OK]",           # ✅
    "\u274c": "[X]",            # ❌
    "\u2717": "x",              # ✗
    "\u2713": "v",              # ✓
    "\ufe0f": "",               # variation selector
    "\U0001f916": "[Bot]",      # 🤖
    "\u220e": "#",              # ∎
    "\u223f": "~",              # ∿
    "\u2581": "_",              # ▁
    "\u2583": "=",              # ▃
    "\u2585": "#",              # ▅
    "\U0001f534": "(R)",        # 🔴
    "\U0001f7e1": "(Y)",        # 🟡
    "\U0001f7e2": "(G)",        # 🟢
    "\u25b6": ">",              # ▶
}


def strip_emoji(text):
    for emoji, replacement in EMOJI_MAP.items():
        text = text.replace(emoji, replacement)
    return text

FONT_DIR = "C:/Windows/Fonts"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXAMPLES_DIR = os.path.dirname(SCRIPT_DIR)  # parent of tools/
BOOK_DIR = os.path.join(EXAMPLES_DIR, "book")
MANIFEST = os.path.join(BOOK_DIR, "MANIFEST.txt")
INPUT_FILE = os.path.join(EXAMPLES_DIR, "BOOK_BG.md")  # fallback single file
OUTPUT_FILE = os.path.join(EXAMPLES_DIR, "BOOK_BG_v5.pdf")

def load_book_lines():
    """Load book content: from book/ split files if available, else BOOK_BG.md."""
    if os.path.isfile(MANIFEST):
        lines = []
        with open(MANIFEST, "r", encoding="utf-8") as mf:
            filenames = [l.strip() for l in mf if l.strip()]
        for fname in filenames:
            fpath = os.path.join(BOOK_DIR, fname)
            if os.path.isfile(fpath):
                with open(fpath, "r", encoding="utf-8") as f:
                    lines.extend(f.readlines())
                lines.append("\n")  # separator between files
        print(f"Loaded {len(filenames)} files from book/")
        return lines
    else:
        with open(INPUT_FILE, "r", encoding="utf-8") as f:
            return f.readlines()

# Margins and sizes
MARGIN_LEFT = 15
MARGIN_RIGHT = 15
MARGIN_TOP = 15
PAGE_WIDTH = 210  # A4
CONTENT_WIDTH = PAGE_WIDTH - MARGIN_LEFT - MARGIN_RIGHT

# Font sizes
FONT_H1 = 20
FONT_H2 = 16
FONT_H3 = 13
FONT_H4 = 11
FONT_BODY = 9.5
FONT_CODE = 8
FONT_TABLE = 8
FONT_SMALL = 8


class BookPDF(FPDF):
    def __init__(self):
        super().__init__()
        self.set_auto_page_break(auto=True, margin=20)
        self.set_margins(MARGIN_LEFT, MARGIN_TOP, MARGIN_RIGHT)

        # Register fonts with Cyrillic support
        self.add_font("body", "", os.path.join(FONT_DIR, "arial.ttf"), )
        self.add_font("body", "B", os.path.join(FONT_DIR, "arialbd.ttf"), )
        self.add_font("body", "I", os.path.join(FONT_DIR, "ariali.ttf"), )
        self.add_font("body", "BI", os.path.join(FONT_DIR, "arialbi.ttf"), )
        self.add_font("mono", "", os.path.join(FONT_DIR, "consola.ttf"), )
        self.add_font("mono", "B", os.path.join(FONT_DIR, "consolab.ttf"), )

        self.chapter_title = ""

    def header(self):
        if self.page_no() > 1:
            self.set_font("body", "I", 7)
            self.set_text_color(128, 128, 128)
            self.cell(0, 8, self.chapter_title, align="L")
            self.cell(0, 8, f"стр. {self.page_no()}", align="R", new_x="LMARGIN", new_y="NEXT")
            self.set_draw_color(200, 200, 200)
            self.line(MARGIN_LEFT, self.get_y(), PAGE_WIDTH - MARGIN_RIGHT, self.get_y())
            self.ln(3)
            self.set_text_color(0, 0, 0)

    def footer(self):
        pass  # handled in header


def clean_inline(text):
    """Remove markdown inline formatting markers for plain text output."""
    text = strip_emoji(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'\1', text)
    text = re.sub(r'\*(.+?)\*', r'\1', text)
    text = re.sub(r'`(.+?)`', r'\1', text)
    text = re.sub(r'\[(.+?)\]\(.+?\)', r'\1', text)
    return text


def render_inline(pdf, text, base_size=FONT_BODY, base_style="", heading_links=None):
    """Render a line with inline markdown: **bold**, *italic*, `code`, [links], and internal Глава N links."""
    text = strip_emoji(text)
    # Split by inline patterns + "Глава N" / "Глави N-M" / "Част X" references
    parts = re.split(r'(\*\*.*?\*\*|`.*?`|\*.*?\*|\[.*?\]\(.*?\)|Глава \d+|Глави \d+[-–]\d+|Част [IVXL]+б?)', text)
    for part in parts:
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            pdf.set_font("body", "B", base_size)
            pdf.write(pdf.font_size * 1.6, part[2:-2])
            pdf.set_font("body", base_style, base_size)
        elif part.startswith("`") and part.endswith("`"):
            pdf.set_font("mono", "", max(base_size - 1, 7))
            pdf.set_text_color(40, 40, 40)
            pdf.write(pdf.font_size * 1.6, part[1:-1])
            pdf.set_text_color(0, 0, 0)
            pdf.set_font("body", base_style, base_size)
        elif part.startswith("*") and part.endswith("*") and not part.startswith("**"):
            pdf.set_font("body", "I", base_size)
            pdf.write(pdf.font_size * 1.6, part[1:-1])
            pdf.set_font("body", base_style, base_size)
        elif part.startswith("[") and "](" in part:
            m = re.match(r'\[(.+?)\]\((.+?)\)', part)
            if m:
                pdf.set_font("body", base_style, base_size)
                pdf.set_text_color(0, 0, 180)
                pdf.write(pdf.font_size * 1.6, m.group(1))
                pdf.set_text_color(0, 0, 0)
        elif heading_links and re.match(r'Глава \d+$', part):
            link = heading_links.get(part)
            if link:
                pdf.set_font("body", "B", base_size)
                pdf.set_text_color(0, 60, 180)
                pdf.write(pdf.font_size * 1.6, part, link)
                pdf.set_text_color(0, 0, 0)
                pdf.set_font("body", base_style, base_size)
            else:
                pdf.write(pdf.font_size * 1.6, part)
        elif heading_links and re.match(r'Глави \d+[-–]\d+$', part):
            # Link to the first chapter in range
            nums = re.findall(r'\d+', part)
            key = f"Глава {nums[0]}"
            link = heading_links.get(key)
            if link:
                pdf.set_font("body", "B", base_size)
                pdf.set_text_color(0, 60, 180)
                pdf.write(pdf.font_size * 1.6, part, link)
                pdf.set_text_color(0, 0, 0)
                pdf.set_font("body", base_style, base_size)
            else:
                pdf.write(pdf.font_size * 1.6, part)
        elif heading_links and re.match(r'Част [IVXL]+б?$', part):
            link = heading_links.get(part)
            if link:
                pdf.set_font("body", "B", base_size)
                pdf.set_text_color(0, 60, 180)
                pdf.write(pdf.font_size * 1.6, part, link)
                pdf.set_text_color(0, 0, 0)
                pdf.set_font("body", base_style, base_size)
            else:
                pdf.write(pdf.font_size * 1.6, part)
        else:
            pdf.set_font("body", base_style, base_size)
            pdf.write(pdf.font_size * 1.6, part)


def render_table(pdf, rows):
    """Render a markdown table."""
    if len(rows) < 2:
        return
    # Parse header and data rows (skip separator row)
    header = [c.strip() for c in rows[0].strip("|").split("|")]
    data = []
    for row in rows[2:]:  # skip separator
        cells = [c.strip() for c in row.strip("|").split("|")]
        data.append(cells)

    num_cols = len(header)
    if num_cols == 0:
        return

    # Calculate column widths based on content
    col_widths = []
    for i in range(num_cols):
        max_len = len(clean_inline(header[i])) if i < len(header) else 0
        for row in data:
            if i < len(row):
                max_len = max(max_len, len(clean_inline(row[i])))
        col_widths.append(max_len)

    total = sum(col_widths) or 1
    col_widths = [max(w / total * CONTENT_WIDTH, 15) for w in col_widths]

    # Normalize to fit page
    scale = CONTENT_WIDTH / sum(col_widths)
    col_widths = [w * scale for w in col_widths]

    pdf.set_font("body", "B", FONT_TABLE)
    line_h = pdf.font_size * 2

    # Check if table fits on page
    needed = line_h * (1 + len(data)) + 5
    if pdf.get_y() + needed > pdf.h - 25:
        pdf.add_page()

    # Header
    pdf.set_fill_color(230, 230, 240)
    for i, h in enumerate(header):
        if i < len(col_widths):
            pdf.cell(col_widths[i], line_h, clean_inline(h), border=1, fill=True)
    pdf.ln()

    # Data rows
    pdf.set_font("body", "", FONT_TABLE)
    pdf.set_fill_color(248, 248, 248)
    for row_idx, row in enumerate(data):
        fill = row_idx % 2 == 1
        row_h = line_h
        for i in range(num_cols):
            cell_text = clean_inline(row[i]) if i < len(row) else ""
            if i < len(col_widths):
                pdf.cell(col_widths[i], row_h, cell_text, border=1, fill=fill)
        pdf.ln()

    pdf.ln(2)


def convert(input_path, output_path):
    pdf = BookPDF()
    pdf.add_page()

    lines = load_book_lines()

    # --- Pre-scan: create internal link targets for all headings ---
    heading_links = {}  # "Глава 14" -> link_id, "Част IX" -> link_id, etc.
    for line in lines:
        s = line.strip()
        for prefix in ("## ", "### "):
            if s.startswith(prefix):
                title = clean_inline(s[len(prefix):])
                # Extract "Глава N" or "Част X" key
                m = re.match(r'(Глава \d+)', title)
                if m:
                    key = m.group(1)
                    if key not in heading_links:
                        heading_links[key] = pdf.add_link()
                m2 = re.match(r'(Част [IVXL]+)', title)
                if m2:
                    key = m2.group(1)
                    if key not in heading_links:
                        heading_links[key] = pdf.add_link()
                # Also create link for full heading text (for TOC)
                if title not in heading_links:
                    heading_links[title] = pdf.add_link()

    # Also add "Глава 0" specifically
    if "Глава 0" not in heading_links:
        heading_links["Глава 0"] = pdf.add_link()

    def set_heading_link(title):
        """Set the link target for this heading at current position."""
        clean = clean_inline(title)
        if clean in heading_links:
            pdf.set_link(heading_links[clean], y=pdf.get_y(), page=pdf.page)
        m = re.match(r'(Глава \d+)', clean)
        if m and m.group(1) in heading_links:
            pdf.set_link(heading_links[m.group(1)], y=pdf.get_y(), page=pdf.page)
        m2 = re.match(r'(Част [IVXL]+)', clean)
        if m2 and m2.group(1) in heading_links:
            pdf.set_link(heading_links[m2.group(1)], y=pdf.get_y(), page=pdf.page)

    def get_chapter_link(text):
        """Find link for 'Глава N' or 'Част X' reference in text."""
        m = re.match(r'(Глава \d+)', text)
        if m and m.group(1) in heading_links:
            return heading_links[m.group(1)]
        return None

    i = 0
    in_code = False
    code_buf = []
    table_buf = []
    in_blockquote = False
    bq_buf = []

    while i < len(lines):
        line = lines[i].rstrip("\n")
        raw = line

        # Code blocks
        if line.startswith("```"):
            if not in_code:
                # Flush any pending content
                if table_buf:
                    render_table(pdf, table_buf)
                    table_buf = []
                in_code = True
                code_buf = []
            else:
                # Render code block
                in_code = False
                code_text = "\n".join(code_buf)
                if code_text.strip():
                    # Background
                    pdf.set_font("mono", "", FONT_CODE)
                    code_lines = code_text.split("\n")
                    line_h = pdf.font_size * 1.5
                    block_h = line_h * len(code_lines) + 6

                    # Check page break
                    if pdf.get_y() + block_h > pdf.h - 25:
                        pdf.add_page()

                    y0 = pdf.get_y()
                    pdf.set_fill_color(245, 245, 245)
                    pdf.rect(MARGIN_LEFT, y0, CONTENT_WIDTH, block_h, "F")
                    pdf.set_draw_color(200, 200, 200)
                    pdf.rect(MARGIN_LEFT, y0, CONTENT_WIDTH, block_h, "D")

                    pdf.set_xy(MARGIN_LEFT + 3, y0 + 3)
                    pdf.set_text_color(30, 30, 30)
                    for cl in code_lines:
                        cl = strip_emoji(cl)
                        # Truncate very long lines
                        if len(cl) > 110:
                            cl = cl[:107] + "..."
                        pdf.cell(CONTENT_WIDTH - 6, line_h, cl)
                        pdf.ln(line_h)
                        pdf.set_x(MARGIN_LEFT + 3)
                    pdf.set_text_color(0, 0, 0)
                    pdf.set_y(y0 + block_h + 2)
                code_buf = []
            i += 1
            continue

        if in_code:
            code_buf.append(line)
            i += 1
            continue

        # Table rows
        if line.startswith("|"):
            table_buf.append(line)
            i += 1
            continue
        elif table_buf:
            render_table(pdf, table_buf)
            table_buf = []

        # Blockquotes
        if line.startswith("> ") or line == ">":
            content = line[2:] if line.startswith("> ") else ""
            bq_buf.append(content)
            in_blockquote = True
            i += 1
            continue
        elif in_blockquote:
            # Render blockquote
            bq_text = strip_emoji(" ".join(bq_buf).strip())
            if bq_text:
                pdf.set_font("body", "I", FONT_BODY)
                y0 = pdf.get_y()
                pdf.set_fill_color(240, 245, 255)
                # Estimate height
                est_lines = max(1, len(bq_text) // 85 + 1)
                bq_h = pdf.font_size * 1.6 * est_lines + 6
                if pdf.get_y() + bq_h > pdf.h - 25:
                    pdf.add_page()
                    y0 = pdf.get_y()
                pdf.set_draw_color(100, 130, 200)
                pdf.set_x(MARGIN_LEFT + 4)
                pdf.multi_cell(CONTENT_WIDTH - 8, pdf.font_size * 1.6, clean_inline(bq_text))
                y1 = pdf.get_y()
                pdf.line(MARGIN_LEFT + 2, y0, MARGIN_LEFT + 2, y1)
                pdf.ln(2)
                pdf.set_font("body", "", FONT_BODY)
            bq_buf = []
            in_blockquote = False
            # Don't skip this line - fall through to process it

        stripped = line.strip()

        # Empty line
        if not stripped:
            pdf.ln(2)
            i += 1
            continue

        # Headings
        if stripped.startswith("# ") and not stripped.startswith("## "):
            text = stripped[2:]
            pdf.chapter_title = clean_inline(text)
            pdf.add_page()
            set_heading_link(text)
            pdf.set_font("body", "B", FONT_H1)
            pdf.set_text_color(20, 60, 120)
            pdf.multi_cell(CONTENT_WIDTH, FONT_H1 * 0.5, clean_inline(text))
            pdf.set_text_color(0, 0, 0)
            pdf.ln(6)
            i += 1
            continue

        if stripped.startswith("## "):
            text = stripped[3:]
            pdf.chapter_title = clean_inline(text)
            if pdf.get_y() > 60:
                pdf.add_page()
            set_heading_link(text)
            pdf.set_font("body", "B", FONT_H2)
            pdf.set_text_color(20, 80, 140)
            pdf.multi_cell(CONTENT_WIDTH, FONT_H2 * 0.5, clean_inline(text))
            pdf.set_text_color(0, 0, 0)
            pdf.set_draw_color(20, 80, 140)
            pdf.line(MARGIN_LEFT, pdf.get_y(), MARGIN_LEFT + CONTENT_WIDTH, pdf.get_y())
            pdf.ln(4)
            i += 1
            continue

        if stripped.startswith("### "):
            text = stripped[4:]
            pdf.chapter_title = clean_inline(text)
            if pdf.get_y() > pdf.h - 60:
                pdf.add_page()
            pdf.ln(3)
            set_heading_link(text)
            pdf.set_font("body", "B", FONT_H3)
            pdf.set_text_color(30, 90, 130)
            pdf.multi_cell(CONTENT_WIDTH, FONT_H3 * 0.5, clean_inline(text))
            pdf.set_text_color(0, 0, 0)
            pdf.ln(3)
            i += 1
            continue

        if stripped.startswith("#### "):
            text = stripped[5:]
            if pdf.get_y() > pdf.h - 40:
                pdf.add_page()
            pdf.ln(2)
            pdf.set_font("body", "B", FONT_H4)
            pdf.set_text_color(50, 100, 120)
            pdf.multi_cell(CONTENT_WIDTH, FONT_H4 * 0.5, clean_inline(text))
            pdf.set_text_color(0, 0, 0)
            pdf.ln(2)
            i += 1
            continue

        # Horizontal rule
        if stripped in ("---", "***", "___"):
            pdf.set_draw_color(180, 180, 180)
            pdf.line(MARGIN_LEFT, pdf.get_y(), MARGIN_LEFT + CONTENT_WIDTH, pdf.get_y())
            pdf.ln(4)
            i += 1
            continue

        # Image references - show as italic text
        img_match = re.match(r'!\[(.+?)\]\((.+?)\)', stripped)
        if img_match:
            caption = img_match.group(1)
            path = img_match.group(2)
            # Try to embed the image
            img_dir = EXAMPLES_DIR  # images are under examples/img/
            img_path = os.path.join(img_dir, path)
            if os.path.exists(img_path) and img_path.lower().endswith(".png"):
                try:
                    if pdf.get_y() > pdf.h - 80:
                        pdf.add_page()
                    pdf.image(img_path, x=MARGIN_LEFT, w=min(CONTENT_WIDTH, 160))
                    pdf.ln(2)
                    pdf.set_font("body", "I", FONT_SMALL)
                    pdf.set_text_color(80, 80, 80)
                    pdf.cell(CONTENT_WIDTH, pdf.font_size * 1.5, caption, align="C")
                    pdf.ln(4)
                    pdf.set_text_color(0, 0, 0)
                except Exception:
                    pdf.set_font("body", "I", FONT_SMALL)
                    pdf.set_text_color(100, 100, 100)
                    pdf.cell(CONTENT_WIDTH, pdf.font_size * 1.5, f"[{caption}] ({path})")
                    pdf.ln(4)
                    pdf.set_text_color(0, 0, 0)
            else:
                pdf.set_font("body", "I", FONT_SMALL)
                pdf.set_text_color(100, 100, 100)
                pdf.cell(CONTENT_WIDTH, pdf.font_size * 1.5, f"[{caption}] ({path})")
                pdf.ln(4)
                pdf.set_text_color(0, 0, 0)
            i += 1
            continue

        # List items
        list_match = re.match(r'^(\s*)([-*]|\d+\.)\s+(.+)', stripped)
        if list_match:
            indent = len(list_match.group(1)) // 2
            marker = list_match.group(2)
            content = list_match.group(3)
            x_offset = MARGIN_LEFT + 4 + indent * 6
            pdf.set_font("body", "", FONT_BODY)
            pdf.set_x(x_offset)
            if marker in ("-", "*"):
                bullet = "\u2022 "
            else:
                bullet = marker + " "
            # Check if this is a TOC entry (e.g. "Глава 14. DAC изход...")
            toc_match = re.match(r'(Глава \d+)\.\s+(.+)', content)
            if toc_match and toc_match.group(1) in heading_links:
                link = heading_links[toc_match.group(1)]
                pdf.set_font("body", "", FONT_BODY)
                pdf.write(pdf.font_size * 1.6, bullet)
                pdf.set_text_color(0, 60, 180)
                pdf.set_font("body", "B", FONT_BODY)
                pdf.write(pdf.font_size * 1.6, clean_inline(content), link)
                pdf.set_text_color(0, 0, 0)
                pdf.set_font("body", "", FONT_BODY)
            else:
                pdf.set_font("body", "B", FONT_BODY)
                pdf.write(pdf.font_size * 1.6, bullet)
                pdf.set_font("body", "", FONT_BODY)
                render_inline(pdf, content, heading_links=heading_links)
            pdf.ln(pdf.font_size * 1.6)
            i += 1
            continue

        # Regular paragraph
        pdf.set_font("body", "", FONT_BODY)
        render_inline(pdf, stripped, heading_links=heading_links)
        pdf.ln(pdf.font_size * 1.6)
        i += 1

    # Flush remaining
    if table_buf:
        render_table(pdf, table_buf)
    if in_blockquote and bq_buf:
        bq_text = " ".join(bq_buf).strip()
        if bq_text:
            pdf.set_font("body", "I", FONT_BODY)
            pdf.multi_cell(CONTENT_WIDTH - 8, pdf.font_size * 1.6, clean_inline(bq_text))

    pdf.output(output_path)
    print(f"PDF saved: {output_path}")
    print(f"Pages: {pdf.pages_count}")


if __name__ == "__main__":
    convert(INPUT_FILE, OUTPUT_FILE)
